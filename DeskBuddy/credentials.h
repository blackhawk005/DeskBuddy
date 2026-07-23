// ============================================================================
// credentials.h  —  runtime Wi-Fi credentials in NVS (persist across OTA)
//
// WHY: over-the-air updates replace the ENTIRE firmware image. If Wi-Fi creds
// were compiled in, every auto-built binary from CI would need them baked in —
// and since the release binaries live on a PUBLIC GitHub repo, that would
// publish the home Wi-Fi password to anyone who downloads the .bin.
//
// Instead, creds live in the device's own NVS flash (via Preferences), which
// OTA does NOT erase. They are provisioned ONCE and then survive every update:
//
//   * A locally built USB flash still has real creds in secrets.h. On boot, if
//     NVS is empty, those compile-time values are copied into NVS (one-time
//     provisioning). So flashing the current tree over USB provisions the device.
//   * CI-built OTA binaries ship an EMPTY secrets.h (see the workflow). They
//     never overwrite NVS; they just read whatever was provisioned earlier.
//   * `credsSet(ssid, pass)` over serial re-provisions without a recompile.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "secrets.h"

inline Preferences& credsStore() { static Preferences p; return p; }

struct WifiCreds { String ssid; String pass; };

// Load creds from NVS. If NVS is empty but secrets.h carries non-empty values
// (i.e. a local USB build), provision NVS from them once.
inline WifiCreds credsLoad() {
  credsStore().begin("deskbuddy", false);
  String ssid = credsStore().getString("ssid", "");
  String pass = credsStore().getString("pass", "");

  if (ssid.length() == 0 && strlen(WIFI_SSID) > 0) {
    ssid = WIFI_SSID;
    pass = WIFI_PASS;
    credsStore().putString("ssid", ssid);
    credsStore().putString("pass", pass);
    Serial.println("creds: provisioned NVS from compile-time secrets.h");
  }
  credsStore().end();

  WifiCreds c; c.ssid = ssid; c.pass = pass;
  return c;
}

// Message-feed URL, same NVS pattern as Wi-Fi creds so its secret token never
// lands in a public binary. Provisioned once from secrets.h MESSAGE_URL.
inline String msgUrlLoad() {
  credsStore().begin("deskbuddy", false);
  String url = credsStore().getString("msgurl", "");
  if (url.length() == 0 && strlen(MESSAGE_URL) > 0) {
    url = MESSAGE_URL;
    credsStore().putString("msgurl", url);
    Serial.println("creds: provisioned message URL into NVS from secrets.h");
  }
  credsStore().end();
  return url;
}

// Re-provision from a serial command, no recompile needed.
inline void credsSet(const String& ssid, const String& pass) {
  credsStore().begin("deskbuddy", false);
  credsStore().putString("ssid", ssid);
  credsStore().putString("pass", pass);
  credsStore().end();
  Serial.printf("creds: saved SSID \"%s\" to NVS (reboot to apply)\n", ssid.c_str());
}

// Poll serial for a "wifi <ssid> <pass>" provisioning line. Call from loop().
// Lets the device be pointed at a new network over USB without rebuilding.
inline void credsSerialTask() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.startsWith("wifi ")) {
        int sp = buf.indexOf(' ', 5);
        if (sp > 0) credsSet(buf.substring(5, sp), buf.substring(sp + 1));
        else Serial.println("usage: wifi <ssid> <password>");
      } else if (buf.startsWith("msgurl ")) {
        credsStore().begin("deskbuddy", false);
        credsStore().putString("msgurl", buf.substring(7));
        credsStore().end();
        Serial.println("creds: message URL saved to NVS (reboot to apply)");
      }
      buf = "";
    } else if (buf.length() < 128) {
      buf += c;
    }
  }
}
