#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <math.h>

// ---------------- Hardware Configuration ----------------
#define BT_DEVICE_NAME "DLMS BALAPs"

constexpr int PIN_I2S0_BCK = 5;
constexpr int PIN_I2S0_WS = 18;
constexpr int PIN_I2S0_DOUT = 19;

constexpr int PIN_I2S1_BCK = 4;
constexpr int PIN_I2S1_WS = 13;
constexpr int PIN_I2S1_DOUT = 14;

constexpr int PIN_OLED_SDA = 21;
constexpr int PIN_OLED_SCL = 22;
constexpr uint8_t OLED_ADDR = 0x3C;

constexpr int PIN_ENC_CLK = 32;
constexpr int PIN_ENC_DT = 33;
constexpr int PIN_ENC_SW = 27;

// ---------------- Audio Engine Parameters ----------------
constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
constexpr size_t AUDIO_BLOCK_FRAMES = 128;
constexpr size_t AUDIO_QUEUE_DEPTH = 20;
constexpr float DSP_HEADROOM = 0.95f;
constexpr float LIMITER_THRESHOLD =
    0.95f; // Threshold before brickwall limiting (-0.45 dBFS)
constexpr float LIMITER_CEILING =
    0.98f; // Absolute brickwall ceiling clamp (-0.18 dBFS)
constexpr float LIMITER_ATTACK_MS = 0.1f;   // Sub-millisecond peak capture
constexpr float LIMITER_RELEASE_MS = 60.0f; // Smooth transparent release time
constexpr size_t LIMITER_LOOKAHEAD_SAMPLES =
    16; // 16 samples = ~0.36 ms latency @ 44.1 kHz
constexpr float TWEETER_MIN_FREQ_HZ =
    1500.0f; // Tweeter physical safety frequency floor (1.5 kHz)
constexpr size_t DELAY_BUFFER_MAX =
    128; // Internal RAM buffer (128 samples ≈ 2.8ms @ 44.1kHz for ESP32-WROOM)
constexpr float MAX_DELAY_MS = 2.8f;
constexpr uint8_t MATRIX_CHANNEL_COUNT = 4;
constexpr uint8_t PEQ_BAND_COUNT = 5;

// ---------------- System Fault & Diagnostic Definitions ----------------
enum class SystemFault : uint32_t {
  NONE = 0,
  I2S_INIT_FAIL = (1 << 0),
  I2S_WRITE_FAIL = (1 << 1),
  DSP_TASK_FAIL = (1 << 2),
  DSP_TIMEOUT = (1 << 3),
  QUEUE_OVERFLOW = (1 << 4),
  NVS_CRC_FAIL = (1 << 5),
  PSRAM_FAIL = (1 << 6),
  A2DP_RATE_FAIL = (1 << 7)
};

struct Stereo16 {
  int16_t l;
  int16_t r;
};

struct AudioBlock {
  uint16_t frames;
  Stereo16 samples[AUDIO_BLOCK_FRAMES];
};

enum class InputSource : uint8_t { SRC_LEFT = 0, SRC_RIGHT, SRC_MONO };

enum class FilterBand : uint8_t {
  BAND_SUB = 0,
  BAND_MID,
  BAND_HIGH,
  BAND_FULL
};

enum class Preset : uint8_t { FLAT = 0, BASS_BOOST, VOCAL, ROCK, CUSTOM };

enum class PEQType : uint8_t { PEAKING = 0, LOW_SHELF, HIGH_SHELF };

struct PEQBandConfig {
  float freq = 1000.0f; // 20 Hz - 20000 Hz
  float q = 1.0f;       // 0.1 - 10.0
  float gainDb = 0.0f;  // -12.0 dB - +12.0 dB
  PEQType type = PEQType::PEAKING;
  bool enabled = true;
};

struct OutputChannelConfig {
  InputSource source = InputSource::SRC_MONO;
  FilterBand band = FilterBand::BAND_FULL;
  float gainDb = 0.0f; // -20.0 dB - +6.0 dB
  bool mute = false;
  float delayMs = 0.0f;     // 0.0 ms - 20.0 ms (Time Alignment)
  bool phaseInvert = false; // false = 0°, true = 180° (Polarity Inversion)
};

struct AudioSettings {
  uint8_t masterVolume = 85;

  // Linkwitz-Riley 24dB Crossover Frequencies
  float subLpfHz = 120.0f;
  float midLowHz = 120.0f;
  float midHighHz = 3500.0f;
  float highHpfHz = 3500.0f;

  // 5-Band Parametric EQ
  PEQBandConfig peq[PEQ_BAND_COUNT];

  Preset preset = Preset::FLAT;
  OutputChannelConfig channel[MATRIX_CHANNEL_COUNT];
};

