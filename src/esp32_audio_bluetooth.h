#pragma once
#include "BluetoothA2DPSink.h"
#include "esp32_audio_crossover_config.h"
#include "esp32_audio_dsp.h"
#include "esp32_audio_i2s_handler.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

// Forward declaration for OLED Toast Notification
void uiShowToast(const char *msg, uint32_t durationMs);

inline BluetoothA2DPSink g_a2dpSink;
inline QueueHandle_t g_audioQueue = nullptr;
inline std::atomic<bool> g_btConnected{false};
inline std::atomic<uint16_t> g_btSampleRate{0};
inline std::atomic<bool> g_btSampleRateSupported{false};
inline char g_trackTitle[64] = "Waiting for Bluetooth...";
inline portMUX_TYPE g_btStateMux = portMUX_INITIALIZER_UNLOCKED;

// ── Bluetooth Notification Tone Synthesizer State ───────────────────────────
enum class ToneType : uint8_t { NONE = 0, CONNECTED, DISCONNECTED };
inline std::atomic<ToneType> g_pendingBtTone{ToneType::NONE};
inline std::atomic<uint32_t> g_btToneRemainingBlocks{0};
inline ToneType g_currentBtTone = ToneType::NONE;
inline uint32_t g_toneSampleIndex = 0;

inline void updateMuteState() {
  const bool connected = g_btConnected.load(std::memory_order_acquire);
  const bool rateOk = g_btSampleRateSupported.load(std::memory_order_acquire);
  const bool safe = g_systemSafeMode.load(std::memory_order_acquire);
  const bool toneActive =
      g_btToneRemainingBlocks.load(std::memory_order_acquire) > 0;

  if ((connected && rateOk && !safe) || (toneActive && !safe)) {
    g_audioMuted.store(false, std::memory_order_release);
  } else {
    g_audioMuted.store(true, std::memory_order_release);
  }
}

inline void triggerBtTone(ToneType type) {
  g_pendingBtTone.store(type, std::memory_order_release);
  // 104 blocks of 128 frames @ 44.1kHz ≈ 300 ms total sound duration
  g_btToneRemainingBlocks.store(104, std::memory_order_release);
  updateMuteState();
}

inline AudioBlock generateToneBlock() {
  AudioBlock block;
  block.frames = AUDIO_BLOCK_FRAMES;

  ToneType type =
      g_pendingBtTone.exchange(ToneType::NONE, std::memory_order_acq_rel);
  if (type != ToneType::NONE) {
    g_currentBtTone = type;
    g_toneSampleIndex = 0;
  }

  const uint32_t halfTotal = 6656;    // 52 blocks * 128
  const float vol = 0.20f * 32767.0f; // 20% peak volume (-14 dBFS)

  for (size_t i = 0; i < AUDIO_BLOCK_FRAMES; i++) {
    const uint32_t sampleIdx = g_toneSampleIndex++;
    float freq = 440.0f;
    uint32_t noteIdx = sampleIdx;

    if (sampleIdx < halfTotal) {
      freq = (g_currentBtTone == ToneType::CONNECTED) ? 523.25f : 659.25f;
      noteIdx = sampleIdx;
    } else {
      freq = (g_currentBtTone == ToneType::CONNECTED) ? 659.25f : 440.00f;
      noteIdx = sampleIdx - halfTotal;
    }

    const float phase = 2.0f * (float)M_PI * freq *
                        ((float)sampleIdx / (float)AUDIO_SAMPLE_RATE);
    float sampleVal = sinf(phase);

    // Envelope (5ms attack / 5ms decay for each note)
    const uint32_t rampSamples = 220;
    float env = 1.0f;
    if (noteIdx < rampSamples) {
      env = (float)noteIdx / (float)rampSamples;
    } else if (noteIdx > (halfTotal - rampSamples)) {
      env = (float)(halfTotal - noteIdx) / (float)rampSamples;
    }

    int16_t outSample = (int16_t)(sampleVal * vol * env);
    block.samples[i].l = outSample;
    block.samples[i].r = outSample;
  }

  uint32_t rem = g_btToneRemainingBlocks.load(std::memory_order_relaxed);
  if (rem > 0) {
    rem--;
    g_btToneRemainingBlocks.store(rem, std::memory_order_release);
    if (rem == 0) {
      g_currentBtTone = ToneType::NONE;
      updateMuteState();
    }
  }

  return block;
}

