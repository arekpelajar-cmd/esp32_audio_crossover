#pragma once
// =============================================================================
// esp32_audio_oled_ui.h
// SSD1306 128x64 OLED UI — Event-Driven State Machine Renderer
//
// Design Principles:
//   - All draw functions build into the GFX framebuffer; they do NOT call
//     g_oled.display(). A single I2C transfer is issued at the end of
//     uiLoop() after all draw operations are complete (including overlays).
//     This eliminates redundant I2C traffic when overlays are active.
//   - HOME screen refreshes at ~30 FPS unconditionally (live VU meters).
//   - Menu screens refresh only when g_uiDirty == true (event-driven).
//   - Safe Mode overrides all screens with a full-width fault banner.
// =============================================================================

#ifndef ESP32_AUDIO_OLED_UI_H
#define ESP32_AUDIO_OLED_UI_H

#include "esp32_audio_crossover_config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// ── Bluetooth state (defined in esp32_audio_bluetooth.h) ────────────────────
extern std::atomic<bool> g_btConnected;
extern char g_trackTitle[64];
void btStatusSnapshot(bool &connected, char *title, size_t titleLen);

// ── NVS save notification (defined in esp32_audio_nvs.h) ────────────────────
extern volatile bool g_nvsSaveNotify;
extern volatile uint32_t g_nvsSaveNotifyMs;

// ── OLED device instance ─────────────────────────────────────────────────────
inline Adafruit_SSD1306 g_oled(128, 64, &Wire, -1);

// ── UI State Machine ─────────────────────────────────────────────────────────
enum class UIScreen : uint8_t {
  HOME = 0,
  MAIN_MENU,
  PRESET_MENU,
  MATRIX_MENU,
  CHANNEL_SETUP,
  PEQ_BAND_SELECT,
  PEQ_BAND_EDIT,
  DELAY_MENU,
  PHASE_MENU,
  CROSSOVER_MENU,
  BT_SELECT,
  DIAGNOSTICS
};

// ── Icon Bitmaps (8x8) ───────────────────────────────────────────────────────
static const uint8_t PROGMEM icon_vol[] = {0x00, 0x08, 0x18, 0x3E,
                                           0x3E, 0x18, 0x08, 0x00};
static const uint8_t PROGMEM icon_presets[] = {0x18, 0x3C, 0x7E, 0xFF,
                                               0xFF, 0x7E, 0x3C, 0x18};
static const uint8_t PROGMEM icon_matrix[] = {0xAA, 0x55, 0xAA, 0x55,
                                              0xAA, 0x55, 0xAA, 0x55};
static const uint8_t PROGMEM icon_eq[] = {0x44, 0x44, 0x54, 0x54,
                                          0x55, 0x55, 0x55, 0x55};
static const uint8_t PROGMEM icon_time[] = {0x3C, 0x42, 0x81, 0x91,
                                            0x89, 0x81, 0x42, 0x3C};
static const uint8_t PROGMEM icon_phase[] = {0x18, 0x24, 0x42, 0x81,
                                             0x81, 0x42, 0x24, 0x18};
static const uint8_t PROGMEM icon_cross[] = {0x81, 0x42, 0x24, 0x18,
                                             0x18, 0x24, 0x42, 0x81};
static const uint8_t PROGMEM icon_bt[] = {0x10, 0x30, 0x54, 0x38,
                                          0x38, 0x54, 0x30, 0x10};
static const uint8_t PROGMEM icon_diag[] = {0x00, 0x18, 0x18, 0x7E,
                                            0x7E, 0x18, 0x18, 0x00};

static const uint8_t *const MAIN_MENU_ICONS[] = {
    icon_vol,   icon_presets, icon_matrix, icon_eq,  icon_time,
    icon_phase, icon_cross,   icon_bt,     icon_diag};

// ── UI Global State (written by encoder, read by draw functions) ─────────────
inline volatile UIScreen g_uiScreen = UIScreen::HOME;
inline volatile bool g_inMenu = false;
inline volatile bool g_editing = false;
inline volatile uint8_t g_menuIndex = 0;
inline volatile uint8_t g_selectedChannel = 0;
inline volatile uint8_t g_selectedPeqBand = 0;
// B-17 FIX: g_uiDirty is defined in esp32_audio_crossover_config.h (single
// source of truth)
inline volatile bool g_inBTSelect = false;
void btExitSelectMode();
inline volatile uint32_t g_lastUIRefresh = 0;
inline volatile uint32_t g_lastUserActivity = 0;
inline volatile bool g_toastActive = false;
inline volatile uint32_t g_toastEndTime = 0;
inline char g_toastMessage[32] = {0};

