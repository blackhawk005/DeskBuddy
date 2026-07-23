// ============================================================================
// config.h  —  Tiny ESP DeskBuddy
// Board: Waveshare ESP32-C6-Touch-LCD-1.47 (JD9853 LCD / AXS5106L touch / QMI8658 IMU)
// ============================================================================
#pragma once

// ---- Display (JD9853 driven as ST7789-compatible via GFX_Library_for_Arduino)
// CORRECTED 2026-07-22 against Waveshare's official pin table at
// https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47
//
// The previous values (DC=45, CS=21, SCK=38, MOSI=39, RST=47) were wrong and were
// mislabelled "confirmed" — they are ESP32-S3 pin numbers copied from a different
// board's demo. The ESP32-C6 only has GPIO0..GPIO30, so 38/39/45/47 cannot exist
// on this hardware. Symptom of the old values: dead black screen, no SPI traffic.
#define LCD_DC    15
#define LCD_CS    14
#define LCD_SCK   1         // NOTE: shared with the TF card slot's SCK
#define LCD_MOSI  2         // NOTE: shared with the TF card slot's MOSI/CMD
#define LCD_RST   22
// Native panel geometry — do NOT swap these for landscape. They describe the
// physical glass and are what the Arduino_ST7789 constructor expects.
#define LCD_W     172
#define LCD_H     320
#define LCD_COL_OFFSET 34   // JD9853 panel column offset used by the factory demo
#define LCD_ROW_OFFSET 0

// ---- Orientation
// 0 = portrait (172x320), 1 = landscape (320x172), 2 = portrait flipped,
// 3 = landscape flipped. Applied via gfx->setRotation() after the panel init.
// Flip 1 <-> 3 if the display comes out upside down for how the case sits.
#define LCD_ROTATION 1

// Logical screen size AFTER rotation — use these for all layout maths.
// Odd rotations swap width and height.
#if (LCD_ROTATION % 2) == 0
  #define SCREEN_W LCD_W
  #define SCREEN_H LCD_H
#else
  #define SCREEN_W LCD_H
  #define SCREEN_H LCD_W
#endif

// ---- Backlight
// GPIO23 per the Waveshare pin table (this one the original guess got right).
// Minor residual doubt: some write-ups for the NON-touch ESP32-C6-LCD-1.47 list
// the backlight on GPIO22, which is RST on this touch variant. diag/diag.ino
// Phase C sweeps both and settles it. Set to -1 to disable PWM control.
#define LCD_BL    23

// ---- I2C bus (shared: AXS5106L touch + QMI8658 IMU)
// CORRECTED 2026-07-22 — was 6/7 (a guess); the real shared bus is 18/19.
// GPIO5/GPIO6 are the QMI8658's INT1/INT2 lines, which is likely where the
// original 6/7 guess came from.
#define I2C_SDA   18
#define I2C_SCL   19
#define I2C_FREQ  400000

// CORRECTED 2026-07-22 — these are broken out on this board, previously -1.
#define TOUCH_INT 21
#define TOUCH_RST 20
#define QMI8658_ADDR 0x6B     // 0x6B or 0x6A depending on ADDR strap; Waveshare
                              // lists 0x6B among the onboard addresses (with
                              // 0x51 and 0x7E) — diag Phase A confirms.

// ---- Battery ADC
// GPIO0 per the Waveshare pin table (original guess was correct).
// The divider ratio below is still UNVERIFIED — calibrate against a meter once a
// battery is actually installed.
#define BAT_ADC_PIN 0
#define BAT_ADC_DIVIDER 3.0f  // Waveshare uses a divider; calibrate against a meter

// ---- UI timing
#define FACE_FPS          30
#define IDLE_BLINK_MS_MIN 2500
#define IDLE_BLINK_MS_MAX 6000
#define NET_REFRESH_MS    (15UL * 60UL * 1000UL)   // weather/quote/github every 15 min
#define QUOTE_ROTATE_MS   (60UL * 1000UL)          // rotate cached quote each minute

// ---- Location for Open-Meteo (San Jose, CA default — change to yours)
#define WX_LAT  37.3382
#define WX_LON -121.8863
#define WX_TZ   "America/Los_Angeles"

// ---- Greeting shown under the face (replaces the old mood name)
#define FACE_GREETING "Hi Pannu"

// ---- GitHub
// The account whose public stats show on the GitHub page (the recipient, Pannu).
// This is NOT the dev/push account (blackhawk005) — see .env dev_gh.
#define GH_USER "PannagaRao"   // public stats only; no token needed
