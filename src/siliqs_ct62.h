#pragma once

// Umbrella header for the Siliqs CT62 library.
//
// HT-CT62 = ESP32-C3FN4 + SX1262 LoRa module. This library wraps RadioLib's
// LoRaWAN stack with the policies we've validated across Siliqs HT-CT62 boards
// (Belt / SQC485I / SQ001S): ABP + OTAA activation, NVS-persisted FCntUp,
// automatic DR fallback after consecutive failures, config-retain radio sleep
// before deep-sleep, and the AS923-TW band patch (applied at build time by
// scripts/patch_radiolib_as923tw.py — see README for how to wire it into
// platformio.ini).

#include "ct62_lora.h"
