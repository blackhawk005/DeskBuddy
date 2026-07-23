// ============================================================================
// face.h  —  Monochrome face dashboard + eyes + moods + IMU eye drift + pages
// Rendering via GFX_Library_for_Arduino (Arduino_GFX). Mono look = white on black.
// ============================================================================
#pragma once
#include <Arduino_GFX_Library.h>
#include "config.h"
#include "panel.h"
#include "net.h"

// Page indices for swipe navigation. Weather removed per owner request
// 2026-07-23. PAGE_MESSAGE added same day — live message feed from an endpoint.
enum Page { PAGE_FACE, PAGE_MESSAGE, PAGE_CLOCK, PAGE_DATE, PAGE_MOON,
            PAGE_QUOTE, PAGE_GITHUB, PAGE_COUNT };

// Moods set by tap
enum Mood { MOOD_NEUTRAL, MOOD_HAPPY, MOOD_LOVE, MOOD_SLEEPY, MOOD_SURPRISED, MOOD_COUNT };

class Face {
public:
  // `b` is the same bus the Arduino_GFX object was built on — needed so begin()
  // can push the JD9853 register sequence, which the ST7789 class doesn't know.
  Face(Arduino_GFX* g, Arduino_DataBus* b) : gfx(g), bus(b) {}

  void begin() {
    if (!gfx->begin()) {
      Serial.println("gfx->begin() failed! (check LCD SPI pins in config.h)");
    }
    // MUST run before the first draw: the panel is a JD9853, not a real ST7789,
    // and comes up blank/wrong without its own register init. See panel.h.
    jd9853_init(bus);
    // Rotation must come after the register init, matching the factory demo.
    gfx->setRotation(LCD_ROTATION);
    gfx->fillScreen(RGB565_BLACK);
    // Backlight on only after the panel is initialised, so the user never sees
    // a lit screen full of uninitialised garbage.
    #if LCD_BL >= 0
      ledcAttach(LCD_BL, 5000, 8);
      ledcWrite(LCD_BL, 220);
    #endif
    lastBlink = millis() + random(IDLE_BLINK_MS_MIN, IDLE_BLINK_MS_MAX);
  }

  void setBrightness(uint8_t v) {
    #if LCD_BL >= 0
      ledcWrite(LCD_BL, v);
    #endif
  }

  // --- input from main loop
  void setPage(Page p)         { if (p != page) { page = p; dirty = true; } }
  Page getPage() const         { return page; }
  void nextPage()  { setPage((Page)((page + 1) % PAGE_COUNT)); }
  void prevPage()  { setPage((Page)((page + PAGE_COUNT - 1) % PAGE_COUNT)); }
  void setMood(Mood m)         { mood = m; blinkUntil = 0; dirty = true; }
  void cycleMood() { setMood((Mood)((mood + 1) % MOOD_COUNT)); }

  // --- IMU eye drift: feed accel in g (x,y). Eyes lean toward "down".
  void setTilt(float ax, float ay) {
    // low-pass filter so eyes glide, not jitter
    tiltX = tiltX * 0.85f + ax * 0.15f;
    tiltY = tiltY * 0.85f + ay * 0.15f;
  }

  // --- data injection for pages
  void setWeather(const WxData& w) { wx = w; }   // weather page removed; kept so net fetch still links
  void setMoon(const MoonData& m)  { moon = m; if (page==PAGE_MOON) dirty=true; }
  void setGitHub(const GhData& g)  { gh = g; if (page==PAGE_GITHUB) dirty=true; }
  void setQuote(const char* q)     { strlcpy(quote, q, sizeof(quote)); if (page==PAGE_QUOTE) dirty=true; }
  void setBattery(int pct)         { batPct = pct; }

  // --- live message feed
  // Store a freshly-fetched message. If the text differs from what is on screen,
  // it's NEW: raise the unread flag, jump to the message page to show it, and
  // flash once so it's noticed. Returns true if it was a new message.
  bool setMessage(const char* m) {
    if (strncmp(m, message, sizeof(message)) == 0) return false;  // unchanged
    strlcpy(message, m, sizeof(message));
    hasMessage = true;
    msgUnread = true;
    msgFlashUntil = millis() + 1200;      // brief attention flash
    setPage(PAGE_MESSAGE);                // bring it to the front
    dirty = true;
    return true;
  }
  bool messageUnread() const { return msgUnread; }

