#pragma once
#include "esp32_audio_crossover_config.h"
#include "esp32_audio_i2s_handler.h"
#include <math.h>
#include <esp_heap_caps.h>

// ---------------- 2nd-Order Biquad Filter (Direct Form II Transposed) ----------------
struct Biquad {
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
  float targetB0 = 1.0f, targetB1 = 0.0f, targetB2 = 0.0f, targetA1 = 0.0f, targetA2 = 0.0f;
  uint16_t coefficientTransitionSamples = 0;
  float z1 = 0.0f, z2 = 0.0f;

  static constexpr uint16_t COEFFICIENT_TRANSITION_SAMPLES = 441; // 10 ms @ 44.1 kHz

  void reset() {
    z1 = z2 = 0.0f;
  }

  void setCoefficients(float nextB0, float nextB1, float nextB2, float nextA1, float nextA2) {
    if (!isFiniteSafe(nextB0) || !isFiniteSafe(nextB1) || !isFiniteSafe(nextB2) ||
        !isFiniteSafe(nextA1) || !isFiniteSafe(nextA2)) {
      nextB0 = 1.0f; nextB1 = nextB2 = nextA1 = nextA2 = 0.0f;
    }
    // B-05 FIX: Capture current coefficients as interpolation start point.
    startB0 = b0; startB1 = b1; startB2 = b2; startA1 = a1; startA2 = a2;
    targetB0 = nextB0; targetB1 = nextB1; targetB2 = nextB2;
    targetA1 = nextA1; targetA2 = nextA2;
    totalTransitionSamples = COEFFICIENT_TRANSITION_SAMPLES;
    coefficientTransitionSamples = COEFFICIENT_TRANSITION_SAMPLES;
  }

  // B-05 FIX: Use true linear interpolation based on time fraction rather
  // than the exponential approach (which never reaches the exact target).
  // We store initial coefficients at the start of a transition to compute
  // the correct lerp fraction each step.
  float startB0 = 1.0f, startB1 = 0.0f, startB2 = 0.0f, startA1 = 0.0f, startA2 = 0.0f;
  uint16_t totalTransitionSamples = 0;

  inline void advanceCoefficients() {
    if (coefficientTransitionSamples == 0) return;
    --coefficientTransitionSamples;
    if (coefficientTransitionSamples == 0) {
      // Final step: snap exactly to target to eliminate any floating-point drift.
      b0 = targetB0; b1 = targetB1; b2 = targetB2; a1 = targetA1; a2 = targetA2;
      return;
    }
    // True linear interpolation: t goes from 0.0 (start) to 1.0 (end).
    // t = 1 - (remaining / total)  →  avoids division-by-zero because
    // we already handled the totalTransitionSamples==0 case in setCoefficients.
    const float t = 1.0f - (float)coefficientTransitionSamples / (float)totalTransitionSamples;
    b0 = startB0 + (targetB0 - startB0) * t;
    b1 = startB1 + (targetB1 - startB1) * t;
    b2 = startB2 + (targetB2 - startB2) * t;
    a1 = startA1 + (targetA1 - startA1) * t;
    a2 = startA2 + (targetA2 - startA2) * t;
  }

  inline float process(float x) {
    advanceCoefficients();
    if (!isFiniteSafe(x)) {
      reset();
      return 0.0f;
    }
    const float y = b0 * x + z1;
    const float nextZ1 = b1 * x - a1 * y + z2;
    const float nextZ2 = b2 * x - a2 * y;
    if (!isFiniteSafe(y) || !isFiniteSafe(nextZ1) || !isFiniteSafe(nextZ2)) {
      reset();
      return 0.0f;
    }
    z1 = nextZ1;
    z2 = nextZ2;
    return y;
  }