inline void dspAudioTask(void *) {
  AudioBlock block;
  static int32_t i2s0Out[AUDIO_BLOCK_FRAMES * 2];
  static int32_t i2s1Out[AUDIO_BLOCK_FRAMES * 2];

  for (;;) {
    uint32_t toneRem = g_btToneRemainingBlocks.load(std::memory_order_acquire);
    if (toneRem > 0) {
      block = generateToneBlock();
      // Drain any stale BT audio from queue while tone is playing
      AudioBlock dummy;
      while (xQueueReceive(g_audioQueue, &dummy, 0) == pdTRUE) {
      }

      dspProcessBlock(block, i2s0Out, i2s1Out);
      if (i2sWriteBlock(i2s0Out, i2s1Out, block.frames)) {
        g_processedBlocks.fetch_add(1, std::memory_order_relaxed);
        g_successfulI2SWrites.fetch_add(1, std::memory_order_relaxed);
        g_dspHeartbeatMs.store(millis(), std::memory_order_relaxed);
      }
      continue;
    }

    if (xQueueReceive(g_audioQueue, &block, pdMS_TO_TICKS(2)) == pdTRUE) {
      dspProcessBlock(block, i2s0Out, i2s1Out);
      if (i2sWriteBlock(i2s0Out, i2s1Out, block.frames)) {
        g_processedBlocks.fetch_add(1, std::memory_order_relaxed);
        g_successfulI2SWrites.fetch_add(1, std::memory_order_relaxed);
        g_dspHeartbeatMs.store(millis(), std::memory_order_relaxed);
      } else {
        g_audioDrops.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      // Feed both TX DMA streams with silence during an A2DP underrun.
      g_queueUnderflows.fetch_add(1, std::memory_order_relaxed);
      memset(i2s0Out, 0, sizeof(i2s0Out));
      memset(i2s1Out, 0, sizeof(i2s1Out));
      for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
        g_vuLevels[ch] = 0.0f;
      }
      if (i2sWriteBlock(i2s0Out, i2s1Out, AUDIO_BLOCK_FRAMES)) {
        g_successfulI2SWrites.fetch_add(1, std::memory_order_relaxed);
        g_dspHeartbeatMs.store(millis(), std::memory_order_relaxed);
      }
      taskYIELD();
    }
  }
}

inline void audioDataCallback(const uint8_t *data, uint32_t length) {
  if (!g_audioQueue || !data || length < 4)
    return;

  const int16_t *src = reinterpret_cast<const int16_t *>(data);
  size_t frames = length / (2 * sizeof(int16_t));

  while (frames) {
    AudioBlock block;
    block.frames = (uint16_t)min(frames, AUDIO_BLOCK_FRAMES);
    memcpy(block.samples, src, block.frames * sizeof(Stereo16));

    if (xQueueSend(g_audioQueue, &block, pdMS_TO_TICKS(10)) != pdTRUE) {
      g_queueOverflows.fetch_add(1, std::memory_order_relaxed);
      g_audioDrops.fetch_add(block.frames, std::memory_order_relaxed);
    }

    src += block.frames * 2;
    frames -= block.frames;
  }

  const uint32_t waiting = (uint32_t)uxQueueMessagesWaiting(g_audioQueue);
  g_queueCurrentLevel.store(waiting, std::memory_order_relaxed);
  uint32_t high = g_queueHighWater.load(std::memory_order_relaxed);
  while (waiting > high && !g_queueHighWater.compare_exchange_weak(
                               high, waiting, std::memory_order_relaxed,
                               std::memory_order_relaxed)) {
  }
}

inline void btConnectionCallback(esp_a2d_connection_state_t state, void *) {
  const bool connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
  const bool wasConnected = g_btConnected.load(std::memory_order_acquire);

  g_btConnected.store(connected, std::memory_order_release);
  if (!connected) {
    portENTER_CRITICAL(&g_btStateMux);
    strncpy(g_trackTitle, "Waiting for Bluetooth...", sizeof(g_trackTitle) - 1);
    g_trackTitle[sizeof(g_trackTitle) - 1] = '\0';
    portEXIT_CRITICAL(&g_btStateMux);
  }

  // Notifikasi BT Connected / Disconnected (OLED Toast + Audio Chime + Serial)
  if (connected && !wasConnected) {
    uiShowToast("BT CONNECTED", 2000);
    Serial.println("[BT] >>> BLUETOOTH CONNECTED <<<");
    triggerBtTone(ToneType::CONNECTED);
  } else if (!connected && wasConnected) {
    uiShowToast("BT DISCONNECTED", 2000);
    Serial.println("[BT] >>> BLUETOOTH DISCONNECTED <<<");
    triggerBtTone(ToneType::DISCONNECTED);
  }

  // B-09 FIX: Centralized mute state evaluation
  updateMuteState();
  g_uiDirty.store(true, std::memory_order_release);
}