  // --- main render tick
  void tick() {
    uint32_t now = millis();
    switch (page) {
      case PAGE_FACE: renderFace(now); break;
      case PAGE_CLOCK: if (dirty || now - lastClock > 1000) { renderClock(); lastClock = now; dirty=false; } break;
      case PAGE_DATE:  if (dirty) { renderDate(); dirty=false; } break;
      case PAGE_MOON:    if (dirty) { renderMoon(); dirty=false; } break;
      case PAGE_QUOTE:   if (dirty) { renderQuote(); dirty=false; } break;
      case PAGE_MESSAGE: if (dirty || msgFlashUntil) { renderMessage(now); if (!msgFlashUntil) dirty=false; } break;
      case PAGE_GITHUB:  if (dirty) { renderGitHub(); dirty=false; } break;
      default: break;
    }
  }

private:
  Arduino_GFX* gfx;
  Arduino_DataBus* bus;   // retained for the JD9853 register init in begin()
  Page page = PAGE_FACE;
  // Default mood is HAPPY so it greets you smiling on power-up. MOOD_NEUTRAL
  // draws a flat mouth and plain round eyes, which reads as blank, not friendly.
  Mood mood = MOOD_HAPPY;
  bool dirty = true;
  float tiltX = 0, tiltY = 0;
  uint32_t lastBlink = 0, blinkUntil = 0, lastClock = 0;
  int batPct = -1;
  WxData wx; MoonData moon; GhData gh;
  char quote[160] = "You are my favorite notification.";

  // Live message state.
  char message[160] = "";
  bool hasMessage = false;
  bool msgUnread = false;
  uint32_t msgFlashUntil = 0;   // non-zero while the new-message flash is active

  // GFX_Library_for_Arduino >= 1.4 uses RGB565_* names; the bare WHITE/BLACK
  // aliases were removed. The panel is full-colour (JD9853, 262K colours) — the
  // dashboard is mono by design, but these accents prove colour works and make
  // the message/notification stand out.
  static const uint16_t FG    = RGB565_WHITE, BG = RGB565_BLACK;
  static const uint16_t ACCENT = RGB565_CYAN;    // message text
  static const uint16_t ALERT  = RGB565_RED;     // unread badge
  int cx() { return SCREEN_W / 2; }

  // ---- status strip (battery + page dots) drawn on every page
  void statusStrip() {
    gfx->fillRect(0, 0, SCREEN_W, 14, BG);
    // battery box top-right
    int bw = 22, bh = 9, bx = SCREEN_W - bw - 6, by = 3;
    gfx->drawRect(bx, by, bw, bh, FG);
    gfx->fillRect(bx + bw, by + 2, 2, bh - 4, FG);
    if (batPct >= 0) {
      int fill = (bw - 4) * batPct / 100;
      gfx->fillRect(bx + 2, by + 2, fill, bh - 4, FG);
    }
    // unread-message badge (red dot, top-left) — visible from every page until
    // the message page is opened.
    if (msgUnread) gfx->fillCircle(8, 7, 4, ALERT);
    // page dots bottom
    int dots = PAGE_COUNT, gap = 12, tot = dots * gap;
    int sx = cx() - tot / 2 + gap / 2;
    for (int i = 0; i < dots; i++) {
      if (i == page) gfx->fillCircle(sx + i * gap, SCREEN_H - 8, 3, FG);
      else           gfx->drawCircle(sx + i * gap, SCREEN_H - 8, 2, FG);
    }
  }

  // ============================ FACE ============================
  void renderFace(uint32_t now) {
    // Blink scheduling
    bool blinking = false;
    if (blinkUntil == 0 && now >= lastBlink) { blinkUntil = now + 120; }
    if (blinkUntil && now < blinkUntil) blinking = true;
    if (blinkUntil && now >= blinkUntil) {
      blinkUntil = 0;
      lastBlink = now + random(IDLE_BLINK_MS_MIN, IDLE_BLINK_MS_MAX);
    }
    // Sleepy mood auto-blinks slow & half-closed
    if (mood == MOOD_SLEEPY) blinking = ((now / 400) % 2) == 0;

    gfx->fillScreen(BG);
    statusStrip();

    int eyeY = SCREEN_H / 2 - 20;
    // Eye separation scales with screen width so the face stays proportionate
    // in landscape instead of huddling in the middle of a wide panel.
    int eyeDX = SCREEN_W / 5;
    int driftX = (int)(tiltX * 14);     // IMU drift
    int driftY = (int)(tiltY * 14);
    driftX = constrain(driftX, -10, 10);
    driftY = constrain(driftY, -8, 8);

    drawEye(cx() - eyeDX + driftX, eyeY + driftY, blinking);
    drawEye(cx() + eyeDX + driftX, eyeY + driftY, blinking);
    drawMouth(cx(), eyeY + 60);

    // Greeting under the face. Was the mood name ("happy"/"love"/...); now a
    // fixed message, so the mood is conveyed by the face itself rather than a
    // word. Size 2 because "Hi Pannu" is the point of the whole device.
    gfx->setTextColor(FG, BG);
    gfx->setTextSize(2);
    const char* tag = FACE_GREETING;
    gfx->setCursor(cx() - (int)strlen(tag) * 6, SCREEN_H - 30);
    gfx->print(tag);
  }

