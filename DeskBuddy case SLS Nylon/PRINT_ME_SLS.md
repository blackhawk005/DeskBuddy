# DeskBuddy case — SLS Nylon files (for Form Now / Formlabs)

These STLs are re-tuned for **SLS Nylon 12**, not FDM. The mating clearances and snap
lip are loosened (0.35 mm relief per surface) so the lid actually seats on the tray
after SLS printing, and the engraving is cut a bit deeper (0.8 mm) so it reads through
the grainier nylon surface. **Use these, not the FDM STLs, if you order from Form Now.**

## Order these

Pick **one tray** + the **lid**:

| File | Use |
|---|---|
| `DeskBuddy_lid_SLS.stl` | Lid with engraving + screen window |
| `DeskBuddy_tray_battery_SLS.stl` | Tray with LiPo pocket (if adding a battery) |
| `DeskBuddy_tray_slim_SLS.stl` | Tray for USB-only (no battery) |

## On Form Now

1. Upload the lid + your chosen tray (STL, read in **mm** — confirm the lid shows
   ~**41.7 × 63.7 mm**, not 25× bigger).
2. Process/material: **SLS**, **Nylon 12** (sometimes listed as PA 12 / Nylon 12
   Powder). Do **not** pick a standard resin — nylon is the right call for a case
   around a warm battery.
3. Natural/grey nylon is standard; dyed black is often an option if you want it to
   match the mono screen.
4. Check the price at checkout — a two-piece nylon case will likely run more than the
   FDM quotes you saw. That's expected for SLS.

## Fit check when it arrives

SLS holds tight tolerances, so test the snap gently:
- Lid too tight / won't seat → increase `snap_relief` (try 0.5) in the `.scad` and
  re-export. Also nudge `fit_gap` up slightly.
- Lid too loose / falls off → decrease `snap_relief` toward 0.2.
- Board rattles in the tray → decrease `fit_gap`.

All of these are one-line changes at the top of `DeskBuddy_case.scad` (the `PROCESS`,
`fit_gap`, and `snap_relief` params). Tell me your measured board/battery and how the
first fit felt, and I'll dial it in exactly.

## Note

I sized these from the board's published outline, not a physical measurement, and the
USB-C/RESET/BOOT cutout positions are placeholders. For a paid SLS order it's worth
confirming those against your actual board first — a mis-placed port cutout in nylon
isn't fixable after printing the way it is with a hobby FDM reprint.
