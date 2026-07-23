// ============================================================================
// diag_touch2.ino  —  raw register dump for the AXS5106L
//
// Two drivers disagree about this chip, so stop trusting either and just look
// at the bytes:
//   Waveshare vendor : addr 0x63, data reg 0x01, count at data[1]
//   schematic.io     : addr 0x3B, data reg 0x00, count at buf[0]
//
// Our bus scan found 0x63 and 0x6B only (0x6B is the IMU), so 0x3B is probed
// here purely to prove it absent. This sketch dumps registers 0x00..0x0F and
// flags any byte that CHANGES between samples — a changing byte while you press
// the glass is the touch data, wherever it happens to live.
//
// TOUCH AND HOLD, then drag slowly. Changing bytes are marked with '*'.
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include "../DeskBuddy/config.h"

#define DUMP_LEN 16

static uint8_t prev[DUMP_LEN] = {0};
static bool havePrev = false;

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Read `len` bytes starting at `reg`. Returns false on any I2C error.
static bool dumpFrom(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  delayMicroseconds(300);
  if (Wire.requestFrom(addr, len, (uint8_t)true) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n\n=== AXS5106L RAW REGISTER DUMP ===");
  Wire.begin(I2C_SDA, I2C_SCL, 100000);   // 100kHz, more forgiving

  // Vendor reset sequence.
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(200);
  digitalWrite(TOUCH_RST, HIGH); delay(300);

  Serial.printf("probe 0x63 (Waveshare): %s\n", probe(0x63) ? "ACK" : "no reply");
  Serial.printf("probe 0x3B (schematic.io): %s\n", probe(0x3B) ? "ACK" : "no reply");

  pinMode(TOUCH_INT, INPUT_PULLUP);
  Serial.println("\nDumping regs 0x00..0x0F from addr 0x63 every 250ms.");
  Serial.println("'*' marks a byte that changed since the previous sample.");
  Serial.println(">>> PRESS AND HOLD THE GLASS, THEN DRAG SLOWLY <<<\n");
}

void loop() {
  uint8_t buf[DUMP_LEN] = {0};

  // Read from register 0x00 so the dump covers BOTH candidate layouts at once:
  // schematic.io's count byte (reg 0x00) and the vendor's (reg 0x02).
  if (!dumpFrom(0x63, 0x00, buf, DUMP_LEN)) {
    Serial.println("  I2C read FAILED");
    delay(500);
    return;
  }

  bool changed = false;
  for (uint8_t i = 0; i < DUMP_LEN; i++) {
    if (havePrev && buf[i] != prev[i]) { changed = true; break; }
  }

  // Print every sample for the first while, then only when something moves —
  // keeps the log readable but never hides the interesting moment.
  static uint32_t samples = 0;
  samples++;
  if (!havePrev || changed || samples < 8 || (samples % 20) == 0) {
    Serial.printf("INT=%d  ", digitalRead(TOUCH_INT));
    for (uint8_t i = 0; i < DUMP_LEN; i++) {
      bool diff = havePrev && buf[i] != prev[i];
      Serial.printf("%02d:%02X%c ", i, buf[i], diff ? '*' : ' ');
    }
    if (changed) Serial.print("  <<< CHANGED");
    Serial.println();
  }

  memcpy(prev, buf, DUMP_LEN);
  havePrev = true;
  delay(250);
}