  void lowpass(float fs, float fc, float Q = 0.70710678f) {
    fc = clampf(fc, 10.0f, fs * 0.45f);
    Q = clampf(Q, 0.1f, 10.0f);
    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * Q);
    const float b0n = (1.0f - c) * 0.5f;
    const float b1n = 1.0f - c;
    const float b2n = (1.0f - c) * 0.5f;
    const float a0  = 1.0f + alpha;
    const float a1n = -2.0f * c;
    const float a2n = 1.0f - alpha;
    setCoefficients(b0n / a0, b1n / a0, b2n / a0, a1n / a0, a2n / a0);
  }

  void highpass(float fs, float fc, float Q = 0.70710678f) {
    fc = clampf(fc, 10.0f, fs * 0.45f);
    Q = clampf(Q, 0.1f, 10.0f);
    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * Q);
    const float b0n = (1.0f + c) * 0.5f;
    const float b1n = -(1.0f + c);
    const float b2n = (1.0f + c) * 0.5f;
    const float a0  = 1.0f + alpha;
    const float a1n = -2.0f * c;
    const float a2n = 1.0f - alpha;
    setCoefficients(b0n / a0, b1n / a0, b2n / a0, a1n / a0, a2n / a0);
  }

  void peaking(float fs, float fc, float gainDb, float Q = 1.0f) {
    fc = clampf(fc, 20.0f, fs * 0.45f);
    Q = clampf(Q, 0.1f, 10.0f);
    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * Q);
    const float b0n = 1.0f + alpha * A;
    const float b1n = -2.0f * c;
    const float b2n = 1.0f - alpha * A;
    const float a0  = 1.0f + alpha / A;
    const float a1n = -2.0f * c;
    const float a2n = 1.0f - alpha / A;
    setCoefficients(b0n / a0, b1n / a0, b2n / a0, a1n / a0, a2n / a0);
  }

  void lowShelf(float fs, float fc, float gainDb, float Q = 0.70710678f) {
    fc = clampf(fc, 10.0f, fs * 0.45f);
    Q = clampf(Q, 0.1f, 10.0f);
    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * Q);
    const float sqrtA = sqrtf(A);
    const float twoSqrtAAlpha = 2.0f * sqrtA * alpha;

    const float b0n = A * ((A + 1.0f) - (A - 1.0f) * c + twoSqrtAAlpha);
    const float b1n = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c);
    const float b2n = A * ((A + 1.0f) - (A - 1.0f) * c - twoSqrtAAlpha);
    const float a0  = (A + 1.0f) + (A - 1.0f) * c + twoSqrtAAlpha;
    const float a1n = -2.0f * ((A - 1.0f) + (A + 1.0f) * c);
    const float a2n = (A + 1.0f) + (A - 1.0f) * c - twoSqrtAAlpha;

    setCoefficients(b0n / a0, b1n / a0, b2n / a0, a1n / a0, a2n / a0);
  }

  void highShelf(float fs, float fc, float gainDb, float Q = 0.70710678f) {
    fc = clampf(fc, 10.0f, fs * 0.45f);
    Q = clampf(Q, 0.1f, 10.0f);
    const float A = powf(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * (float)M_PI * fc / fs;
    const float c = cosf(w0);
    const float s = sinf(w0);
    const float alpha = s / (2.0f * Q);
    const float sqrtA = sqrtf(A);
    const float twoSqrtAAlpha = 2.0f * sqrtA * alpha;

    const float b0n = A * ((A + 1.0f) + (A - 1.0f) * c + twoSqrtAAlpha);
    const float b1n = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c);
    const float b2n = A * ((A + 1.0f) + (A - 1.0f) * c - twoSqrtAAlpha);
    const float a0  = (A + 1.0f) - (A - 1.0f) * c + twoSqrtAAlpha;
    const float a1n = 2.0f * ((A - 1.0f) - (A + 1.0f) * c);
    const float a2n = (A + 1.0f) - (A - 1.0f) * c - twoSqrtAAlpha;

    setCoefficients(b0n / a0, b1n / a0, b2n / a0, a1n / a0, a2n / a0);
  }
};

// ---------------- Linkwitz-Riley 4th Order (LR24, 24 dB/octave) ----------------
// Cascades two identical 2nd-order Butterworth filters (Q = 1/sqrt(2) = 0.70710678)
// Summed magnitude response is strictly flat (0 dB) with zero phase cancellation at fc.
struct BiquadLR24 {
  Biquad stage1;
  Biquad stage2;

