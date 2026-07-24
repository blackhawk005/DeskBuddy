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
GyroData gyro;

// ---- pet-like behaviour + night mode
// lastInteractionMs is bumped by a tap OR by physical movement (pick-up/shake).
// Used both to bring the buddy back from "lonely" and to wake the screen at night.
uint32_t lastInteractionMs = 0;
uint32_t reactUntilMs = 0;        // a startle (surprised) reaction is showing until this
#define LONELY_MS   (2UL * 60UL * 60UL * 1000UL)   // untouched this long -> sleepy
#define SHAKE_DPS   220.0f        // gyro magnitude (deg/s) that counts as a shake
#define BUMP_G      0.45f         // sudden accel change (g) that counts as a pick-up
#define NIGHT_START 22            // 22:00
#define NIGHT_END    6            //  6:00
#define BL_DAY      220           // backlight duty by day / when awake
#define BL_NIGHT      0           // backlight off at night when idle
#define WAKE_MS     (15UL * 1000UL)   // stay lit this long after a night interaction

// ---- touch state
bool touchActive = false;
// Time of the last frame in which the controller actually reported a point.
uint32_t lastTouchMs = 0;
// How long the controller must stay silent before we call it a finger lift.
// Must exceed the AXS5106L's quiet gap between polls during a held press, so one
// physical tap = one action even though the controller drops frames.
#define TOUCH_RELEASE_MS 120

// ---- quote rotation (offline pack, cheap — stays on the UI task)
uint32_t lastQuoteRot = 0;
int quoteIdx = 0;

// ---- config loaded once in setup(), then read-only (safe to share)
String messageUrl;            // /raw URL from NVS (secret token inside); the SSE
                              // stream URL is derived from it (same token)
String wifiSsid, wifiPass;
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

// ---- Pet-like behaviour: react to being picked up / shaken, and drift to
// "sleepy" when left alone for a long time. Runs every frame on the UI thread.
void updatePet() {
  imu.getGyro(&gyro);
  float gmag = sqrtf(gyro.gyroX*gyro.gyroX + gyro.gyroY*gyro.gyroY + gyro.gyroZ*gyro.gyroZ);
  float amag = sqrtf(accel.accelX*accel.accelX + accel.accelY*accel.accelY + accel.accelZ*accel.accelZ);
  static float prevA = 1.0f;
  float jerk = fabsf(amag - prevA);
  prevA = amag;
  uint32_t now = millis();

  bool disturbed = (gmag > SHAKE_DPS) || (jerk > BUMP_G);
  if (disturbed) {
    lastInteractionMs = now;                 // movement counts as attention
    if (now > reactUntilMs) {                // startle (don't re-trigger constantly)
      face.setMood(MOOD_SURPRISED);
      reactUntilMs = now + 1200;
    }
  } else if (reactUntilMs && now > reactUntilMs) {
    reactUntilMs = 0;
    face.setMood(MOOD_HAPPY);                // settle to content after being startled
  }

  // Lonely: no attention for a long time -> quietly go sleepy.
  if (reactUntilMs == 0 && now - lastInteractionMs > LONELY_MS &&
      face.getMood() != MOOD_SLEEPY) {
    face.setMood(MOOD_SLEEPY);
  }
}