// ---------------- Thread-Safe Global State ----------------
inline AudioSettings
    g_settings; // Staging settings manipulated by Core 1 (UI/Encoder)
inline portMUX_TYPE g_settingsMux = portMUX_INITIALIZER_UNLOCKED;
inline std::atomic<bool> g_settingsDirtyFlag{true};

// B-17: Single source of truth for UI dirty flag.
// Defined here (included by all modules); esp32_audio_oled_ui.h and
// esp32_audio_bluetooth.h must NOT re-declare this variable.
inline std::atomic<bool> g_uiDirty{true};

inline volatile float g_vuLevels[MATRIX_CHANNEL_COUNT] = {0.0f, 0.0f, 0.0f,
                                                          0.0f};
inline volatile bool g_isClipping = false;
inline volatile uint32_t g_lastClipTimeMs = 0;

// System Health & Diagnostics (Thread-Safe)
inline std::atomic<uint32_t> g_systemFaults{0};
inline std::atomic<bool> g_systemSafeMode{false};
inline std::atomic<bool> g_audioMuted{true};
inline std::atomic<uint32_t> g_audioDrops{0};
inline std::atomic<uint32_t> g_i2sErrors{0};
inline std::atomic<uint32_t> g_dspErrors{0};
inline std::atomic<uint32_t> g_queueOverflows{0};
inline std::atomic<uint32_t> g_configErrors{0};
inline std::atomic<uint32_t> g_queueHighWater{0};
inline std::atomic<uint32_t> g_queueCurrentLevel{0};
inline std::atomic<uint32_t> g_queueUnderflows{0};
inline std::atomic<uint32_t> g_processedBlocks{0};
inline std::atomic<uint32_t> g_successfulI2SWrites{0};
inline std::atomic<uint32_t> g_dspHeartbeatMs{0};
inline std::atomic<float> g_dspCpuLoadPct{0.0f};
inline bool g_psramActive = false;

inline void triggerSafeMode(SystemFault fault) {
  g_systemFaults.fetch_or(static_cast<uint32_t>(fault),
                          std::memory_order_relaxed);
  g_systemSafeMode.store(true, std::memory_order_release);
  g_audioMuted.store(true, std::memory_order_release);
}

// Thread-safe helper functions for Core 1 (UI / Encoder / NVS)
inline void audioSettingsLock() { portENTER_CRITICAL(&g_settingsMux); }

// B-01 FIX: Release the spinlock BEFORE setting the dirty flag so that
// Core 0's dspSyncSettings() never attempts to acquire an already-held mutex.
inline void audioSettingsUnlock() {
  portEXIT_CRITICAL(&g_settingsMux);
  g_settingsDirtyFlag.store(true, std::memory_order_release);
}

// Atomically synchronized copy from Core 1 staging to Core 0 DSP working copy
inline bool dspSyncSettings(AudioSettings &dest) {
  if (g_settingsDirtyFlag.load(std::memory_order_acquire)) {
    portENTER_CRITICAL(&g_settingsMux);
    dest = g_settings;
    g_settingsDirtyFlag.store(false, std::memory_order_relaxed);
    portEXIT_CRITICAL(&g_settingsMux);
    return true;
  }
  return false;
}

inline const char *presetName(Preset p) {
  switch (p) {
  case Preset::FLAT:
    return "Flat";
  case Preset::BASS_BOOST:
    return "Bass Boost";
  case Preset::VOCAL:
    return "Vocal";
  case Preset::ROCK:
    return "Rock";
  default:
    return "Custom";
  }
}

inline const char *inputSourceName(InputSource s) {
  switch (s) {
  case InputSource::SRC_LEFT:
    return "Left";
  case InputSource::SRC_RIGHT:
    return "Right";
  default:
    return "Mono";
  }
}

inline const char *inputSourceShort(InputSource s) {
  switch (s) {
  case InputSource::SRC_LEFT:
    return "L";
  case InputSource::SRC_RIGHT:
    return "R";
  default:
    return "M";
  }
}

inline const char *filterBandName(FilterBand b) {
  switch (b) {
  case FilterBand::BAND_SUB:
    return "Sub LR24";
  case FilterBand::BAND_MID:
    return "Mid LR24";
  case FilterBand::BAND_HIGH:
    return "High LR24";
  default:
    return "Full Bypass";
  }
}

inline const char *filterBandShort(FilterBand b) {
  switch (b) {
  case FilterBand::BAND_SUB:
    return "SUB";
  case FilterBand::BAND_MID:
    return "MID";
  case FilterBand::BAND_HIGH:
    return "HI";
  default:
    return "FUL";
  }
}

inline const char *peqTypeName(PEQType t) {
  switch (t) {
  case PEQType::LOW_SHELF:
    return "LowShelf";
  case PEQType::HIGH_SHELF:
    return "HighShelf";
  default:
    return "Peaking";
  }
}

