# DeskBuddy — Project Handoff / Knowledge Transfer

This document is for **Claude Code** picking up this project fresh. It captures not
just what the project is, but the decisions already made and why, so you don't
re-litigate settled questions or contradict prior choices. Read this before touching
any files.

---

## What this is

A pocket desk companion built on the **Waveshare ESP32-C6 1.47" Touch Display
Development Board**. Monochrome animated face (blinks, 5 tap-triggered moods, IMU
eye drift), swipe-navigated pages (clock, date, Open-Meteo weather, moon phase,
rotating romantic quotes, GitHub stats), OTA wireless firmware updates, and a
custom 3D-printed enclosure with an engraved gift dedication.

**Owner:** Shinit (goes by "Shinti" in the case engraving — see Case section, this
is intentional, not a typo). Building this as a gift. Recipient: "Pannu."
Location: San Jose, CA, zip 95136.

---

## Project structure

```
DeskBuddy/
  DeskBuddy/
    DeskBuddy.ino       main sketch — display init, IMU, touch, gestures, scheduler, OTA hookup
    config.h             pin definitions + tunables (SOME PINS UNVERIFIED — see below)
    face.h                face renderer + all page renderers
    net.h                 WiFi, Open-Meteo, moon phase math, quotes, GitHub stats
    ota.h                 OTA update server (browser upload page + ArduinoOTA)
    secrets.h.example    template — copy to secrets.h (gitignored), never commit real one
  case/
    DeskBuddy_case.scad  parametric OpenSCAD source, single source of truth for all case geometry
    stl/                  FDM-tuned STL exports (PLA/PETG desktop printing)
    stl_sls/               SLS-tuned STL exports (for Formlabs "Form Now" nylon printing) ← ACTIVE
      DeskBuddy_lid_SLS.stl
      DeskBuddy_tray_battery_SLS.stl
      DeskBuddy_tray_slim_SLS.stl
      PRINT_ME_SLS.md
  README.md               general build/flash/battery/case guide (written for the human, not an agent)
```

---

## Hardware — pins (RESOLVED 2026-07-22, hardware-verified)

**All pins are now verified.** Board arrived, firmware is flashed and running.
Values below are confirmed three ways: Waveshare's published pin table, their
factory demo source (`Arduino/examples/01_gfx_helloworld` etc. in the demo zip),
and an on-hardware I2C/ADC scan (`diag/diag.ino`).

| Signal | Value | |
|---|---|---|
| `LCD_SCK` / `LCD_MOSI` | 1 / 2 | shared with the TF card slot |
| `LCD_CS` / `LCD_DC` | 14 / 15 | |
| `LCD_RST` | 22 | |
| `LCD_BL` | 23 | |
| `I2C_SDA` / `I2C_SCL` | 18 / 19 | scan found 2 devices here |
| `TOUCH_RST` / `TOUCH_INT` | 20 / 21 | were `-1`; they are broken out |
| `QMI8658_ADDR` | 0x6B | scan confirmed; touch answers at 0x63 |
| `BAT_ADC_PIN` | 0 | steady 1391 mV; every other ADC pin floats |
| `BAT_ADC_DIVIDER` | 3.0 | matches demo's `analogVolts * 3.0` |

**⚠️ The previous version of this section was wrong, and confidently so.** It
listed `DC=45, CS=21, SCK=38, MOSI=39, RST=47` as "confirmed from Waveshare's
factory GFX demo." Those are ESP32-**S3** pin numbers — the C6 has no GPIO above
30, so they cannot ever have been read off this board. Meanwhile the two pins the
old section flagged as unverified guesses (`LCD_BL=23`, `BAT_ADC_PIN=0`) were the
only ones that happened to be right.

**Lesson for whoever reads this next:** the one claim in this document labelled
"confirmed" was the only hardware claim that was false. Verify against
`docs.waveshare.com` or the demo zip before trusting any hardware assertion here.

**Board ordered:** Waveshare ESP32-C6 1.47" **Touch** Display Dev Board
(ASIN B0FC5LWVXG, ~4.5★, or a 3.6★ alternate listing at $25.99 — both same product,
confirmed by title text containing both "C6" and "Touch"). **Critical: any listing
missing the word "Touch" is a different, incompatible board** (no touchscreen) — this
was almost bought by mistake once already (see Decision Log).

