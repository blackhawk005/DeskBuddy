// ============================================================================
// festivals.h  —  special-day calendar (festivals + personal dates)
//
// On a matching day the home greeting becomes a festive message in a themed
// colour and the face is tinted to match. Two kinds of entries:
//   year == 0   -> recurs EVERY year on (month, day)   [birthdays, anniversary]
//   year == YYYY-> that Gregorian year only             [lunar festivals]
//
// LUNAR FESTIVALS SHIFT EACH YEAR. The 2026 dates below were looked up for this
// build; add new rows (or bump the years) to keep them accurate. When a year has
// no matching row, that festival simply doesn't fire — no wrong dates shown.
// Sources: India TV / Hindutone 2026 calendars; Navratri colours per SmartPuja.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <time.h>
#include <Arduino_GFX_Library.h>

// Sharad Navratri 2026 day-wise colours (Day 1..9).
static const uint16_t kNavratriColors[9] = {
  RGB565_ORANGE,          // Day 1  Orange
  RGB565_WHITE,           // Day 2  White
  RGB565_RED,             // Day 3  Red
  RGB565(30, 60, 210),    // Day 4  Royal Blue
  RGB565_YELLOW,          // Day 5  Yellow
  RGB565_GREEN,           // Day 6  Green
  RGB565(150, 150, 150),  // Day 7  Grey
  RGB565_PURPLE,          // Day 8  Purple
  RGB565(0, 150, 150),    // Day 9  Peacock Green
};

struct Festival {
  int          year;       // 0 = every year; else a specific Gregorian year
  uint8_t      month;      // 1-12
  uint8_t      day;        // first day
  uint8_t      span;       // number of days (1 = single day)
  const char*  greeting;   // "Day N" is appended automatically when span > 1
  uint16_t     color;      // theme colour (ignored if dayColors is set)
  const uint16_t* dayColors; // per-day colours (Navratri); nullptr otherwise
};

static const Festival kFestivals[] = {
  // ---- Personal fixed dates (every year) -----------------------------------
  {   0,  1, 14, 1, "Happy Makar Sankranti, Pannu!", RGB565_YELLOW,  nullptr },
  {   0,  4, 26, 1, "Happy Birthday, Amma!",          RGB565_PINK,    nullptr },
  {   0,  5, 15, 1, "Happy Anniversary, Pannu!",      RGB565_RED,     nullptr },
  {   0,  7, 28, 1, "Happy Birthday, Pannu!",         RGB565_MAGENTA, nullptr },
  {   0, 10, 21, 1, "Happy Birthday, Appa!",          RGB565_CYAN,    nullptr },
  // ---- Lunar festivals — 2026 dates (verify & extend for later years) ------
  {2026,  2, 15, 1, "Happy Maha Shivratri, Pannu!",   RGB565_WHITE,   nullptr },
  {2026,  3,  4, 1, "Happy Holi, Pannu!",             RGB565_MAGENTA, nullptr },
  {2026,  3, 19, 1, "Happy Ugadi, Pannu!",            RGB565_GREEN,   nullptr },
  {2026,  3, 26, 1, "Happy Ram Navami, Pannu!",       RGB565_ORANGE,  nullptr },
  {2026,  4,  2, 1, "Happy Hanuman Jayanti, Pannu!",  RGB565_ORANGE,  nullptr },
  {2026,  8, 25, 1, "Happy Janmashtami, Pannu!",      RGB565(30,60,210), nullptr },
  {2026,  9, 14,10, "Happy Ganesh Chaturthi",        RGB565_ORANGE,  nullptr },
  {2026, 10, 11, 9, "Happy Navratri",                0,              kNavratriColors },
  {2026, 10, 20, 1, "Happy Dussehra, Pannu!",         RGB565_RED,     nullptr },
  {2026, 11,  8, 1, "Happy Deepavali, Pannu!",        RGB565_ORANGE,  nullptr },
};
static const int kFestivalCount = sizeof(kFestivals) / sizeof(kFestivals[0]);

// If today matches a festival, fill greetingOut (with " Day N" for multi-day)
// and colorOut, and return true. First match wins.
inline bool festivalToday(const struct tm& t, char* greetingOut, size_t goSize,
                          uint16_t* colorOut) {
  int y = t.tm_year + 1900, mo = t.tm_mon + 1, d = t.tm_mday;
  for (int i = 0; i < kFestivalCount; i++) {
    const Festival& f = kFestivals[i];
    if (f.year != 0 && f.year != y) continue;
    if (mo != f.month) continue;
    if (d < f.day || d >= f.day + f.span) continue;
    int idx = d - f.day;
    *colorOut = f.dayColors ? f.dayColors[idx] : f.color;
    if (f.span > 1) snprintf(greetingOut, goSize, "%s Day %d", f.greeting, idx + 1);
    else            snprintf(greetingOut, goSize, "%s", f.greeting);
    return true;
  }
  return false;
}
