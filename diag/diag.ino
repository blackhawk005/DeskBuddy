// ============================================================================
// diag.ino  —  DeskBuddy hardware pin discovery
// Board: Waveshare ESP32-C6-Touch-LCD-1.47
//
// Purpose: resolve the CONFIRM-flagged pins in config.h against the physical
// board instead of guessing. Flash THIS before DeskBuddy.ino.
//
//   Phase A  I2C brute-force scan  -> proves I2C_SDA / I2C_SCL, and reports the
//                                     real QMI8658 address (0x6B vs 0x6A strap)
//                                     plus the AXS5106L touch address.
//   Phase B  ADC survey            -> proves BAT_ADC_PIN (which pin tracks VBUS/VBAT)
//   Phase C  Backlight sweep       -> proves LCD_BL (watch the screen, note the pin)
//
// Build/flash (CDCOnBoot=cdc is REQUIRED or Serial output goes nowhere):
//   arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc \
//     --upload -p /dev/cu.usbmodem14101 diag
//   arduino-cli monitor -p /dev/cu.usbmodem14101 -c baudrate=115200
// ============================================================================
#include <Arduino.h>
#include <Wire.h>

// ESP32-C6 has GPIO0..GPIO30. Excluded from probing:
//   12, 13  -> native USB D-/D+ (touching these kills the serial link)
//   24..30  -> SPI flash on the ESP32-C6-WROOM-1 module (driving these crashes)
static const uint8_t kSafePins[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 18, 19, 20, 21, 22, 23
};
static const size_t kNumSafe = sizeof(kSafePins) / sizeof(kSafePins[0]);

// ADC1 channels on the C6 are GPIO0..GPIO6.
static const uint8_t kAdcPins[] = { 0, 1, 2, 3, 4, 5, 6 };
static const size_t kNumAdc = sizeof(kAdcPins) / sizeof(kAdcPins[0]);

static const char* deviceName(uint8_t addr) {
  switch (addr) {
    case 0x6A: case 0x6B: return "QMI8658 IMU";
    case 0x63: case 0x3B: return "AXS5106L touch (likely)";
    default:              return "unknown";
  }
}

// Scan one candidate SDA/SCL pair. Returns number of devices answering.
static int scanPair(uint8_t sda, uint8_t scl) {
  Wire.end();
  if (!Wire.begin(sda, scl, 100000)) return 0;   // 100kHz: more forgiving than 400k
  Wire.setTimeOut(10);

  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found == 0) Serial.printf("\n  SDA=%2u SCL=%2u :", sda, scl);
      Serial.printf(" 0x%02X(%s)", addr, deviceName(addr));
      found++;
      if (found > 6) break;   // a "responds to everything" bus is a false positive
    }
  }
  return found;
}

static void phaseA_i2c() {
  Serial.println("\n=== PHASE A: I2C scan ===");

  // First: the documented bus. Waveshare's pin table says SDA=18, SCL=19, and
  // lists 0x51, 0x6B and 0x7E as the onboard addresses. Expect 0x6B (QMI8658 IMU)
  // and the AXS5106L touch controller to answer here.
  Serial.println("A1. Testing the DOCUMENTED bus (SDA=18, SCL=19)...");
  int documented = scanPair(18, 19);
  if (documented > 0) {
    Serial.printf("\n  -> OK: %d device(s) on 18/19. config.h I2C pins are correct.\n", documented);
    Wire.end();
    return;   // no need to brute force
  }

  // Fallback only: the documented pins produced nothing, so sweep everything.
  Serial.println("\n  -> NOTHING on 18/19. Falling back to brute-force sweep.");
  Serial.println("     Real buses report 1-3 devices. A pair reporting >6 is a");
  Serial.println("     floating-pin false positive, ignore it.");

  int hits = 0;
  for (size_t i = 0; i < kNumSafe; i++) {
    for (size_t j = 0; j < kNumSafe; j++) {
      if (i == j) continue;
      if (scanPair(kSafePins[i], kSafePins[j]) > 0) hits++;
    }
  }
  Wire.end();

  if (hits == 0) {
    Serial.println("\n  NOTHING FOUND on any pin pair.");
    Serial.println("  -> I2C peripherals may need a power-enable pin asserted first,");
    Serial.println("     or TOUCH_RST (GPIO20) must be released high before the touch");
    Serial.println("     controller will answer. Try holding GPIO20 HIGH and re-running.");
  } else {
    Serial.printf("\n  %d candidate pair(s) above. The one showing 0x6A/0x6B is your bus.\n", hits);
  }
}

