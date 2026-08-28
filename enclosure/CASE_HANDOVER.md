# Gehäuse — Übergabe an die nächste Session

**Stand: 2026-08-28.** Die Elektronik läuft komplett (Display + Sensor, Firmware fertig und
verifiziert — siehe die `firmware/`-Ordner und die Commit-Historie). Was fehlt, sind **zwei
3D-druckbare Gehäuse**. Reihenfolge nach Markus' Ansage:

1. **TRMNL-Display-Gehäuse — viel wichtiger, existiert noch NICHT.**
2. **Sensor-Gehäuse — erster Entwurf existiert** (`sensor-case.scad`), muss überarbeitet werden.

> **Arbeitsweise (binding, aus dem Projekt-Leitfaden):** Markus ist Nicht-Programmierer. Erst das
> Konzept klären und von ihm freigeben lassen, dann bauen — keine autonomen Schnellschüsse. Und
> **keine Maße erfinden**: was nicht sicher bekannt ist, wird gemessen oder nachgeschlagen, nicht
> geraten. Beim Rendern von OpenSCAD-Dateien immer `--render` mitgeben (der Preview-Modus zeichnet
> kleine `difference()` manchmal als Vollblock — steht auch in `README.md`).

---

## 1. TRMNL-Display-Gehäuse (PRIORITÄT 1 — von Grund auf neu)

### Was es physisch ist
Der TRMNL 7.5"-OG-DIY-Bausatz besteht aus **drei** getrennten Teilen, per Kabel verbunden:

- **E-Ink-Panel**, 7.5", 800×480, mono (Controller UC8179). Dünnes Glas, über ein **flexibles
  FPC-Flachbandkabel** mit der Treiberplatine verbunden (auf den Fotos gut sichtbar).
- **Treiberplatine: Seeed „XIAO 7.5 inch ePaper Panel / Driver Board" (Aufdruck „seeed studio
  Board (A)")** mit aufgestecktem **Seeed XIAO ESP32-S3 Plus** und einer **XIAO-FPC-Antenne (A-01)**.
  Auf der Platine: **RESET**-Knopf, **KEY1/2/3**, ein **ON/OFF-Schiebeschalter**, USB-C (am XIAO,
  für Laden + Flashen), JST-Akkustecker.
- **LiPo-Akku**, Aufdruck **`654060 · 2000 mAh · 7.4 Wh`** → also grob **6 × 54 × 60 mm** (Dicke ×
  Breite × Länge, das ist die übliche Lesart der Zahl 654060). Hängt am JST-Stecker der Platine.

⚠️ **Die grünen Status-LEDs sind bereits deaktiviert** (Traces durchtrennt) — es braucht also
**keine** Lichtleiter/Fenster im Gehäuse.

### Was VOR dem Entwurf gemessen/geklärt werden muss (nicht raten!)
Diese Maße hatte die vorige Session **nicht** verlässlich — die neue Session muss sie mit dem Teil
in der Hand messen (Messschieber) oder aus Seeed/TRMNL-Doku holen:

- **Panel:** exakte Außenmaße des Glases (L × B × Dicke) und die **aktive Fläche** vs. Rand. Grob
  ist ein 7.5"-800×480-Panel ~**170 × 111 mm** Glas mit ~**163 × 98 mm** aktiver Fläche — **das ist
  eine Schätzung, unbedingt am echten Panel nachmessen** (die genaue Blende entscheidet über die
  sichtbare Öffnung im Rahmen).
- **Treiberplatine:** Außenmaße L × B, Dicke mit aufgestecktem XIAO, Position von USB-C, Schaltern,
  RESET, Akkustecker und dem FPC-Steckverbinder zum Panel.
- **FPC-Kabel:** Länge und wie eng es gebogen werden darf (bestimmt, wie weit Platine hinter/unter
  dem Panel sitzen kann).
- **Akku:** echte Maße nachmessen (die 654060-Angabe ist ein Richtwert).

### Design-Absicht (mit Markus zu bestätigen)
- **Nachttisch-Display im Querformat** (800×480 liegt quer). Vermutlich ein **Bilderrahmen-/
  Aufsteller-Look** mit leichtem Neigungswinkel (~15°) oder ein flacher Standfuß — Winkel mit Markus
  abstimmen, er legt viel Wert auf Optik (siehe seine Design-Mockups fürs Display-UI).
- **Panel-Blende:** sauberer, schmaler Rahmen um die aktive Fläche; das Glas gefasst, die Ränder
  (Nicht-Bild-Bereich) verdeckt.
- **Platine + Akku** wandern **hinter** das Panel (Gehäusetiefe) — das FPC-Kabel erlaubt das, Biegung
  beachten.
