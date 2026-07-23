// ============================================================================
// touch.h  —  polling AXS5106L reader (replaces esp_lcd_touch_axs5106l)
//
// WHY THIS EXISTS
// Waveshare's esp_lcd_touch_axs5106l gates every read behind an interrupt:
//     void bsp_touch_read(void) { ... if (g_touch_int_flag == false) return; ... }
// On this board the INT line (GPIO21) does not assert. Measured 2026-07-22 with
// diag_touch2: 121 consecutive samples contained live touch data while INT read
// HIGH the entire time. So the vendor driver discards every touch before it ever
// reads the chip, and no amount of gesture-logic tuning can fix that.
//
// The controller itself is healthy. Verified register map, reading from 0x01:
//     reg 0x02        touch count (0 = up, 1 = one finger down)
//     reg 0x03..0x04  X, 12-bit, big-endian, high nibble masked
//     reg 0x05..0x06  Y, 12-bit, big-endian, high nibble masked
// Raw coordinates arrive in NATIVE PORTRAIT space: X 0..171, Y 0..319,
// regardless of the display rotation. This file maps them to screen space.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#define AXS5106L_I2C_ADDR 0x63
#define AXS5106L_DATA_REG 0x01

// If a finger lands mirrored along an axis, flip the matching value to 1.
// Rotation 1 was derived from the vendor driver's own case-1 mapping (a plain
// X/Y swap); these switches make correcting it a one-character edit.
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0

inline void touchBegin() {
  // Wire.begin() is the caller's responsibility (the bus is shared with the IMU).
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(200);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);
  // INT is deliberately left unconfigured: it never asserts on this board and
  // nothing here depends on it.
}

// Returns true when a finger is down, writing screen-space coords to x/y.
inline bool touchPoll(uint16_t* x, uint16_t* y) {
  uint8_t d[8] = {0};

  Wire.beginTransmission(AXS5106L_I2C_ADDR);
  Wire.write(AXS5106L_DATA_REG);
  if (Wire.endTransmission(true) != 0) return false;
  delayMicroseconds(300);
  if (Wire.requestFrom((uint8_t)AXS5106L_I2C_ADDR, (uint8_t)8, (uint8_t)true) != 8) return false;
  for (uint8_t i = 0; i < 8; i++) d[i] = Wire.read();

  uint8_t count = d[1];
  if (count == 0 || count > 5) return false;

  uint16_t rawX = ((uint16_t)(d[2] & 0x0F) << 8) | d[3];   // 0..LCD_W-1
  uint16_t rawY = ((uint16_t)(d[4] & 0x0F) << 8) | d[5];   // 0..LCD_H-1

  // Guard against the all-ones idle pattern the chip emits between frames.
  if (rawX >= LCD_W || rawY >= LCD_H) return false;

  uint16_t sx, sy;
#if (LCD_ROTATION == 1)
  sx = rawY;                 // native Y (0..319) runs along the wide axis
  sy = rawX;                 // native X (0..171) runs down the short axis
#elif (LCD_ROTATION == 3)
  sx = (LCD_H - 1) - rawY;
  sy = (LCD_W - 1) - rawX;
#elif (LCD_ROTATION == 2)
  sx = (LCD_W - 1) - rawX;
  sy = (LCD_H - 1) - rawY;
#else                        // rotation 0, native portrait
  sx = rawX;
  sy = rawY;
#endif

#if TOUCH_FLIP_X
  sx = (SCREEN_W - 1) - sx;
#endif
#if TOUCH_FLIP_Y
  sy = (SCREEN_H - 1) - sy;
#endif

  *x = constrain(sx, 0, SCREEN_W - 1);
  *y = constrain(sy, 0, SCREEN_H - 1);
  return true;
}
