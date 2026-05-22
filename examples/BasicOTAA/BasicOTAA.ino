// BasicOTAA — minimal OTAA uplink loop for Heltec HT-CT62 (ESP32-C3 + SX1262)
//
// Replace JOIN_EUI / DEV_EUI / APP_KEY with the values ChirpStack (or your LNS)
// generated for this device. For LoRaWAN 1.0.x leave nwkKey as nullptr — the
// library passes that through to RadioLib so session keys are derived from
// appKey alone.

#include <Arduino.h>
#include <SPI.h>
#include <siliqs_ct62.h>

static siliqs::Ct62LoRaWan   lora;
static siliqs::Ct62LoRaConfig cfg;

// REPLACE THESE WITH YOUR DEVICE'S OTAA CREDENTIALS
static const uint64_t JOIN_EUI = 0x0000000000000000ULL;
static const uint64_t DEV_EUI  = 0x0000000000000000ULL;
static const uint8_t  APP_KEY[16] = {0};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== siliqs_ct62 BasicOTAA example ===");

  SPI.begin(cfg.pins.sck, cfg.pins.miso, cfg.pins.mosi, cfg.pins.nss);

  int16_t rc = lora.begin(cfg);
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("lora.begin failed: %d\n", rc);
    while (true) delay(1000);
  }

  // activateOTAA blocks until join completes (or RadioLib gives up retrying).
  rc = lora.activateOTAA(JOIN_EUI, DEV_EUI, APP_KEY, /*nwkKey=*/nullptr);
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("activateOTAA failed: %d\n", rc);
    while (true) delay(1000);
  }
}

void loop() {
  uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
  lora.sendUplink(payload, sizeof(payload), /*fPort=*/2);
  delay(60000);
}
