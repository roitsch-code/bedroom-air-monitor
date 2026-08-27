// Bedroom Air Monitor — sensor enclosure, first rough draft.
//
// Open this in OpenSCAD (free: openscad.org) to view and export an STL. This is a CONCEPT model —
// dimensions marked "verified" come from Adafruit/Sensirion's own specs; anything marked
// "estimated" is a guess and should be checked against the real part before printing final.
//
// Layout, left to right: [ QT Py bay ][ SCD-41 bay, vented front ][ SEN54 module bay, vented front + side ]
// Two open compartments (no dividing wall to the ceiling — STEMMA QT cables run between bays), one
// lid. USB-C cable exits the back. SEN54's JST-GH pigtail exits through a slot into its own bay.

/* [Part to render] */
part = "both"; // [base, lid, both]

/* [Wall & fit] */
wall = 2.2;           // mm, printed wall thickness
floor = 2.0;          // mm, base floor thickness
lid_thickness = 2.0;
fit_gap = 0.3;         // mm, clearance between lid lip and base wall (per side)
corner_r = 3;          // outer corner rounding

/* [QT Py — verified 21.8 x 17.9 x 5.7mm, Adafruit #5325] */
qtpy_l = 21.8;
qtpy_w = 17.9;
qtpy_h = 5.7;
qtpy_clear = 4;        // mm clearance around the board for its bay

/* [SCD-41 breakout — verified 25.5 x 22.8 x 7.7mm, Adafruit #5190] */
scd_l = 25.5;
scd_w = 22.8;
scd_h = 7.7;
scd_clear = 5;

/* [SEN54 module — verified 52.3 x 43.3 x 22.3mm, Sensirion datasheet] */
sen_l = 52.3;
sen_w = 43.3;
sen_h = 22.3;
sen_tail_l = 17;       // mm, JST-GH connector tail sticking out one corner (estimated from drawing)
sen_clear = 6;         // more clearance: fan needs open air, not a snug fit

/* [Derived bay sizes — internal, before walls] */
qtpy_bay_l = qtpy_l + qtpy_clear;
qtpy_bay_w = qtpy_w + qtpy_clear;
scd_bay_l  = scd_l + scd_clear;
scd_bay_w  = scd_w + scd_clear;
sen_bay_l  = sen_l + sen_clear + sen_tail_l;
sen_bay_w  = sen_w + sen_clear;

inner_h = max(qtpy_h, scd_h, sen_h) + 6;   // headroom above the tallest part for cables/lid clearance
case_h  = floor + inner_h + wall;           // outer height of the base

// Internal length is the three bays side by side, separated by thin internal ribs (not full walls —
// STEMMA QT cables need to pass between them).
rib = 1.5;
inner_l = qtpy_bay_l + rib + scd_bay_l + rib + sen_bay_l;
inner_w = max(qtpy_bay_w, scd_bay_w, sen_bay_w);

case_l = inner_l + 2 * wall;
case_w = inner_w + 2 * wall;

echo(str("Outer footprint: ", case_l, " x ", case_w, " x ", case_h, " mm"));

/* [Vent slots] */
slot_w = 1.6;
slot_gap = 1.6;
slot_margin = 4;

module vent_slots(panel_w, panel_h, count) {
  // A row of horizontal slots, centered, for a passive/active air vent on a front panel.
  total_w = count * slot_w + (count - 1) * slot_gap;
  start_x = -total_w / 2;
  for (i = [0 : count - 1])
    translate([start_x + i * (slot_w + slot_gap), -panel_h / 2 + slot_margin, -1])
      cube([slot_w, panel_h - 2 * slot_margin, wall + 2]);
}

// A 2D rounded-rectangle outline — used as the profile for linear_extrude below. Must stay 2D
// (circle, not cylinder) or linear_extrude silently drops it.
module rounded_rect(l, w, r) {
  hull() {
    for (x = [r, l - r])
      for (y = [r, w - r])
        translate([x, y, 0]) circle(r = r, $fn = 32);
  }
}

module screw_boss(h) {
  difference() {
    cylinder(h = h, d = 7, $fn = 24);
    translate([0, 0, -0.5]) cylinder(h = h + 1, d = 2.6, $fn = 16); // M3 self-tap pilot hole
  }
}

