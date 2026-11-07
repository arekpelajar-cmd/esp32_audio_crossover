#pragma once
#include "esp32_audio_crossover_config.h"
#include "esp32_audio_bluetooth.h"

inline volatile bool g_btSelectMode = false;
inline volatile bool g_btShowingStatus = false;

// Simple Bluetooth device selection via menu
// User can disconnect and reconnect to switch devices

inline void btDisconnectTask(void*) {
  Serial.println("BT: Disconnecting current device (async)...");
  g_a2dpSink.disconnect();
  vTaskDelete(nullptr);
}

inline void btSelectDeviceMode() {
  g_btSelectMode = true;
  g_btShowingStatus = true;
  
  if (g_btConnected.load(std::memory_order_acquire)) {
    xTaskCreate(btDisconnectTask, "BT_DISC", 2048, nullptr, 2, nullptr);
  }
  
  Serial.println("BT: Ready to accept connections");
  Serial.print("BT: Turn on Bluetooth on your device and connect to: ");
  Serial.println(BT_DEVICE_NAME);
}

inline void btExitSelectMode() {
  g_btSelectMode = false;
  g_btShowingStatus = false;
}