  void reset() {
    stage1.reset();
    stage2.reset();
  }

  inline float process(float x) {
    return stage2.process(stage1.process(x));
  }

  void lowpass(float fs, float fc) {
    constexpr float Q_BUTTERWORTH = 0.70710678f;
    stage1.lowpass(fs, fc, Q_BUTTERWORTH);
    stage2.lowpass(fs, fc, Q_BUTTERWORTH);
  }

  void highpass(float fs, float fc) {
    constexpr float Q_BUTTERWORTH = 0.70710678f;
    stage1.highpass(fs, fc, Q_BUTTERWORTH);
    stage2.highpass(fs, fc, Q_BUTTERWORTH);
  }
};

// ---------------- Multi-Band Parametric EQ (5 Bands) ----------------
struct ParametricEQDSP {
  Biquad band[PEQ_BAND_COUNT];

  void reset() {
    for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
      band[i].reset();
    }
  }

  inline float process(float x) {
    for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
      x = band[i].process(x);
    }
    return x;
  }

  void updateBand(uint8_t i, const PEQBandConfig& cfg, float fs) {
    if (i >= PEQ_BAND_COUNT) return;
    if (!cfg.enabled || (fabsf(cfg.gainDb) < 0.05f && cfg.type == PEQType::PEAKING)) {
      // Pass-through identity filter
      band[i].setCoefficients(1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
      return;
    }

    switch (cfg.type) {
      case PEQType::LOW_SHELF:
        band[i].lowShelf(fs, cfg.freq, cfg.gainDb, cfg.q);
        break;
      case PEQType::HIGH_SHELF:
        band[i].highShelf(fs, cfg.freq, cfg.gainDb, cfg.q);
        break;
      default:
        band[i].peaking(fs, cfg.freq, cfg.gainDb, cfg.q);
        break;
    }
  }
};

// ---------------- Channel DSP (LR24 Crossover Stages) ----------------
struct MatrixChannelDSP {
  BiquadLR24 subLPF;
  BiquadLR24 midHPF;
  BiquadLR24 midLPF;
  BiquadLR24 highHPF;

  void reset() {
    subLPF.reset();
    midHPF.reset();
    midLPF.reset();
    highHPF.reset();
  }
};

// ---------------- 1st-Order IIR DC-Blocking Filter (10 Hz Highpass) ----------------
struct DCBlocker {
  float x1 = 0.0f;
  float y1 = 0.0f;
  static constexpr float R = 0.998575f; // fc ≈ 10Hz @ 44.1kHz

  void reset() {
    x1 = 0.0f;
    y1 = 0.0f;
  }

  inline float process(float x) {
    const float y = x - x1 + R * y1;
    x1 = x;
    y1 = y;
    return y;
  }
};

// ---------------- Channel Time Alignment / Delay Ring Buffer in PSRAM ----------------
struct ChannelDelay {
  float* buffer = nullptr;
  size_t writeIdx = 0;
  size_t capacity = DELAY_BUFFER_MAX;
  float currentDelayMs = 0.0f;

  ~ChannelDelay() {
    if (buffer) {
      free(buffer);
      buffer = nullptr;
    }
  }

  void init(size_t cap = DELAY_BUFFER_MAX) {
    capacity = cap;
    buffer = nullptr;
    bool allocatedInPsram = false;

    // Allocate in PSRAM if available
    #if defined(BOARD_HAS_PSRAM)
    if (psramFound()) {
      buffer = (float*)ps_malloc(capacity * sizeof(float));
      if (buffer) {
        allocatedInPsram = true;
        Serial.printf("[PSRAM] Allocated %u bytes for channel delay buffer.\n", capacity * sizeof(float));
      }
    }
    #endif

    // Fallback to internal RAM if PSRAM not present/failed
    if (!buffer) {
      buffer = (float*)malloc(capacity * sizeof(float));
      if (buffer) {
        Serial.printf("[RAM] Allocated %u bytes in internal RAM for channel delay.\n", capacity * sizeof(float));
      } else {
        Serial.println("[ERROR] Failed to allocate channel delay buffer in RAM!");
        triggerSafeMode(SystemFault::PSRAM_FAIL);
      }
    }

    // B-04 FIX: Return whether this channel successfully used PSRAM.
    // The caller (dspBegin) aggregates results across all channels.
    (void)allocatedInPsram; // stored via return value below
    this->_usedPsram = allocatedInPsram;
    reset();
  }