  const char* moodName() {
    switch (mood) {
      case MOOD_HAPPY: return "happy";
      case MOOD_LOVE: return "love";
      case MOOD_SLEEPY: return "sleepy";
      case MOOD_SURPRISED: return "!";
      default: return "hi";
    }
  }

  void drawEye(int x, int y, bool blinking) {
    if (blinking) { gfx->drawFastHLine(x - 12, y, 24, FG); return; }
    switch (mood) {
      case MOOD_LOVE: {                 // heart-ish eyes
        gfx->fillCircle(x - 5, y - 3, 6, FG);
        gfx->fillCircle(x + 5, y - 3, 6, FG);
        gfx->fillTriangle(x - 11, y, x + 11, y, x, y + 12, FG);
      } break;
      case MOOD_SURPRISED:
        gfx->drawCircle(x, y, 14, FG); gfx->drawCircle(x, y, 13, FG);
        gfx->fillCircle(x, y, 4, FG); break;
      case MOOD_HAPPY:                  // upward arcs
        gfx->drawFastHLine(x - 12, y + 4, 24, FG);
        gfx->drawFastHLine(x - 9,  y + 1, 18, FG);
        gfx->drawFastHLine(x - 5,  y - 2, 10, FG); break;
      case MOOD_SLEEPY:
        gfx->drawFastHLine(x - 12, y, 24, FG);
        gfx->drawFastHLine(x - 10, y + 3, 20, FG); break;
      default:                          // neutral round
        gfx->fillCircle(x, y, 13, FG);
        gfx->fillCircle(x + (tiltX>0?3:-3), y + (tiltY>0?3:-3), 4, BG);
    }
  }

  void drawMouth(int x, int y) {
    switch (mood) {
      case MOOD_HAPPY: case MOOD_LOVE:
        // Smile = U-shaped: centre BELOW the corners. y grows downward, so the
        // parabola must be subtracted. (The original added it, which put the
        // centre above the corners and drew a frown for the "happy" mood.)
        for (int i = -18; i <= 18; i++) {
          int yy = y - (int)(0.03f * i * i) + 6;
          gfx->drawPixel(x + i, yy, FG); gfx->drawPixel(x + i, yy + 1, FG);
        } break;
      case MOOD_SURPRISED:
        gfx->drawCircle(x, y + 2, 8, FG); break;
      case MOOD_SLEEPY:
        gfx->drawFastHLine(x - 8, y, 16, FG); break;
      default:
        gfx->drawFastHLine(x - 12, y, 24, FG);
    }
  }

  // ============================ PAGES ============================
  void header(const char* t) {
    gfx->fillScreen(BG); statusStrip();
    gfx->setTextColor(FG, BG); gfx->setTextSize(1);
    gfx->setCursor(8, 20); gfx->print(t);
    gfx->drawFastHLine(8, 32, SCREEN_W - 16, FG);
  }

  void bigText(const char* s, int y, int size) {
    gfx->setTextSize(size);
    int w = strlen(s) * 6 * size;
    gfx->setCursor(cx() - w / 2, y);
    gfx->print(s);
  }

  void renderClock() {
    // '~' means the time came from the build-time seed, not NTP — it can be
    // hours or days stale. Better to show an honest approximate time than a
    // confident wrong one.
    header(timeIsSynced() ? "CLOCK" : "CLOCK ~approx");
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    char hm[8], sec[4];
    strftime(hm, sizeof(hm), "%H:%M", &t);
    strftime(sec, sizeof(sec), "%S", &t);
    gfx->setTextColor(FG, BG);
    bigText(hm, SCREEN_H/2 - 20, 4);
    bigText(sec, SCREEN_H/2 + 24, 2);
  }

  void renderDate() {
    header(timeIsSynced() ? "DATE" : "DATE ~approx");
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    char dow[12], dm[16], yr[8];
    strftime(dow, sizeof(dow), "%A", &t);
    strftime(dm,  sizeof(dm),  "%b %d", &t);
    strftime(yr,  sizeof(yr),  "%Y", &t);
    gfx->setTextColor(FG, BG);
    bigText(dow, SCREEN_H/2 - 30, 2);
    bigText(dm,  SCREEN_H/2,      3);
    bigText(yr,  SCREEN_H/2 + 34, 2);
  }

