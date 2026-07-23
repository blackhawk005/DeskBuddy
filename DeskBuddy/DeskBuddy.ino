// ============================================================================
// DeskBuddy.ino  —  Tiny ESP DeskBuddy
// Waveshare ESP32-C6-Touch-LCD-1.47
//
// Features: monochrome animated face, touch swipes (page nav), tap moods,
// IMU eye drift, clock/date, Open-Meteo weather, moon phase, romantic quotes,
// GitHub public stats. Wi-Fi creds live in secrets.h.
//
// Libraries (Arduino Library Manager unless noted):
//   - GFX_Library_for_Arduino  (Moon Wang)         >= 1.5.9   [LCD driver]
//   - FastIMU                   (LiquidCGS)         >= 1.2.8   [QMI8658]
//   - esp_lcd_touch_axs5106l    (Waveshare, OFFLINE from board demo zip) [touch]
//   - ArduinoJson               (Benoit Blanchon)  >= 7.x
//   Board package: "esp32 by Espressif Systems" >= 3.0.0, board = "ESP32C6 Dev Module"
//
// Touch note: the AXS5106L library ships in Waveshare's board demo .zip, not the
// Library Manager. If you'd rather not install it, set USE_TOUCH 0 below and drive
// pages with the BOOT button instead (see loop()).
// ============================================================================
#define USE_TOUCH 1

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include "FastIMU.h"
#include "config.h"
#include "version.h"
#include "credentials.h"
#include "clockseed.h"
#include "face.h"
#include "net.h"
#include "ota.h"          // LAN OTA (browser/IDE upload) — the "update whenever" path
// Auto-update (pull-OTA from GitHub) removed 2026-07-23 at owner's request:
// updates are infrequent and the board is physically accessible, and the
// periodic network check added latency. Update now via USB flash or the LAN OTA
// page at the device's IP. (former: #include "otapull.h")

#if USE_TOUCH
  // Was Waveshare's esp_lcd_touch_axs5106l. Replaced by our own polling reader:
  // that driver only reads the chip when its INT line fires, and on this board
  // INT never asserts, so it never saw a single touch. See touch.h.
  #include "touch.h"
#endif

// ---- Display (JD9853 as ST7789-compatible; offsets from factory demo)
// Arduino_HWSPI, not Arduino_ESP32SPI — HWSPI is what Waveshare's own
// 01_gfx_helloworld demo uses on this board and is the known-good path.
Arduino_DataBus* bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX* gfx = new Arduino_ST7789(
  bus, LCD_RST, 0 /*rotation*/, false /*IPS flag per factory demo*/,
  LCD_W, LCD_H, LCD_COL_OFFSET, LCD_ROW_OFFSET, LCD_COL_OFFSET, LCD_ROW_OFFSET);

Face face(gfx, bus);

// ---- IMU
QMI8658 imu;
calData calib = {0};
AccelData accel;

// ---- gesture state
int  touchStartX = -1, touchStartY = -1;
// Last position seen while the finger was down. The AXS5106L reports nothing at
// all on release, so the release edge has to be classified using this instead.
int  touchLastX = -1, touchLastY = -1;
bool touchActive = false;
bool gestureConsumed = false;   // a swipe already fired for this finger-down
uint32_t touchT0 = 0;
// Time of the last frame in which the controller actually reported a point.
uint32_t lastTouchMs = 0;
// How long the controller must stay silent before we call it a finger lift.
// Must exceed the AXS5106L's quiet gap between interrupts during a held press.
#define TOUCH_RELEASE_MS 120
// Horizontal travel (screen px) that counts as a page swipe. Lower = easier.
// Fires MID-DRAG, not on release, so navigation feels immediate.
#define SWIPE_THRESHOLD 26

// ---- quote rotation (offline pack, cheap — stays on the UI task)
uint32_t lastQuoteRot = 0;
int quoteIdx = 0;

// ---- config loaded once in setup(), then read-only (safe to share)
String messageUrl;            // provisioned from NVS (secret token inside)
String wifiSsid, wifiPass;
#define MSG_POLL_MS   (30UL * 1000UL)   // check for a new message every 30s
#define WIFI_RETRY_MS (20UL * 1000UL)

// ---- Networking runs on its OWN FreeRTOS task (netTask), so a slow/blocking
// HTTPS fetch or Wi-Fi handshake never freezes the display or touch. The task
// fetches into `netShared` under a mutex; the UI task drains it each frame and
// applies the results to `face` (keeping all face mutation on the UI thread).
SemaphoreHandle_t netMx = nullptr;
struct {
  char     msg[160];  bool msgReady = false;
  MoonData moon;      GhData gh;     bool dataReady = false;
} netShared;

// ---- battery
int readBatteryPct() {
  int raw = analogReadMilliVolts(BAT_ADC_PIN);
  float vbat = raw * BAT_ADC_DIVIDER / 1000.0f;      // volts
  int pct = (int)((vbat - 3.30f) / (4.20f - 3.30f) * 100.0f);
  return constrain(pct, 0, 100);
}