---

## Decision log (chronological, don't reverse without reason)

1. **Board:** Waveshare ESP32-C6 1.47" **Capacitive Touch** — confirmed correct
   after nearly ordering a non-touch variant from the same product family. Always
   verify listing title has both "C6" AND "Touch."

2. **Wi-Fi/secrets:** `secrets.h` pattern, gitignored, template in
   `secrets.h.example`. Contains `WIFI_SSID`, `WIFI_PASS`, `OTA_PASSWORD`, optional
   `QUOTES_URL`.

3. **Romantic quotes:** shipped as an **original offline quote pack** (in `net.h`,
   `kQuotePack[]`) to avoid copyright issues and work with no network. `QUOTES_URL`
   is optional — if set, fetches a JSON array of strings over HTTPS instead.

4. **OTA added:** two update paths — browser upload page (device hosts a page at
   its own IP, shown on Serial at boot) and ArduinoOTA network port (shows up in
   Arduino IDE under Network Ports). Password-gated via `OTA_PASSWORD` in
   `secrets.h`. Libraries used (`ArduinoOTA`, `WebServer`, `Update`) are bundled
   with the ESP32 board package — nothing extra to install.

5. **Case manufacturing process: went from FDM (PETG) → SLS Nylon 12.** Owner
   chose Formlabs' "Form Now" on-demand service over local FDM shops. This changed
   fit tolerances — **`stl_sls/` is the active/correct folder to build from now,
   not `stl/`.** The FDM files in `stl/` are stale/legacy; don't delete them (owner
   may still want a cheap FDM copy later) but don't treat them as current.

6. **SLS fit tuning:** added `PROCESS`, `fit_gap` (0.35mm for SLS), and
   `snap_relief` (0.35mm for SLS) parameters to `DeskBuddy_case.scad` specifically
   because SLS nylon fuses slightly oversized and holds tighter tolerances than
   FDM — the original FDM clearances would have made the lid jam or crack on SLS.
   **If the owner reports the lid is too tight/loose after printing, these are the
   two parameters to adjust** (see `PRINT_ME_SLS.md` for the specific guidance
   already given).

