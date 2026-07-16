#include "ct62_lora.h"
#include <cstring>

namespace siliqs {

static constexpr char NVS_SESSION_KEY[] = "session";

Ct62LoRaWan::~Ct62LoRaWan() {
  delete _node;
  delete _radio;
}

int16_t Ct62LoRaWan::begin(const Ct62LoRaConfig& cfg) {
  _cfg = cfg;

  // Idempotent: if begin() is called again, tear the previous radio/node down
  // first so we don't leak Module/SX1262/LoRaWANNode instances.
  if (_node)  { delete _node;  _node  = nullptr; }
  if (_radio) { delete _radio; _radio = nullptr; }

  _radio = new SX1262(new Module(_cfg.pins.nss,
                                 _cfg.pins.dio1,
                                 _cfg.pins.nrst,
                                 _cfg.pins.busy));
  int16_t state = _radio->begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] radio.begin() failed: %d\n", state);
    _state = Ct62LoRaState::ERROR;
    return state;
  }

  // Raise the PA over-current limit to the SX1262 datasheet value. RadioLib's begin()
  // defaults OCP to 60 mA (the SX1261 +15 dBm value); on the SX1262 high-power PA that
  // current-limits and COMPRESSES output at high power — the level stops tracking the
  // setting near the top. AS923-TW here runs +22 dBm, so set 140 mA (SX1262 datasheet).
  _radio->setCurrentLimit(140.0);

  if (_cfg.tcxoVoltage > 0.0f) {
    state = _radio->setTCXO(_cfg.tcxoVoltage);
    if (state != RADIOLIB_ERR_NONE) {
      Serial.printf("[ct62-lora] setTCXO(%.1f) failed: %d\n", _cfg.tcxoVoltage, state);
      _state = Ct62LoRaState::ERROR;
      return state;
    }
  }
  Serial.printf("[ct62-lora] SX1262 initialized (TCXO=%.2fV)\n", _cfg.tcxoVoltage);

  _node = new LoRaWANNode(_radio, _cfg.band, _cfg.subBand);
  _currentDR = _cfg.defaultDR;
  return RADIOLIB_ERR_NONE;
}

int16_t Ct62LoRaWan::activateABP(uint32_t      devAddr,
                                  const uint8_t nwkSKey[16],
                                  const uint8_t appSKey[16]) {
  if (!_node) {
    Serial.println("[ct62-lora] activateABP: begin() not called");
    return RADIOLIB_ERR_WRONG_MODEM;
  }

  // LoRaWAN 1.0.x: pass nullptr for fNwkSIntKey/sNwkSIntKey; nwkSKey doubles as
  // nwkSEncKey (RadioLib treats it as the single nwkSKey).
  int16_t state = _node->beginABP(devAddr,
                                   nullptr,
                                   nullptr,
                                   nwkSKey,
                                   appSKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] beginABP() failed: %d\n", state);
    _state = Ct62LoRaState::ERROR;
    return state;
  }

  _restoreSession();

  state = _node->activateABP();
  if (state != RADIOLIB_LORAWAN_NEW_SESSION
   && state != RADIOLIB_LORAWAN_SESSION_RESTORED
   && state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] activateABP() failed: %d\n", state);
    _state = Ct62LoRaState::ERROR;
    return state;
  }

  int16_t fin = _finalizeActivation();
  if (fin != RADIOLIB_ERR_NONE) return fin;

  Serial.printf("[ct62-lora] ABP active. devAddr=0x%08lX  DR%u  ADR=%d  dwell=%d\n",
                (unsigned long)devAddr, _currentDR, (int)_cfg.adr, (int)_cfg.dwellTime);
  return RADIOLIB_ERR_NONE;
}

int16_t Ct62LoRaWan::activateOTAA(uint64_t       joinEUI,
                                   uint64_t       devEUI,
                                   const uint8_t  appKey[16],
                                   const uint8_t* nwkKey) {
  if (!_node) {
    Serial.println("[ct62-lora] activateOTAA: begin() not called");
    return RADIOLIB_ERR_WRONG_MODEM;
  }

  // RadioLib 7.x signature: beginOTAA(joinEUI, devEUI, nwkKey, appKey).
  // For 1.0.x deployments nwkKey is nullptr — RadioLib derives session keys
  // from appKey only.
  int16_t state = _node->beginOTAA(joinEUI,
                                    devEUI,
                                    const_cast<uint8_t*>(nwkKey),
                                    const_cast<uint8_t*>(appKey));
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] beginOTAA() failed: %d\n", state);
    _state = Ct62LoRaState::ERROR;
    return state;
  }

  _restoreSession();

  // activateOTAA() blocks until the join completes or RadioLib gives up.
  // Returns RADIOLIB_LORAWAN_NEW_SESSION on a fresh join,
  //         RADIOLIB_LORAWAN_SESSION_RESTORED if NVS restored a prior session.
  state = _node->activateOTAA();
  if (state != RADIOLIB_LORAWAN_NEW_SESSION
   && state != RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.printf("[ct62-lora] activateOTAA() failed: %d\n", state);
    _state = Ct62LoRaState::ERROR;
    return state;
  }

  int16_t fin = _finalizeActivation();
  if (fin != RADIOLIB_ERR_NONE) return fin;

  Serial.printf("[ct62-lora] OTAA joined (state=%d) devEUI=%016llX  DR%u  ADR=%d\n",
                state, (unsigned long long)devEUI, _currentDR, (int)_cfg.adr);
  return RADIOLIB_ERR_NONE;
}