// Publish a fetched message into the shared buffer for the UI to pick up.
static void publishMessage(const char* m) {
  xSemaphoreTake(netMx, portMAX_DELAY);
  strlcpy(netShared.msg, m, sizeof(netShared.msg));
  netShared.msgReady = true;
  xSemaphoreGive(netMx);
}
static void publishData(const MoonData& m, const GhData& g) {
  xSemaphoreTake(netMx, portMAX_DELAY);
  netShared.moon = m; netShared.gh = g; netShared.dataReady = true;
  xSemaphoreGive(netMx);
}

// The ENTIRE networking lifecycle lives here, off the UI thread: initial
// connect, reconnect on the marginal AP, LAN-OTA servicing, and the periodic
// message + GitHub fetches. Blocking calls here only stall THIS task; the UI
// keeps rendering at full rate because FreeRTOS runs it while this task waits
// on the socket.
void netTask(void*) {
  bool     wasUp    = false;
  uint32_t lastTry  = 0, lastMsgF = 0, lastDataF = 0;

  netBegin(wifiSsid, wifiPass);          // blocking connect — UI unaffected

  for (;;) {
    uint32_t t = millis();

    if (netUp()) {
      if (!wasUp) {                       // just (re)connected
        wasUp = true;
        Serial.print("WiFi up: http://"); Serial.println(WiFi.localIP());
        otaBegin("deskbuddy");            // LAN OTA (browser/IDE) lives on this task
        lastMsgF = lastDataF = 0;         // force an immediate first fetch
      }
      otaHandle();                        // service LAN OTA without touching the UI

      if (messageUrl.length() && (lastMsgF == 0 || t - lastMsgF > MSG_POLL_MS)) {
        char b[160];
        if (fetchMessage(messageUrl, b, sizeof(b))) publishMessage(b);
        lastMsgF = t ? t : 1;
      }
      if (lastDataF == 0 || t - lastDataF > NET_REFRESH_MS) {
        MoonData m = computeMoon(time(nullptr));   // local calc
        GhData   g = fetchGitHub();                // network
        publishData(m, g);
        lastDataF = t ? t : 1;
      }
    } else {
      wasUp = false;
      if (t - lastTry > WIFI_RETRY_MS) {
        Serial.println("WiFi: retrying...");
        WiFi.disconnect();
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        lastTry = t;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));        // yield so UI + idle always run
  }
}

void setup() {
  Serial.begin(115200);
  // Native-USB CDC needs a moment to enumerate before anything is readable, and
  // a host-side capture tool needs time to attach. Without this the boot banner
  // is printed into the void and a running board is indistinguishable from a
  // dead one on the serial line.
  delay(3000);
  Serial.println("\n\n=== DeskBuddy boot ===");
  Serial.printf("rotation %d -> logical screen %dx%d\n",
                LCD_ROTATION, SCREEN_W, SCREEN_H);

  randomSeed(esp_random());

  // Before anything renders: without this the clock page reads 00:00 and the
  // date page reads Jan 1 1970 whenever Wi-Fi is unavailable. NTP overwrites
  // this later if it connects.
  seedClockFromBuildTime();

  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);

  face.begin();
  Serial.println("display initialised, backlight on");
  face.setQuote(kQuotePack[0]);

  // IMU
  if (imu.init(calib, QMI8658_ADDR) == 0) {
    imu.setAccelRange(4);
  } else {
    Serial.println("IMU init failed (check QMI8658_ADDR / I2C pins)");
  }

  #if USE_TOUCH
    // Just the reset pulse — rotation mapping is compile-time in touch.h.
    touchBegin();
    Serial.println("touch: polling AXS5106L at 0x63 (INT line unused)");
  #endif

  Serial.printf("firmware version: %s\n", FW_VERSION);

  // Wi-Fi creds and message URL come from NVS (survive OTA), not compile-time.
  WifiCreds wc = credsLoad();
  wifiSsid = wc.ssid; wifiPass = wc.pass;
  messageUrl = msgUrlLoad();
  WiFi.setAutoReconnect(true);

  // Hand ALL networking to a background task so the display/touch never block on
  // it. 16KB stack because mbedTLS (HTTPS) is stack-hungry; priority 1 == the
  // Arduino loop task, so they time-slice and the UI stays smooth.
  netMx = xSemaphoreCreateMutex();
  xTaskCreate(netTask, "net", 16384, nullptr, 1, nullptr);
  Serial.println("networking moved to background task");
}