static void phaseB_adc() {
  Serial.println("\n=== PHASE B: ADC survey ===");
  Serial.println("Board is on USB with no battery, so a divider pin should read a");
  Serial.println("steady ~1500-2200 mV (VBUS/2 or /3). Pins reading ~0 or noisy-random");
  Serial.println("are floating and are NOT the battery pin.");

  for (int pass = 0; pass < 3; pass++) {
    Serial.printf("\n  pass %d:", pass + 1);
    for (size_t i = 0; i < kNumAdc; i++) {
      Serial.printf("  GPIO%u=%4dmV", kAdcPins[i], analogReadMilliVolts(kAdcPins[i]));
    }
    delay(400);
  }
  Serial.println("\n  -> A pin steady across all 3 passes is the real BAT_ADC_PIN.");
}

static void phaseC_backlight() {
  Serial.println("\n=== PHASE C: backlight sweep ===");
  Serial.println("WATCH THE SCREEN. Each pin is driven LOW 2s then HIGH 2s.");
  Serial.println("Note the GPIO number printed when the backlight visibly changes.");
  Serial.println("Starting in 3s...");
  delay(3000);

  // The two real candidates first, slowly: GPIO23 per the Waveshare table for the
  // touch variant, GPIO22 per some write-ups of the non-touch variant. One of
  // these two is almost certainly it — watch these closely.
  const uint8_t primary[] = { 23, 22 };
  for (uint8_t k = 0; k < 2; k++) {
    uint8_t p = primary[k];
    Serial.printf("\n  >>> PRIMARY CANDIDATE GPIO%u — watch now <<<\n", p);
    Serial.flush();
    pinMode(p, OUTPUT);
    for (int cycle = 0; cycle < 3; cycle++) {
      Serial.println("      OFF"); digitalWrite(p, LOW);  delay(2000);
      Serial.println("      ON ");  digitalWrite(p, HIGH); delay(2000);
    }
    pinMode(p, INPUT);
  }

  Serial.println("\n  If neither of those did anything, sweeping the rest...");
  for (size_t i = 0; i < kNumSafe; i++) {
    uint8_t p = kSafePins[i];
    if (p == 23 || p == 22) continue;         // already covered above
    if (p == 1 || p == 2 || p == 14 || p == 15) continue;  // LCD SPI, don't disturb
    Serial.printf("  testing GPIO%-2u ... ", p);
    Serial.flush();
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
    delay(1000);
    digitalWrite(p, HIGH);
    delay(1000);
    pinMode(p, INPUT);          // release so we don't fight the next test
    Serial.println("done");
  }
  Serial.println("  -> The GPIO that made the screen light up / go dark is LCD_BL.");
}

void setup() {
  Serial.begin(115200);
  // Native-USB CDC needs to enumerate AND give a host-side capture tool time to
  // attach. 4s lost the race and Phase A/B scrolled past uncaptured; 15s is
  // comfortably enough to start `cat /dev/cu.*` after the upload resets us.
  delay(15000);

  Serial.println("\n\n########################################");
  Serial.println("#  DeskBuddy pin discovery");
  Serial.println("########################################");
  Serial.printf("chip: %s  rev %d  cores %d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("flash: %u bytes   free heap: %u bytes\n",
                ESP.getFlashChipSize(), ESP.getFreeHeap());

  phaseA_i2c();
  phaseB_adc();
  phaseC_backlight();

  Serial.println("\n=== DONE. Copy this entire log back. ===");
}

void loop() {
  delay(5000);
  Serial.println("(idle - diagnostic complete, reflash to re-run)");
}
