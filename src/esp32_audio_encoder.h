#pragma once
// =============================================================================
// esp32_audio_encoder.h
// Hardware-Interrupt Rotary Encoder + Button Handler
//
// Architecture:
//   - ISR (CHANGE on CLK+DT): 4-bit quadrature state machine → atomic delta
//   - ISR (CHANGE on SW):     dual-edge → short/long press detection
//   - encoderProcess():       reads atomics, fires navigation callbacks
//
// Thread Safety:
//   All state shared between ISR and loop() is std::atomic<>.
//   ISR-private state (g_isrPrevAB, g_isrEncAccum) is volatile, single-writer.
// =============================================================================

#include "esp32_audio_oled_ui.h"
#include "esp32_audio_bt_select.h"
#include <Arduino.h>
#include <atomic>
#include "driver/gpio.h"

// ── External linkage (defined in esp32_audio_nvs.h) ─────────────────────────
extern volatile bool     g_settingsDirty;
extern volatile uint32_t g_lastEditTime;

// ── Atomic shared state: ISR ↔ loop() ───────────────────────────────────────
inline std::atomic<int16_t> g_encoderDelta{0};       // Signed quadrature tick accumulator
inline std::atomic<bool>    g_btnShortPress{false};  // Set by ISR on valid short release
inline std::atomic<bool>    g_btnLongFired{false};   // Set by loop() when long press fires

// ── ISR-private state (only written+read by ISR, not shared with loop) ──────
static volatile uint8_t  g_isrPrevAB    = 0;
static volatile int8_t   g_isrEncAccum  = 0;   // Accumulates partial detent ticks
static volatile bool     g_isrBtnDown   = false;
static volatile uint32_t g_isrPressMs   = 0;

// ── Timing constants ─────────────────────────────────────────────────────────
static constexpr uint32_t LONG_PRESS_MS   = 600;   // Long press threshold (ms)
static constexpr uint32_t BTN_DEBOUNCE_MS = 15;    // Reject glitches shorter than this

// ── 4-bit Quadrature Lookup Table ────────────────────────────────────────────
// Index = (prevAB << 2) | currAB  →  step: -1 (CCW), 0 (no move), +1 (CW)
// Handles all gray-code transitions including invalid/bounce states (→ 0).
static const int8_t ENC_TABLE[16] = {
     0, -1,  1,  0,   // prev = 00
     1,  0,  0, -1,   // prev = 01
    -1,  0,  0,  1,   // prev = 10
     0,  1, -1,  0    // prev = 11
};

// ── ISR: Rotary Encoder (CLK and DT both trigger on CHANGE) ─────────────────
static void IRAM_ATTR encoderISR(void*) {
    const uint8_t clk = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENC_CLK);
    const uint8_t dt  = (uint8_t)gpio_get_level((gpio_num_t)PIN_ENC_DT);
    const uint8_t ab  = (clk << 1) | dt;
    const uint8_t idx = (g_isrPrevAB << 2) | ab;
    g_isrPrevAB = ab;

    g_isrEncAccum += ENC_TABLE[idx];

    // Emit one step per full detent (4 transitions)
    if (g_isrEncAccum >= 4) {
        g_isrEncAccum -= 4;
        g_encoderDelta.fetch_add(1, std::memory_order_relaxed);
    } else if (g_isrEncAccum <= -4) {
        g_isrEncAccum += 4;
        g_encoderDelta.fetch_add(-1, std::memory_order_relaxed);
    }
}