inline void btSampleRateCallback(uint16_t sampleRate) {
  const bool supported = (sampleRate == AUDIO_SAMPLE_RATE);
  g_btSampleRate.store(sampleRate, std::memory_order_release);
  g_btSampleRateSupported.store(supported, std::memory_order_release);
  if (!supported) {
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    g_systemFaults.fetch_or(static_cast<uint32_t>(SystemFault::A2DP_RATE_FAIL),
                            std::memory_order_relaxed);
    Serial.printf(
        "[BT] Unsupported A2DP rate %u Hz; output muted (44.1 kHz required).\n",
        sampleRate);
  }
  // B-09 FIX: Centralized mute state evaluation
  updateMuteState();
  g_uiDirty.store(true, std::memory_order_release);
}

inline void avrcMetadataCallback(uint8_t id, const uint8_t *text) {
  if (!text)
    return;
  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    portENTER_CRITICAL(&g_btStateMux);

    const uint8_t *src = text;
    size_t j = 0;

    while (*src != '\0' && j < sizeof(g_trackTitle) - 1) {
      uint8_t c = *src;

      // Deteksi pola 3-byte UTF-8 yang umum (biasanya diawali 0xE2 0x80)
      if (c == 0xE2 && *(src + 1) == 0x80) {
        uint8_t next_c = *(src + 2);

        // Terjemahkan 3 byte UTF-8 menjadi 1 byte ASCII
        if (next_c == 0x98 ||
            next_c == 0x99) { // Petik tunggal melengkung (‘ atau ’)
          g_trackTitle[j++] = '\'';
        } else if (next_c == 0x9C ||
                   next_c == 0x9D) { // Petik ganda melengkung (“ atau ”)
          g_trackTitle[j++] = '"';
        } else if (next_c == 0x93 ||
                   next_c == 0x94) { // Strip panjang (en-dash / em-dash —)
          g_trackTitle[j++] = '-';
        } else if (next_c == 0xA6) { // Titik tiga (ellipsis …)
          g_trackTitle[j++] = '.';
        } else {
          g_trackTitle[j++] = ' '; // Jika tidak dikenali, ganti spasi
        }

        src += 3; // Lompati 3 byte UTF-8 karena sudah digantikan oleh 1
                  // karakter ASCII
        continue;
      }

      // Abaikan byte karakter UTF-8 lain (byte >= 128) agar tidak muncul
      // sebagai simbol acak
      if (c >= 0x80) {
        src++;
        continue;
      }

      // Hanya simpan karakter ASCII standar yang bisa dibaca layar OLED
      if (c >= 32 && c <= 126) {
        g_trackTitle[j++] = c;
      } else {
        g_trackTitle[j++] = ' ';
      }

      src++;
    }

    g_trackTitle[j] = '\0'; // Kunci string dengan aman
    portEXIT_CRITICAL(&g_btStateMux);
    g_uiDirty = true;
  }
}

inline void btStatusSnapshot(bool &connected, char *title, size_t titleLen) {
  connected = g_btConnected.load(std::memory_order_acquire);
  if (!title || titleLen == 0)
    return;
  portENTER_CRITICAL(&g_btStateMux);
  strncpy(title, g_trackTitle, titleLen - 1);
  title[titleLen - 1] = '\0';
  portEXIT_CRITICAL(&g_btStateMux);
}

inline void bluetoothBegin() {
  g_audioQueue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(AudioBlock));
  if (!g_audioQueue) {
    Serial.println(
        "[BT] ERROR: Audio queue allocation failed. Entering Safe Mode.");
    triggerSafeMode(SystemFault::QUEUE_OVERFLOW);
    return;
  }

  BaseType_t taskRes = xTaskCreatePinnedToCore(dspAudioTask, "DSP_AUDIO", 8192,
                                               nullptr, 15, nullptr, 1);
  if (taskRes != pdPASS) {
    Serial.println(
        "[DSP] ERROR: Failed to create DSP Audio Task. Entering Safe Mode.");
    triggerSafeMode(SystemFault::DSP_TASK_FAIL);
    return;
  }

  // Disable A2DP library's own I2S output; this project owns both I2S buses.
  g_a2dpSink.set_stream_reader(audioDataCallback, false);
  g_a2dpSink.set_sample_rate_callback(btSampleRateCallback);
  g_a2dpSink.set_on_connection_state_changed(btConnectionCallback);
  g_a2dpSink.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE);
  g_a2dpSink.set_avrc_metadata_callback(avrcMetadataCallback);
  g_a2dpSink.start(BT_DEVICE_NAME);
  Serial.println("[BT] Bluetooth A2DP Sink Ready.");
}
