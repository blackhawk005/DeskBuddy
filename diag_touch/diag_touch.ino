// ============================================================================
// diag_touch.ino  —  is the AXS5106L reporting, and is its INT line firing?
//
// esp_lcd_touch_axs5106l gates EVERY read behind an interrupt flag:
//   bsp_touch_read() { if (g_touch_int_flag == false) return; ... }
// So if the INT wire never fires, touches are invisible no matter how healthy
// the controller is. That flag is a static global in the .cpp with no accessor,
// so this sketch talks to the chip directly over I2C and watches the INT pin
// itself, separating the two failure modes:
//
//   INT count stays 0 but polling shows touches -> INT pin/config is wrong
//   polling shows nothing at all                -> controller/reset problem
//   both work                                   -> bug is in DeskBuddy's logic
//
// Touch the screen continuously while this runs.
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include "../DeskBuddy/config.h"

#define AXS_ADDR      0x63
#define AXS_DATA_REG  0x01
#define AXS_ID_REG    0x08

volatile uint32_t intCount = 0;
static void IRAM_ATTR onTouchInt() { intCount++; }

static bool i2cRead(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(AXS_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom((uint8_t)AXS_ADDR, len);
  if (Wire.available() != len) return false;
  Wire.readBytes(buf, len);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n\n=== TOUCH DIAGNOSTIC ===");
  Serial.printf("I2C SDA=%d SCL=%d   TOUCH_RST=%d  TOUCH_INT=%d\n",
                I2C_SDA, I2C_SCL, TOUCH_RST, TOUCH_INT);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  // Reset pulse, same timing as the vendor driver.
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(200);
  digitalWrite(TOUCH_RST, HIGH); delay(300);

  // Does it ACK at all?
  Wire.beginTransmission(AXS_ADDR);
  bool ack = (Wire.endTransmission() == 0);
  Serial.printf("controller ACKs at 0x63: %s\n", ack ? "YES" : "NO");

  uint8_t id[3] = {0};
  if (i2cRead(AXS_ID_REG, id, 3)) {
    Serial.printf("ID register: %02X %02X %02X\n", id[0], id[1], id[2]);
  } else {
    Serial.println("ID register read FAILED");
  }

  // Watch INT as a plain input first so we can see its resting level.
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Serial.printf("INT pin resting level (expect HIGH=1 when idle): %d\n",
                digitalRead(TOUCH_INT));

  attachInterrupt(digitalPinToInterrupt(TOUCH_INT), onTouchInt, FALLING);

  Serial.println("\n--- TOUCH THE SCREEN NOW (30 seconds) ---");
  Serial.println("cols: t=seconds  INTs=interrupt count  INT=pin level  n=touch_num  x,y");
}

void loop() {
  static uint32_t t0 = millis();
  static uint32_t lastPrint = 0;
  static int reports = 0;

  uint32_t el = millis() - t0;

  // Runs forever — no deadline to race against. Every 5s it re-prints whether
  // the controller is alive, so the key facts can never scroll past unseen.
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    Wire.beginTransmission(AXS_ADDR);
    bool ack = (Wire.endTransmission() == 0);
    uint8_t id[3] = {0};
    bool idok = i2cRead(AXS_ID_REG, id, 3);
    Serial.printf("\n[%2lus] ACK@0x63=%s  ID=%02X%02X%02X%s  INTs=%lu  INTpin=%d  reports=%d\n",
                  (unsigned long)(el / 1000), ack ? "YES" : "NO",
                  id[0], id[1], id[2], idok ? "" : "(read failed)",
                  (unsigned long)intCount, digitalRead(TOUCH_INT), reports);
    Serial.println("      >>> TOUCH AND SWIPE THE SCREEN NOW <<<");
    lastStatus = millis();
  }

  // Poll directly, ignoring the interrupt entirely.
  uint8_t d[14] = {0};
  if (i2cRead(AXS_DATA_REG, d, 14)) {
    uint8_t n = d[1];
    if (n > 0 && n <= 5) {
      uint16_t x = (((uint16_t)(d[2] & 0x0f)) << 8) | d[3];
      uint16_t y = (((uint16_t)(d[4] & 0x0f)) << 8) | d[5];
      reports++;
      if (millis() - lastPrint > 200) {
        Serial.printf("  t=%2lus  INTs=%-4lu INT=%d  n=%u  x=%u y=%u\n",
                      (unsigned long)(el / 1000), (unsigned long)intCount,
                      digitalRead(TOUCH_INT), n, x, y);
        lastPrint = millis();
      }
    }
  }

  // Heartbeat so silence is distinguishable from a hang.
  if (millis() - lastPrint > 3000) {
    Serial.printf("  t=%2lus  INTs=%-4lu INT=%d  (no touch detected)\n",
                  (unsigned long)(el / 1000), (unsigned long)intCount,
                  digitalRead(TOUCH_INT));
    lastPrint = millis();
  }
  delay(20);
}
