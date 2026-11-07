#pragma once
#ifndef ESP32_AUDIO_NVS_H
#define ESP32_AUDIO_NVS_H

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp32_audio_crossover_config.h"

inline Preferences g_prefs;
inline SemaphoreHandle_t g_nvsMutex = nullptr;

inline volatile bool g_settingsDirty = false;
inline volatile uint32_t g_lastEditTime = 0;
inline volatile bool g_nvsSaveNotify = false;
inline volatile uint32_t g_nvsSaveNotifyMs = 0;

static const char* NVS_NS = "dsp_cfg";
static const uint32_t NVS_MAGIC = 0xD5B10004UL;
static const uint16_t NVS_VERSION = 2;

inline uint32_t calculateCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(crc & 1)));
    }
  }
  return ~crc;
}

// A single Preferences blob is journaled atomically by NVS.  It is retained as
// a recovery record while the legacy field-by-field representation is updated.
struct NvsRecoveryRecord {
  uint32_t crc;
  AudioSettings settings;
};

inline void nvsEnsureMutex() {
  if (!g_nvsMutex) {
    g_nvsMutex = xSemaphoreCreateMutex();
  }
}

inline bool nvsLock(TickType_t ticks = pdMS_TO_TICKS(250)) {
  nvsEnsureMutex();
  return g_nvsMutex && xSemaphoreTake(g_nvsMutex, ticks) == pdTRUE;
}

inline void nvsUnlock() {
  if (g_nvsMutex) {
    xSemaphoreGive(g_nvsMutex);
  }
}

inline void nvsKey(char* out, size_t len, uint8_t ch, const char* suffix) {
  snprintf(out, len, "c%u%s", ch, suffix);
}

inline void nvsPeqKey(char* out, size_t len, uint8_t band, const char* suffix) {
  snprintf(out, len, "p%u%s", band, suffix);
}

inline InputSource clampInputSource(uint8_t v) {
  return (v <= (uint8_t)InputSource::SRC_MONO) ? (InputSource)v : InputSource::SRC_MONO;
}

inline FilterBand clampFilterBand(uint8_t v) {
  return (v <= (uint8_t)FilterBand::BAND_FULL) ? (FilterBand)v : FilterBand::BAND_FULL;
}

inline Preset clampPreset(uint8_t v) {
  return (v <= (uint8_t)Preset::CUSTOM) ? (Preset)v : Preset::CUSTOM;
}

inline PEQType clampPEQType(uint8_t v) {
  return (v <= (uint8_t)PEQType::HIGH_SHELF) ? (PEQType)v : PEQType::PEAKING;
}

inline bool nvsPutUChar(const char* key, uint8_t value) {
  return g_prefs.putUChar(key, value) == sizeof(value);
}

inline bool nvsPutUShort(const char* key, uint16_t value) {
  return g_prefs.putUShort(key, value) == sizeof(value);
}

inline bool nvsPutUInt(const char* key, uint32_t value) {
  return g_prefs.putUInt(key, value) == sizeof(value);
}

inline bool nvsPutFloat(const char* key, float value) {
  return g_prefs.putFloat(key, value) == sizeof(value);
}

// B-06 FIX: Internal implementation that performs the NVS recovery record read
// WITHOUT acquiring g_nvsMutex. Call this only from a context that already
// holds the mutex (e.g., nvsLoad). The public wrapper nvsRestoreRecovery()
// acquires the mutex itself for external callers.
static bool _nvsRestoreRecoveryUnlocked() {
  NvsRecoveryRecord recovery{};
  const bool opened = g_prefs.begin(NVS_NS, true);
  const size_t read = opened ? g_prefs.getBytes("recovery", &recovery, sizeof(recovery)) : 0;
  if (opened) g_prefs.end();

  if (read != sizeof(recovery) ||
      calculateCrc32(reinterpret_cast<const uint8_t*>(&recovery.settings), sizeof(AudioSettings)) != recovery.crc) {
    return false;
  }

  if (!sanitizeAudioSettings(recovery.settings)) {
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
  }
  audioSettingsLock();
  g_settings = recovery.settings;
  audioSettingsUnlock();
  Serial.println("[NVS] Primary configuration invalid; restored journaled recovery record.");
  return true;
}

