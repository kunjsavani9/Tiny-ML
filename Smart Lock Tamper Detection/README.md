# Smart Lock Tamper Detection 🔒

A **TinyML-based tamper-detection system** for a smart locker, built on the **Arduino Nano 33 BLE Sense** with **Edge Impulse**. It fuses three sensor streams — vibration (IMU), impact sound (onboard PDM microphone), and door state (magnetic reed switch) — to classify locker conditions in real time, and combines the neural-network output with **rule-based overrides** for robust real-world detection.

## Tamper Classes

| Label | Description | LED |
|-------|-------------|-----|
| `idle` | Locker still and locked — no movement, no sound | White/Blue (D2) |
| `normal_open` | Door opened normally (spring transitions 1→0) | Green (D3) |
| `forced_vibration` | Strong continuous shaking while locked | Yellow/Orange (D4) |
| `hammer` | Sharp high-amplitude impact + loud bang | Red (D5) |

Buzzer on D6 sounds for tamper conditions.

## Hardware

| Component | Qty | Purpose |
|-----------|-----|---------|
| Arduino Nano 33 BLE Sense | 1 | TinyML board (onboard IMU + PDM mic) |
| SH1106 OLED (128×64) | 1 | Real-time status display |
| Magnetic Spring Sensor (reed switch) | 1 | Door open/close detection (D7) |
| LEDs (4×) + 220Ω resistors | 4 | Condition indication |
| Buzzer | 1 | Audible tamper alert |
| Breadboard, jumpers, USB cable | — | Prototyping, power & programming |

## Wiring

**OLED (SH1106) — I2C:** `SDA → A4`, `SCL → A5`, `VCC → 3.3V`, `GND → GND`

**LEDs:** D2–D5 (one per class, each through 220Ω) · **Buzzer:** D6

**Magnetic spring sensor:** `Signal → D7 (INPUT_PULLUP)`, `Ground → GND`. Magnet present (locked) → D7 reads HIGH; magnet absent (open) → D7 reads LOW. No external resistor needed.

> **Prototyping tip:** during data collection a pair of wired headphones was used as the magnet source — the speaker driver's permanent magnet triggers the reed switch. Hold the earcup against the sensor to simulate LOCKED, remove it for OPEN. In production, replace with a proper reed switch (NO type) on the frame and a neodymium magnet on the door — same 2-wire connection, no code change.

## TinyML Workflow (Edge Impulse)

1. **Data collection** — 8 axes streamed at 100 Hz via the Data Forwarder:
   `ax, ay, az, gx, gy, gz, mic, door`. Each window is 2000 ms. ~6 min 20 s total across the four classes.
   ```
   edge-impulse-data-forwarder --frequency 100
   ```
2. **Impulse design** — Spectral Analysis, 2000 ms window / 200 ms increase, FFT length 32, log spectrum, overlapping frames.
3. **Model training** — NN classifier (Input → Dense 32 → Dense 16 → Output 4), 150 cycles, LR 0.0005, 80/20 split, auto-weighted classes.
4. **Deployment** — exported as an Arduino library and flashed via Arduino IDE.

## Key Engineering Decisions

- **Gravity correction** on `az` (`az − 1g`) so `idle` reads ~0.0 and doesn't overlap with tamper classes.
- **Peak vibration** tracked across the full window to catch short, sharp hammer impacts.
- **Double-buffered PDM mic** so no audio frame is missed between IMU reads; mic RMS stored per IMU frame.
- **Door state** stored as a float (0.0/1.0) for direct use in the feature vector.

## Hybrid Detection — Rule-Based Overrides

The model output is combined with deterministic rules applied in priority order (idle → hammer → vibration → door); if none fire, the AI prediction is used with a 0.65 confidence threshold. The OLED shows which rule fired (`RULE:IDLE`, `RULE:HAMM`, `RULE:VIB`, `RULE:DOOR`, or `AI MODEL`) for live debugging and threshold tuning.

This solved two real problems: hammer being misclassified as forced_vibration (separated by a mic threshold > 800), and an initial model overfitting to 100% training accuracy on too little data (fixed with more varied samples, FFT reduced to 32, and dropout — targeting 85–93% accuracy).

## Getting Started

1. Install the **Arduino IDE 2.x** and the **Arduino Mbed OS Nano** board package.
2. Install libraries: `Arduino_LSM9DS1`, `U8g2`, and the onboard PDM library.
3. Import the Edge Impulse inferencing library (`.zip`) via *Sketch → Include Library → Add .ZIP Library*.
4. Open the sketch in `firmware/`, select the board and port, and **Upload**.
5. Simulate door lock/open with the sensor (or headphone) and test idle / shake / impact.

The full report with impulse settings, confusion matrix, and results is in [`docs/`](docs/).

## License

MIT — see the repository [LICENSE](../LICENSE).