inline float g_vuPeakVal[MATRIX_CHANNEL_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
inline uint32_t g_vuPeakTime[MATRIX_CHANNEL_COUNT] = {0, 0, 0, 0};

inline void uiShowToast(const char *msg, uint32_t durationMs = 1500) {
  strncpy(g_toastMessage, msg, sizeof(g_toastMessage) - 1);
  g_toastEndTime = millis() + durationMs;
  g_toastActive = true;
  g_uiDirty = true;
}

// ── Menu Definitions ─────────────────────────────────────────────────────────
static const char *MAIN_MENU_ITEMS[] = {
    "Master Volume",  "Presets",        "Output Matrix",
    "Parametric EQ",  "Time Alignment", "Phase Invert",
    "LR24 Crossover", "Bluetooth Info", "Diagnostics"};
static const uint8_t MAIN_MENU_COUNT = 9;
static const uint8_t CHANNEL_FIELD_COUNT = 5;
static const uint8_t PEQ_FIELD_COUNT = 4;
static const uint8_t CROSSOVER_FIELD_COUNT = 4;

// =============================================================================
// uiBegin() — call once from setup() before encoderBegin()
// =============================================================================
inline void uiBegin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000); // 400 kHz Fast-mode I2C

  if (!g_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] ERROR: SSD1306 not found on I2C bus.");
    return;
  }

  // ── Animated Boot Sequence (~800ms) ──────────────────────────────────────
  for (int frame = 0; frame <= 20; frame++) {
    g_oled.clearDisplay();

    // Header Banner
    g_oled.fillRect(0, 0, 128, 11, SSD1306_WHITE);
    g_oled.setTextColor(SSD1306_BLACK);
    g_oled.setCursor(8, 2);
    g_oled.print("ESP32 AUDIO DSP PRO");
    g_oled.setTextColor(SSD1306_WHITE);

    // Subtitle
    g_oled.setCursor(12, 15);
    g_oled.print("LR24 + 5-BAND PEQ");

    // Animated Audio Waveform (Moving Spectrum Waves)
    const int16_t waveCenterY = 30;
    for (int x = 8; x < 120; x += 4) {
      float phase = (float)x * 0.12f + (float)frame * 0.35f;
      int16_t h = (int16_t)(sinf(phase) * 5.0f);
      if (h > 0) {
        g_oled.drawFastVLine(x, waveCenterY - h, h * 2, SSD1306_WHITE);
      } else {
        g_oled.drawPixel(x, waveCenterY, SSD1306_WHITE);
      }
    }

    // Animated Progress Bar (0% -> 100%)
    float progress = (float)frame / 20.0f;
    g_oled.drawRoundRect(14, 43, 100, 7, 2, SSD1306_WHITE);
    int16_t fillW = (int16_t)(progress * 96.0f);
    if (fillW > 0) {
      g_oled.fillRoundRect(16, 45, fillW, 3, 1, SSD1306_WHITE);
    }

    // Footer Status
    g_oled.setCursor(18, 55);
    g_oled.print(g_psramActive ? "SYSTEM READY (PSRAM)" : "SYSTEM READY (RAM)");

    g_oled.display();
    delay(35); // Smooth 28 FPS boot animation
  }

  g_lastUIRefresh = millis();
  g_lastUserActivity = millis();
  Serial.println("[OLED] SSD1306 128x64 ready at 400kHz I2C.");
}

// =============================================================================
// Internal Draw Helpers
// NOTE: None of these functions call g_oled.display().
//       The single display() call is at the end of uiLoop().
// =============================================================================

inline void drawVUBar(uint8_t ch, int16_t x, int16_t y, uint8_t width,
                      uint8_t height, float value) {
  value = clampf(value, 0.0f, 1.0f);
  float scaledVal = powf(value, 0.4f); // Perceptual scaling

  uint32_t now = millis();
  if (ch < MATRIX_CHANNEL_COUNT) {
    if (scaledVal >= g_vuPeakVal[ch]) {
      g_vuPeakVal[ch] = scaledVal;
      g_vuPeakTime[ch] = now;
    } else {
      if (now - g_vuPeakTime[ch] > 800) { // 800ms hold time
        g_vuPeakVal[ch] -= 0.04f;         // Smooth decay
        if (g_vuPeakVal[ch] < scaledVal)
          g_vuPeakVal[ch] = scaledVal;
      }
    }
  }

  // Draw smooth continuous filled bar
  g_oled.drawRoundRect(x, y, width, height, 2, SSD1306_WHITE);
  int16_t fillW = (int16_t)(scaledVal * (width - 2));
  if (fillW > 0) {
    g_oled.fillRoundRect(x + 1, y + 1, fillW, height - 2, 1, SSD1306_WHITE);
  }

  // Draw Peak Hold tick mark line
  if (ch < MATRIX_CHANNEL_COUNT) {
    int16_t peakX = x + 1 + (int16_t)(g_vuPeakVal[ch] * (width - 3));
    if (peakX >= x + 1 && peakX <= x + width - 2) {
      g_oled.drawFastVLine(peakX, y + 1, height - 2, SSD1306_WHITE);
    }
  }
}

inline void drawVUMeter(uint8_t ch, int16_t x, int16_t y, uint8_t width,
                        uint8_t height = 5) {
  if (ch >= MATRIX_CHANNEL_COUNT)
    return;
  drawVUBar(ch, x, y, width, height, g_vuLevels[ch]);
}

