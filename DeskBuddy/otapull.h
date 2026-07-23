// ============================================================================
// otapull.h  —  pull-based OTA from GitHub Releases
// (NOT named update.h: on a case-insensitive filesystem that shadows the ESP32
//  core's <Update.h>, which HTTPUpdate.h depends on — breaks the whole build.)
//
// "Push to GitHub, device updates itself" can't be a push from the cloud —
// GitHub's runners aren't on your LAN, and the device has no public address.
// So the device PULLS: a GitHub Action builds the firmware on every push and
// publishes it to the repo's "latest" release; the device polls that release
// and self-flashes when the published version differs from what it is running.
//
// Two tiny files on the release:
//   version.txt   the git SHA of that build (one line)
//   DeskBuddy.bin the firmware image
//
// The device compares version.txt against its own FW_VERSION. DIFFERENT (not
// "greater") triggers an update, so any new push wins — no version ordering to
// maintain. Repo is public, so no token is needed to download.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>        // must precede HTTPUpdate.h — it uses UpdateClass
#include <HTTPUpdate.h>
#include "version.h"

// GitHub repo that hosts the releases. dev account = blackhawk005.
#define OTA_REPO "blackhawk005/DeskBuddy"
#define OTA_VERSION_URL "https://github.com/" OTA_REPO "/releases/latest/download/version.txt"
#define OTA_BINARY_URL  "https://github.com/" OTA_REPO "/releases/latest/download/DeskBuddy.bin"

// How often to check once connected. First check runs shortly after boot.
#define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)   // every 6 hours

// Fetch the published version string, following GitHub's redirect to the CDN.
inline String otaFetchLatestVersion() {
  WiFiClientSecure client;
  client.setInsecure();                 // no CA bundle on-device; matches net.h
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // release URL 302s to the CDN
  http.setTimeout(8000);
  if (!http.begin(client, OTA_VERSION_URL)) return "";
  String v;
  if (http.GET() == HTTP_CODE_OK) { v = http.getString(); v.trim(); }
  http.end();
  return v;
}

// Check once. Returns true only if an update was applied (device then reboots,
// so a true return is effectively never seen). Safe to call when offline.
inline bool otaCheckOnce() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String latest = otaFetchLatestVersion();
  if (latest.length() == 0) {
    Serial.println("OTA: no version.txt yet (no release published?)");
    return false;
  }
  if (latest == FW_VERSION) {
    Serial.printf("OTA: up to date (%s)\n", FW_VERSION);
    return false;
  }

  Serial.printf("OTA: update %s -> %s, downloading...\n", FW_VERSION, latest.c_str());
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);      // reboot into the new image on success

  t_httpUpdate_return r = httpUpdate.update(client, OTA_BINARY_URL);
  switch (r) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA: FAILED (%d) %s\n",
                    httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      return false;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA: server reported no update");
      return false;
    default:
      return true;                      // unreachable — device reboots first
  }
}

// Call every loop; it self-throttles to OTA_CHECK_INTERVAL_MS. `firstDelayMs`
// lets the first check wait until after Wi-Fi/NTP settle at boot.
inline void otaTask(uint32_t firstDelayMs = 20000) {
  static uint32_t nextCheck = 0;
  static bool armed = false;
  uint32_t now = millis();
  if (!armed) { nextCheck = now + firstDelayMs; armed = true; }
  if (now >= nextCheck) {
    otaCheckOnce();
    nextCheck = now + OTA_CHECK_INTERVAL_MS;
  }
}
