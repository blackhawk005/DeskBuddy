// ============================================================================
// diag_wifi.ino  —  why won't DeskBuddy join the network?
//
// The ESP32-C6 radio is 2.4 GHz only. If the target SSID is broadcast on 5 GHz
// (or is 5 GHz-only on a band-steering router that shares one name), the board
// physically cannot see it and WiFi.begin() just times out with no explanation.
//
// This scans for every visible 2.4 GHz AP, then attempts a real connection and
// prints the decoded failure status instead of a bare "failed".
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "../DeskBuddy/secrets.h"

static const char* statusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (network not visible on 2.4GHz)";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
    case WL_CONNECTED:       return "CONNECTED";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED (usually a wrong password)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED:    return "DISCONNECTED";
    default:                 return "unknown";
  }
}

void setup() {
  Serial.begin(115200);
  delay(15000);   // give the host capture tool time to attach

  Serial.println("\n\n=== WIFI DIAGNOSTIC ===");
  Serial.printf("Looking for SSID: \"%s\"\n", WIFI_SSID);
  Serial.printf("Password length: %d chars\n", (int)strlen(WIFI_PASS));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);

  Serial.println("\n--- scanning (2.4GHz only, this is all the C6 can see) ---");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  NO networks found at all. Antenna or radio problem.");
  } else {
    bool foundTarget = false;
    for (int i = 0; i < n; i++) {
      bool isTarget = (WiFi.SSID(i) == String(WIFI_SSID));
      if (isTarget) foundTarget = true;
      Serial.printf("  %2d) %-32s  ch%-3d  %4d dBm  %s%s\n",
                    i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                    WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "secured",
                    isTarget ? "   <<< TARGET" : "");
    }
    Serial.printf("\n  target SSID visible on 2.4GHz: %s\n", foundTarget ? "YES" : "NO");
    if (!foundTarget) {
      Serial.println("  -> The SSID is not reachable on 2.4GHz. Either it is a");
      Serial.println("     5GHz-only network, or the name differs (check case and");
      Serial.println("     trailing spaces). Compare against the list above.");
    }
  }

  Serial.println("\n--- attempting connection (20s) ---");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  wl_status_t last = WL_IDLE_STATUS;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    wl_status_t s = WiFi.status();
    if (s != last) { Serial.printf("  status -> %s\n", statusName(s)); last = s; }
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  CONNECTED");
    Serial.printf("  IP:      %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("  RSSI:    %d dBm\n", WiFi.RSSI());
    Serial.println("\n  -> Open http://" + WiFi.localIP().toString() + " for OTA updates.");
  } else {
    Serial.printf("\n  FAILED. Final status: %s\n", statusName(WiFi.status()));
  }
  Serial.println("\n=== DONE ===");
}

void loop() { delay(5000); }