// Render horizontal scrolling marquee text for long strings
inline void drawMarqueeText(int16_t x, int16_t y, uint8_t maxPixelWidth,
                            const char *text) {
  if (!text || text[0] == '\0')
    return;

  const size_t len = strlen(text);
  const uint8_t maxChars =
      maxPixelWidth / 6; // 6px per char (5px font + 1px spacing)

  if (len <= maxChars) {
    g_oled.setCursor(x, y);
    g_oled.print(text);
    return;
  }

  // Scroll timing: advance offset every 180ms
  static uint32_t lastScrollMs = 0;
  static uint16_t scrollOffset = 0;
  const uint32_t now = millis();

  if (now - lastScrollMs >= 180) {
    lastScrollMs = now;
    scrollOffset++;
  }

  // Create padded string: text + "   •   "
  char paddedBuf[128];
  snprintf(paddedBuf, sizeof(paddedBuf), "%s   •   ", text);
  const size_t paddedLen = strlen(paddedBuf);

  if (scrollOffset >= paddedLen) {
    scrollOffset = 0;
  }

  // Build visible window string of maxChars
  char windowBuf[32];
  uint8_t wIdx = 0;
  for (uint8_t i = 0; i < maxChars && i < (sizeof(windowBuf) - 1); i++) {
    const size_t srcIdx = (scrollOffset + i) % paddedLen;
    windowBuf[wIdx++] = paddedBuf[srcIdx];
  }
  windowBuf[wIdx] = '\0';

  g_oled.setCursor(x, y);
  g_oled.print(windowBuf);
}

inline void formatChannelStatus(uint8_t ch, char *out, size_t len) {
  if (ch >= MATRIX_CHANNEL_COUNT || len == 0) {
    if (len)
      out[0] = '\0';
    return;
  }
  const OutputChannelConfig &cfg = g_settings.channel[ch];
  snprintf(out, len, "%c%s[%s-%s]%s", channelName(ch),
           cfg.phaseInvert ? "~" : " ", filterBandShort(cfg.band),
           inputSourceShort(cfg.source), cfg.mute ? " X" : "");
}

inline const char *presetValue(uint8_t idx) {
  switch (idx) {
  case 0:
    return presetName(Preset::FLAT);
  case 1:
    return presetName(Preset::BASS_BOOST);
  case 2:
    return presetName(Preset::VOCAL);
  case 3:
    return presetName(Preset::ROCK);
  default:
    return presetName(Preset::CUSTOM);
  }
}

// Render a text string with inverted colors (white background, black text)
// Used to highlight the active edit field.
inline void drawInvertedValue(int16_t x, int16_t y, const char *text) {
  const uint16_t w = (uint16_t)(strlen(text) * 6U + 4U);
  g_oled.fillRect(x, y, (int16_t)w, 9, SSD1306_WHITE);
  g_oled.setTextColor(SSD1306_BLACK);
  g_oled.setCursor(x + 2, y + 1);
  g_oled.print(text);
  g_oled.setTextColor(SSD1306_WHITE);
}