// ── ISR: Button SW (dual-edge CHANGE) ────────────────────────────────────────
static void IRAM_ATTR buttonISR(void*) {
    const bool     pressed = (gpio_get_level((gpio_num_t)PIN_ENC_SW) == 0);
    // B-10 FIX: Never call millis() in ISR context. Use esp_timer_get_time() instead.
    const uint32_t now     = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (pressed && !g_isrBtnDown) {
        // Falling edge — record press start
        g_isrBtnDown = true;
        g_isrPressMs = now;
        g_btnLongFired.store(false, std::memory_order_relaxed);
    } else if (!pressed && g_isrBtnDown) {
        // Rising edge — evaluate duration
        g_isrBtnDown = false;
        const uint32_t held = now - g_isrPressMs;
        if (held >= BTN_DEBOUNCE_MS && held < LONG_PRESS_MS) {
            if (!g_btnLongFired.load(std::memory_order_relaxed)) {
                g_btnShortPress.store(true, std::memory_order_release);
            }
        }
    }
}

// =============================================================================
// encoderBegin() — call once from setup()
// =============================================================================
inline void encoderBegin() {
    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT,  INPUT_PULLUP);
    pinMode(PIN_ENC_SW,  INPUT_PULLUP);

    // Capture initial pin state so first ISR fires correctly (no phantom step)
    g_isrPrevAB = (uint8_t)((digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT));

    attachInterruptArg(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, nullptr, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(PIN_ENC_DT),  encoderISR, nullptr, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(PIN_ENC_SW),  buttonISR,  nullptr, CHANGE);

    Serial.println("[ENC] Hardware interrupt encoder ready (CLK=" 
                   + String(PIN_ENC_CLK) + " DT=" + String(PIN_ENC_DT)
                   + " SW=" + String(PIN_ENC_SW) + ")");
}

// =============================================================================
// Helper: mark settings dirty for NVS auto-save and UI refresh
// =============================================================================
inline void markSettingsDirty() {
    g_settingsDirty = true;
    g_lastEditTime  = millis();
    g_uiDirty       = true;
}

// =============================================================================
// Navigation: escape to HOME from any screen depth
// =============================================================================
inline void goHome() {
    if (g_inBTSelect) btExitSelectMode();
    g_uiScreen   = UIScreen::HOME;
    g_inMenu     = false;
    g_inBTSelect = false;
    g_editing    = false;
    g_menuIndex  = 0;
    g_uiDirty    = true;
}

// =============================================================================
// Navigation: double‑click returns to previous screen level (or HOME if none)
// =============================================================================
inline void goBack() {
    if (g_editing) {
        g_editing = false;
        g_uiDirty = true;
        return;
    }

    switch (g_uiScreen) {
        case UIScreen::CHANNEL_SETUP:
            g_uiScreen = UIScreen::MATRIX_MENU;
            g_menuIndex = g_selectedChannel; 
            break;
        case UIScreen::PEQ_BAND_EDIT:
            g_uiScreen = UIScreen::PEQ_BAND_SELECT;
            g_menuIndex = g_selectedPeqBand;
            break;
        case UIScreen::PRESET_MENU:
        case UIScreen::MATRIX_MENU:
        case UIScreen::PEQ_BAND_SELECT:
        case UIScreen::DELAY_MENU:
        case UIScreen::PHASE_MENU:
        case UIScreen::CROSSOVER_MENU:
        case UIScreen::BT_SELECT:
        case UIScreen::DIAGNOSTICS:
            g_uiScreen = UIScreen::MAIN_MENU;
            break;
        default:
            return;
    }
    g_uiDirty = true;
}