  // B-04 FIX: Per-channel PSRAM allocation result, read by dspBegin().
  bool _usedPsram = false;

  void reset() {
    if (buffer) {
      memset(buffer, 0, capacity * sizeof(float));
    }
    writeIdx = 0;
    currentDelayMs = 0.0f;
  }

  // Process sample with smooth fractional delay interpolation
  inline float process(float in, float targetDelayMs) {
    if (!buffer) return in;

    // Slew-rate limiter on delay changes to prevent sudden phase jumps / pitch flutter
    currentDelayMs += (targetDelayMs - currentDelayMs) * 0.005f;

    buffer[writeIdx] = in;

    // Convert ms to sample delay: delaySamples = delayMs * (fs / 1000)
    float delaySamples = currentDelayMs * (AUDIO_SAMPLE_RATE * 0.001f);
    if (delaySamples < 0.0f) delaySamples = 0.0f;
    const float maxAllowed = (float)(capacity - 2);
    if (delaySamples > maxAllowed) delaySamples = maxAllowed;

    const int dInt = (int)delaySamples;
    const float frac = delaySamples - (float)dInt;

    int rIdx1 = (int)writeIdx - dInt;
    if (rIdx1 < 0) rIdx1 += capacity;
    int rIdx2 = rIdx1 - 1;
    if (rIdx2 < 0) rIdx2 += capacity;

    const float s1 = buffer[rIdx1];
    const float s2 = buffer[rIdx2];
    const float out = s1 + frac * (s2 - s1);

    writeIdx++;
    if (writeIdx >= capacity) writeIdx = 0;
    return out;
  }
};

// ---------------- Non-linear Soft Clipper (Knee Saturator) ----------------
// ---------------- Non-linear Soft Clipper (Knee Saturator) ----------------
inline float softClip(float x) {
  constexpr float KNEE = 0.70f;
  if (x > KNEE) {
    const float diff = x - KNEE;
    return KNEE + (0.30f * diff) / (0.30f + diff);
  } else if (x < -KNEE) {
    const float diff = -x - KNEE;
    return -(KNEE + (0.30f * diff) / (0.30f + diff));
  }
  return x;
}

// ---------------- 4-Channel Sample-Peak Lookahead Limiter ----------------
// High-performance lookahead limiter with incremental peak tracking
struct TruePeakLimiter {
  float lookahead[MATRIX_CHANNEL_COUNT][LIMITER_LOOKAHEAD_SAMPLES];
  size_t writeIdx = 0;
  float envelope[MATRIX_CHANNEL_COUNT];
  float smoothGain[MATRIX_CHANNEL_COUNT];
  float releaseCoeff = 0.999f;

  void init() {
    float releaseMs = LIMITER_RELEASE_MS;
    if (releaseMs <= 0.0f) {
      releaseMs = 60.0f;
    }
    releaseCoeff = expf(-1.0f / (static_cast<float>(AUDIO_SAMPLE_RATE) * (releaseMs * 0.001f)));
    reset();
  }

