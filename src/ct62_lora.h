#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>

namespace siliqs {

// HT-CT62 default LoRa pinout. These are ESP32-C3 GPIO numbers brought out
// from the module — same across Belt (XDA003B), SQC485I, SQ001S, and any
// other HT-CT62 carrier. Override per-board if a future variant repins.
struct Ct62LoRaPins {
  int8_t nss  = 8;
  int8_t sck  = 10;
  int8_t miso = 6;
  int8_t mosi = 7;
  int8_t busy = 4;
  int8_t nrst = 5;
  int8_t dio1 = 3;
};

struct Ct62LoRaConfig {
  Ct62LoRaPins pins;
  const LoRaWANBand_t* band = &AS923;     // see scripts/patch_radiolib_as923tw.py for the AS923-TW patch
  uint8_t subBand           = 0;           // AS923 sub-band 0 = AS923-1
  float   tcxoVoltage       = 0.0f;        // HT-CT62 uses a crystal — 0.0 disables TCXO control
  uint8_t defaultDR         = 5;           // AS923 DR5 = SF7BW125
  uint8_t fallbackDR        = 3;           // DR3 = SF9BW125
  uint8_t fallbackThreshold = 5;           // consecutive errors before DR fallback
  bool    adr               = false;       // ChirpStack + AS923-TW deployment policy
  bool    dwellTime         = false;       // NCC Taiwan does not mandate the 400 ms cap
  const char* nvsNamespace  = "lorawan";   // Preferences namespace for FCntUp persistence
};

enum class Ct62LoRaState : uint8_t {
  IDLE,
  ACTIVE,
  ERROR,
};

class Ct62LoRaWan {
public:
  Ct62LoRaWan() = default;
  ~Ct62LoRaWan();

  // Construct the radio + node from cfg. Idempotent — safe to re-call.
  // Does NOT establish a session: call activateABP() or activateOTAA() after.
  // Caller is responsible for SPI.begin(...) with the same pins beforehand.
  int16_t begin(const Ct62LoRaConfig& cfg);

  // ABP activation. LoRaWAN 1.0.x: pass the 16-byte nwkSKey AND appSKey; the
  // fNwkSIntKey/sNwkSIntKey slots are sent as nullptr.
  int16_t activateABP(uint32_t       devAddr,
                      const uint8_t  nwkSKey[16],
                      const uint8_t  appSKey[16]);

  // OTAA activation. Performs the join handshake (blocks on RX1/RX2 windows
  // — RadioLib handles the retry/back-off internally). For LoRaWAN 1.0.x pass
  // nwkKey=nullptr and RadioLib will derive session keys from appKey alone.
  int16_t activateOTAA(uint64_t       joinEUI,
                       uint64_t       devEUI,
                       const uint8_t  appKey[16],
                       const uint8_t* nwkKey = nullptr);

  // Unconfirmed uplink. FCntUp persists to NVS after every send.
  int16_t sendUplink(const uint8_t *payload, uint8_t len, uint8_t fPort);

  // Unconfirmed uplink + capture any downlink that arrives in RX1/RX2.
  // *downLen must be initialised to the size of dataDown before calling and is
  // updated to the actual received length. Returns RADIOLIB_LORAWAN_DOWNLINK
  // when a downlink was received, RADIOLIB_ERR_NONE on a successful uplink
  // with no downlink, or a negative RadioLib error code.
  int16_t sendReceive(const uint8_t *dataUp,  size_t   lenUp,    uint8_t  upFPort,
                      uint8_t       *dataDown, size_t  *downLen,  uint8_t *downFPort = nullptr);

  // Drop the SX1262 into its lowest-power sleep mode (config-retain). Call
  // before esp_deep_sleep_start() — a SX1262 left in standby still pulls
  // ~600 µA - 1.5 mA, which dominates the chip's own ~5 µA deep-sleep draw.
  void radioSleep();

  bool         isActive()  const { return _state == Ct62LoRaState::ACTIVE; }
  uint8_t      currentDR() const { return _currentDR; }
  SX1262*      rawRadio()        { return _radio; }
  LoRaWANNode* rawNode()         { return _node;  }

private:
  SX1262        *_radio = nullptr;
  LoRaWANNode   *_node  = nullptr;
  Ct62LoRaState  _state = Ct62LoRaState::IDLE;
  Ct62LoRaConfig _cfg{};
  Preferences    _prefs;
  uint8_t        _currentDR         = 5;
  uint8_t        _consecutiveErrors = 0;

  int16_t _finalizeActivation();
  void    _saveSession();
  void    _restoreSession();
  void    _maybeFallbackDR();
};

} // namespace siliqs