int16_t Ct62LoRaWan::_finalizeActivation() {
  // Order matters: setDatarate() triggers createSession() which re-applies
  // band->dwellTimeUp (or 0 after our AS923-TW patch); setDwellTime(false)
  // must come last to be safe regardless of patch state.
  _node->setADR(_cfg.adr);
  _node->setDatarate(_cfg.defaultDR);
  _node->setDwellTime(_cfg.dwellTime);
  _currentDR = _cfg.defaultDR;
  _saveSession();
  _state = Ct62LoRaState::ACTIVE;
  return RADIOLIB_ERR_NONE;
}

int16_t Ct62LoRaWan::sendUplink(const uint8_t *payload, uint8_t len, uint8_t fPort) {
  if (!isActive()) {
    Serial.println("[ct62-lora] sendUplink: not active");
    return RADIOLIB_ERR_NETWORK_NOT_JOINED;
  }

  LoRaWANEvent_t eventUp;
  int16_t state = _node->sendReceive(
      const_cast<uint8_t *>(payload), len, fPort,
      /*isConfirmed=*/false, &eventUp);

  _saveSession();

  if (state >= RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] uplink sent (%uB, fPort=%u, freq=%.3fMHz, DR%u, %ddBm, FCnt=%lu)\n",
                  len, fPort, eventUp.freq, eventUp.datarate,
                  eventUp.power, (unsigned long)eventUp.fCnt);
    _consecutiveErrors = 0;
    return RADIOLIB_ERR_NONE;
  }

  Serial.printf("[ct62-lora] sendReceive failed: %d (consecutiveErrors=%u)\n",
                state, ++_consecutiveErrors);
  _maybeFallbackDR();
  return state;
}

int16_t Ct62LoRaWan::sendReceive(const uint8_t *dataUp, size_t lenUp, uint8_t upFPort,
                                  uint8_t *dataDown, size_t *downLen, uint8_t *downFPort) {
  if (!isActive()) {
    Serial.println("[ct62-lora] sendReceive: not active");
    return RADIOLIB_ERR_NETWORK_NOT_JOINED;
  }

  LoRaWANEvent_t eventUp{}, eventDown{};
  size_t inLen = (downLen && dataDown) ? *downLen : 0;
  int16_t state = _node->sendReceive(
      const_cast<uint8_t *>(dataUp), lenUp, upFPort,
      dataDown, &inLen,
      /*isConfirmed=*/false, &eventUp, &eventDown);

  _saveSession();

  if (downLen)   *downLen   = inLen;
  if (downFPort) *downFPort = eventDown.fPort;

  if (state == RADIOLIB_LORAWAN_DOWNLINK) {
    Serial.printf("[ct62-lora] uplink sent + downlink received (up fPort=%u FCnt=%lu, down fPort=%u %uB)\n",
                  upFPort, (unsigned long)eventUp.fCnt,
                  eventDown.fPort, (unsigned)inLen);
    _consecutiveErrors = 0;
    return state;
  }

  if (state >= RADIOLIB_ERR_NONE) {
    Serial.printf("[ct62-lora] uplink sent, no downlink (fPort=%u FCnt=%lu DR%u)\n",
                  upFPort, (unsigned long)eventUp.fCnt, eventUp.datarate);
    _consecutiveErrors = 0;
    return RADIOLIB_ERR_NONE;
  }

  Serial.printf("[ct62-lora] sendReceive failed: %d (consecutiveErrors=%u)\n",
                state, ++_consecutiveErrors);
  _maybeFallbackDR();
  return state;
}

void Ct62LoRaWan::radioSleep() {
  if (!_radio) return;
  int16_t rc = _radio->sleep(/*retainConfig=*/true);
  Serial.printf("[ct62-lora] radio sleep rc=%d\n", rc);
}

void Ct62LoRaWan::_maybeFallbackDR() {
  if (_cfg.fallbackThreshold == 0) return;
  if (_consecutiveErrors >= _cfg.fallbackThreshold
   && _currentDR == _cfg.defaultDR) {
    Serial.printf("[ct62-lora] DR%u → DR%u fallback after %u consecutive errors\n",
                  _currentDR, _cfg.fallbackDR, _consecutiveErrors);
    _node->setDatarate(_cfg.fallbackDR);
    _node->setDwellTime(_cfg.dwellTime);
    _currentDR = _cfg.fallbackDR;
    _consecutiveErrors = 0;
    _saveSession();
  }
}

void Ct62LoRaWan::_saveSession() {
  uint8_t *buf = _node->getBufferSession();
  _prefs.begin(_cfg.nvsNamespace, /*readOnly=*/false);
  _prefs.putBytes(NVS_SESSION_KEY, buf, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  _prefs.end();
}

void Ct62LoRaWan::_restoreSession() {
  _prefs.begin(_cfg.nvsNamespace, /*readOnly=*/true);
  size_t len = _prefs.getBytesLength(NVS_SESSION_KEY);
  if (len == RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    uint8_t buf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    _prefs.getBytes(NVS_SESSION_KEY, buf, len);
    int16_t rc = _node->setBufferSession(buf);
    if (rc == RADIOLIB_ERR_NONE || rc == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      Serial.println("[ct62-lora] FCntUp restored from NVS");
    } else {
      Serial.printf("[ct62-lora] setBufferSession failed: %d (will start fresh)\n", rc);
    }
  } else {
    Serial.println("[ct62-lora] no saved session — starting fresh FCntUp=0");
  }
  _prefs.end();
}

} // namespace siliqs