  void reset() {
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      for (size_t s = 0; s < LIMITER_LOOKAHEAD_SAMPLES; s++) {
        lookahead[ch][s] = 0.0f;
      }
      envelope[ch] = 0.0f;
      smoothGain[ch] = 1.0f;
    }
    writeIdx = 0;
  }

  inline void process(const float in[MATRIX_CHANNEL_COUNT], float out[MATRIX_CHANNEL_COUNT], bool& limitTriggered) {
    limitTriggered = false;

    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      const float x = in[ch];
      if (!isFiniteSafe(x)) {
        envelope[ch] = 0.0f;
        smoothGain[ch] = 0.0f;
        lookahead[ch][writeIdx] = 0.0f;
        out[ch] = 0.0f;
        limitTriggered = true;
        continue;
      }
      const float delayed = lookahead[ch][writeIdx];
      lookahead[ch][writeIdx] = x;
      
      const float absX = fabsf(x);
      const float absDelayed = fabsf(delayed);

      // Fast-path lookahead peak scan: re-scan only if the leaving sample was the peak
      if (absX >= envelope[ch]) {
        envelope[ch] = absX;
      } else if (absDelayed >= envelope[ch]) {
        float maxVal = 0.0f;
        for (size_t s = 0; s < LIMITER_LOOKAHEAD_SAMPLES; ++s) {
          const float candidate = fabsf(lookahead[ch][s]);
          if (candidate > maxVal) maxVal = candidate;
        }
        envelope[ch] = maxVal;
      } else {
        envelope[ch] = envelope[ch] * releaseCoeff;
        if (absX > envelope[ch]) envelope[ch] = absX;
      }

      // Compute dynamic gain reduction
      float targetGain = 1.0f;
      if (envelope[ch] > LIMITER_THRESHOLD) {
        targetGain = LIMITER_THRESHOLD / (envelope[ch] + 1e-6f);
        limitTriggered = true;
      }

      // Smooth gain slew (fast pull-down, smooth release)
      if (targetGain < smoothGain[ch]) {
        smoothGain[ch] = targetGain;
      } else {
        smoothGain[ch] += (targetGain - smoothGain[ch]) * 0.05f;
      }

      float limited = delayed * smoothGain[ch];

      if (fabsf(limited) >= LIMITER_THRESHOLD) {
        limited = softClip(limited);
      }
      out[ch] = isFiniteSafe(limited) ? clampf(limited, -LIMITER_CEILING, LIMITER_CEILING) : 0.0f;
    }

    writeIdx++;
    if (writeIdx >= LIMITER_LOOKAHEAD_SAMPLES) {
      writeIdx = 0;
    }
  }
};

// ---------------- DSP Engine Instances & Working State ----------------
inline ParametricEQDSP dspPeqL;
inline ParametricEQDSP dspPeqR;
inline MatrixChannelDSP dspCh[MATRIX_CHANNEL_COUNT];
inline ChannelDelay dspDelay[MATRIX_CHANNEL_COUNT];
inline DCBlocker dspDCBlock[MATRIX_CHANNEL_COUNT];
inline TruePeakLimiter dspLimiter;

// Core 0 working copy of AudioSettings to prevent race conditions
inline AudioSettings g_dspSettings;
inline AudioSettings g_lastKnownGoodSettings;

// Smooth gain ramping variables (prevents audio pops/clicks during real-time tuning)
inline float g_currentMasterGain = 0.85f;
inline float g_currentChGain[MATRIX_CHANNEL_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};
inline float g_headroomGain = 1.0f;
inline float g_unmuteRamp = 0.0f;

inline float appliedSub = -1.0f;
inline float appliedMidLow = -1.0f;
inline float appliedMidHigh = -1.0f;
inline float appliedHigh = -1.0f;
inline PEQBandConfig appliedPeq[PEQ_BAND_COUNT];

inline float computePeqHeadroomGain(const PEQBandConfig peq[PEQ_BAND_COUNT]) {
  float maxBoostDb = 0.0f;
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    if (peq[i].enabled && peq[i].gainDb > 0.0f) {
      maxBoostDb += peq[i].gainDb * 0.5f; // Partial acoustic summation
    }
  }
  if (maxBoostDb > 12.0f) maxBoostDb = 12.0f;
  return dbToLinear(-maxBoostDb);
}

