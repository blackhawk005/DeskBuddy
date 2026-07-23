// ============================================================================
// DeskBuddy_case.scad — parametric two-part snap case
// Waveshare ESP32-C6-Touch-LCD-1.47
//
// This is a STARTING POINT sized from the board's published outline. Measure your
// actual board + LiPo and adjust the params below, ideally cross-checking against
// Waveshare's official 2D/3D STEP file. Print in PETG. Render each part separately
// (set PART) and export STL.
// ============================================================================

PART = "both";     // "tray", "lid", or "both" (both = preview only)

// ---- Board (measure yours; these are nominal for the 1.47" C6 board) ----
board_w   = 36;    // board width  (X)
board_h   = 58;    // board length (Y)  — verify against STEP
board_th  = 1.6;   // PCB thickness
comp_clear= 4.0;   // clearance below PCB for back components / standoffs

// ---- Screen window (active area within the board) ----
scr_w     = 25;    // ~1.47" 172px width in mm (approx; verify)
scr_h     = 46;    // ~1.47" 320px height in mm (approx; verify)
scr_off_x = 0;     // window offset from board center X
scr_off_y = 2;     // window offset from board center Y

// ---- Battery pocket (measure your cell) ----
bat_w     = 30;
bat_h     = 40;
bat_th    = 6;     // cell thickness + slack

// ---- Case shell ----
wall      = 2.0;
floor_t   = 2.0;
lid_t     = 1.6;
fillet    = 3;     // corner rounding
snap_h    = 1.2;   // snap lip height

// ---- Fit tuning by process ----
// FDM prints loose (extrusion width); SLS nylon fuses slightly oversized and needs
// more relief on mating parts or the lid jams/cracks. Set PROCESS then re-export.
PROCESS = "sls";   // "fdm" or "sls"
fit_gap   = (PROCESS == "sls") ? 0.35 : 0.0;   // extra clearance per mating surface
snap_relief = (PROCESS == "sls") ? 0.35 : 0.0; // shrink snap lip so it seats, not binds

// ---- Engraving (recessed into front of lid, below the screen window) ----
// Placed in the solid band between the screen window's bottom edge and the lid
// bottom, so neither line collides with the window cutout.
engrave_line1 = "To Pannu,";
engrave_line2 = "From Shinti";
engrave_size  = 3.0;    // text height in mm (was 3.4; smaller so both lines clear)
engrave_depth = 0.8;    // recess depth (0.8 for SLS graininess; 0.6 fine for FDM/SLA)
engrave_y     = -26;    // centered in solid band below window (window bottom ≈ -21)
engrave_font  = "Liberation Sans:style=Bold";

// ---- Port / button cutouts (positions measured from board edges; EDIT) ----
usb_w = 9;  usb_h = 3.5; usb_from_bottom = 0;   // USB-C on bottom edge
btn_d = 3;                                        // RESET/BOOT holes on a side edge

// ---------------------------------------------------------------- helpers
inner_w = board_w + 1.0 + 2*fit_gap;   // 0.5 mm clearance each side (+SLS relief)
inner_h = board_h + 1.0 + 2*fit_gap;
inner_d = comp_clear + board_th + bat_th;   // stack: components + PCB + battery
outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;
outer_d = inner_d + floor_t + lid_t;

module rrect(w, h, r, th) {
  linear_extrude(th) offset(r=r) offset(r=-r) square([w, h], center=true);
}

// ---------------------------------------------------------------- TRAY
module tray() {
  difference() {
    // outer solid
    rrect(outer_w, outer_h, fillet, floor_t + inner_d);

    // inner cavity for board + battery
    translate([0,0,floor_t])
      rrect(inner_w, inner_h, fillet-0.5, inner_d + 1);

    // battery sub-pocket in the floor
    translate([0,0,-0.1])
      rrect(bat_w+1, bat_h+1, 2, floor_t + bat_th);

    // USB-C cutout on bottom edge (-Y)
    translate([0, -outer_h/2, floor_t + comp_clear + bat_th + usb_from_bottom])
      cube([usb_w, wall*3, usb_h], center=true);

    // RESET / BOOT holes on +X edge (adjust Y positions to your board)
    for (yy = [-8, 8])
      translate([outer_w/2, yy, floor_t + comp_clear + bat_th])
        rotate([0,90,0]) cylinder(h=wall*3, d=btn_d, center=true, $fn=24);

    // airflow slots near battery — widened to 1.8mm (from 1.4mm) with wider spacing
    // so the ribs between slots and the floor around them stay comfortably above
    // SLS Nylon's minimum wall guideline, not just barely over it.
    for (yy = [-8,0,8])
      translate([0, yy, -0.1]) cube([bat_w*0.5, 1.8, floor_t+0.4], center=true);
  }

  // PCB support shelf (holds board above the battery)
  // Ledge widened from 2.5mm to 3.2mm and inner fillet reduced relative to the
  // cavity fillet to avoid a thin-wall pinch point at the rounded corners where
  // two offset curves nearly intersect (root cause of Form Now's <0.6mm flag).
  shelf = comp_clear + bat_th;
  ledge = 3.2;
  difference() {
    translate([0,0,floor_t])
      rrect(inner_w, inner_h, fillet-0.5, shelf);
    translate([0,0,floor_t-0.1])
      rrect(inner_w-2*ledge, inner_h-2*ledge, 0.5, shelf+0.2);  // leave a lip ledge
  }
}

// ---------------------------------------------------------------- LID
module lid() {
  difference() {
    union() {
      rrect(outer_w, outer_h, fillet, lid_t);
      // snap lip that tucks inside the tray walls.
      // Overlap 0.4 mm up into the lid slab so the two fuse into ONE solid volume
      // (prevents the print service from seeing them as separate bodies / split prompt).
      translate([0,0,-snap_h])
        difference() {
          rrect(inner_w-0.3-2*snap_relief, inner_h-0.3-2*snap_relief, fillet-0.5, snap_h+0.4);
          translate([0,0,-0.1]) rrect(inner_w-2*wall, inner_h-2*wall, 1, snap_h+0.6);
        }
    }
    // screen window
    translate([scr_off_x, scr_off_y, -snap_h-0.1])
      rrect(scr_w, scr_h, 1.5, lid_t + snap_h + 0.2);

    // engraving recessed into the front (top) face of the lid
    // lid top face is at z = lid_t; cut downward from there
    translate([0, engrave_y + engrave_size*0.75, lid_t - engrave_depth])
      linear_extrude(engrave_depth + 0.1)
        text(engrave_line1, size=engrave_size, font=engrave_font,
             halign="center", valign="center");
    translate([0, engrave_y - engrave_size*0.75, lid_t - engrave_depth])
      linear_extrude(engrave_depth + 0.1)
        text(engrave_line2, size=engrave_size, font=engrave_font,
             halign="center", valign="center");
  }
}

// ---------------------------------------------------------------- render
if (PART == "tray") tray();
else if (PART == "lid") lid();
else {
  tray();
  translate([0,0,outer_d + 6]) rotate([180,0,0]) lid();  // exploded preview
}
