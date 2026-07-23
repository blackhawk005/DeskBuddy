// ============================================================================
// net.h  —  Wi-Fi + Open-Meteo weather + moon phase + romantic quotes + GitHub
// All calls are non-blocking-friendly: run them from a task/timer, not the face loop.
// ============================================================================
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include "config.h"
#include "secrets.h"
#include "clockseed.h"   // timeIsSynced() — set once NTP replaces the build-time seed

struct WxData   { float tempC=NAN; float wind=NAN; int code=-1; bool ok=false; };
struct MoonData { float phase=0; const char* name="—"; int illum=0; };
struct GhData   { long followers=0; long repos=0; long stars=0; bool ok=false; };

// ---- Built-in offline romantic quote pack (used when QUOTES_URL is empty).
// Original lines written for this project — safe to ship, no copyright issues.
static const char* kQuotePack[] = {
  "You are my favorite notification.",
  "Every timezone is better when it's yours.",
  "I'd reboot a thousand times to boot up next to you.",
  "You're the signal in all my noise.",
  "My favorite place is wherever you're buffering.",
  "You make my little heart overclock.",
  "In a world of defaults, you're my custom build.",
  "You're the only push notification I never mute.",
  "Home is a handshake away when it's you.",
  "You had me at hello world.",
};
static const int kQuoteCount = sizeof(kQuotePack)/sizeof(kQuotePack[0]);

// ------------------------------------------------------------ Wi-Fi
// Creds come from NVS at runtime (see credentials.h), NOT from compile-time
// macros, so OTA binaries can ship with no secrets.
inline bool netBegin(const String& ssid, const String& pass, uint32_t timeoutMs = 15000) {
  if (ssid.length() == 0) {
    Serial.println("WiFi: no credentials provisioned (send 'wifi <ssid> <pass>' over serial)");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
  if (WiFi.status() != WL_CONNECTED) return false;
  // NTP for clock/date pages. Overwrites the build-time seed from clockseed.h.
  configTzTime(BUILD_TZ, "pool.ntp.org", "time.nist.gov");

  // configTzTime is asynchronous, so wait briefly for a real answer rather than
  // claiming sync we don't have. Anything past 2024 can only have come from NTP;
  // the build-time seed is checked against, not assumed.
  time_t seeded = time(nullptr);
  uint32_t t1 = millis();
  while (millis() - t1 < 5000) {
    time_t now = time(nullptr);
    if (now > seeded + 2) {           // clock jumped -> NTP landed
      timeIsSynced() = true;
      Serial.println("clock: NTP synced");
      break;
    }
    delay(200);
  }
  if (!timeIsSynced()) Serial.println("clock: NTP did not respond, time stays approximate");
  return true;
}
inline bool netUp() { return WiFi.status() == WL_CONNECTED; }

// ------------------------------------------------------------ Open-Meteo (no key)
inline WxData fetchWeather() {
  WxData w;
  if (!netUp()) return w;
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
             + String(WX_LAT,4) + "&longitude=" + String(WX_LON,4)
             + "&current=temperature_2m,wind_speed_10m,weather_code&timezone=" + WX_TZ;
  HTTPClient http; http.begin(url);
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream())) {
      auto cur = doc["current"];
      w.tempC = cur["temperature_2m"] | NAN;
      w.wind  = cur["wind_speed_10m"] | NAN;
      w.code  = cur["weather_code"]   | -1;
      w.ok = true;
    }
  }
  http.end();
  return w;
}

// ------------------------------------------------------------ Live message feed
// Fetches the plain-text body from `url` (provisioned in NVS, see credentials.h).
// Returns true and fills `out` on success; leaves `out` untouched on failure so
// the last good message stays on screen.
inline bool fetchMessage(const String& url, char* out, size_t outSize) {
  if (!netUp() || url.length() == 0) return false;
  WiFiClientSecure client;
  client.setInsecure();                    // no CA bundle on-device
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setConnectTimeout(3000);            // cap the UI freeze on a bad network
  http.setTimeout(4000);
  if (!http.begin(client, url)) return false;
  bool ok = false;
  if (http.GET() == 200) {
    String body = http.getString();
    body.trim();
    if (body.length() > 0) { strlcpy(out, body.c_str(), outSize); ok = true; }
  }
  http.end();
  return ok;
}

// Map WMO weather codes to a compact label for the mono dashboard.
inline const char* wxLabel(int code) {
  if (code < 0) return "—";
  if (code == 0) return "Clear";
  if (code <= 3) return "Cloudy";
  if (code <= 48) return "Fog";
  if (code <= 67) return "Rain";
  if (code <= 77) return "Snow";
  if (code <= 82) return "Showers";
  if (code <= 99) return "Storm";
  return "—";
}

// ------------------------------------------------------------ Moon phase (offline calc)
// Conway-style approximation; good enough for a desk toy. 0=new, 0.5=full.
inline MoonData computeMoon(time_t now) {
  struct tm t; gmtime_r(&now, &t);
  int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
  if (m < 3) { y--; m += 12; }
  double jd = floor(365.25*(y+4716)) + floor(30.6001*(m+1)) + d - 1524.5;
  double days = jd - 2451549.5;           // days since known new moon
  double lun  = 29.53058867;
  double frac = fmod(days / lun, 1.0); if (frac < 0) frac += 1.0;
  MoonData md; md.phase = (float)frac;
  md.illum = (int)round(50.0 * (1 - cos(2*M_PI*frac)));  // 0..100 (approx)
  if      (frac < 0.03 || frac > 0.97) md.name = "New";
  else if (frac < 0.22) md.name = "Waxing Crescent";
  else if (frac < 0.28) md.name = "First Quarter";
  else if (frac < 0.47) md.name = "Waxing Gibbous";
  else if (frac < 0.53) md.name = "Full";
  else if (frac < 0.72) md.name = "Waning Gibbous";
  else if (frac < 0.78) md.name = "Last Quarter";
  else                  md.name = "Waning Crescent";
  return md;
}

// ------------------------------------------------------------ Romantic quotes
// Returns a quote into buf. Uses QUOTES_URL if set (HTTPS JSON array), else the
// offline pack. idx rotates through whatever source is available.
inline void fetchQuote(char* buf, size_t n, int idx) {
  #ifdef QUOTES_URL
  if (netUp() && strlen(QUOTES_URL) > 0) {
    WiFiClientSecure cli; cli.setInsecure();      // desk toy: skip cert pinning
    HTTPClient http; http.begin(cli, QUOTES_URL);
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getStream()) && doc.is<JsonArray>() && doc.size() > 0) {
        int i = idx % doc.size();
        strlcpy(buf, doc[i] | kQuotePack[0], n);
        http.end(); return;
      }
    }
    http.end();
  }
  #endif
  strlcpy(buf, kQuotePack[idx % kQuoteCount], n);
}

// ------------------------------------------------------------ GitHub public stats
inline GhData fetchGitHub() {
  GhData g;
  if (!netUp()) return g;
  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  http.begin(cli, String("https://api.github.com/users/") + GH_USER);
  http.addHeader("User-Agent", "DeskBuddy");
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream())) {
      g.followers = doc["followers"] | 0;
      g.repos     = doc["public_repos"] | 0;
      g.ok = true;
    }
  }
  http.end();
  return g;
}