inline void dspApplySettingsIfNeeded() {
  // Check Crossover LR24 frequencies
  if (fabsf(appliedSub - g_dspSettings.subLpfHz) > 0.01f) {
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      dspCh[ch].subLPF.lowpass(AUDIO_SAMPLE_RATE, g_dspSettings.subLpfHz);
    }
    appliedSub = g_dspSettings.subLpfHz;
  }

  if (fabsf(appliedMidLow - g_dspSettings.midLowHz) > 0.01f) {
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      dspCh[ch].midHPF.highpass(AUDIO_SAMPLE_RATE, g_dspSettings.midLowHz);
    }
    appliedMidLow = g_dspSettings.midLowHz;
  }

  if (fabsf(appliedMidHigh - g_dspSettings.midHighHz) > 0.01f) {
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      dspCh[ch].midLPF.lowpass(AUDIO_SAMPLE_RATE, g_dspSettings.midHighHz);
    }
    appliedMidHigh = g_dspSettings.midHighHz;
  }

  if (fabsf(appliedHigh - g_dspSettings.highHpfHz) > 0.01f) {
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      dspCh[ch].highHPF.highpass(AUDIO_SAMPLE_RATE, g_dspSettings.highHpfHz);
    }
    appliedHigh = g_dspSettings.highHpfHz;
  }

  // Check 5-Band Parametric EQ changes
  bool peqChanged = false;
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    const PEQBandConfig& cur = g_dspSettings.peq[i];
    PEQBandConfig& prev = appliedPeq[i];

    if (fabsf(cur.freq - prev.freq) > 0.1f ||
        fabsf(cur.q - prev.q) > 0.01f ||
        fabsf(cur.gainDb - prev.gainDb) > 0.01f ||
        cur.type != prev.type ||
        cur.enabled != prev.enabled) {
      dspPeqL.updateBand(i, cur, AUDIO_SAMPLE_RATE);
      dspPeqR.updateBand(i, cur, AUDIO_SAMPLE_RATE);
      prev = cur;
      peqChanged = true;
    }
  }

  if (peqChanged) {
    g_headroomGain = computePeqHeadroomGain(g_dspSettings.peq);
  }
}

inline float selectInput(float l, float r, InputSource source) {
  switch (source) {
    case InputSource::SRC_LEFT: return l;
    case InputSource::SRC_RIGHT: return r;
    default: return 0.5f * (l + r);
  }
}

inline float processBandLR24(uint8_t ch, float x, FilterBand band) {
  switch (band) {
    case FilterBand::BAND_SUB:
      return dspCh[ch].subLPF.process(x);
    case FilterBand::BAND_MID:
      return dspCh[ch].midLPF.process(dspCh[ch].midHPF.process(x));
    case FilterBand::BAND_HIGH:
      return dspCh[ch].highHPF.process(x);
    default:
      return x; // BAND_FULL: bypass crossover
  }
}

inline int32_t floatToI2S24(float x) {
  const float limited = isFiniteSafe(x) ? clampf(x * DSP_HEADROOM, -1.0f, 1.0f) : 0.0f;
  const int32_t sample24 = (int32_t)(limited * 8388607.0f);
  // Signed left-shift of a negative value is undefined in C++. Multiplication
  // is defined and remains inside int32_t for the 24-bit range.
  return sample24 * 256;
}

inline void updateVULevels(const float peak[MATRIX_CHANNEL_COUNT],
                           const float sumSq[MATRIX_CHANNEL_COUNT],
                           uint16_t frames) {
  if (frames == 0) return;

  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    const float rms = sqrtf(sumSq[ch] / (float)frames);
    const float target = clampf((peak[ch] * 0.65f) + (rms * 0.75f), 0.0f, 1.0f);
    const float previous = g_vuLevels[ch];
    const float coeff = (target > previous) ? 0.45f : 0.08f;
    g_vuLevels[ch] = previous + ((target - previous) * coeff);
  }
}