// ── New Visual Helpers ──────────────────────────────────────────────────────
inline void drawRightAlignedValue(int16_t y, const char *text, bool isEditing) {
  int16_t x1, y1;
  uint16_t w, h;
  g_oled.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = 122 - w; // Aligned just before the scrollbar (x=125)

  if (isEditing) {
    if ((millis() / 400) % 2 == 0) { // Blink every 400ms
      g_oled.fillRect(x - 2, y - 1, w + 4, 10, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
      g_oled.setCursor(x, y);
      g_oled.print(text);
      g_oled.setTextColor(SSD1306_WHITE);
    } else {
      g_oled.setCursor(x, y);
      g_oled.print(text);
    }
  } else {
    g_oled.setCursor(x, y);
    g_oled.print(text);
  }
}

inline void drawHorizontalSlider(int16_t x, int16_t y, int16_t w, int16_t h,
                                 float minVal, float maxVal, float currentVal,
                                 uint16_t color = SSD1306_WHITE) {
  g_oled.drawRoundRect(x, y, w, h, 2, color);
  float range = maxVal - minVal;
  if (range <= 0)
    return;
  float pct = (currentVal - minVal) / range;
  if (pct < 0.0f)
    pct = 0.0f;
  if (pct > 1.0f)
    pct = 1.0f;
  int16_t fillW = (int16_t)(pct * (w - 2));
  if (fillW > 0) {
    g_oled.fillRoundRect(x + 1, y + 1, fillW, h - 2, 1, color);
  }
}

inline void drawToggleSwitch(int16_t x, int16_t y, bool isOn,
                             uint16_t color = SSD1306_WHITE) {
  g_oled.drawRoundRect(x, y, 14, 8, 4, color);
  if (isOn) {
    g_oled.fillCircle(x + 9, y + 3, 2, color);
  } else {
    g_oled.drawCircle(x + 4, y + 3, 2, color);
  }
}

inline void drawRadioButton(int16_t x, int16_t y, bool checked,
                            uint16_t color = SSD1306_WHITE) {
  g_oled.drawCircle(x + 3, y + 3, 3, color);
  if (checked) {
    g_oled.fillCircle(x + 3, y + 3, 1, color);
  }
}

// ── Sandwich UI Layout Helpers ──────────────────────────────────────────────
inline void drawSandwichHeader(const char *title) {
  g_oled.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  g_oled.setTextColor(SSD1306_BLACK);
  g_oled.setCursor(4, 1);
  g_oled.print(title);
  g_oled.setTextColor(SSD1306_WHITE);
}

inline void drawSandwichFooter(const char *footerText) {
  g_oled.fillRect(0, 54, 128, 10, SSD1306_WHITE);
  g_oled.setTextColor(SSD1306_BLACK);
  g_oled.setCursor(4, 55);
  g_oled.print(footerText);
  g_oled.setTextColor(SSD1306_WHITE);
}

inline void drawVerticalScrollbar(uint8_t currentIndex, uint8_t totalItems,
                                  uint8_t visibleRows) {
  if (totalItems <= visibleRows || totalItems == 0)
    return;
  const int16_t trackY = 12;
  const int16_t trackH = 41;
  const int16_t sbX = 125;

  // Background track line
  g_oled.drawLine(sbX, trackY, sbX, trackY + trackH - 1, SSD1306_WHITE);

  // Thumb height & position
  int16_t thumbH = (trackH * visibleRows) / totalItems;
  if (thumbH < 4)
    thumbH = 4;
  if (thumbH > trackH)
    thumbH = trackH;

  const uint8_t maxIndex = (totalItems > 1) ? (totalItems - 1) : 1;
  const int16_t thumbY =
      trackY + (int16_t)((uint32_t)currentIndex * (uint32_t)(trackH - thumbH) /
                         maxIndex);

  g_oled.fillRect(sbX - 1, thumbY, 3, thumbH, SSD1306_WHITE);
}

// =============================================================================
// Screen Rendering Functions — all build into GFX buffer, NO display() call
// =============================================================================

inline void drawHome() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  const bool safeMode = g_systemSafeMode.load(std::memory_order_relaxed);

  if (safeMode) {
    // Full-width fault banner (inverted)
    g_oled.fillRect(0, 0, 128, 10, SSD1306_WHITE);
    g_oled.setTextColor(SSD1306_BLACK);
    g_oled.setCursor(8, 1);
    g_oled.print("! SAFE MODE: MUTED !");
    g_oled.setTextColor(SSD1306_WHITE);

    const uint32_t faults = g_systemFaults.load(std::memory_order_relaxed);
    g_oled.setCursor(0, 14);
    g_oled.println("SYSTEM FAULT DETECTED:");
    g_oled.setCursor(0, 26);
    if (faults & static_cast<uint32_t>(SystemFault::I2S_INIT_FAIL))
      g_oled.println("- I2S Bus Init Failed");
    else if (faults & static_cast<uint32_t>(SystemFault::I2S_WRITE_FAIL))
      g_oled.println("- I2S Write DMA Fault");
    else if (faults & static_cast<uint32_t>(SystemFault::DSP_TASK_FAIL))
      g_oled.println("- DSP Task Starvation");
    else if (faults & static_cast<uint32_t>(SystemFault::DSP_TIMEOUT))
      g_oled.println("- DSP Engine Timeout");
    else if (faults & static_cast<uint32_t>(SystemFault::NVS_CRC_FAIL))
      g_oled.println("- NVS CRC Checksum Err");
    else
      g_oled.println("- Memory/Buffer Error");

    g_oled.setCursor(0, 42);
    g_oled.printf("CPU:%2.0f%% Drops:%u\n",
                  g_dspCpuLoadPct.load(std::memory_order_relaxed),
                  g_audioDrops.load(std::memory_order_relaxed));
    g_oled.setCursor(0, 55);
    g_oled.print("Check Serial / Reboot");
    return; // uiLoop() will call display()
  }

  // ── Normal HOME: Status Bar (y=0) ────────────────────────────────────────
  uint8_t masterVol = 85;
  audioSettingsLock();
  masterVol = g_settings.masterVolume;
  audioSettingsUnlock();

  g_oled.drawBitmap(0, 0, icon_bt, 8, 8, SSD1306_WHITE);
  g_oled.setCursor(10, 0);
  g_oled.print(g_btConnected.load(std::memory_order_acquire) ? "ON " : "-- ");

  g_oled.drawBitmap(36, 0, icon_vol, 8, 8, SSD1306_WHITE);
  g_oled.setCursor(46, 0);
  g_oled.print(masterVol);
  g_oled.print("%");

  // Brick-wall LIMIT indicator (top-right, inverted)
  if (g_isClipping) {
    g_oled.fillRect(86, 0, 42, 9, SSD1306_WHITE);
    g_oled.setTextColor(SSD1306_BLACK);
    g_oled.setCursor(88, 1);
    g_oled.print("[LIMIT]");
    g_oled.setTextColor(SSD1306_WHITE);
  }

  // ── Track Title Marquee (Option 1B: Row 1, y=10) ─────────────────────────
  bool connected = false;
  char trackTitle[64];
  btStatusSnapshot(connected, trackTitle, sizeof(trackTitle));

  g_oled.setCursor(0, 10);
  if (connected && trackTitle[0] != '\0') {
    g_oled.print(">");
    drawMarqueeText(7, 10, 121, trackTitle);
  } else {
    g_oled.print(connected ? "> Bluetooth Ready" : "> BT Disconnected");
  }

  // ── 4-Channel Matrix Labels + VU Meters (y=21, 31, 41, 51) ───────────────
  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    const int16_t y = 21 + (int16_t)(ch * 10);
    char label[20];
    formatChannelStatus(ch, label, sizeof(label));
    g_oled.setCursor(0, y);
    g_oled.print(label);
    drawVUMeter(ch, 74, y + 1, 52, 5);
  }
}