void handleGesture(int x, int y, bool pressed) {
  if (pressed && !touchActive) {                 // touch down
    touchActive = true; gestureConsumed = false;
    touchStartX = x; touchStartY = y; touchT0 = millis();
    touchLastX = x; touchLastY = y;
  } else if (pressed && touchActive) {           // still down
    touchLastX = x; touchLastY = y;
    // Fire the swipe THE MOMENT the finger has travelled far enough, instead of
    // waiting for release. This is the responsiveness fix — the old code only
    // classified on release, so a swipe that didn't lift cleanly did nothing.
    if (!gestureConsumed) {
      int dx = x - touchStartX, dy = y - touchStartY;
      if (abs(dx) >= SWIPE_THRESHOLD && abs(dx) > abs(dy)) {
        if (dx < 0) face.nextPage(); else face.prevPage();
        gestureConsumed = true;                  // one page-turn per finger-down
        Serial.printf("[touch] swipe dx=%d -> page %d\n", dx, (int)face.getPage());
      }
    }
  } else if (!pressed && touchActive) {          // release
    touchActive = false;
    if (gestureConsumed) return;                 // already handled as a swipe
    int dx = touchLastX - touchStartX, dy = touchLastY - touchStartY;
    uint32_t dt = millis() - touchT0;
    // Anything that didn't become a swipe and stayed roughly put is a tap.
    if (abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD && dt < 600) {
      if (face.getPage() == PAGE_FACE) face.cycleMood();     // tap face -> mood
      else face.setPage(PAGE_FACE);                          // tap elsewhere -> home
      Serial.printf("[touch] tap at %d,%d\n", touchLastX, touchLastY);
    } else {
      Serial.printf("[touch] ignored dx=%d dy=%d dt=%lums\n", dx, dy, (unsigned long)dt);
    }
  }
}

void loop() {
  // ---- IMU -> eye drift
  // FastIMU 1.3.0's update() returns void, so there is no status to test here.
  imu.update();
  imu.getAccel(&accel);
  face.setTilt(accel.accelX, accel.accelY);

  // ---- Touch
  #if USE_TOUCH
    // Two-step in this library: bsp_touch_read() polls the controller, then
    // bsp_touch_get_coordinates() reports whether a valid point is available.
    //
    // IMPORTANT: bsp_touch_read() zeroes touch_num on every call and only
    // refills it when the AXS5106L's INT line has fired since the last read.
    // The controller does NOT interrupt on every one of our ~33ms loops, so a
    // finger held still reports "no touch" on most iterations. Treating each of
    // those as a release ends the gesture instantly and makes every swipe
    // register as a tap. So only declare a release after the controller has
    // been quiet for TOUCH_RELEASE_MS.
    uint16_t tx = 0, ty = 0;
    if (touchPoll(&tx, &ty)) {
      lastTouchMs = millis();
      handleGesture(tx, ty, true);
    } else if (touchActive && millis() - lastTouchMs > TOUCH_RELEASE_MS) {
      // Genuinely lifted. handleGesture() ignores the coordinates here and
      // classifies the gesture from touchStartX/Y vs touchLastX/Y.
      handleGesture(0, 0, false);
    }
  #else
    // Fallback: BOOT button (GPIO9) cycles pages
    static bool prev = true;
    bool now = digitalRead(9);
    if (prev && !now) face.nextPage();
    prev = now;
  #endif

  // ---- battery + brightness dim when idle-dark could go here
  face.setBattery(readBatteryPct());

  // ---- Serial provisioning: "wifi <ssid> <pass>" to re-point without a rebuild
  credsSerialTask();

  uint32_t t = millis();

  // ---- Drain results the network task fetched (non-blocking: skip if it holds
  // the lock this instant, pick them up next frame). All face mutation stays
  // here on the UI thread.
  char newMsg[160]; bool haveMsg = false, haveData = false;
  MoonData newMoon; GhData newGh;
  if (netMx && xSemaphoreTake(netMx, 0) == pdTRUE) {
    if (netShared.msgReady)  { strlcpy(newMsg, netShared.msg, sizeof(newMsg)); netShared.msgReady = false; haveMsg = true; }
    if (netShared.dataReady) { newMoon = netShared.moon; newGh = netShared.gh; netShared.dataReady = false; haveData = true; }
    xSemaphoreGive(netMx);
  }
  if (haveMsg && face.setMessage(newMsg)) Serial.printf("[message] NEW: \"%s\"\n", newMsg);
  if (haveData) { face.setMoon(newMoon); face.setGitHub(newGh); }

  // ---- quote rotation (offline pack — instant, safe on the UI thread)
  if (t - lastQuoteRot > QUOTE_ROTATE_MS) {
    quoteIdx++; char q[160]; fetchQuote(q, sizeof(q), quoteIdx); face.setQuote(q);
    lastQuoteRot = t;
  }

  // ---- render
  face.tick();

  // Heartbeat: proves the loop is alive even when the screen shows nothing, so
  // "blank display" can be told apart from "firmware crashed or hung".
  static uint32_t lastBeat = 0;
  if (t - lastBeat > 5000) {
    Serial.printf("[alive] page=%d mood-tag=\"%s\" bat=%d%% wifi=%s\n",
                  (int)face.getPage(), FACE_GREETING, readBatteryPct(),
                  netUp() ? "up" : "down");
    lastBeat = t;
  }

  delay(1000 / FACE_FPS);
}