// ---------------- Main Core 0 DSP Execution Pipeline ----------------
// Pipeline: [BT Audio] -> [Auto Headroom] -> [5-Band PEQ] -> [Matrix Routing & Polarity] 
//           -> [LR24 Crossover] -> [Gain Ramping & Delay Buffer] 
//           -> [DC Blocker] -> [True Peak Limiter (Brickwall)] -> [Soft Unmute Ramp] -> [Dual I2S]
inline void dspProcessBlock(const AudioBlock& in, int32_t* i2s0Out, int32_t* i2s1Out) {
  const int64_t dspStartUs = esp_timer_get_time();

  // 1. Thread-safe atomic sync from Core 1 staging configuration
  if (dspSyncSettings(g_dspSettings)) {
    if (!validateAudioSettings(g_dspSettings)) {
      g_configErrors.fetch_add(1, std::memory_order_relaxed);
      g_dspErrors.fetch_add(1, std::memory_order_relaxed);
      g_dspSettings = g_lastKnownGoodSettings;
      Serial.println("[DSP] Invalid settings snapshot rejected; using last-known-good settings.");
    } else {
      g_lastKnownGoodSettings = g_dspSettings;
    }
  }
  dspApplySettingsIfNeeded();

  const uint16_t numFrames = in.frames;
  if (numFrames == 0) return;

  // Safe Mode / Mute check: output silence if fault or muted
  const bool isSafeMode = g_systemSafeMode.load(std::memory_order_relaxed);
  const bool isMuted = g_audioMuted.load(std::memory_order_relaxed);

  if (isSafeMode || isMuted) {
    g_unmuteRamp = 0.0f;
    for (size_t i = 0; i < numFrames * 2; i++) {
      i2s0Out[i] = 0;
      i2s1Out[i] = 0;
    }
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      g_vuLevels[ch] = 0.0f;
    }
    return;
  }

  // 2. Prepare Smooth Gain Slew (anti-pop/click ramping across the block)
  const float targetMaster = (float)g_dspSettings.masterVolume * 0.01f;
  const float masterStep = (targetMaster - g_currentMasterGain) / (float)numFrames;

  float targetChGain[MATRIX_CHANNEL_COUNT];
  float chGainStep[MATRIX_CHANNEL_COUNT];

  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    const OutputChannelConfig& cfg = g_dspSettings.channel[ch];
    targetChGain[ch] = cfg.mute ? 0.0f : dbToLinear(cfg.gainDb);
    chGainStep[ch] = (targetChGain[ch] - g_currentChGain[ch]) / (float)numFrames;
  }

  float peak[MATRIX_CHANNEL_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
  float sumSq[MATRIX_CHANNEL_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool blockLimited = false;

  // Unmute soft-ramp step (150ms ramp-in)
  const float unmuteStep = 1.0f / (0.15f * AUDIO_SAMPLE_RATE);

  for (size_t i = 0; i < numFrames; i++) {
    // 3. Convert incoming 16-bit integer samples to normalized float with headroom scaling
    const float rawL = in.samples[i].l * (1.0f / 32768.0f) * g_headroomGain;
    const float rawR = in.samples[i].r * (1.0f / 32768.0f) * g_headroomGain;

    // 4. 5-Band Parametric EQ Stage
    const float eqL = dspPeqL.process(rawL);
    const float eqR = dspPeqR.process(rawR);

    // Interpolate master volume for this sample
    g_currentMasterGain += masterStep;

    float preLimiter[MATRIX_CHANNEL_COUNT];

    // 5 - 7. Per-Channel DSP Pipeline
    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      const OutputChannelConfig& cfg = g_dspSettings.channel[ch];

      // Interpolate channel gain for this sample (anti-click)
      g_currentChGain[ch] += chGainStep[ch];

      // 5. Matrix Routing & Polarity Invert (0° / 180°)
      float y = selectInput(eqL, eqR, cfg.source);
      if (cfg.phaseInvert) {
        y = -y;
      }

      // 6. Linkwitz-Riley 24dB/oct Crossover Filter
      y = processBandLR24(ch, y, cfg.band);

      // 7. Channel Gain, Master Volume & PSRAM Delay Buffer (Time Alignment)
      y *= g_currentChGain[ch] * g_currentMasterGain;
      y = dspDelay[ch].process(y, cfg.delayMs);

      // 8. 10 Hz DC-Blocking Filter
      y = dspDCBlock[ch].process(y);

      preLimiter[ch] = isFiniteSafe(y) ? y : 0.0f;
    }

    // 9. True Peak Limiter with Lookahead and Brick-wall Protection
    float postLimiter[MATRIX_CHANNEL_COUNT];
    bool sampleLimited = false;
    dspLimiter.process(preLimiter, postLimiter, sampleLimited);
    if (sampleLimited) {
      blockLimited = true;
    }

    // 10. Startup/Unmute Soft Ramp
    if (g_unmuteRamp < 1.0f) {
      g_unmuteRamp += unmuteStep;
      if (g_unmuteRamp > 1.0f) g_unmuteRamp = 1.0f;
    }

    for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
      postLimiter[ch] *= g_unmuteRamp;
      const float absY = fabsf(postLimiter[ch]);
      if (absY > peak[ch]) peak[ch] = absY;
      sumSq[ch] += absY * absY;
    }

    // Dual I2S 32-bit transport (24-bit audio depth)
    i2s0Out[2 * i]     = floatToI2S24(postLimiter[0]); // CH A (Mid Left)
    i2s0Out[2 * i + 1] = floatToI2S24(postLimiter[1]); // CH B (Mid Right)
    i2s1Out[2 * i]     = floatToI2S24(postLimiter[2]); // CH C (Subwoofer)
    i2s1Out[2 * i + 1] = floatToI2S24(postLimiter[3]); // CH D (Tweeter / High)
  }

  // Settle exact target gains at end of block
  g_currentMasterGain = targetMaster;
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    g_currentChGain[ch] = targetChGain[ch];
  }

  // Update clipping/limiting status flag with hold decay
  if (blockLimited) {
    g_isClipping = true;
    g_lastClipTimeMs = millis();
  } else if (g_isClipping && (millis() - g_lastClipTimeMs > 180)) {
    g_isClipping = false;
  }

  updateVULevels(peak, sumSq, numFrames);

  // Measure DSP execution latency & update CPU load percentage
  const int64_t dspElapsedUs = esp_timer_get_time() - dspStartUs;
  const float blockDurationUs = (float)numFrames * (1000000.0f / (float)AUDIO_SAMPLE_RATE);
  if (blockDurationUs > 0.0f) {
    const float currentLoad = ((float)dspElapsedUs / blockDurationUs) * 100.0f;
    const float prevLoad = g_dspCpuLoadPct.load(std::memory_order_relaxed);
    g_dspCpuLoadPct.store(prevLoad + (currentLoad - prevLoad) * 0.1f, std::memory_order_relaxed);
  }
}

