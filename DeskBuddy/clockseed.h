// ============================================================================
// clockseed.h  —  seed the system clock from build time
//
// Without this, time(nullptr) returns 0 until NTP succeeds, so the clock page
// shows 00:00 and the date page shows Jan 1 1970. Since this device is often
// offline (the C6 has no 5GHz radio, so a 5GHz-only network leaves it
// permanently disconnected), that is the normal case, not the exception.
//
// Seeding from __DATE__/__TIME__ makes the clock plausible the moment it powers
// on. It is only ever as fresh as the last flash and does not survive a power
// cycle with any added accuracy, so it is explicitly APPROXIMATE — the UI marks
// it with a '~' until NTP replaces it. Borrowed from the schematic.io DeskBuddy
// variant, which seeds this way instead of relying on NTP.
//
// Unlike that version, this writes into the real system clock via settimeofday()
// rather than tracking a separate millis() offset, so every existing
// time(nullptr) caller (clock, date, moon phase) benefits with no changes.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

// Must match the TZ string used for NTP in net.h, or the clock jumps when NTP
// lands.
#define BUILD_TZ "PST8PDT,M3.2.0,M11.1.0"

// True once NTP has actually set the time; until then the clock is the build
// time plus uptime, which can be arbitrarily stale.
inline bool& timeIsSynced() { static bool synced = false; return synced; }

inline uint8_t buildMonthNumber() {
  static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (uint8_t i = 0; i < 12; i++) {
    if (strncmp(__DATE__, names + i * 3, 3) == 0) return i + 1;
  }
  return 1;
}

inline void seedClockFromBuildTime() {
  setenv("TZ", BUILD_TZ, 1);
  tzset();

  // __DATE__ is "Mmm dd yyyy" — day is space-padded for single digits.
  // __TIME__ is "hh:mm:ss".
  struct tm t = {};
  t.tm_year = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
              (__DATE__[9] - '0') * 10   + (__DATE__[10] - '0') - 1900;
  t.tm_mon  = buildMonthNumber() - 1;
  t.tm_mday = (__DATE__[4] == ' ' ? 0 : (__DATE__[4] - '0')) * 10 + (__DATE__[5] - '0');
  t.tm_hour = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
  t.tm_min  = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');
  t.tm_sec  = (__TIME__[6] - '0') * 10 + (__TIME__[7] - '0');
  t.tm_isdst = -1;                       // let the TZ rules decide

  time_t epoch = mktime(&t);
  if (epoch <= 0) {
    Serial.println("clock: build-time seed FAILED, staying at epoch 0");
    return;
  }

  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  settimeofday(&tv, nullptr);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&epoch));
  Serial.printf("clock: seeded from build time -> %s (approximate)\n", buf);
}