inline void drawMainMenu() {
  g_oled.clearDisplay();
  g_oled.drawRoundRect(0, 0, 128, 64, 2, SSD1306_WHITE);
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("MAIN MENU");

  const uint8_t VISIBLE_ROWS = 3;
  uint8_t topIdx = 0;
  if (g_menuIndex >= 2) {
    if (MAIN_MENU_COUNT > VISIBLE_ROWS) {
      topIdx = (g_menuIndex >= MAIN_MENU_COUNT - 1)
                   ? (MAIN_MENU_COUNT - VISIBLE_ROWS)
                   : (g_menuIndex - 1);
    } else {
      topIdx = 0;
    }
  }

  uint8_t masterVol = 85;
  audioSettingsLock();
  masterVol = g_settings.masterVolume;
  audioSettingsUnlock();

  for (uint8_t i = 0; i < VISIBLE_ROWS && (topIdx + i) < MAIN_MENU_COUNT; i++) {
    const uint8_t itemIdx = topIdx + i;
    const int16_t y = 14 + (int16_t)(i * 14);

    if (itemIdx == g_menuIndex) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = (itemIdx == g_menuIndex) ? SSD1306_BLACK : SSD1306_WHITE;
    g_oled.drawBitmap(2, y, MAIN_MENU_ICONS[itemIdx], 8, 8, color);

    g_oled.setCursor(14, y);
    // Always show Master Volume slider
    if (itemIdx == 0) {
      char vol[8];
      snprintf(vol, sizeof(vol), "%3u%%", masterVol);
      g_oled.print("Vol:");
      g_oled.print(vol);
      drawHorizontalSlider(66, y + 1, 54, 6, 0.0f, 100.0f, (float)masterVol,
                           color);
    } else {
      g_oled.print(MAIN_MENU_ITEMS[itemIdx]);
    }
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter("Click:Open  Hold:Home");
  drawVerticalScrollbar(g_menuIndex, MAIN_MENU_COUNT, VISIBLE_ROWS);
}

inline void drawPresetMenu() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("PRESETS");

  const uint8_t VISIBLE_ROWS = 3;
  uint8_t topIdx = 0;
  if (g_menuIndex >= VISIBLE_ROWS - 1) {
    if (5 > VISIBLE_ROWS) {
      topIdx = (g_menuIndex >= 5 - 1) ? (5 - VISIBLE_ROWS) : (g_menuIndex - 1);
    }
  }

  for (uint8_t i = 0; i < VISIBLE_ROWS && (topIdx + i) < 5; i++) {
    const uint8_t presetIdx = topIdx + i;
    const int16_t y = 14 + (int16_t)(i * 14);
    const bool isSelected = (presetIdx == g_menuIndex);
    const bool isActive = ((uint8_t)g_settings.preset == presetIdx);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    g_oled.drawBitmap(2, y, icon_presets, 8, 8, color);
    g_oled.setCursor(14, y);
    g_oled.print(presetValue(presetIdx));

    drawRadioButton(106, y + 1, isActive, color);
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter("Click:Apply Hold:Home");
  drawVerticalScrollbar(g_menuIndex, 5, VISIBLE_ROWS);
}

inline void drawMatrixMenu() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("OUTPUT MATRIX");

  const uint8_t VISIBLE_ROWS = 3;
  uint8_t topIdx = 0;
  if (g_menuIndex >= VISIBLE_ROWS - 1) {
    if (MATRIX_CHANNEL_COUNT > VISIBLE_ROWS) {
      topIdx = (g_menuIndex >= MATRIX_CHANNEL_COUNT - 1)
                   ? (MATRIX_CHANNEL_COUNT - VISIBLE_ROWS)
                   : (g_menuIndex - 1);
    }
  }

  for (uint8_t i = 0; i < VISIBLE_ROWS && (topIdx + i) < MATRIX_CHANNEL_COUNT;
       i++) {
    const uint8_t ch = topIdx + i;
    const int16_t y = 14 + (int16_t)(i * 14);
    const bool isSelected = (ch == g_menuIndex);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    char label[22];
    formatChannelStatus(ch, label, sizeof(label));

    g_oled.setCursor(4, y);
    g_oled.print("Ch ");
    g_oled.print(channelName(ch));
    g_oled.print(" ");
    g_oled.print(label + 1); // Skip channel letter

    const OutputChannelConfig &cfg = g_settings.channel[ch];
    drawToggleSwitch(104, y + 1, !cfg.mute, color);
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter("Click:Setup Hold:Home");
  drawVerticalScrollbar(g_menuIndex, MATRIX_CHANNEL_COUNT, VISIBLE_ROWS);
}

inline const char *channelFieldLabel(uint8_t field) {
  switch (field) {
  case 0:
    return "Source";
  case 1:
    return "Filter";
  case 2:
    return "Gain  ";
  case 3:
    return "Mute  ";
  default:
    return "Delay ";
  }
}

inline const char *channelFieldValue(uint8_t ch, uint8_t field) {
  static char buf[16];
  const OutputChannelConfig &cfg = g_settings.channel[ch];
  switch (field) {
  case 0:
    snprintf(buf, sizeof(buf), "%s", inputSourceName(cfg.source));
    break;
  case 1:
    snprintf(buf, sizeof(buf), "%s", filterBandName(cfg.band));
    break;
  case 2:
    snprintf(buf, sizeof(buf), "%+.1f dB", cfg.gainDb);
    break;
  case 3:
    snprintf(buf, sizeof(buf), "%s", cfg.mute ? "MUTED" : "UNMUTE");
    break;
  default:
    snprintf(buf, sizeof(buf), "%.1f ms", cfg.delayMs);
    break;
  }
  return buf;
}

inline void drawChannelSetup() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  char title[32];
  snprintf(title, sizeof(title), "CH %c %s", channelName(g_selectedChannel),
           g_editing ? "[EDIT]" : "SETUP");
  drawSandwichHeader(title);

  const uint8_t VISIBLE_ROWS = 3;
  uint8_t topIdx = 0;
  if (g_menuIndex >= VISIBLE_ROWS - 1) {
    if (CHANNEL_FIELD_COUNT > VISIBLE_ROWS) {
      topIdx = (g_menuIndex >= CHANNEL_FIELD_COUNT - 1)
                   ? (CHANNEL_FIELD_COUNT - VISIBLE_ROWS)
                   : (g_menuIndex - 1);
    }
  }

  const OutputChannelConfig &cfg = g_settings.channel[g_selectedChannel];

  for (uint8_t i = 0; i < VISIBLE_ROWS && (topIdx + i) < CHANNEL_FIELD_COUNT;
       i++) {
    const uint8_t field = topIdx + i;
    const int16_t y = 14 + (int16_t)(i * 14);
    const bool isSelected = (field == g_menuIndex);
    const char *val = channelFieldValue(g_selectedChannel, field);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    g_oled.setCursor(4, y);
    g_oled.print(channelFieldLabel(field));
    g_oled.print(":");

    if (field == 2) { // Gain: Text + Mini Slider
      char gainStr[10];
      snprintf(gainStr, sizeof(gainStr), "%+.1fdB", cfg.gainDb);
      g_oled.setCursor(50, y);
      g_oled.print(gainStr);
      drawHorizontalSlider(88, y + 2, 32, 5, -12.0f, 12.0f, cfg.gainDb, color);
    } else if (field == 3) { // Mute: Toggle Switch
      drawToggleSwitch(104, y + 1, cfg.mute, color);
    } else { // Source, Filter, Delay
      drawRightAlignedValue(y, val, (g_editing && isSelected));
    }
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter(g_editing ? "Click:Ok 2x:Cancel" : "Click:Edit 2x:Back");
  drawVerticalScrollbar(g_menuIndex, CHANNEL_FIELD_COUNT, VISIBLE_ROWS);
}

inline void drawPEQBandSelect() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("5-BAND PEQ SETUP");

  const uint8_t VISIBLE_ROWS = 3;
  uint8_t topIdx = 0;
  if (g_menuIndex >= VISIBLE_ROWS - 1) {
    if (PEQ_BAND_COUNT > VISIBLE_ROWS) {
      topIdx = (g_menuIndex >= PEQ_BAND_COUNT - 1)
                   ? (PEQ_BAND_COUNT - VISIBLE_ROWS)
                   : (g_menuIndex - 1);
    }
  }

  for (uint8_t i = 0; i < VISIBLE_ROWS && (topIdx + i) < PEQ_BAND_COUNT; i++) {
    const uint8_t bandIdx = topIdx + i;
    const PEQBandConfig &b = g_settings.peq[bandIdx];
    const int16_t y = 14 + (int16_t)(i * 14);
    const bool isSelected = (bandIdx == g_menuIndex);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    g_oled.setCursor(4, y);
    g_oled.print("B");
    g_oled.print(bandIdx + 1);
    g_oled.print(" ");

    if (b.freq >= 1000.0f) {
      g_oled.printf("%4.1fk ", b.freq * 0.001f);
    } else {
      g_oled.printf("%3.0fHz ", b.freq);
    }
    g_oled.printf("%+4.1fdB %s", b.gainDb, peqTypeShort(b.type));
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawVerticalScrollbar(g_menuIndex, PEQ_BAND_COUNT, VISIBLE_ROWS);

  // Mini Equalizer Bar Chart (y=37 to y=52)
  g_oled.drawFastHLine(12, 45, 104, SSD1306_WHITE);

  for (uint8_t i = 0; i < PEQ_BAND_COUNT; i++) {
    const float db = clampf(g_settings.peq[i].gainDb, -12.0f, 12.0f);
    const int8_t barH = (int8_t)(db * (7.0f / 12.0f));
    const int16_t bx = 18 + (int16_t)(i * 20);
    if (barH > 0) {
      g_oled.fillRect(bx, 45 - barH, 8, barH, SSD1306_WHITE);
    } else if (barH < 0) {
      g_oled.fillRect(bx, 45, 8, -barH, SSD1306_WHITE);
    } else {
      g_oled.fillRect(bx, 44, 8, 2, SSD1306_WHITE);
    }
  }

  drawSandwichFooter("Click:Edit  Hold:Home");
}

inline const char *peqFieldLabel(uint8_t field) {
  switch (field) {
  case 0:
    return "Type ";
  case 1:
    return "Freq ";
  case 2:
    return "Gain ";
  default:
    return "Q-Fac";
  }
}

inline const char *peqFieldValue(uint8_t band, uint8_t field) {
  static char buf[16];
  const PEQBandConfig &b = g_settings.peq[band];
  switch (field) {
  case 0:
    snprintf(buf, sizeof(buf), "%s", peqTypeName(b.type));
    break;
  case 1:
    if (b.freq >= 1000.0f)
      snprintf(buf, sizeof(buf), "%.2f kHz", b.freq * 0.001f);
    else
      snprintf(buf, sizeof(buf), "%.0f Hz", b.freq);
    break;
  case 2:
    snprintf(buf, sizeof(buf), "%+.1f dB", b.gainDb);
    break;
  default:
    snprintf(buf, sizeof(buf), "%.2f", b.q);
    break;
  }
  return buf;
}

inline void drawPEQBandEdit() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  char title[32];
  snprintf(title, sizeof(title), "PEQ B%u %s", g_selectedPeqBand + 1,
           g_editing ? "[EDIT]" : "SETUP");
  drawSandwichHeader(title);

  const PEQBandConfig &b = g_settings.peq[g_selectedPeqBand];

  for (uint8_t field = 0; field < PEQ_FIELD_COUNT; field++) {
    const int16_t y = 14 + (int16_t)(field * 10);
    const bool isSelected = (field == g_menuIndex);
    const char *val = peqFieldValue(g_selectedPeqBand, field);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    g_oled.setCursor(4, y);
    g_oled.print(peqFieldLabel(field));
    g_oled.print(":");

    if (field == 2) { // Gain: Mini Slider
      char gainStr[10];
      snprintf(gainStr, sizeof(gainStr), "%+.1fdB", b.gainDb);
      g_oled.setCursor(46, y);
      g_oled.print(gainStr);
      drawHorizontalSlider(84, y + 2, 36, 5, -12.0f, 12.0f, b.gainDb, color);
    } else {
      drawRightAlignedValue(y, val, (g_editing && isSelected));
    }
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter(g_editing ? "Click:Ok 2x:Cancel" : "Click:Edit 2x:Back");
}

inline void drawDelayMenu() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("TIME ALIGNMENT");

  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    const int16_t y = 14 + (int16_t)(ch * 10);
    const bool isSelected = (ch == g_menuIndex);
    const float ms = g_settings.channel[ch].delayMs;

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    char val[16];
    snprintf(val, sizeof(val), "%4.1fms", ms);

    g_oled.setCursor(4, y);
    g_oled.print("CH ");
    g_oled.print(channelName(ch));
    g_oled.print(":");

    if (g_editing && isSelected) {
      drawInvertedValue(38, y, val);
    } else {
      g_oled.setCursor(38, y);
      g_oled.print(val);
    }

    drawHorizontalSlider(78, y + 2, 40, 5, 0.0f, 20.0f, ms, color);
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter(g_editing ? "Rotate:Step Click:Ok" : "Click:Set 2x:Back");
}