inline const char *peqTypeShort(PEQType t) {
  switch (t) {
  case PEQType::LOW_SHELF:
    return "LS";
  case PEQType::HIGH_SHELF:
    return "HS";
  default:
    return "PK";
  }
}

inline char channelName(uint8_t ch) {
  return (ch < MATRIX_CHANNEL_COUNT) ? (char)('A' + ch) : '?';
}

inline float dbToLinear(float db) {
  if (!isfinite(db))
    return 1.0f;
  return powf(10.0f, db / 20.0f);
}

inline bool isFiniteSafe(float value) { return isfinite(value); }

inline float clampf(float x, float lo, float hi) {
  if (!isFiniteSafe(x))
    return lo;
  return x < lo ? lo : (x > hi ? hi : x);
}

// Enforce strict relational rules: subLpfHz <= midLowHz < midHighHz <=
// highHpfHz, highHpfHz >= 1500Hz
inline void validateCrossoverRelational(float &subLpf, float &midLow,
                                        float &midHigh, float &highHpf) {
  // 1. High HPF Tweeter Safety Hard Floor
  if (highHpf < TWEETER_MIN_FREQ_HZ) {
    highHpf = TWEETER_MIN_FREQ_HZ;
  }
  highHpf = clampf(highHpf, TWEETER_MIN_FREQ_HZ, 20000.0f);

  // 2. B-02 FIX: Mid LPF must stay at least 50 Hz below High HPF to prevent
  // null output caused by identical crossover frequencies on adjacent bands.
  // Guard: if highHpf is too close to the minimum, clamp midHigh to at least
  // 500 Hz.
  const float midHighMax = (highHpf > 550.0f) ? (highHpf - 50.0f) : highHpf;
  midHigh = clampf(midHigh, 500.0f, midHighMax);

  // 3. Mid HPF must be below Mid LPF with at least 50 Hz separation
  const float maxMidLow = midHigh - 50.0f;
  midLow = clampf(midLow, 20.0f, maxMidLow > 20.0f ? maxMidLow : 20.0f);

  // 4. Sub LPF must not exceed Mid HPF
  subLpf = clampf(subLpf, 20.0f, midLow);
}

// Strict validation is used before a settings snapshot may enter the DSP path.
// Do not rely on clampf alone: a caller needs to know that a value was invalid.
inline bool validateAudioSettings(const AudioSettings &settings) {
  if (settings.masterVolume > 100 || !isFiniteSafe(settings.subLpfHz) ||
      !isFiniteSafe(settings.midLowHz) || !isFiniteSafe(settings.midHighHz) ||
      !isFiniteSafe(settings.highHpfHz) || settings.subLpfHz < 20.0f ||
      settings.midLowHz < 20.0f || settings.midHighHz < 500.0f ||
      settings.highHpfHz < TWEETER_MIN_FREQ_HZ ||
      settings.highHpfHz > 20000.0f || settings.subLpfHz > settings.midLowHz ||
      settings.midLowHz >= settings.midHighHz ||
      settings.midHighHz > settings.highHpfHz) {
    return false;
  }

  for (uint8_t i = 0; i < PEQ_BAND_COUNT; ++i) {
    const PEQBandConfig &band = settings.peq[i];
    if (!isFiniteSafe(band.freq) || !isFiniteSafe(band.q) ||
        !isFiniteSafe(band.gainDb) || band.freq < 20.0f ||
        band.freq > 20000.0f || band.q < 0.1f || band.q > 10.0f ||
        band.gainDb < -12.0f || band.gainDb > 12.0f ||
        static_cast<uint8_t>(band.type) >
            static_cast<uint8_t>(PEQType::HIGH_SHELF)) {
      return false;
    }
  }

  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ++ch) {
    const OutputChannelConfig &cfg = settings.channel[ch];
    if (!isFiniteSafe(cfg.gainDb) || !isFiniteSafe(cfg.delayMs) ||
        cfg.gainDb < -20.0f || cfg.gainDb > 6.0f || cfg.delayMs < 0.0f ||
        cfg.delayMs > MAX_DELAY_MS ||
        static_cast<uint8_t>(cfg.source) >
            static_cast<uint8_t>(InputSource::SRC_MONO) ||
        static_cast<uint8_t>(cfg.band) >
            static_cast<uint8_t>(FilterBand::BAND_FULL)) {
      return false;
    }
  }
  return true;
}