// ---- Night mode: dim the screen off during night hours, wake it briefly on
// any interaction (tap or movement). No-op if the clock isn't set yet.
void updateNightMode() {
  time_t tt = time(nullptr);
  if (tt < 100000) return;                   // clock not seeded — leave lit
  struct tm t; localtime_r(&tt, &t);
  bool night = (t.tm_hour >= NIGHT_START || t.tm_hour < NIGHT_END);
  bool awake = !night || (millis() - lastInteractionMs < WAKE_MS);
  face.setBrightness(awake ? BL_DAY : BL_NIGHT);
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

// Parse one Server-Sent-Events line. "data:" -> new message (existing draw path);
// ":" heartbeat and everything else -> ignored. Returns true if it was a message.
static bool handleSseLine(const String& line) {
  if (line.startsWith("data:")) {
    String msg = line.substring(5);
    if (msg.startsWith(" ")) msg = msg.substring(1);   // trim one leading space
    publishMessage(msg.c_str());                       // -> UI draws via face.setMessage
    Serial.printf("[sse] message: \"%s\"\n", msg.c_str());
    return true;
  }
  // ":" heartbeat, "event:" lines, blanks -> ignore
  return false;
}

// The ENTIRE networking lifecycle lives here, off the UI thread: WiFi connect +
// reconnect, LAN-OTA, the periodic GitHub/moon refresh, and a PERSISTENT SSE
// stream for the live message (replaces the old 30s poll). Blocking calls only
// stall THIS task; the UI keeps rendering because FreeRTOS runs it meanwhile.
void netTask(void*) {
  bool     wasUp    = false;
  uint32_t lastTry  = 0, lastDataF = 0;

  // SSE state
  static WiFiClientSecure sse;           // one persistent TLS connection
  String   sseHost, ssePath;
  bool     streaming     = false;
  uint32_t lastRxMs      = 0;            // last byte seen on the stream
  uint32_t nextConnectMs = 0;            // backoff gate for the next connect attempt
  uint8_t  backoffIdx    = 0;
  uint8_t  failStreak    = 0;            // consecutive failed connects (for fallback)
  String   lineBuf;
  const uint32_t kBackoff[] = { 1000, 2000, 5000, 10000 };   // 1s,2s,5s,cap 10s
  const uint32_t kStaleMs   = 40000;    // no data/heartbeat this long -> reconnect

  if (messageUrl.length()) sseDeriveStream(messageUrl, sseHost, ssePath);

  netBegin(wifiSsid, wifiPass);          // blocking connect — UI unaffected

  for (;;) {
    uint32_t t = millis();

    if (!netUp()) {                       // ---- WiFi down: drop stream, retry link
      if (streaming) { sse.stop(); streaming = false; }
      wasUp = false;
      if (t - lastTry > WIFI_RETRY_MS) {
        Serial.println("WiFi: retrying...");
        WiFi.disconnect();
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        lastTry = t;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!wasUp) {                         // ---- just (re)connected
      wasUp = true;
      Serial.print("WiFi up: http://"); Serial.println(WiFi.localIP());
      otaBegin("deskbuddy");              // LAN OTA (browser/IDE) lives on this task
      lastDataF   = 0;                    // force an immediate GitHub/moon refresh
      nextConnectMs = 0;                  // connect the stream now
    }
    otaHandle();

    // ---- periodic GitHub + moon (unchanged, every 15 min)
    if (lastDataF == 0 || t - lastDataF > NET_REFRESH_MS) {
      MoonData m = computeMoon(time(nullptr));
      GhData   g = fetchGitHub();
      publishData(m, g);
      lastDataF = t ? t : 1;
    }

    // ---- SSE stream
    if (!streaming) {
      if (sseHost.length() && t >= nextConnectMs) {
        if (sseOpen(sse, sseHost, ssePath)) {
          streaming = true; lastRxMs = t; backoffIdx = 0; failStreak = 0; lineBuf = "";
          Serial.println("[sse] connected");
        } else {
          sse.stop();
          failStreak++;
          uint32_t bo = kBackoff[backoffIdx < 4 ? backoffIdx : 3];
          if (backoffIdx < 3) backoffIdx++;
          nextConnectMs = t + bo;
          Serial.printf("[sse] connect failed (#%u) -> retry in %lums\n",
                        failStreak, (unsigned long)bo);
          // Fallback: after a few failures, one-shot the old /raw poll so the
          // screen still shows the current message while we keep retrying.
          if (failStreak == 3) {
            char b[160];
            if (fetchMessage(messageUrl, b, sizeof(b))) publishMessage(b);
          }
        }
      }
    } else {
      // Non-blocking: consume only the bytes already available, split on '\n'.
      while (sse.available()) {
        char ch = (char)sse.read();
        lastRxMs = t;
        if (ch == '\n')      { handleSseLine(lineBuf); lineBuf = ""; }
        else if (ch != '\r') { if (lineBuf.length() < 300) lineBuf += ch; }
      }
      // Reconnect if the socket closed or the stream went silent past the heartbeat.
      if (!sse.connected() || t - lastRxMs > kStaleMs) {
        Serial.println("[sse] stale/closed -> reconnect");
        sse.stop(); streaming = false; lineBuf = "";
        backoffIdx = 0; nextConnectMs = t + kBackoff[0];
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));        // yield so UI + idle run; feeds the WDT
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

// Touch-zone navigation. Acts on the touch-DOWN edge (snappiest possible), by
// which third of the screen was tapped. The touchActive flag + the release
// debounce in loop() guarantee exactly one action per physical tap even though
// the AXS5106L drops frames mid-touch.
void handleGesture(int x, int y, bool pressed) {
  if (pressed && !touchActive) {                 // touch DOWN -> act now
    touchActive = true;
    lastInteractionMs = millis();                // a tap is attention (un-lonely, wakes)
    if (x < ZONE_LEFT_MAX) {
      face.prevPage();  Serial.printf("[touch] LEFT@%d -> prev (page %d)\n", x, (int)face.getPage());
    } else if (x > ZONE_RIGHT_MIN) {
      face.nextPage();  Serial.printf("[touch] RIGHT@%d -> next (page %d)\n", x, (int)face.getPage());
    } else {                                     // center third = select
      if (face.getPage() == PAGE_FACE) face.cycleMood();
      else face.setPage(PAGE_FACE);
      Serial.printf("[touch] CENTER@%d -> mood/home\n", x);
    }
  } else if (!pressed && touchActive) {          // released -> ready for next tap
    touchActive = false;
  }
}

void loop() {
  // ---- IMU -> eye drift + pet reactions
  // FastIMU 1.3.0's update() returns void, so there is no status to test here.
  imu.update();
  imu.getAccel(&accel);
  face.setTilt(accel.accelX, accel.accelY);
  updatePet();          // pick-up / shake / lonely behaviour

  // ---- Touch
  #if USE_TOUCH
    // Two-step in this library: bsp_touch_read() polls the controller, then
    // bsp_touch_get_coordinates() reports whether a valid point is available.
    //
    // IMPORTANT: the AXS5106L drops frames — a finger held still reports "no
    // touch" on many polls. Treating each gap as a release would let one tap
    // fire repeatedly, so a release is only declared after TOUCH_RELEASE_MS of
    // real silence. That gives exactly one zone action per physical tap.
    uint16_t tx = 0, ty = 0;
    if (touchPoll(&tx, &ty)) {
      lastTouchMs = millis();
      handleGesture(tx, ty, true);           // acts on the touch-down edge
    } else if (touchActive && millis() - lastTouchMs > TOUCH_RELEASE_MS) {
      handleGesture(0, 0, false);            // genuine lift — arm the next tap
    }
  #else
    // Fallback: BOOT button (GPIO9) cycles pages
    static bool prev = true;
    bool now = digitalRead(9);
    if (prev && !now) face.nextPage();
    prev = now;
  #endif

  // ---- battery
  face.setBattery(readBatteryPct());

  // ---- night mode: dim the screen off overnight, wake on interaction
  updateNightMode();

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