inline void drawPhaseMenu() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("PHASE POLARITY");

  for (uint8_t ch = 0; ch < MATRIX_CHANNEL_COUNT; ch++) {
    const int16_t y = 14 + (int16_t)(ch * 10);
    const bool isSelected = (ch == g_menuIndex);
    const bool inv = g_settings.channel[ch].phaseInvert;

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    uint16_t color = isSelected ? SSD1306_BLACK : SSD1306_WHITE;

    g_oled.setCursor(4, y);
    g_oled.print("CH ");
    g_oled.print(channelName(ch));
    g_oled.print(": ");
    g_oled.print(inv ? "INVERT (180)" : "NORMAL (0)");

    drawToggleSwitch(104, y + 1, inv, color);
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter("Click:Toggle 2x:Back");
}

inline const char *crossoverFieldLabel(uint8_t field) {
  switch (field) {
  case 0:
    return "Sub LPF ";
  case 1:
    return "Mid HPF ";
  case 2:
    return "Mid LPF ";
  default:
    return "High HPF";
  }
}

inline const char *crossoverFieldValue(uint8_t field) {
  static char buf[16];
  float f = 0.0f;
  switch (field) {
  case 0:
    f = g_settings.subLpfHz;
    break;
  case 1:
    f = g_settings.midLowHz;
    break;
  case 2:
    f = g_settings.midHighHz;
    break;
  default:
    f = g_settings.highHpfHz;
    break;
  }
  if (f >= 1000.0f)
    snprintf(buf, sizeof(buf), "%.2f kHz", f * 0.001f);
  else
    snprintf(buf, sizeof(buf), "%.0f Hz", f);
  return buf;
}

