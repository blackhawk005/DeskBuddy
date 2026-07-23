# Tiny ESP DeskBuddy

A pocket desk companion on the **Waveshare ESP32-C6-Touch-LCD-1.47**: an animated
monochrome face that blinks, reacts to taps with moods, and drifts its eyes as you
tilt it (IMU). Swipe through clock, date, weather, moon phase, a rotating romantic
quote, and your GitHub stats. Runs on USB or a small LiPo.

## What it does

- **Face dashboard** — white-on-black eyes + mouth, idle blinking, 5 moods.
- **Touch swipes** — left/right swipe changes page; tap the face to cycle mood; tap elsewhere to go home.
- **IMU eye drift** — QMI8658 accelerometer nudges the pupils toward "down."
- **Clock / Date** — NTP-synced.
- **Weather** — Open-Meteo current temp + condition (no API key needed).
- **Moon phase** — computed on-device (works offline), shown as a shaded disc.
- **Romantic quotes** — built-in offline pack, or fetch your own from an HTTPS JSON URL.
- **GitHub stats** — public repo + follower counts for one user (no token).
- **Wireless updates (OTA)** — reflash over Wi-Fi from a browser or the Arduino IDE, no cable needed after the first flash.

## Hardware

- Waveshare ESP32-C6-Touch-LCD-1.47 (JD9853 LCD, AXS5106L touch, QMI8658 IMU, ETA6098 charger)
- 1-cell LiPo, 3.7 V nominal, with **JST-PH 1.25 or 1.0** connector to match the board's BAT pad — **confirm polarity before plugging in** (see Battery below)
- USB-C cable
- Enclosure (buy or 3D-print, see below)

## Files

```
DeskBuddy/
  DeskBuddy.ino      main sketch: display, touch, IMU, gestures, scheduler
  config.h           pins + tunables  (EDIT the pins flagged "CONFIRM")
  face.h             monochrome face + all page renderers
  net.h              Wi-Fi, Open-Meteo, moon math, quotes, GitHub
  secrets.h.example  copy to secrets.h and fill in Wi-Fi
```

## Libraries

Install via Arduino Library Manager unless noted:

| Library | Version | Purpose |
|---|---|---|
| GFX_Library_for_Arduino (Moon Wang) | ≥ 1.5.9 | LCD driver |
| FastIMU (LiquidCGS) | ≥ 1.2.8 | QMI8658 IMU |
| ArduinoJson (Benoit Blanchon) | ≥ 7.x | JSON parsing |
| esp_lcd_touch_axs5106l (Waveshare) | — | **Offline** — from the board's demo .zip |

Board package: **esp32 by Espressif Systems ≥ 3.0.0**. Select board
**"ESP32C6 Dev Module"**.

The AXS5106L touch library isn't in the Library Manager — it lives in
`ESP32-C6-Touch-LCD-1.47-Demo/Arduino/libraries` inside Waveshare's demo download.
Copy it into your Arduino `libraries/` folder. If you'd rather skip touch, set
`#define USE_TOUCH 0` in the sketch and the BOOT button cycles pages instead.

## Setup

1. `cp DeskBuddy/secrets.h.example DeskBuddy/secrets.h` and fill in `WIFI_SSID` / `WIFI_PASS`.
2. In `config.h`, set `WX_LAT`/`WX_LON`/`WX_TZ` and `GH_USER`.
3. **Verify the three pin groups flagged `CONFIRM` in `config.h`** against the board's
   factory demo — backlight (`LCD_BL`), I2C (`I2C_SDA`/`I2C_SCL`), and battery ADC
   (`BAT_ADC_PIN`). The LCD SPI pins (DC 45, CS 21, SCK 38, MOSI 39, RST 47) and the
   34-px column offset are taken directly from Waveshare's published GFX demo and
   should be correct. The others are the common defaults for this board family but
   weren't in the doc excerpt I verified, so a 30-second check saves a debugging hour.
4. Wi-Fi is optional. With no network, DeskBuddy still runs the face, clock (after
   first NTP sync), moon phase, and the offline quote pack.

## Flashing

1. Board: **ESP32C6 Dev Module**. USB CDC On Boot: **Enabled**. Flash size: **8MB**.
2. Plug in USB-C, pick the COM port, hit Upload.
3. **If upload fails or the port won't appear:** hold **BOOT**, tap **RESET** (or
   plug in USB while holding BOOT), release BOOT — this forces download mode and
   fixes the large majority of flashing problems on this board.
4. First compile is slow (Arduino builds the whole ESP32 core once). Be patient.
5. Mac users: if flashing fails, install the CH34x serial driver.