  void renderWeather() {
    header("WEATHER");
    gfx->setTextColor(FG, BG);
    if (!wx.ok) { bigText("no data", SCREEN_H/2, 2); return; }
    char tc[12]; snprintf(tc, sizeof(tc), "%d C", (int)round(wx.tempC));
    bigText(tc, SCREEN_H/2 - 30, 4);
    bigText(wxLabel(wx.code), SCREEN_H/2 + 10, 2);
    char wnd[20]; snprintf(wnd, sizeof(wnd), "wind %d", (int)round(wx.wind));
    bigText(wnd, SCREEN_H/2 + 40, 1);
  }

  void renderMoon() {
    header("MOON");
    // draw the phase as a shaded disc
    int mx = cx(), my = SCREEN_H/2 - 10, r = 40;
    gfx->drawCircle(mx, my, r, FG);
    // illuminate a fraction based on phase
    for (int y = -r; y <= r; y++) {
      int xw = (int)sqrt((float)(r*r - y*y));
      float p = moon.phase;                 // 0..1
      // terminator x position across the disc
      float term = cos(2*M_PI*p);           // -1..1
      int tx = (int)(term * xw);
      if (p < 0.5) gfx->drawFastHLine(mx - xw, my + y, xw + tx, FG);   // waxing: light right
      else         gfx->drawFastHLine(mx + tx, my + y, xw - tx, FG);   // waning: light left
    }
    gfx->setTextColor(FG, BG);
    bigText(moon.name, SCREEN_H/2 + 44, 1);
    char il[16]; snprintf(il, sizeof(il), "%d%% lit", moon.illum);
    bigText(il, SCREEN_H/2 + 58, 1);
  }

  // Word-wrap `text` at size `sz` and draw it centred (each line, and the block
  // vertically) in the area below the header. Shared by the quote and message
  // pages so both stay readable across the desk.
  void drawWrappedBlock(const char* text, uint8_t sz, uint16_t color) {
    const int charW = 6 * sz, lineh = 8 * sz + 4, maxw = SCREEN_W - 20;
    gfx->setTextColor(color, BG); gfx->setTextSize(sz);
    char tmp[160]; strlcpy(tmp, text, sizeof(tmp));

    String lines[8]; int nLines = 0;
    char* word = strtok(tmp, " ");
    String line = "";
    while (word && nLines < 8) {
      String test = line.length() ? line + " " + word : String(word);
      if ((int)test.length() * charW > maxw) { lines[nLines++] = line; line = word; }
      else line = test;
      word = strtok(nullptr, " ");
    }
    if (line.length() && nLines < 8) lines[nLines++] = line;

    int y = 34 + ((SCREEN_H - 34) - nLines * lineh) / 2;
    for (int i = 0; i < nLines; i++) {
      int w = (int)lines[i].length() * charW;
      gfx->setCursor((SCREEN_W - w) / 2, y);
      gfx->print(lines[i]);
      y += lineh;
    }
  }

  void renderQuote() {
    header("FOR YOU");
    drawWrappedBlock(quote, 2, FG);
  }

  // Live message page. Cyan text (colour, to stand out from the mono UI). While
  // a new message is flashing, the header pulses so it grabs attention; once the
  // flash ends the message is marked read and the red badge clears.
  void renderMessage(uint32_t now) {
    bool flashing = msgFlashUntil && now < msgFlashUntil;
    if (msgFlashUntil && now >= msgFlashUntil) { msgFlashUntil = 0; msgUnread = false; }

    header(flashing && ((now / 250) % 2) ? "* NEW MESSAGE *" : "MESSAGE");
    if (!hasMessage) { drawWrappedBlock("waiting for a message...", 2, FG); return; }
    drawWrappedBlock(message, 2, ACCENT);
    // Viewing the page clears the unread state even without a flash.
    if (!flashing) msgUnread = false;
  }

  void renderGitHub() {
    header("GITHUB");
    gfx->setTextColor(FG, BG);
    if (!gh.ok) { bigText("no data", SCREEN_H/2, 2); return; }
    char a[24], b[24];
    snprintf(a, sizeof(a), "%ld repos", gh.repos);
    snprintf(b, sizeof(b), "%ld followers", gh.followers);
    bigText(GH_USER, SCREEN_H/2 - 30, 2);
    bigText(a, SCREEN_H/2 + 4, 2);
    bigText(b, SCREEN_H/2 + 30, 1);
  }
};