inline void drawCrossoverMenu() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("LR24 CROSSOVER");

  for (uint8_t field = 0; field < CROSSOVER_FIELD_COUNT; field++) {
    const int16_t y = 14 + (int16_t)(field * 10);
    const bool isSelected = (field == g_menuIndex);
    const char *val = crossoverFieldValue(field);

    if (isSelected) {
      g_oled.fillRoundRect(0, y - 2, 122, 12, 2, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
    } else {
      g_oled.setTextColor(SSD1306_WHITE);
    }

    g_oled.setCursor(4, y);
    g_oled.print(crossoverFieldLabel(field));
    g_oled.print(":");

    drawRightAlignedValue(y, val, (g_editing && isSelected));
  }
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichFooter(g_editing ? "Click:Ok 2x:Cancel" : "Click:Edit 2x:Back");
}

inline void drawBTSelect() {
  bool connected = false;
  char trackTitle[64];
  btStatusSnapshot(connected, trackTitle, sizeof(trackTitle));

  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("BLUETOOTH STATUS");

  g_oled.setCursor(4, 14);
  g_oled.print("State: ");
  g_oled.print(connected ? "Connected" : "Pairing");
  drawToggleSwitch(104, 14, connected, SSD1306_WHITE);

  g_oled.setCursor(4, 26);
  g_oled.print("Name : ");
  g_oled.print(BT_DEVICE_NAME);

  g_oled.setCursor(4, 38);
  g_oled.print("Track: ");
  drawMarqueeText(46, 38, 76, trackTitle[0] ? trackTitle : "None");

  drawSandwichFooter("Click:Back Hold:Home");
}