- **Zugänglich bleiben müssen:** USB-C (Laden/Flashen, ohne Öffnen) und der **ON/OFF-Schalter**.
  RESET nice-to-have (kleines Loch reicht). Die KEY-Knöpfe werden von der Firmware nicht genutzt →
  kein Zugang nötig.
- **E-Ink braucht keine Belüftung** und keine Lichtleiter (LEDs aus).

### Offene Design-Frage für Markus
Aufsteller (leichte Neigung, Kabel hinten raus) **oder** an die Wand hängbar? Das ändert die
Rückseite grundlegend — **zuerst fragen.**

---

## 2. Sensor-Gehäuse (PRIORITÄT 2 — Entwurf existiert, überarbeiten)

Erster Wurf liegt als **`sensor-case.scad`** vor (+ `README.md` erklärt ihn). Drei Kammern in einem
Kasten: QT Py · SCD-41 (vorne belüftet) · SEN54-Modul + SEN5x-Adapter (vorne + seitlich belüftet,
Lüfterloch). Schraub-Deckel, M3.

### Was verlässlich ist (aus den Datenblättern/gemessen, steht kommentiert im .scad)
- QT Py ESP32-S2: 21.8 × 17.9 × 5.7 mm (Adafruit #5325)
- SCD-41-Breakout: 25.5 × 22.8 × 7.7 mm (Adafruit #5190)
- SEN54-Modul: 52.3 × 43.3 × 22.3 mm (Sensirion-Datenblatt)
- SEN5x-Adapter: 25.4 × 20.0 × 5.0 mm (von Markus gemessen)

### Offene Punkte am Sensor-Gehäuse
- **Position des SEN54-Lüfterlochs** und **Länge des Anschluss-„Schwanzes" am Modul** sind noch
  **Schätzungen** aus der Datenblatt-Zeichnung → mit dem Modul in der Hand gegenprüfen und die
  `translate()` des Lüfterlochs / `sen_tail_l` anpassen, bevor final gedruckt wird.
- **LEDs sind auch hier aus** (Traces durchtrennt) → keine Lichtfenster nötig (der .scad hat ohnehin
  keine).
- **Belüftung nicht zubauen** (Leitplanke aus `WIRING.md`): der SEN54 zieht Luft durch den Lüfter,
  der SCD-41 misst Raumluft passiv — beide brauchen offenen Luftweg, nicht dichtpacken.
- **STEMMA-QT-Kabel** laufen zwischen den Kammern (Rippen haben oben eine Lücke) — das JST-GH-Kabel
  Adapter↔SEN54 bleibt komplett innen.
- Verkabelung/Platzierung: siehe `docs/WIRING.md` (der Sensor darf **nicht** direkt neben einen
  schlafenden Kopf, nicht auf/über die Heizung, nicht ans Fenster).

---

## Render- / Export-Workflow (OpenSCAD, kostenlos)

```
openscad -o base.stl sensor-case.scad -D 'part="base"'
openscad -o lid.stl  sensor-case.scad -D 'part="lid"'
# Zum Screenshot-Prüfen einer Änderung IMMER --render mitgeben:
openscad --render -o check.png sensor-case.scad
```

Für das TRMNL-Gehäuse eine neue Datei anlegen (z. B. `trmnl-case.scad`) und eine eigene
`trmnl-case/README.md` oder diesen Abschnitt fortschreiben.

---

## Stand der Elektronik (Kontext — alles fertig, nichts offen)
- Display-Firmware `firmware/trmnl-display/`: smarte Lüften-Logik (absolute Feuchte + VOC + Kälte),
  hellgrauer Kasten bei „alles gut" / schwarzer bei Handlung (nach Markus' Mockups), Umlaut-freie
  ASCII-Texte, °-Zeichen selbst gezeichnet.
- Sensor-Firmware `firmware/qtpy-sensor/`: CO₂/Temp/Feuchte (SCD-41) + Feinstaub/VOC (SEN54). **VOC
  war lange 0 — Ursache war Idle zwischen den Bursts (Gassensor stromlos); Fix: RHT/Gas-Only-Modus,
  Gassensor läuft durchgehend. Bestätigt: VOC = 99 nach ~8 Min.** Sensor-IP im Heimnetz:
  `http://192.168.2.123/now`.
- Beide grünen LEDs deaktiviert (Traces durchtrennt).
- Einziger bewusst offener Elektronik-Punkt: TRMNL-Akkulaufzeit (~7,5 h) — von Markus zurückgestellt
  („Scheiss auf TRMNL Akku"), Dauerkabel ist ok.
