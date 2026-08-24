# Bedroom Air Monitor

A small, **sovereign** bedroom air-quality monitor: it measures the air you actually sleep in —
CO₂, temperature, humidity, fine dust and a VOC index — shows it live on an e-ink display, and
sends nothing to anyone else's cloud. Two devices talk to each other over your own WLAN. That's it.

It was built for one concrete purpose: **to answer, right before bed, "should I air the room out?"**
CO₂ climbs through the night with the people in the room; the value you fall asleep at largely
decides where you wake up. With four people in a closed bedroom that's the difference between a
fresh night and a stuffy 2000 ppm one. So the display's job is not to look pretty — it's to say
`1450 ppm — open the window` when it's worth it, and stay quiet when it isn't.

> Part of a larger personal-health project, but this repo is **standalone and self-contained** —
> no health data, no accounts, nothing private. Just two ESP32 boards, some I²C sensors, and code
> you can copy.

---

## What it does

```
   ┌──────────────────────────┐        GET /now  (every 10–15 min)      ┌────────────────────┐
   │  QT Py ESP32-S2          │  ◀──────────────────────────────────────│  TRMNL 7.5" e-ink  │
   │  ├─ SCD-41  CO₂/temp/RH  │        JSON: {co2, temp, rh, pm25, voc}  │  (XIAO ESP32-S3)   │
   │  └─ SEN54   PM / VOC     │  ────────────────────────────────────▶  │  draws it itself,  │
   │                          │                                         │  deep-sleeps between│
   │  serves JSON on the LAN  │                                         └────────────────────┘
   └──────────┬───────────────┘
              │  HTTP POST  (every 5 min, OPTIONAL)
              ▼
      your own server / logging endpoint     ← optional; the display works without it
```

- **The sensor** (`firmware/qtpy-sensor`) reads the two Sensirion sensors over I²C, serves the
  current reading as JSON on your LAN (`GET http://<sensor-ip>/now`), and — optionally — POSTs the
  same reading to a logging endpoint every few minutes.
- **The display** (`firmware/trmnl-display`) fetches that JSON on an interval, draws CO₂ +
  temperature + humidity + a plain-language airing hint on the e-ink panel, and deep-sleeps in
  between to save the battery.

**No third-party server is involved.** The two devices find each other by IP on your own network.
If your internet is down, the display still works.

---

## Hardware

| Part | Role |
|---|---|
| **Adafruit QT Py ESP32-S2** | The sensor's brain: Wi-Fi, USB-C, STEMMA QT (I²C) |
| **Adafruit SCD-41** | CO₂ (400–5000 ppm), temperature, humidity |
| **Sensirion SEN54** | Fine dust (PM1.0–PM10) and a VOC index (0–500) |
| **Adafruit SEN5x STEMMA QT adapter** | Brings the SEN54 onto the STEMMA QT chain (100 mA 5 V boost) |
| STEMMA QT / JST-SH cables | Solderless I²C wiring — one chain, no soldering |
| **TRMNL 7.5" (OG) DIY Kit** | The display: 800×480 mono e-ink on a Seeed XIAO ESP32-S3 PLUS, USB-C flashable |

All sensor wiring is **STEMMA QT / Qwiic — plug-in, no soldering.** See [`docs/WIRING.md`](docs/WIRING.md).

A note on the SEN54's fan: it's audible. The firmware runs the **silent** SCD-41 continuously and
only spins the SEN54 up **in short bursts** to sample dust, so it doesn't hum next to where you
sleep. See the `SEN54_*` options in the sensor config.

---

## Getting started

You'll need the [Arduino IDE](https://www.arduino.cc/en/software) with **ESP32 board support**
installed, and these libraries (Library Manager):

- `Sensirion I2C SCD4x`
- `Sensirion I2C SEN5X`
- `ArduinoJson`

### 1. Flash the sensor

```
cp firmware/qtpy-sensor/config.example.h firmware/qtpy-sensor/config.h
# edit config.h: your Wi-Fi, a fixed IP is recommended (see below)
```

Open `firmware/qtpy-sensor/qtpy-sensor.ino`, select **Adafruit QT Py ESP32-S2** as the board,
and upload. Open the Serial Monitor at 115200 baud — it prints its IP and the first readings.

> ⚠️ **Use a data-capable USB-C cable.** Many cables are charge-only and the board won't appear as
> a port. And give the sensor a **fixed IP** — a DHCP reservation in your router, or set a static
> IP in `config.h` — so the display can always find it after a reboot.

Check it works: open `http://<sensor-ip>/now` in a browser. You should see JSON.

### 2. Flash the display

```
cp firmware/trmnl-display/config.example.h firmware/trmnl-display/config.h
# edit config.h: your Wi-Fi, and the sensor's IP from step 1
```

Open `firmware/trmnl-display/trmnl-display.ino`, select the **XIAO ESP32-S3** board, and upload.

> ⚠️ The e-ink panel driver is the **one hardware-specific line** you must set for your exact panel
> — it's marked clearly in the sketch. Everything else (Wi-Fi, fetch, parse, layout) is ready.

---

## Configuration you'll care about

| Setting | Where | What it does |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASS` | both `config.h` | your network |
| `SENSOR_URL` | display `config.h` | `http://<sensor-ip>/now` |
| `REFRESH_MINUTES` | display `config.h` | how often the display wakes and redraws (10–15 is plenty) |
| `CO2_AIR_PPM` | display `config.h` | the CO₂ level above which it says "open the window" |
| `POST_URL` / `POST_SECRET` | sensor `config.h` | optional logging endpoint; leave blank to disable |
| `SEN54_INTERMITTENT` | sensor `config.h` | run the dust fan in short bursts (quiet) vs. continuously |
| `PLACEMENT` | sensor `config.h` | a label sent with each reading (e.g. `dresser`) — handy if you log |

---

## Design notes (why it's built this way)

- **The two devices are independent.** The display fetches from the sensor; nothing sits between
  them. Either can fail without taking the other down. The optional logging POST runs on its own.
- **The airing hint hangs on the measurement, and it's timed for bedtime** — because airing only
  helps the night if you do it shortly before sleeping. Below the threshold the display says the
  air is fine; it never invents an alarm.
- **E-ink is not a live screen.** A full refresh flashes black/white and takes a couple of seconds,
  so the display updates on an interval and deep-sleeps between. Don't expect per-second numbers —
  CO₂ moves over minutes, not seconds.

---

## Status

The **firmware here compiles against the named libraries and follows their documented APIs**, but
it has **not yet been flashed onto the final hardware** — treat the first upload as the real test,
watch the Serial Monitor, and expect to adjust the two things that are genuinely board-specific:
the sensor's fixed IP, and the display's e-ink panel driver line. Both are marked in the code.

## License

MIT — see [LICENSE](LICENSE). Copy it, build your own, change whatever you like.