## Wireless updates (OTA)

After the **first** USB flash, you can push new firmware over Wi-Fi — no cable.

Set `OTA_PASSWORD` in `secrets.h` to a real password first (anyone on your LAN who
knows it can reflash the device).

**Two ways to update:**

1. **Browser (easiest).** On boot, DeskBuddy prints its IP to Serial (e.g.
   `http://192.168.1.42`). Open that in any browser on the same Wi-Fi, choose a
   compiled `.bin`, and hit Update. To make the `.bin`: in Arduino IDE,
   *Sketch → Export Compiled Binary*, then grab the `.bin` from the build folder.
2. **Arduino IDE network port.** With the board running, it shows up under
   *Tools → Port → Network Ports* as `deskbuddy`. Select it and Upload as usual;
   enter the OTA password when prompted.

OTA is LAN-only by design. Don't run it on untrusted/public Wi-Fi, and keep a real
password set. If an OTA ever fails mid-flash, just reconnect USB and flash normally —
the board can't be bricked by a failed OTA (it keeps the old image until the new one
is verified).

Libraries for OTA (bundled with the ESP32 board package, nothing extra to install):
`ArduinoOTA`, `WebServer`, `Update`.

## Battery & charging — safety

The board has an onboard **ETA6098** LiPo charger and a **VBAT** pad.

- **Polarity is on you.** The board silkscreen marks BAT +/–. A reversed cell can
  vent or catch fire. Meter the cell and match +/– before the first connection.
- **Never connect VBAT to 3V3 or VBUS.** Per Waveshare: VBAT is battery-positive
  only, battery-negative to GND. Bridging it to a regulated rail can destroy the
  board or the cell.
- Use a **protected** 1-cell LiPo (built-in protection PCB) rated ~300–800 mAh for
  this size class. Protection guards against over-discharge and short.
- Charge the first time **on USB while you watch it**, on a non-flammable surface.
  Don't leave a bare LiPo charging unattended.
- Don't crush, puncture, or fold the cell into the case. Leave slack; route wires
  away from pins. If a cell ever puffs, stop using it.
- Operating range for most LiPos is roughly 0–45 °C charge / -20–60 °C discharge —
  keep it off radiators and out of hot cars.
- The `readBatteryPct()` curve is a rough 3.3–4.2 V linear map. Calibrate
  `BAT_ADC_DIVIDER` against a multimeter for an accurate gauge.

This is general guidance, not a substitute for the safety sheet that comes with your
specific cell — follow the manufacturer's limits if they differ.

## Enclosure

**Buy-it option.** Waveshare and its resellers sell display/board-specific acrylic
or ABS cases for their dev boards; search "ESP32-C6-Touch-LCD-1.47 case." Third-party
maker shops (Amazon/AliExpress) also list clip-on frames for the 1.47" module. Check
that the listing names *this exact board*, since the 1.47" screen ships on several
different Waveshare/others boards with different footprints.

**3D-print option.** Waveshare publishes an official **2D/3D STEP file** for this
board — the "ESP32-C6-Touch-LCD-1.47 2D/3D file" download on their wiki Resources
section. Import that STEP into Fusion 360 / FreeCAD / Onshape and shell a two-part
snap case around it. That's the reliable path, because you're modeling to the real
board outline and connector positions rather than guessing.

Print/design tips:
- Material **PETG** over PLA — PLA softens on a warm desk or in sun and can sag
  around a charging LiPo. PETG is tougher and more heat-tolerant.
- Leave a **cutout for USB-C**, the **RESET/BOOT buttons**, and a slot for **airflow**
  near the battery. Don't seal a LiPo in an airtight box.
- Add a **1–2 mm standoff shelf** so the board doesn't press its back pins into the
  cell, and a shallow pocket sized to your cell with a little slack.
- A friction-fit or M2 screw-boss lid beats glue — you'll want to open it to service
  the battery.
- Recess the screen bezel slightly to protect the glass; a thin clear window or open
  cutout both work.

I've included a ready-to-print `case/DeskBuddy_case.scad` as a **parametric starting
point** — set your measured board and battery dimensions at the top and it generates a
two-part case. Treat it as a template to refine against the official STEP, not a
drop-in final part, since I sized it from the published 1.47" board dimensions rather
than a physical measurement.

## Tuning

- Face frame rate, blink interval, and network refresh cadence are all in `config.h`.
- Add pages by extending the `Page` enum and adding a `renderX()` in `face.h`.
- Quote source: leave `QUOTES_URL` empty for offline, or point it at any HTTPS
  endpoint returning a JSON array of strings.
