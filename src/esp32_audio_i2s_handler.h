#pragma once
#include "esp32_audio_crossover_config.h"
#include <ESP_I2S.h>

inline I2SClass g_i2sMid(I2S_NUM_0);
inline I2SClass g_i2sSubHigh(I2S_NUM_1);

inline bool i2sAudioBegin() {
  g_i2sMid.setPins(PIN_I2S0_BCK, PIN_I2S0_WS, PIN_I2S0_DOUT);
  g_i2sSubHigh.setPins(PIN_I2S1_BCK, PIN_I2S1_WS, PIN_I2S1_DOUT);

  // 32-bit I2S transport depth carrying 24-bit audio resolution
  bool a = g_i2sMid.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE,
                          I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  bool b = g_i2sSubHigh.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE,
                              I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);

  if (!a || !b) {
    Serial.println("[I2S] ERROR: Dual I2S initialization failed! Entering Safe Mode.");
    triggerSafeMode(SystemFault::I2S_INIT_FAIL);
    return false;
  }
  Serial.println("[I2S] Dual I2S Bus Initialized (I2S0 & I2S1 @ 44.1kHz / 32-bit slot).");
  return true;
}

// B-14 FIX: Elevate consecutiveErrors to file-level static so it can be
// reset by i2sResetErrorCounter() when Safe Mode is cleared on recovery.
static uint32_t s_i2sConsecutiveErrors = 0;

// Direct, non-fragmented, synchronous block transfer to dual I2S DMA controllers
// Eliminates artificial chunking and context switches that cause audio dropouts
inline bool i2sWriteBlock(const int32_t* i2s0, const int32_t* i2s1, size_t frames) {
  if (g_systemSafeMode.load(std::memory_order_relaxed)) {
    return false;
  }

  const size_t bytesToWrite = frames * 2 * sizeof(int32_t); // 2 channels per I2S bus
  
  const size_t written0 = g_i2sMid.write((const uint8_t*)i2s0, bytesToWrite);
  const size_t written1 = g_i2sSubHigh.write((const uint8_t*)i2s1, bytesToWrite);

  const bool success = (written0 == bytesToWrite) && (written1 == bytesToWrite);
  // A partial transfer desynchronizes the two independent I2S DMA streams.
  // Mute immediately rather than allowing phase-misaligned crossover outputs.
  if (!success && (written0 != 0 || written1 != 0)) {
    g_i2sErrors.fetch_add(1, std::memory_order_relaxed);
    Serial.printf("[I2S] CRITICAL: Partial dual-bus write (%u/%u, %u/%u). Entering Safe Mode.\n",
                  (unsigned)written0, (unsigned)bytesToWrite,
                  (unsigned)written1, (unsigned)bytesToWrite);
    triggerSafeMode(SystemFault::I2S_WRITE_FAIL);
    return false;
  }
  
  if (!success) {
    g_i2sErrors.fetch_add(1, std::memory_order_relaxed);
    s_i2sConsecutiveErrors++;
    if (s_i2sConsecutiveErrors >= 5) {
      Serial.println("[I2S] CRITICAL: Consecutive I2S DMA write failures. Entering Safe Mode.");
      triggerSafeMode(SystemFault::I2S_WRITE_FAIL);
    }
  } else {
    s_i2sConsecutiveErrors = 0;
  }

  return success;
}

// B-14 FIX: Reset the consecutive error counter when Safe Mode is cleared.
// Must be called from clearSafeMode() in esp32_audio_dsp.h.
inline void i2sResetErrorCounter() {
  s_i2sConsecutiveErrors = 0;
}
