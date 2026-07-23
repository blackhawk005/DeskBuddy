// ============================================================================
// ota.h  —  Over-the-air firmware updates for DeskBuddy
//
// Two ways to update, both over Wi-Fi (no USB cable needed after the first flash):
//
//   A) WEB BROWSER (easiest): DeskBuddy hosts a tiny upload page on its own IP.
//      Open http://<deskbuddy-ip>/ in a browser, pick a compiled .bin, click
//      Update. The IP prints to Serial on boot and shows on the boot screen.
//
//   B) ARDUINO IDE network port: with ArduinoOTA running, the board appears under
//      Tools > Port > Network Ports. Select it and Upload wirelessly.
//
// To make a .bin for option A: in Arduino IDE, Sketch > Export Compiled Binary,
// then grab the .bin from the sketch's build folder.
//
// Security note: this is LAN-only and protected by a password you set in secrets.h
// (OTA_PASSWORD). Anyone on your network who knows it can reflash the device, so
// use a real password and keep it off untrusted Wi-Fi.
// ============================================================================
#pragma once
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>
#include "secrets.h"

static WebServer otaServer(80);

// Minimal upload page for browser-based flashing
static const char* OTA_PAGE =
  "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
  "<title>DeskBuddy OTA</title><style>body{font-family:sans-serif;background:#111;"
  "color:#eee;max-width:420px;margin:40px auto;padding:0 16px}h2{font-weight:600}"
  "input[type=file]{width:100%;margin:12px 0}button{background:#eee;color:#111;"
  "border:0;padding:10px 16px;border-radius:6px;font-size:16px;width:100%}"
  ".bar{height:8px;background:#333;border-radius:4px;margin-top:14px;overflow:hidden}"
  ".fill{height:100%;width:0;background:#4ade80;transition:width .2s}</style></head>"
  "<body><h2>DeskBuddy firmware update</h2>"
  "<p>Pick a compiled <code>.bin</code> and upload.</p>"
  "<input id=f type=file accept='.bin'>"
  "<button onclick='go()'>Update</button>"
  "<div class=bar><div id=fill class=fill></div></div><p id=s></p>"
  "<script>function go(){var f=document.getElementById('f').files[0];"
  "if(!f){alert('Choose a .bin first');return;}var fd=new FormData();fd.append('u',f);"
  "var x=new XMLHttpRequest();x.open('POST','/update');"
  "x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);"
  "document.getElementById('fill').style.width=p+'%';document.getElementById('s').innerText=p+'%';}};"
  "x.onload=function(){document.getElementById('s').innerText=x.responseText;"
  "if(x.status==200)setTimeout(function(){document.getElementById('s').innerText='Rebooting…';},500);};"
  "x.send(fd);}</script></body></html>";

// Optional callback so the UI can show update status on the LCD.
typedef void (*OtaStatusCb)(const char* msg, int pct);
static OtaStatusCb otaCb = nullptr;

inline void otaBegin(const char* hostname = "deskbuddy", OtaStatusCb cb = nullptr) {
  otaCb = cb;

  // ---- Option B: ArduinoOTA (IDE network port)
  ArduinoOTA.setHostname(hostname);
  #ifdef OTA_PASSWORD
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  #endif
  ArduinoOTA.onStart([]() { if (otaCb) otaCb("OTA start", 0); });
  ArduinoOTA.onEnd([]()   { if (otaCb) otaCb("OTA done", 100); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    if (otaCb) otaCb("Updating", (int)(p * 100 / t));
  });
  ArduinoOTA.onError([](ota_error_t e) { if (otaCb) otaCb("OTA error", -1); });
  ArduinoOTA.begin();

  // ---- Option A: browser upload page
  otaServer.on("/", HTTP_GET, []() {
    otaServer.send(200, "text/html", OTA_PAGE);
  });
  otaServer.on("/update", HTTP_POST,
    []() {  // completion handler
      bool ok = !Update.hasError();
      otaServer.send(200, "text/plain", ok ? "OK — rebooting" : "FAILED");
      if (ok) { delay(600); ESP.restart(); }
    },
    []() {  // upload handler (streamed chunks)
      HTTPUpload& up = otaServer.upload();
      if (up.status == UPLOAD_FILE_START) {
        #ifdef OTA_PASSWORD
          // simple gate: require ?pw= on the URL if a password is set
          if (strlen(OTA_PASSWORD) > 0 && otaServer.arg("pw") != OTA_PASSWORD) {
            if (otaCb) otaCb("OTA denied", -1);
            return;
          }
        #endif
        if (otaCb) otaCb("Receiving", 0);
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
        if (otaCb) otaCb("Writing", -2);
      } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
        if (otaCb) otaCb("Flashed", 100);
      }
    });
  otaServer.begin();
}

// Call this frequently from loop() — it's cheap and non-blocking.
inline void otaHandle() {
  ArduinoOTA.handle();
  otaServer.handleClient();
}