module case_base() {
  x_qtpy = wall;
  x_scd  = x_qtpy + qtpy_bay_l + rib;
  x_sen  = x_scd + scd_bay_l + rib;

  difference() {
    // Outer shell
    linear_extrude(height = case_h)
      rounded_rect(case_l, case_w, corner_r);

    // Hollow out the inside, leaving `wall` on the sides and `floor` on the bottom. Screw bosses
    // and ribs are added back AFTER this cut (below) — adding them before, inside the same union
    // that gets hollowed out, would have deleted them: they sit inside the exact region this cube
    // removes, and subtracting the same cube from a union subtracts it from every member of that
    // union too. (Caught this by test-exporting the STL and checking the boss holes existed.)
    translate([wall, wall, floor])
      cube([case_l - 2 * wall, case_w - 2 * wall, case_h]);

    // USB-C cable exit, back wall, centered on the QT Py bay
    translate([x_qtpy + qtpy_bay_l / 2, -1, floor + 2])
      cube([9, wall + 2, 4], center = false);

    // SCD-41 bay: passive vent slots on the front wall
    translate([x_scd + scd_bay_l / 2, 0, floor + inner_h / 2])
      rotate([90, 0, 0])
        vent_slots(scd_bay_w, inner_h - 4, 5);

    // SEN54 bay: intake slots on the front wall
    translate([x_sen + sen_l / 2, 0, floor + inner_h / 2])
      rotate([90, 0, 0])
        vent_slots(sen_w - 6, inner_h - 4, 7);

    // SEN54 bay: fan grille on the right side wall (round cutout, roughly matching the module's
    // fan opening per the datasheet drawing — position is an ESTIMATE, adjust once the module is
    // in hand and you can see exactly where the fan opening lines up)
    translate([case_l - 1, wall + sen_bay_w * 0.65, floor + inner_h / 2])
      rotate([0, 90, 0])
        cylinder(h = wall + 2, d = 20, $fn = 36);

    // JST-GH pigtail exit slot, back wall of the SEN54 bay
    translate([x_sen + 4, -1, floor + inner_h - 6])
      cube([10, wall + 2, 5]);
  }

  // Rebuild the two internal ribs (thin, with a gap near the top for cables to cross over).
  // Overlap slightly into the front/back walls and the floor (0.2mm) rather than touch them
  // exactly — an exact zero-thickness touch can leave the boolean union as two separate solids
  // instead of one fused part.
  ov = 0.2;
  for (x = [x_qtpy + qtpy_bay_l, x_scd + scd_bay_l])
    translate([x, wall - ov, floor - ov])
      cube([rib, case_w - 2 * wall + 2 * ov, inner_h - 8 + ov]); // 8mm cable gap at the top

  // Corner screw bosses, inset from the outer corners — added here (after the cavity cut, not
  // before) so they actually survive. Overlapped 0.2mm into the floor for the same fusing reason.
  for (pos = [[6, 6], [case_l - 6, 6], [6, case_w - 6], [case_l - 6, case_w - 6]])
    translate([pos[0], pos[1], floor - ov]) screw_boss(inner_h - 2 + ov);
}

module case_lid() {
  lip_h = 4;
  translate([case_l + 15, 0, 0]) { // offset in the viewport so base + lid don't overlap
    union() {
      linear_extrude(height = lid_thickness)
        rounded_rect(case_l, case_w, corner_r);
      // Lip that drops down inside the base's walls
      translate([wall + fit_gap, wall + fit_gap, -lip_h])
        linear_extrude(height = lip_h)
          rounded_rect(case_l - 2 * (wall + fit_gap), case_w - 2 * (wall + fit_gap), corner_r - 1);
      // Screw holes matching the base's bosses
      for (pos = [[6, 6], [case_l - 6, 6], [6, case_w - 6], [case_l - 6, case_w - 6]])
        translate([pos[0], pos[1], -0.5])
          cylinder(h = lid_thickness + 1, d = 3.2, $fn = 16);
    }
  }
}

if (part == "base" || part == "both") case_base();
if (part == "lid"  || part == "both") case_lid();
