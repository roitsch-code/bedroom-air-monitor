# Wiring

All sensor wiring is **STEMMA QT / Qwiic** — the little 4-pin JST-SH connectors. No soldering, and
the plugs are keyed so you can't get them the wrong way round.

## The I²C chain

The QT Py and both sensors share one I²C bus. Daisy-chain them in any order:

```
  QT Py ESP32-S2                SCD-41                 SEN5x adapter → SEN54
  ┌───────────┐   STEMMA QT   ┌─────────┐  STEMMA QT  ┌──────────────┐
  │  [QT port]│──────────────▶│ [in][out]│───────────▶│ [in]         │
  └───────────┘   (300 mm)    └─────────┘   (100 mm)  └──────────────┘
```

- **QT Py → SCD-41:** one STEMMA QT cable (the 300 mm one is handy here). The QT Py has one STEMMA
  QT port; the SCD-41 has **two** (they're the same bus, so either is "in" or "out").
- **SCD-41 → SEN5x adapter:** the second STEMMA QT cable (100 mm is fine), from the SCD-41's free
  port to the adapter's STEMMA QT port.
- **SEN5x adapter → SEN54:** a **6-pin JST-GH cable with a plug on BOTH ends** connects the sensor
  to the 6-pin socket on top of the adapter (not the STEMMA QT side — that's already used).
  ⚠️ **This cable is NOT included with either the adapter or the SEN54.** The adapter ships bare,
  and the cable in the SEN54's box has bare breadboard pins on one end, so it does not plug into the
  adapter. You need a JST-GH 6-pin **plug-to-plug** cable — e.g. Adafruit #5754. Order it separately.

Both sensors sit at different I²C addresses (SCD-41 at `0x62`, SEN5x at `0x69`), so sharing the bus
is no problem — no jumpers, no address changes. Every connector is keyed: it only goes in one way,
so don't force one that resists.

Assemble everything **with USB unplugged**, then plug the USB-C cable in last. The adapter's on-board
boost generates the 5 V the SEN54's fan needs, so you don't wire power separately.

## Power

- Power the QT Py from a **decent USB-C supply** with a **data-capable** cable (charge-only cables
  power it but won't let you flash it, and can also cause brown-outs under Wi-Fi + fan peaks).
- The SEN5x adapter provides the 5 V boost the SEN54 needs — you don't wire that separately.

## Placement

- **Not right next to a head** — a CO₂ sensor next to where someone breathes reads their exhaled
  air, not the room. Aim for a shelf a metre or two from the nearest sleeper.
- **Not on/above a radiator, and not right at the window** — the radiator skews temperature, and
  the window is where fresh air arrives first when you air the room, so a sensor there reports the
  room as better than it is.
- The SEN54 draws air through a small fan — don't box it into a tight nook; give it room to breathe.

## First-run checks

1. Serial Monitor at 115200 baud shows the sensor's IP and readings within a minute.
2. `http://<sensor-ip>/now` returns JSON in a browser on the same network.
3. Breathe near the SCD-41 — CO₂ should climb within a few seconds and fall again after. That's the
   quickest proof it's really measuring the room.
