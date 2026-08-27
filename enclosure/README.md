# Enclosure — first rough draft

A first, rough 3D-printable case concept for the bedroom sensor. Open `sensor-case.scad` in
[OpenSCAD](https://openscad.org) (free) to view, adjust, and export an STL.

## What it is

Three bays side by side inside one printed box, an open-topped base plus a separate screw-on lid:

```
[ QT Py bay ] [ SCD-41 bay, vented front ] [ SEN54 module bay, vented front + fan hole on the side ]
```

- **QT Py bay** — no vents needed, just a USB-C cable exit on the back wall.
- **SCD-41 bay** — 5 narrow vertical slots on the front wall. Passive; the sensor doesn't need
  forced airflow, just an open path to room air.
- **SEN54 bay** — 7 slots on the front wall (intake) plus a round cutout on the side wall lined up
  with the module's fan. This is the biggest bay: the SEN54 module itself is 52×43×22mm, much
  bigger than its small STEMMA QT adapter board. The adapter board sits at the far end of this bay
  (away from the rib, near the fan wall) in its own small open-sided nest — sized to the board's
  real measured footprint, open on the side facing the module so the 6-pin JST-GH cable between
  them has somewhere to go. That cable stays fully internal; it never needs to exit the case.

Four screw bosses in the base take M3 self-tapping screws through matching holes in the lid.

## What's verified vs. estimated

Every dimension is commented in the file with its source. In short:
- **QT Py ESP32-S2** (21.8×17.9×5.7mm) and **SCD-41 breakout** (25.5×22.8×7.7mm) — from Adafruit's
  own product pages.
- **SEN54 module** (52.3×43.3×22.3mm) — from Sensirion's datasheet drawing.
- **SEN5x adapter board** (25.4×20.0×5.0mm) — measured directly by Markus with a ruler.
- **The exact position of the SEN54's fan opening**, and the **length of the module's own
  connector tab** — not independently verified, both are best guesses from the datasheet's
  isometric drawing. Once you have the SEN54 in hand, hold it up against a printed base (or just
  measure) and adjust `sensor-case.scad`'s fan-hole `translate()` / `sen_tail_l` before a final print.
- Wall thickness, clearances, and screw boss sizing are standard rough-draft defaults for FDM
  printing (2.2mm walls, 0.3mm fit clearance, M3 self-tap pilot holes) — fine for a first test
  print, worth tuning to your printer once you've test-fit one bay.
- The adapter board's own mounting-hole pattern (if it even has one) isn't published anywhere, so
  its nest is a friction-fit locator, not a screwed mount — add double-sided tape if it needs to
  stay put.

**A note on checking this file's renders:** `openscad -o file.png` without `--render` uses the fast
preview mode, which can draw a small nested `difference()` (like the adapter nest) as a solid block
even though the actual geometry is correct — caught this while double-checking the model, confirmed
by re-rendering with `--render` and by the STL export (which always computes the real geometry).
Always pass `--render` when screenshotting this file to check a design change.

## Rendering / exporting

```
openscad -o base.stl sensor-case.scad -D 'part="base"'
openscad -o lid.stl  sensor-case.scad -D 'part="lid"'
```

Or open the file in the OpenSCAD GUI, use the `part` customizer dropdown (base / lid / both), and
export via File → Export → Export as STL.

## Suggested next step before a full print

Print just the **SCD-41 bay's footprint** (or the whole base at reduced infill) as a quick test to
check the boards physically fit and the vent slots aren't too tight — cheaper than discovering a
fit problem after a full multi-hour print.