// =============================================================================
// Preset Application
// =============================================================================
inline void applyPreset(Preset p) {
    audioSettingsLock();
    g_settings.preset = p;

    // Reset all PEQ gains to flat baseline first
    for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
        g_settings.peq[i].gainDb = 0.0f;
    }

    switch (p) {
        case Preset::BASS_BOOST:
            g_settings.peq[0].gainDb =  6.0f;   // 80 Hz LowShelf
            g_settings.peq[1].gainDb =  2.0f;   // 250 Hz Peak
            g_settings.peq[4].gainDb =  2.0f;   // 12 kHz HighShelf
            break;
        case Preset::VOCAL:
            g_settings.peq[0].gainDb = -2.0f;   // 80 Hz LowShelf
            g_settings.peq[2].gainDb =  3.5f;   // 1 kHz Peak
            g_settings.peq[3].gainDb =  2.0f;   // 4 kHz Peak
            g_settings.peq[4].gainDb =  1.0f;   // 12 kHz HighShelf
            break;
        case Preset::ROCK:
            g_settings.peq[0].gainDb =  4.5f;   // 80 Hz LowShelf
            g_settings.peq[1].gainDb =  1.5f;   // 250 Hz Peak
            g_settings.peq[2].gainDb = -1.5f;   // 1 kHz Peak
            g_settings.peq[3].gainDb =  2.5f;   // 4 kHz Peak
            g_settings.peq[4].gainDb =  3.5f;   // 12 kHz HighShelf
            break;
        default: // FLAT / CUSTOM: gains already zeroed above
            break;
    }
    audioSettingsUnlock();
    markSettingsDirty();
}

// =============================================================================
// Parameter Adjustment Helpers
// =============================================================================

inline void adjustChannelField(uint8_t ch, uint8_t field, int d) {
    audioSettingsLock();
    OutputChannelConfig& cfg = g_settings.channel[ch];
    switch (field) {
        case 0: cfg.source  = (InputSource)constrain((int)cfg.source + d, 0, 2); break;
        case 1: cfg.band    = (FilterBand) constrain((int)cfg.band   + d, 0, 3); break;
        case 2: cfg.gainDb  = clampf(cfg.gainDb  + (float)d * 0.5f, -20.0f, 6.0f); break;
        case 3: if (d != 0) cfg.mute = !cfg.mute; break;
        case 4: cfg.delayMs = clampf(cfg.delayMs + (float)d * 0.1f, 0.0f, MAX_DELAY_MS); break;
        default: break;
    }
    g_settings.preset = Preset::CUSTOM;
    audioSettingsUnlock();
    markSettingsDirty();
}

inline float stepFreq(float f, int d) {
    float step = 10.0f;
    if      (f >= 5000.0f) step = 250.0f;
    else if (f >= 1000.0f) step =  50.0f;
    else if (f >=  200.0f) step =  20.0f;
    return clampf(f + (float)d * step, 20.0f, 20000.0f);
}

inline void adjustPEQField(uint8_t band, uint8_t field, int d) {
    audioSettingsLock();
    PEQBandConfig& b = g_settings.peq[band];
    switch (field) {
        case 0: b.type   = (PEQType)constrain((int)b.type + d, 0, 2); break;
        case 1: b.freq   = stepFreq(b.freq, d); break;
        case 2: b.gainDb = clampf(b.gainDb + (float)d * 0.5f,  -12.0f, 12.0f); break;
        case 3: b.q      = clampf(b.q      + (float)d * 0.05f,   0.1f, 10.0f); break;
        default: break;
    }
    g_settings.preset = Preset::CUSTOM;
    audioSettingsUnlock();
    markSettingsDirty();
}

// B-11 FIX: Adjust raw values safely and let validateCrossoverRelational() enforce boundaries atomically.
inline void adjustCrossoverField(uint8_t field, int d) {
    audioSettingsLock();
    switch (field) {
        case 0: // Sub LPF
            g_settings.subLpfHz = clampf(g_settings.subLpfHz + (float)d * 5.0f, 20.0f, 500.0f);
            break;
        case 1: // Mid HPF
            g_settings.midLowHz = clampf(g_settings.midLowHz + (float)d * 5.0f, 20.0f, 1000.0f);
            break;
        case 2: // Mid LPF
            g_settings.midHighHz = clampf(g_settings.midHighHz + (float)d * 50.0f, 500.0f, 15000.0f);
            break;
        case 3: // High HPF
            g_settings.highHpfHz = clampf(g_settings.highHpfHz + (float)d * 50.0f, TWEETER_MIN_FREQ_HZ, 20000.0f);
            break;
        default: break;
    }
    // Final relational validation pass (enforces all constraints atomically & prevents negative/invalid values)
    validateCrossoverRelational(
        g_settings.subLpfHz, g_settings.midLowHz, g_settings.midHighHz, g_settings.highHpfHz);
    g_settings.preset = Preset::CUSTOM;
    audioSettingsUnlock();
    markSettingsDirty();
}