inline void clearSafeMode() {
  g_systemFaults.store(0, std::memory_order_release);
  g_systemSafeMode.store(false, std::memory_order_release);
  g_audioMuted.store(false, std::memory_order_release);
  g_unmuteRamp = 0.0f;
  i2sResetErrorCounter();
  Serial.println("[DSP] Safe Mode cleared via manual reset.");
}

inline void dspBegin() {
  dspPeqL.reset();
  dspPeqR.reset();
  dspLimiter.init();
  
  bool anyChannelUsedPsram = false;
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    dspCh[ch].reset();
    dspDCBlock[ch].reset();
    dspDelay[ch].init(DELAY_BUFFER_MAX);
    if (dspDelay[ch]._usedPsram) {
      anyChannelUsedPsram = true;
    }
    g_vuLevels[ch] = 0.0f;
    g_currentChGain[ch] = 1.0f;
  }
  g_psramActive = anyChannelUsedPsram;

  g_currentMasterGain = 0.85f;
  g_headroomGain = 1.0f;
  g_unmuteRamp = 0.0f;
  appliedSub = appliedMidLow = appliedMidHigh = appliedHigh = -1.0f;
  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    appliedPeq[i].freq = -1.0f;
  }

  dspSyncSettings(g_dspSettings);
  if (!validateAudioSettings(g_dspSettings)) {
    configLoadDefaults();
    dspSyncSettings(g_dspSettings);
    g_configErrors.fetch_add(1, std::memory_order_relaxed);
  }
  g_lastKnownGoodSettings = g_dspSettings;
  dspApplySettingsIfNeeded();
  Serial.println("[DSP] Core 0 Audio Pipeline Initialized (LR24, 5-Band PEQ, PSRAM Delay, DC-Blocker, Brick-Wall Limiter).");
}
