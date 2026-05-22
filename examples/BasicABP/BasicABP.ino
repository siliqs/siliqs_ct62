// BasicABP — minimal ABP uplink loop for Heltec HT-CT62 (ESP32-C3 + SX1262)
//
// Replace devAddr / nwkSKey / appSKey with the values ChirpStack (or your LNS)
// generated for this device. AppKey/NwkKey are LoRaWAN session keys, NOT the
// OTAA AppKey.

#include <Arduino.h>
#include <SPI.h>
#include <siliqs_ct62.h>

static siliqs::Ct62LoRaWan   lora;
static siliqs::Ct62LoRaConfig cfg;   // defaults: AS923 sub-band 0, DR5 → DR3 fallback, ADR off, dwell off

// REPLACE THESE WITH YOUR PROVISIONED ABP CREDENTIALS
static const uint32_t DEV_ADDR = 0x01234567;
static const uint8_t  NWK_SKEY[16] = {0};
static const uint8_t  APP_SKEY[16] = {0};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== siliqs_ct62 BasicABP example ===");

  SPI.begin(cfg.pins.sck, cfg.pins.miso, cfg.pins.mosi, cfg.pins.nss);

  int16_t rc = lora.begin(cfg);
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("lora.begin failed: %d\n", rc);
    while (true) delay(1000);
  }

  rc = lora.activateABP(DEV_ADDR, NWK_SKEY, APP_SKEY);
  if (rc != RADIOLIB_ERR_NONE) {
    Serial.printf("activateABP failed: %d\n", rc);
    while (true) delay(1000);
  }
}

void loop() {
  uint8_t payload[] = { 0xAA, 0xBB, 0xCC, 0xDD };
  lora.sendUplink(payload, sizeof(payload), /*fPort=*/2);
  delay(60000);   // 60 s, well clear of AS923 duty-cycle limits at DR5
}
