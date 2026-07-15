# Water Pump Fault Detection 🔧

A **TinyML-based predictive-maintenance system** that monitors a water pump in real time and classifies its operating condition using an **Arduino Nano 33 BLE Sense** and a model trained in **Edge Impulse**. The system reads onboard accelerometer (vibration) and pump current, runs on-device inference, and signals the detected condition via LEDs, an OLED display, and a buzzer.

## Fault Classes

| Label | Description | LED |
|-------|-------------|-----|
| `normal_run` | Pump operating under normal load | Green (D2) |
| `idle` | Pump powered but not pumping | Blue (D3) |
| `blockage` | Inlet/outlet blockage detected | Orange (D4) |
| `cavitation` | Air cavity formation in pump | Purple (D5) |
| `overload` | Excessive current / mechanical stress | Red (D6) |

The buzzer (D7) triggers on any non-normal condition.

## Hardware

| Component | Qty | Purpose |
|-----------|-----|---------|
| Arduino Nano 33 BLE Sense | 1 | Main TinyML board (onboard IMU) |
| ACS712 Current Sensor | 1 | Pump current sensing |
| ADS1115 ADC Module | 1 | 16-bit high-resolution ADC |
| SH1106 OLED (128×64) | 1 | Real-time status display |
| LEDs (5×) + 220Ω resistors | 5 | Fault-condition indication |
| Buzzer | 1 | Audible fault alert |
| DC Submersible Water Pump | 1 | Device under test |
| Breadboard, jumpers, 5V/12V supply | — | Prototyping & power |

## Wiring

**OLED (SH1106) & ADS1115 — shared I2C bus**

| Signal | Arduino Pin |
|--------|-------------|
| SDA | A4 |
| SCL | A5 |
| OLED VCC / ADS VCC | 3.3V |
| GND | GND |

**ACS712 current sensor**

- `VCC → 5V`, `GND → GND`, `OUT → ADS1115 A0`
- Pump power path: `Supply(+) → ACS712 IP+`, `ACS712 IP− → Pump(+)`, `Pump(−) → Supply GND`

**LEDs:** D2–D6 (one per class, each through a 220Ω resistor) · **Buzzer:** D7

## TinyML Workflow (Edge Impulse)

1. **Data collection** — vibration + current streamed to Edge Impulse via the Data Forwarder / CSV upload.
2. **Feature generation** — Spectral Analysis, 100 Hz sampling, 2000 ms window, 500 ms stride. Features: `ax, ay, az, current, accel_rms, abs(current)`.
3. **Model training** — fully connected neural network, 5 classes, 80/20 train-test split.
4. **Deployment** — exported as an Arduino library (`WaterPumpFaultDetectionCustom_inferencing.h`) and flashed via Arduino IDE.

## Firmware Libraries

`Arduino_LSM9DS1` (IMU) · `Adafruit_ADS1X15` (ADC) · `U8g2` (OLED) · `Wire` (I2C) · Edge Impulse Inferencing SDK.

**Inference loop:** read IMU (ax, ay, az) → read current via ADS1115 → compute `accel_rms` & `abs(current)` → build feature window → run inference → show label + confidence on OLED → light matching LED → buzz on fault.

## Getting Started

1. Install the **Arduino IDE** and the **Arduino Mbed OS Nano** board package.
2. Install libraries: `Arduino_LSM9DS1`, `Adafruit_ADS1X15`, `U8g2`.
3. Import the Edge Impulse library (`WaterPumpFaultDetectionCustom_inferencing`) via *Sketch → Include Library → Add .ZIP Library*.
4. Open the sketch in `firmware/`, select the board and port, and **Upload**.
5. Power the pump and watch live classification on the OLED.

## Notes & Limitations

During bench testing, vibration was applied to the breadboard rather than a real pump load, which reduced accuracy for subtle classes — real pump-mounted data collection is recommended for production accuracy. See the full report in [`docs/`](docs/) for the confusion matrix, results, and recommended future architecture.

## License

MIT — see the repository [LICENSE](../LICENSE).