inline void drawDiagnostics() {
  g_oled.clearDisplay();
  g_oled.setTextSize(1);
  g_oled.setTextColor(SSD1306_WHITE);

  drawSandwichHeader("SYSTEM DIAGNOSTICS");

  g_oled.setCursor(4, 14);
  g_oled.printf("DSP CPU : %3.0f%%",
                g_dspCpuLoadPct.load(std::memory_order_relaxed));

  g_oled.setCursor(4, 24);
  g_oled.printf("QUEUE   : %u/%u (H:%u)",
                g_queueCurrentLevel.load(std::memory_order_relaxed),
                AUDIO_QUEUE_DEPTH,
                g_queueHighWater.load(std::memory_order_relaxed));

  g_oled.setCursor(4, 34);
  g_oled.printf("DROP:%u UF:%u", g_audioDrops.load(std::memory_order_relaxed),
                g_queueUnderflows.load(std::memory_order_relaxed));

  g_oled.setCursor(4, 44);
  g_oled.printf("I2S:%u  PSRAM:%s", g_i2sErrors.load(std::memory_order_relaxed),
                g_psramActive ? "OK" : "NO");

  drawSandwichFooter("Click:Back Hold:Home");
}

// =============================================================================
// uiLoop() — call every iteration of loop()
// =============================================================================
inline void uiLoop() {
  static constexpr uint32_t HOME_INTERVAL_MS = 33; // ~30 FPS
  static constexpr uint32_t MENU_INTERVAL_MS =
      150;                                        // ~6.7 FPS (event-driven max)
  static constexpr uint32_t MIN_INTERVAL_MS = 33; // Hard throttle floor

  const uint32_t now = millis();
  const uint32_t elapsed = now - g_lastUIRefresh;

  // Hard throttle: never push I2C faster than ~30 FPS
  if (elapsed < MIN_INTERVAL_MS)
    return;

  const bool isHome = (g_uiScreen == UIScreen::HOME);
  const bool periodic =
      elapsed >= (isHome ? HOME_INTERVAL_MS : MENU_INTERVAL_MS);

  // Auto-return to HOME screen after 10s of inactivity in any menu/submenu
  if (g_uiScreen != UIScreen::HOME) {
    if (now - g_lastUserActivity >= 10000) {
      if (g_inBTSelect)
        btExitSelectMode();
      g_uiScreen = UIScreen::HOME;
      g_inMenu = false;
      g_inBTSelect = false;
      g_editing = false;
      g_menuIndex = 0;
      g_uiDirty = true;
      g_lastUserActivity = now;
    }
  }

  // Skip render if nothing changed and periodic refresh not due
  if (!g_uiDirty.load(std::memory_order_acquire) && !periodic)
    return;

  // ── Dispatch to the appropriate screen draw function ─────────────────────
  switch (g_uiScreen) {
  case UIScreen::MAIN_MENU:
    drawMainMenu();
    break;
  case UIScreen::PRESET_MENU:
    drawPresetMenu();
    break;
  case UIScreen::MATRIX_MENU:
    drawMatrixMenu();
    break;
  case UIScreen::CHANNEL_SETUP:
    drawChannelSetup();
    break;
  case UIScreen::PEQ_BAND_SELECT:
    drawPEQBandSelect();
    break;
  case UIScreen::PEQ_BAND_EDIT:
    drawPEQBandEdit();
    break;
  case UIScreen::DELAY_MENU:
    drawDelayMenu();
    break;
  case UIScreen::PHASE_MENU:
    drawPhaseMenu();
    break;
  case UIScreen::CROSSOVER_MENU:
    drawCrossoverMenu();
    break;
  case UIScreen::BT_SELECT:
    drawBTSelect();
    break;
  case UIScreen::DIAGNOSTICS:
    drawDiagnostics();
    break;
  default:
    drawHome();
    break;
  }

  // ── Toast & NVS Notification Overlay ─────────────────────────────────────
  if (g_nvsSaveNotify) {
    uiShowToast("NVS SAVED");
    g_nvsSaveNotify = false;
  }

  if (g_toastActive) {
    if (now < g_toastEndTime) {
      int16_t x1, y1;
      uint16_t w, h;
      g_oled.getTextBounds(g_toastMessage, 0, 0, &x1, &y1, &w, &h);
      int16_t boxW = w + 12;
      int16_t boxX = (128 - boxW) / 2;
      g_oled.fillRoundRect(boxX, 22, boxW, 14, 3, SSD1306_WHITE);
      g_oled.setTextColor(SSD1306_BLACK);
      g_oled.setCursor(boxX + 6, 25);
      g_oled.print(g_toastMessage);
      g_oled.setTextColor(SSD1306_WHITE);
    } else {
      g_toastActive = false;
      g_uiDirty = true; // One more frame to clear the toast
    }
  }

  // ── Single I2C display push (covers both screen + overlay) ───────────────
  g_oled.display();

  g_uiDirty = false;
  g_lastUIRefresh = now;
}

#endif // ESP32_AUDIO_OLED_UI_H