// =============================================================================
// Click Handler — SHORT PRESS navigation
// =============================================================================
inline void handleClick() {
    // ── Leaf screens: go back to parent ─────────────────────────────────────
    if (g_uiScreen == UIScreen::BT_SELECT) {
        btExitSelectMode();
        g_inBTSelect = false;
        g_uiScreen   = UIScreen::MAIN_MENU;
        g_inMenu     = true;
        g_menuIndex  = 7;
        g_uiDirty    = true;
        return;
    }

    if (g_uiScreen == UIScreen::DIAGNOSTICS) {
        g_uiScreen  = UIScreen::MAIN_MENU;
        g_inMenu    = true;
        g_menuIndex = 8;
        g_uiDirty   = true;
        return;
    }

    // ── HOME → enter MAIN MENU ───────────────────────────────────────────────
    if (g_uiScreen == UIScreen::HOME) {
        g_uiScreen  = UIScreen::MAIN_MENU;
        g_inMenu    = true;
        g_editing   = false;
        g_menuIndex = 0;
        g_uiDirty   = true;
        return;
    }

    // ── MAIN MENU → open sub-screen ─────────────────────────────────────────
    if (g_uiScreen == UIScreen::MAIN_MENU) {
        switch (g_menuIndex) {
            case 0: // Master Volume — toggle inline encoder-edit mode
                g_editing = !g_editing;
                break;
            case 1: // Presets
                g_uiScreen  = UIScreen::PRESET_MENU;
                g_editing   = false;
                g_menuIndex = (uint8_t)constrain((int)g_settings.preset, 0, 4);
                break;
            case 2: // Output Matrix
                g_uiScreen  = UIScreen::MATRIX_MENU;
                g_editing   = false;
                g_menuIndex = 0;
                break;
            case 3: // Parametric EQ
                g_uiScreen  = UIScreen::PEQ_BAND_SELECT;
                g_editing   = false;
                g_menuIndex = 0;
                break;
            case 4: // Time Alignment
                g_uiScreen  = UIScreen::DELAY_MENU;
                g_editing   = false;
                g_menuIndex = 0;
                break;
            case 5: // Phase Invert
                g_uiScreen  = UIScreen::PHASE_MENU;
                g_editing   = false;
                g_menuIndex = 0;
                break;
            case 6: // LR24 Crossover Frequency
                g_uiScreen  = UIScreen::CROSSOVER_MENU;
                g_editing   = false;
                g_menuIndex = 0;
                break;
            case 7: // Bluetooth Status
                g_uiScreen   = UIScreen::BT_SELECT;
                g_editing    = false;
                g_inBTSelect = true;
                break;
            case 8: // System Diagnostics
                g_uiScreen = UIScreen::DIAGNOSTICS;
                g_editing  = false;
                break;
            default: break;
        }
        g_uiDirty = true;
        return;
    }

    // ── PRESET MENU → apply preset ──────────────────────────────────────────
    if (g_uiScreen == UIScreen::PRESET_MENU) {
        applyPreset((Preset)constrain((int)g_menuIndex, 0, 4));
        g_uiScreen  = UIScreen::MAIN_MENU;
        g_menuIndex = 1;
        g_uiDirty   = true;
        return;
    }

    // ── MATRIX MENU → open channel setup ────────────────────────────────────
    if (g_uiScreen == UIScreen::MATRIX_MENU) {
        g_selectedChannel = (uint8_t)constrain((int)g_menuIndex, 0, MATRIX_CHANNEL_COUNT - 1);
        g_uiScreen  = UIScreen::CHANNEL_SETUP;
        g_editing   = false;
        g_menuIndex = 0;
        g_uiDirty   = true;
        return;
    }

    // ── PEQ BAND SELECT → open band edit ────────────────────────────────────
    if (g_uiScreen == UIScreen::PEQ_BAND_SELECT) {
        g_selectedPeqBand = (uint8_t)constrain((int)g_menuIndex, 0, PEQ_BAND_COUNT - 1);
        g_uiScreen  = UIScreen::PEQ_BAND_EDIT;
        g_editing   = false;
        g_menuIndex = 0;
        g_uiDirty   = true;
        return;
    }

    // ── PHASE MENU → toggle selected channel phase ───────────────────────────
    if (g_uiScreen == UIScreen::PHASE_MENU) {
        const uint8_t ch = (uint8_t)constrain((int)g_menuIndex, 0, MATRIX_CHANNEL_COUNT - 1);
        audioSettingsLock();
        g_settings.channel[ch].phaseInvert = !g_settings.channel[ch].phaseInvert;
        g_settings.preset = Preset::CUSTOM;
        audioSettingsUnlock();
        markSettingsDirty();
        return;
    }

    // ── Edit screens → toggle edit mode ─────────────────────────────────────
    if (g_uiScreen == UIScreen::DELAY_MENU    ||
        g_uiScreen == UIScreen::CHANNEL_SETUP ||
        g_uiScreen == UIScreen::PEQ_BAND_EDIT ||
        g_uiScreen == UIScreen::CROSSOVER_MENU) {
        g_editing = !g_editing;
        g_uiDirty = true;
    }
}