// Public recovery entry point for callers that do NOT hold g_nvsMutex.
inline bool nvsRestoreRecovery() {
  if (!nvsLock(pdMS_TO_TICKS(1000))) return false;
  const bool ok = _nvsRestoreRecoveryUnlocked();
  nvsUnlock();
  return ok;
}

inline void nvsSave() {
  // B-07 FIX: Track consecutive failures to prevent infinite flash write loops.
  // After NVS_MAX_SAVE_RETRIES consecutive failures, give up and reset the dirty
  // flag so nvsAutoSave() stops hammering the flash unnecessarily.
  static constexpr uint8_t NVS_MAX_SAVE_RETRIES = 3;
  static uint8_t s_saveRetryCount = 0;

  if (!nvsLock()) {
    Serial.println("[NVS] Save skipped: mutex timeout.");
    return;
  }

  // Atomic snapshot of current settings
  audioSettingsLock();
  AudioSettings snapshot{};
  snapshot = g_settings;
  audioSettingsUnlock();

  // Persist only normalized finite settings, even if a caller bypassed the UI.
  if (!sanitizeAudioSettings(snapshot)) {
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    Serial.println("[NVS] Invalid settings normalized before save.");
  }

  // Calculate CRC32 checksum of the configuration payload
  const uint32_t payloadCrc = calculateCrc32(reinterpret_cast<const uint8_t*>(&snapshot), sizeof(AudioSettings));

  if (!g_prefs.begin(NVS_NS, false)) {
    nvsUnlock();
    s_saveRetryCount++;
    Serial.printf("[NVS] Save failed: namespace unavailable (attempt %u/%u).\n",
                  s_saveRetryCount, NVS_MAX_SAVE_RETRIES);
    if (s_saveRetryCount >= NVS_MAX_SAVE_RETRIES) {
      Serial.println("[NVS] Max retries reached. Clearing dirty flag to stop loop.");
      g_settingsDirty = false;
      g_configErrors.fetch_add(1, std::memory_order_relaxed);
      s_saveRetryCount = 0;
    }
    return;
  }

  // Write a self-contained recovery record first.  If power is lost while the
  // individual keys below are being updated, boot can restore this record.
  const NvsRecoveryRecord recovery{payloadCrc, snapshot};
  if (g_prefs.putBytes("recovery", &recovery, sizeof(recovery)) != sizeof(recovery)) {
    g_prefs.end();
    nvsUnlock();
    Serial.println("[NVS] Save failed: recovery record write error.");
    return;
  }

  bool writeOk = nvsPutUChar("masterVol", snapshot.masterVolume);
  
  // Crossover Frequencies
  writeOk &= nvsPutFloat("subLpfHz", snapshot.subLpfHz);
  writeOk &= nvsPutFloat("midLowHz", snapshot.midLowHz);
  writeOk &= nvsPutFloat("midHighHz", snapshot.midHighHz);
  writeOk &= nvsPutFloat("highHpfHz", snapshot.highHpfHz);
  writeOk &= nvsPutUChar("preset", (uint8_t)snapshot.preset);

  // 5-Band PEQ Settings
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    char key[8];
    nvsPeqKey(key, sizeof(key), i, "F");
    writeOk &= nvsPutFloat(key, snapshot.peq[i].freq);
    nvsPeqKey(key, sizeof(key), i, "Q");
    writeOk &= nvsPutFloat(key, snapshot.peq[i].q);
    nvsPeqKey(key, sizeof(key), i, "G");
    writeOk &= nvsPutFloat(key, snapshot.peq[i].gainDb);
    nvsPeqKey(key, sizeof(key), i, "T");
    writeOk &= nvsPutUChar(key, (uint8_t)snapshot.peq[i].type);
    nvsPeqKey(key, sizeof(key), i, "E");
    writeOk &= nvsPutUChar(key, snapshot.peq[i].enabled ? 1 : 0);
  }

  // Per-Channel Settings (Matrix, Gain, Mute, Delay, Phase)
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    char key[8];
    nvsKey(key, sizeof(key), ch, "Src");
    writeOk &= nvsPutUChar(key, (uint8_t)snapshot.channel[ch].source);
    nvsKey(key, sizeof(key), ch, "Band");
    writeOk &= nvsPutUChar(key, (uint8_t)snapshot.channel[ch].band);
    nvsKey(key, sizeof(key), ch, "Gain");
    writeOk &= nvsPutFloat(key, snapshot.channel[ch].gainDb);
    nvsKey(key, sizeof(key), ch, "Mute");
    writeOk &= nvsPutUChar(key, snapshot.channel[ch].mute ? 1 : 0);
    nvsKey(key, sizeof(key), ch, "Dly");
    writeOk &= nvsPutFloat(key, snapshot.channel[ch].delayMs);
    nvsKey(key, sizeof(key), ch, "Phs");
    writeOk &= nvsPutUChar(key, snapshot.channel[ch].phaseInvert ? 1 : 0);
  }

  // Commit marker is written last and only after every payload key succeeded.
  if (writeOk) {
    writeOk &= nvsPutUInt("magic", NVS_MAGIC);
    writeOk &= nvsPutUShort("version", NVS_VERSION);
    writeOk &= nvsPutUInt("length", sizeof(AudioSettings));
    writeOk &= nvsPutUInt("crc32", payloadCrc);
  }

  g_prefs.end();
  nvsUnlock();

  if (!writeOk) {
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    Serial.println("[NVS] Save failed: payload/commit write error; recovery record retained.");
    return;
  }

  g_settingsDirty = false;
  g_nvsSaveNotify = true;
  g_nvsSaveNotifyMs = millis();

  Serial.printf("[NVS] Configuration saved (CRC32: 0x%08X).\n", payloadCrc);
}

