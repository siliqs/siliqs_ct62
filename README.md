# siliqs_ct62

LoRaWAN driver library for Heltec HT-CT62 (ESP32-C3 + SX1262) modules.

Thin RadioLib-based wrapper that encodes the policies we validated across
Siliqs HT-CT62 boards (Belt / SQC485I / SQ001S): ABP and OTAA activation,
NVS-persisted FCntUp, automatic DR fallback after consecutive failures,
config-retain radio sleep before deep-sleep, and the AS923-TW band patch
(applied at build time by a PlatformIO pre-script).

## Install

### PlatformIO

```ini
lib_deps =
    https://github.com/siliqs/siliqs_ct62.git
```

If you target AS923-TW (NCC Taiwan, no 400 ms dwell), enable the band patch
in `platformio.ini`:

```ini
extra_scripts =
    pre:.pio/libdeps/<env>/siliqs_ct62/scripts/patch_radiolib_as923tw.py
```

The script is idempotent and only runs once per `.pio/libdeps` checkout.

### Arduino IDE

Library Manager: search "siliqs_ct62". Or git clone into `Documents/Arduino/libraries/`.

The AS923-TW patch is PlatformIO-only; on Arduino IDE apply the same five
field changes to `LoRaWANBands.cpp` by hand (see `scripts/patch_radiolib_as923tw.py`).

## Quick start — ABP

```cpp
#include <SPI.h>
#include <siliqs_ct62.h>

siliqs::Ct62LoRaWan   lora;
siliqs::Ct62LoRaConfig cfg;   // defaults: AS923 sub-band 0, DR5 → DR3 fallback

void setup() {
  Serial.begin(115200);
  SPI.begin(cfg.pins.sck, cfg.pins.miso, cfg.pins.mosi, cfg.pins.nss);
  lora.begin(cfg);
  lora.activateABP(devAddr, nwkSKey, appSKey);
}

void loop() {
  uint8_t payload[] = { 0xAA, 0xBB };
  lora.sendUplink(payload, sizeof(payload), /*fPort=*/2);
  delay(60000);
}
```

See [`examples/BasicABP`](examples/BasicABP/BasicABP.ino) and [`examples/BasicOTAA`](examples/BasicOTAA/BasicOTAA.ino).

## API surface

| Call | Purpose |
|---|---|
| `begin(cfg)` | Construct radio + node. Idempotent. Caller must `SPI.begin(...)` first. |
| `activateABP(devAddr, nwkSKey, appSKey)` | LoRaWAN 1.0.x ABP. Restores FCntUp from NVS if present. |
| `activateOTAA(joinEUI, devEUI, appKey, nwkKey=nullptr)` | Blocking join. `nwkKey=nullptr` for 1.0.x. |
| `sendUplink(data, len, fPort)` | Unconfirmed uplink. Persists FCntUp on every send. |
| `sendReceive(...)` | Same as above but captures any RX1/RX2 downlink. |
| `radioSleep()` | Config-retain SX1262 sleep — call before `esp_deep_sleep_start()`. |
| `isActive()` / `currentDR()` | Status accessors. |

## HT-CT62 pinout

ESP32-C3 GPIO numbers brought out from the HT-CT62 module — same on every
HT-CT62 carrier board. Override fields on `cfg.pins` if a future variant repins.

| Signal | GPIO |
|---|---|
| NSS  | 8  |
| SCK  | 10 |
| MISO | 6  |
| MOSI | 7  |
| BUSY | 4  |
| NRST | 5  |
| DIO1 | 3  |

## FCntUp persistence

`sendUplink()` and `sendReceive()` call `Preferences.putBytes()` on NVS
namespace `"lorawan"`, key `"session"`, after every successful send.
`activateABP()` / `activateOTAA()` restore the same blob on boot so FCntUp
stays monotonic across resets — LoRaWAN 1.0.x requires this or the LNS will
drop uplinks as replays.

To deliberately rewind FCntUp (e.g. for a fresh ChirpStack profile):

```cpp
Preferences p;
p.begin("lorawan", false);
p.clear();
p.end();
ESP.restart();
```

(Pair with ChirpStack → Device → Reset frame counters.)

## Dependencies

- [RadioLib](https://github.com/jgromes/RadioLib) ^7.1.2
- ESP32 Arduino core (Preferences)

## License

MIT. See [LICENSE](LICENSE).
