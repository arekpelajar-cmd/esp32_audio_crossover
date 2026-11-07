#include <Arduino.h>
#include "esp32_audio_crossover_config.h"
#include "esp32_audio_nvs.h"
#include "esp32_audio_dsp.h"
#include "esp32_audio_i2s_handler.h"
#include "esp32_audio_bluetooth.h"
#include "esp32_audio_oled_ui.h"
#include "esp32_audio_encoder.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=============================================");
  Serial.println("  ESP32 4-CHANNEL MATRIX DSP (ESP32-WROOM)  ");
  Serial.println("  LR24 Crossover | 5-Band PEQ | Time Delay   ");
  Serial.println("=============================================\n");

  configLoadDefaults();  // set defaults first
  nvsLoad();             // then override with saved values from NVS
  i2sAudioBegin();
  dspBegin();
  uiBegin();
  encoderBegin();
  bluetoothBegin();

  Serial.println("[SYSTEM] DSP Engine & Interface Ready.");
  Serial.print("[SYSTEM] Bluetooth Name: ");
  Serial.println(BT_DEVICE_NAME);
}

void loop() {
  // UI / encoder interaction runs on Core 1; Core 0 handles real-time audio DSP task
  encoderProcess();
  nvsAutoSave();  // debounced 3-second auto-save after parameter modification
  uiLoop();

  // DSP Heartbeat Supervisor (Watchdog on Core 1 for Core 0 DSP engine)
  // B-16 NOTE: Unsigned 32-bit subtraction (now - lastHb) handles millis() rollover
  // safely after ~49.7 days without arithmetic overflow issues.
  static uint32_t lastSupervisorCheck = 0;
  const uint32_t now = millis();
  if (now - lastSupervisorCheck >= 500) {
    lastSupervisorCheck = now;
    const uint32_t lastHb = g_dspHeartbeatMs.load(std::memory_order_relaxed);
    // FIX #1: Remove BT dependency — monitor DSP regardless of Bluetooth status
    if (lastHb > 0 && (now - lastHb > 1200)) {
      Serial.println("[SUPERVISOR] CRITICAL: DSP Core 0 Heartbeat Stalled! Entering Safe Mode.");
      triggerSafeMode(SystemFault::DSP_TIMEOUT);
      g_uiDirty.store(true, std::memory_order_release);
    }
  }

  delay(2);
}