// =============================================================================
// Rotation Handler — encoder delta dispatched here
// =============================================================================
inline void handleRotation(int d) {
    if (d == 0) return;

    switch (g_uiScreen) {
        // ── HOME: encoder directly adjusts master volume ─────────────────────
        case UIScreen::HOME:
            audioSettingsLock();
            g_settings.masterVolume = (uint8_t)constrain((int)g_settings.masterVolume + d, 0, 100);
            audioSettingsUnlock();
            markSettingsDirty();
            break;

        // ── MAIN MENU ────────────────────────────────────────────────────────
        case UIScreen::MAIN_MENU:
            if (g_editing && g_menuIndex == 0) {
                audioSettingsLock();
                g_settings.masterVolume = (uint8_t)constrain((int)g_settings.masterVolume + d, 0, 100);
                audioSettingsUnlock();
                markSettingsDirty();
            } else {
                g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, MAIN_MENU_COUNT - 1);
                g_uiDirty = true;
            }
            break;

        case UIScreen::PRESET_MENU:
            g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, 4);
            g_uiDirty = true;
            break;

        case UIScreen::MATRIX_MENU:
            g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, MATRIX_CHANNEL_COUNT - 1);
            g_uiDirty = true;
            break;

        case UIScreen::CHANNEL_SETUP:
            if (g_editing) {
                adjustChannelField(g_selectedChannel, g_menuIndex, d);
            } else {
                g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, CHANNEL_FIELD_COUNT - 1);
                g_uiDirty = true;
            }
            break;

        case UIScreen::PEQ_BAND_SELECT:
            g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, PEQ_BAND_COUNT - 1);
            g_uiDirty = true;
            break;

        case UIScreen::PEQ_BAND_EDIT:
            if (g_editing) {
                adjustPEQField(g_selectedPeqBand, g_menuIndex, d);
            } else {
                g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, PEQ_FIELD_COUNT - 1);
                g_uiDirty = true;
            }
            break;

        case UIScreen::DELAY_MENU:
            if (g_editing) {
                const uint8_t ch = g_menuIndex;
                audioSettingsLock();
                g_settings.channel[ch].delayMs = clampf(
                    g_settings.channel[ch].delayMs + (float)d * 0.1f, 0.0f, MAX_DELAY_MS);
                g_settings.preset = Preset::CUSTOM;
                audioSettingsUnlock();
                markSettingsDirty();
            } else {
                g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, MATRIX_CHANNEL_COUNT - 1);
                g_uiDirty = true;
            }
            break;

        case UIScreen::PHASE_MENU:
            g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, MATRIX_CHANNEL_COUNT - 1);
            g_uiDirty = true;
            break;

        case UIScreen::CROSSOVER_MENU:
            if (g_editing) {
                adjustCrossoverField(g_menuIndex, d);
            } else {
                g_menuIndex = (uint8_t)constrain((int)g_menuIndex + d, 0, CROSSOVER_FIELD_COUNT - 1);
                g_uiDirty = true;
            }
            break;

        default:
            break;
    }
}