inline void nvsLoad() {
  if (!nvsLock(pdMS_TO_TICKS(1000))) {
    Serial.println("[NVS] Load skipped: mutex timeout.");
    return;
  }

  if (!g_prefs.begin(NVS_NS, true)) {
    nvsUnlock();
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    Serial.println("[NVS] Load failed: namespace unavailable.");
    return;
  }
  const uint32_t magic = g_prefs.getUInt("magic", 0);
  const uint16_t version = g_prefs.getUShort("version", 0);
  const uint32_t storedLen = g_prefs.getUInt("length", 0);
  const uint32_t expectedCrc = g_prefs.getUInt("crc32", 0);

  // B-06 FIX: Mutex was released by nvsUnlock() above, so use the public
  // nvsRestoreRecovery() here — it is safe to acquire the mutex again.
  if (magic != NVS_MAGIC || version != NVS_VERSION) {
    g_prefs.end();
    nvsUnlock();
    if (nvsRestoreRecovery()) {
      nvsSave();
      return;
    }
    Serial.println("[NVS] Invalid header (magic/version) - loading defaults.");
    configLoadDefaults();
    nvsSave();
    return;
  }

  // FIX #3b: Verify stored size matches expected AudioSettings struct
  if (storedLen != sizeof(AudioSettings)) {
    g_prefs.end();
    nvsUnlock();
    Serial.printf("[NVS] Size mismatch: stored=%u expected=%u - loading defaults.\n", 
                 storedLen, (uint32_t)sizeof(AudioSettings));
    if (nvsRestoreRecovery()) {
      nvsSave();
      return;
    }
    configLoadDefaults();
    nvsSave();
    return;
  }

  // FIX #3c: NOW safe to load settings after header validation
  AudioSettings temp{};
  temp.masterVolume = g_prefs.getUChar("masterVol", g_settings.masterVolume);
  temp.subLpfHz     = g_prefs.getFloat("subLpfHz", g_settings.subLpfHz);
  temp.midLowHz     = g_prefs.getFloat("midLowHz", g_settings.midLowHz);
  temp.midHighHz    = g_prefs.getFloat("midHighHz", g_settings.midHighHz);
  temp.highHpfHz    = g_prefs.getFloat("highHpfHz", g_settings.highHpfHz);
  temp.preset       = clampPreset(g_prefs.getUChar("preset", (uint8_t)g_settings.preset));

  // Load 5-Band PEQ
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    char key[8];
    nvsPeqKey(key, sizeof(key), i, "F");
    temp.peq[i].freq = clampf(g_prefs.getFloat(key, g_settings.peq[i].freq), 20.0f, 20000.0f);
    nvsPeqKey(key, sizeof(key), i, "Q");
    temp.peq[i].q = clampf(g_prefs.getFloat(key, g_settings.peq[i].q), 0.1f, 10.0f);
    nvsPeqKey(key, sizeof(key), i, "G");
    temp.peq[i].gainDb = clampf(g_prefs.getFloat(key, g_settings.peq[i].gainDb), -12.0f, 12.0f);
    nvsPeqKey(key, sizeof(key), i, "T");
    temp.peq[i].type = clampPEQType(g_prefs.getUChar(key, (uint8_t)g_settings.peq[i].type));
    nvsPeqKey(key, sizeof(key), i, "E");
    temp.peq[i].enabled = (g_prefs.getUChar(key, g_settings.peq[i].enabled ? 1 : 0) != 0);
  }

  // Load Channel Settings
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    char key[8];
    nvsKey(key, sizeof(key), ch, "Src");
    temp.channel[ch].source = clampInputSource(g_prefs.getUChar(key, (uint8_t)g_settings.channel[ch].source));
    nvsKey(key, sizeof(key), ch, "Band");
    temp.channel[ch].band = clampFilterBand(g_prefs.getUChar(key, (uint8_t)g_settings.channel[ch].band));
    nvsKey(key, sizeof(key), ch, "Gain");
    temp.channel[ch].gainDb = clampf(g_prefs.getFloat(key, g_settings.channel[ch].gainDb), -20.0f, 6.0f);
    nvsKey(key, sizeof(key), ch, "Mute");
    temp.channel[ch].mute = (g_prefs.getUChar(key, g_settings.channel[ch].mute ? 1 : 0) != 0);
    nvsKey(key, sizeof(key), ch, "Dly");
    temp.channel[ch].delayMs = clampf(g_prefs.getFloat(key, g_settings.channel[ch].delayMs), 0.0f, MAX_DELAY_MS);
    nvsKey(key, sizeof(key), ch, "Phs");
    temp.channel[ch].phaseInvert = (g_prefs.getUChar(key, g_settings.channel[ch].phaseInvert ? 1 : 0) != 0);
  }

  g_prefs.end();
  nvsUnlock();

  // Verify the stored representation before normalizing any values.
  const uint32_t computedCrc = calculateCrc32(reinterpret_cast<const uint8_t*>(&temp), sizeof(AudioSettings));
  if (computedCrc != expectedCrc) {
    Serial.printf("[NVS] ERROR: CRC32 mismatch (computed: 0x%08X, expected: 0x%08X).\n", computedCrc, expectedCrc);
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    if (nvsRestoreRecovery()) {
      nvsSave();
      return;
    }
    configLoadDefaults();
    nvsSave(); // rewrite healthy defaults
    return;
  }

  if (!sanitizeAudioSettings(temp)) {
    Serial.println("[NVS] Invalid settings payload rejected; restoring recovery/defaults.");
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
    if (nvsRestoreRecovery()) {
      nvsSave();
      return;
    }
    configLoadDefaults();
    nvsSave();
    return;
  }

  // Atomically commit to staging settings (all validation passed)
  audioSettingsLock();
  g_settings = temp;
  audioSettingsUnlock();

  Serial.printf("[NVS] Configuration loaded & verified with CRC32 0x%08X OK.\n", computedCrc);
}

inline void nvsAutoSave() {
  if (!g_settingsDirty) return;

  const uint32_t DEBOUNCE_MS = 3000;
  if (millis() - g_lastEditTime >= DEBOUNCE_MS) {
    nvsSave();
  }
}

#endif