7. **Battery decision: going WITHOUT a battery installed for now** ("I'll put
   battery later"), but the **Adafruit battery has since been ordered** (see Order
   Status — Order #3714053-4766703402, placed 2026-07-20). So: not installed yet,
   but no longer just a future intention — it's in transit. Firmware and case both
   already support this:
   - Case: ordered `DeskBuddy_tray_battery_SLS.stl` (the bigger tray, with the
     battery pocket) even though no battery is installed yet — chosen deliberately
     so the case doesn't need to be reprinted when a battery is added later. The
     pocket just sits empty; the board's shelf position is identical in both tray
     variants.
   - Firmware: runs fine on USB-only. `readBatteryPct()` in `DeskBuddy.ino` will
     read a floating/meaningless ADC value with nothing connected to `BAT_ADC_PIN`.
     **This was flagged as a known cosmetic issue, not yet fixed** — the owner was
     offered a fix (hide the battery icon / stub the reading when no battery is
     present) but the conversation moved on before it was implemented. **This is
     open work — see Open Items.**
   - Battery to use later: **Adafruit 3.7V 500mAh LiPo, Product ID 1578**
     (adafruit.com/product/1578, $5.48). Verified dimensions (29×36×4.75mm) fit
     the case pocket (30×40×6mm) with slack. Comes with JST-PH connector and
     protection circuitry pre-attached. Chosen over Amazon LiPo alternatives
     specifically because Adafruit's polarity is documented/consistent — the owner
     does not own a multimeter, so an unverified-polarity Amazon cell was
     considered too risky to recommend outright.
   - **Standing safety rule for whenever the battery IS added:** meter (or at
     minimum visually cross-check red/black wire against the board's silkscreen
     BAT +/- pad) before ever connecting it. Never assume polarity match across
     brands. This has been repeated multiple times in conversation — treat it as
     a hard requirement, not a suggestion, in any future instructions or docs
     touching the battery.

8. **Engraving added to lid:** "To Pannu," / "From Shinti" (exact casing/spelling
   intentional — owner explicitly said keep "Shinti", not "Shinit," even though
   that's a mismatch with the owner's actual name in `/profile.md`-style context.
   **Do not "fix" this spelling.**) Recessed 0.8mm into the lid's front face,
   in the solid band below the screen window.

9. **Engraving bug found and fixed:** first version had "To Pannu," positioned too
   close to the screen window cutout and it was getting visually clipped in the
   print preview (only "From Shinti" was readable). Root cause: text y-position
   overlapped the window's cutout region. Fixed by moving both lines down
   (`engrave_y` changed from -20 to -26) into the solid band below the window,
   and reduced text size slightly (3.4mm → 3.0mm) so both lines fit with margin.
   **Current `DeskBuddy_case.scad` has the fix — don't reintroduce the old
   `engrave_y = -20` value.**

10. **"Multiple Body Check" / split prompt on Form Now — investigated and
    resolved as a non-issue.** Owner hit Formlabs' upload-time popup warning about
    "2 bodies" and initially clicked "Split Model," which broke the lid into two
    disconnected flat fragments (bad). Root cause turned out to be a **false
    alarm**: OpenSCAD's CGAL "Volumes: N" render stat was misread mid-session as
    meaning N-1 disconnected solids, but calibration testing (single cube vs. two
    separate cubes) showed a single connected solid also reports "Volumes: 2" (the
    count includes the outer infinite space). **The lid IS a single valid solid
    body.** The correct instruction to the owner going forward: if Form Now's
    split-body popup appears again, click **"Keep as Assembly," never "Split
    Model."** No further geometry change was made for this issue — it was a
    UI/upload-flow correction, not a file fix (the file fix in item 9, for
    engraving position, happened in the same session and is unrelated).

---

## Order status (confirmed via email 2026-07-20 — both orders placed and verified)

**Case — Form Now / Formlabs, Order #FN-2536**
- Placed: 2026-07-20. Est. ship: 2026-07-23.
- Items: `DeskBuddy_lid_SLS.stl` (qty 1, $10.41) + `DeskBuddy_tray_battery_SLS.stl`
  (qty 1, $15.99). **Correct files** — the post-fix lid (engraving repositioned
  below the screen window, see Decision Log #9) and the battery tray.
- Total: $38.89 ($24.94 subtotal + $11.36 standard shipping + $2.59 tax).
- Ship-to: 4200 The Woods Dr, Apt 1608, San Jose CA 95136.
- Status at confirmation: "Order Submitted," Form Now reviewing files.
- **Known open risk:** confirmation email states they'll email "if we need
  anything from you" — Form Now's internal review may independently flag the
  same multi-body geometry that triggered the owner-side split-model popup
  (Decision Log #10). **If a follow-up email arrives asking to resolve a
  multi-body/assembly question, the answer is "Keep as Assembly," not "Split."**
  Watch for this — it wasn't resolved proactively, only reactively last time.

**Battery — Adafruit, Order #3714053-4766703402**
- Placed: 2026-07-20.
- Item: Lithium Ion Polymer Battery 3.7V 500mAh, ID 1578, qty 1, $7.95.
- Total: $27.07 ($7.95 item + $18.32 UPS Ground shipping + $0.80 tax). Shipping
  cost exceeds item cost — expected for a small single-item order, not an error.
- Ship-to: same San Jose address.
- This is the exact battery verified earlier to fit the case pocket
  (29×36×4.75mm cell vs. 30×40×6mm pocket) — correct item, correct order.

**Board (Amazon):** not yet confirmed via email/order search — last known state
was the owner comparing two listings (B0FC5LWVXG vs. a 3.6★ alternate at $25.99).
Verify with the owner or re-check email before assuming this is purchased.

**USB-C cable:** owner already has one, not a purchase item.

**Standing reminder for whenever the battery arrives:** do not connect it to the
board until polarity has been checked (meter, or at minimum visual red/black vs.
board silkscreen +/-) — this applies regardless of which battery arrived, Adafruit
included, since Adafruit's polarity guarantee is only against their own
boards/chargers, not Waveshare's.

---

## Open items / known gaps

1. **Battery-percentage UI with no battery installed.** Still open, but now
   measured rather than assumed. With no cell connected, `BAT_ADC_PIN` does *not*
   float randomly — it reads a rock-steady **1391 mV**, which `readBatteryPct()`
   turns into a constant **~97%**. So the symptom is a battery icon permanently
   pinned near full, not visible garbage. Cosmetic only; decide with the owner
   whether to hide the icon until a real cell is detected.

2. ~~Three CONFIRM-flagged pins~~ **RESOLVED 2026-07-22** — see the Hardware
   section above. All pins verified against Waveshare's docs, their demo source,
   and an on-hardware scan. `config.h` is correct.

3. **USB-C / RESET / BOOT cutout positions in the case** are placeholder
   coordinates in `DeskBuddy_case.scad`, sized from the board's *published*
   outline, not a physical measurement. Flagged multiple times as a risk for a
   paid SLS print (unlike cheap FDM, a bad port cutout in nylon isn't fixable
   after the fact). **Owner was offered a chance to measure the real board first
   and declined to block on it** — proceeded with placeholder positions. If a
   physical board is available now, measuring and correcting these before any
   further case orders would be worthwhile.

4. **Case order confirmed placed** (Order #FN-2536, 2026-07-20, est. ship 7/23) —
   no longer unconfirmed. Remaining uncertainty is only whether Form Now's
   internal review flags the multi-body geometry independently (see Order Status
   for the "Keep as Assembly" reminder if a follow-up email arrives).

5. **`stl/` (FDM) folder is stale.** Kept for reference but do not treat as
   representing the current design — `stl_sls/` has since diverged (fit tolerances,
   engraving position, engraving depth) and is the only up-to-date export set.
   If FDM printing is ever revisited, re-export from `DeskBuddy_case.scad` with
   `PROCESS="fdm"` rather than reusing the old `stl/` files, since they predate
   the engraving fix.

6. **Wi-Fi does not connect — `OurMane` is not reachable on 2.4 GHz.** The
   ESP32-C6 radio is 2.4 GHz only. An on-hardware scan (`diag_wifi/diag_wifi.ino`)
   found exactly one 2.4 GHz AP, at −92 dBm, and it was not `OurMane`. So the
   network is almost certainly 5 GHz-only, or band-steered under a single name.
   Everything offline (face, moods, touch, quote pack) works regardless; clock,
   date, weather, moon, GitHub stats and **OTA** stay dark until this is fixed.
   Fix: point the device at a 2.4 GHz SSID (many routers need a separate 2.4 GHz
   guest/IoT network enabled). Re-run `diag_wifi` and confirm the SSID actually
   appears in the scan list before re-flashing the main firmware.
   Secondary concern: seeing only *one* AP at −92 dBm in an apartment complex is
   unusually sparse, and might also indicate weak 2.4 GHz reception at that spot —
   worth re-scanning near the router before concluding it is purely a band issue.

7. **`OTA_PASSWORD` is still the template default `"change-me"`** in `secrets.h`.
   Anyone on the LAN who knows that string can reflash the device. Should be set
   to something real before the device is given away.

---

## Libraries (Arduino)

| Library | Source | Purpose |
|---|---|---|
| GFX_Library_for_Arduino (Moon Wang) | Library Manager, ≥1.5.9 | LCD driver |
| FastIMU (LiquidCGS) | Library Manager, ≥1.2.8 | QMI8658 IMU |
| ArduinoJson (Benoit Blanchon) | Library Manager, ≥7.x | JSON parsing |
| esp_lcd_touch_axs5106l | **Waveshare's board demo .zip — not in Library Manager** | AXS5106L touch |
| ArduinoOTA / WebServer / Update | Bundled with ESP32 board package | OTA updates |

Board package: esp32 by Espressif Systems ≥3.0.0, board = "ESP32C6 Dev Module".

---

## If you're picking this up to continue work

- Don't re-ask settled questions (SLS vs FDM, battery-now vs battery-later,
  "Shinti" vs "Shinit" spelling) — they're closed per the Decision Log.
- Do surface the Open Items list above proactively if relevant to whatever the
  owner asks next — they were left deliberately open, not forgotten.
- Any change touching `config.h` pin definitions should be flagged back to the
  owner as unverified unless you've confirmed it against real hardware or
  Waveshare's actual source — don't silently upgrade a guess to a confident claim.
- Any change to the case geometry should re-export **both** `stl/` (FDM) and
  `stl_sls/` (SLS) variants if both are meant to stay current, or explicitly note
  in this file if one is being deprecated.