// =============================================================================
// encoderProcess() — called every loop() iteration on Core 1
// =============================================================================
inline void encoderProcess() {
    static uint32_t pendingClickMs     = 0;
    static bool     pendingSingleClick = false;

    // ── 0. Safe Mode Guard ───────────────────────────────────────────────────
    if (g_systemSafeMode.load(std::memory_order_relaxed)) {
        if (g_isrBtnDown && !g_btnLongFired.load(std::memory_order_relaxed)) {
            const uint32_t nowMs  = (uint32_t)(esp_timer_get_time() / 1000ULL);
            const uint32_t heldMs = nowMs - g_isrPressMs;
            if (heldMs >= 5000) {
                g_btnLongFired.store(true, std::memory_order_release);
                g_btnShortPress.store(false, std::memory_order_relaxed);
                g_encoderDelta.store(0, std::memory_order_relaxed);
                pendingSingleClick = false;
                clearSafeMode();
                goHome();
                return;
            }
        }
        // Discard any short press or rotation input while in Safe Mode
        g_btnShortPress.store(false, std::memory_order_relaxed);
        g_encoderDelta.store(0, std::memory_order_relaxed);
        pendingSingleClick = false;
        return;
    }

    // ── 1. Long Press (polled here; avoids blocking ISR context) ─────────────
    if (g_isrBtnDown && !g_btnLongFired.load(std::memory_order_relaxed)) {
        const uint32_t nowMs  = (uint32_t)(esp_timer_get_time() / 1000ULL);
        const uint32_t heldMs = nowMs - g_isrPressMs;
        if (heldMs >= LONG_PRESS_MS) {
            g_btnLongFired.store(true, std::memory_order_release);
            g_btnShortPress.store(false, std::memory_order_relaxed);
            g_encoderDelta.store(0, std::memory_order_relaxed);
            pendingSingleClick = false;
            goHome();
            return;
        }
    }

    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // ── 2. Short Press Handling (Double-Click vs Single-Click Window) ────────
    if (g_btnShortPress.exchange(false, std::memory_order_acquire)) {
        g_lastUserActivity = millis();
        if (pendingSingleClick) {
            // Second short press completed while first click was pending → DOUBLE CLICK!
            pendingSingleClick = false;
            goBack();
        } else {
            // First click → Record timestamp and wait for double-click window
            pendingSingleClick = true;
            pendingClickMs     = now;
        }
        g_encoderDelta.store(0, std::memory_order_relaxed);
        return;
    }

    // ── 3. Expire Pending Single Click ───────────────────────────────────────
    // Only expire if button is not currently held down (user might be mid-second-click)
    if (pendingSingleClick && !g_isrBtnDown && (now - pendingClickMs >= 300)) {
        pendingSingleClick = false;
        handleClick();
    }

    // ── 4. Rotation ──────────────────────────────────────────────────────────
    const int16_t delta = g_encoderDelta.exchange(0, std::memory_order_acq_rel);
    if (delta != 0) {
        g_lastUserActivity = millis();
        // If user rotates while a single-click was pending, flush single-click immediately
        if (pendingSingleClick) {
            pendingSingleClick = false;
            handleClick();
        }
        handleRotation((int)delta);
    }
}