// Normalize externally loaded settings before they are persisted or applied.
inline bool sanitizeAudioSettings(AudioSettings &settings) {
  const bool wasValid = validateAudioSettings(settings);
  if (settings.masterVolume > 100)
    settings.masterVolume = 100;
  settings.subLpfHz = clampf(settings.subLpfHz, 20.0f, 20000.0f);
  settings.midLowHz = clampf(settings.midLowHz, 20.0f, 20000.0f);
  settings.midHighHz = clampf(settings.midHighHz, 500.0f, 20000.0f);
  settings.highHpfHz =
      clampf(settings.highHpfHz, TWEETER_MIN_FREQ_HZ, 20000.0f);
  validateCrossoverRelational(settings.subLpfHz, settings.midLowHz,
                              settings.midHighHz, settings.highHpfHz);
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; ++i) {
    PEQBandConfig &band = settings.peq[i];
    band.freq = clampf(band.freq, 20.0f, 20000.0f);
    band.q = clampf(band.q, 0.1f, 10.0f);
    band.gainDb = clampf(band.gainDb, -12.0f, 12.0f);
    if (static_cast<uint8_t>(band.type) >
        static_cast<uint8_t>(PEQType::HIGH_SHELF)) {
      band.type = PEQType::PEAKING;
    }
  }
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ++ch) {
    OutputChannelConfig &cfg = settings.channel[ch];
    cfg.gainDb = clampf(cfg.gainDb, -20.0f, 6.0f);
    cfg.delayMs = clampf(cfg.delayMs, 0.0f, MAX_DELAY_MS);
    if (static_cast<uint8_t>(cfg.source) >
        static_cast<uint8_t>(InputSource::SRC_MONO))
      cfg.source = InputSource::SRC_MONO;
    if (static_cast<uint8_t>(cfg.band) >
        static_cast<uint8_t>(FilterBand::BAND_FULL))
      cfg.band = FilterBand::BAND_FULL;
  }
  return wasValid;
}

inline void configLoadDefaults() {
  audioSettingsLock();
  g_settings = AudioSettings{};

  // Default Crossover Frequencies (Linkwitz-Riley 24dB)
  g_settings.subLpfHz = 120.0f;
  g_settings.midLowHz = 120.0f;
  g_settings.midHighHz = 3500.0f;
  g_settings.highHpfHz = 3500.0f;

  // Default 5-Band Parametric EQ (Standard Audiophile Curve)
  g_settings.peq[0] = {80.0f, 0.707f, 0.0f, PEQType::LOW_SHELF,
                       true}; // Sub / Low Shelf
  g_settings.peq[1] = {250.0f, 1.414f, 0.0f, PEQType::PEAKING,
                       true}; // Low-Mid Peaking
  g_settings.peq[2] = {1000.0f, 1.000f, 0.0f, PEQType::PEAKING,
                       true}; // Mid Peaking
  g_settings.peq[3] = {4000.0f, 1.414f, 0.0f, PEQType::PEAKING,
                       true}; // High-Mid Peaking
  g_settings.peq[4] = {12000.0f, 0.707f, 0.0f, PEQType::HIGH_SHELF,
                       true}; // High Shelf

  // Channel Matrix Routing & Alignment defaults
  // CH A: Left Mid (LR24)
  g_settings.channel[0].source = InputSource::SRC_LEFT;
  g_settings.channel[0].band = FilterBand::BAND_MID;
  g_settings.channel[0].gainDb = 0.0f;
  g_settings.channel[0].mute = false;
  g_settings.channel[0].delayMs = 0.0f;
  g_settings.channel[0].phaseInvert = false;

  // CH B: Right Mid (LR24)
  g_settings.channel[1].source = InputSource::SRC_RIGHT;
  g_settings.channel[1].band = FilterBand::BAND_MID;
  g_settings.channel[1].gainDb = 0.0f;
  g_settings.channel[1].mute = false;
  g_settings.channel[1].delayMs = 0.0f;
  g_settings.channel[1].phaseInvert = false;

  // CH C: Mono Sub (LR24 Lowpass)
  g_settings.channel[2].source = InputSource::SRC_MONO;
  g_settings.channel[2].band = FilterBand::BAND_SUB;
  g_settings.channel[2].gainDb = 0.0f;
  g_settings.channel[2].mute = false;
  g_settings.channel[2].delayMs = 0.0f;
  g_settings.channel[2].phaseInvert = false;

  // CH D: Mono High / Tweeter (LR24 Highpass)
  g_settings.channel[3].source = InputSource::SRC_MONO;
  g_settings.channel[3].band = FilterBand::BAND_HIGH;
  g_settings.channel[3].gainDb = 0.0f;
  g_settings.channel[3].mute = false;
  g_settings.channel[3].delayMs = 0.0f;
  g_settings.channel[3].phaseInvert = false;

  audioSettingsUnlock();

  for (uint8_t i = 0; i < MATRIX_CHANNEL_COUNT; i++) {
    g_vuLevels[i] = 0.0f;
  }
  g_isClipping = false;
}
