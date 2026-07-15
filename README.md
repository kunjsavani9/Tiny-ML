# Tiny-ML

A collection of **TinyML** projects that run trained machine-learning models directly on an **Arduino Nano 33 BLE Sense** using **Edge Impulse Studio** — on-device inference with no cloud, using onboard sensors (IMU, PDM microphone) plus external sensors for real-time classification.

## Projects

### 🔧 [Water Pump Fault Detection](water-pump-fault-detection/)
A predictive-maintenance system that monitors a water pump's vibration (IMU) and current draw (ACS712 + ADS1115) and classifies its operating state in real time — `normal_run`, `idle`, `blockage`, `cavitation`, or `overload` — with LED, OLED, and buzzer indication.

### 🔒 [Smart Lock Tamper Detection](smart-lock-tamper-detection/)
A security system that detects locker tampering by combining vibration (IMU), impact sound (PDM mic), and door state (magnetic reed switch) to classify `idle`, `normal_open`, `forced_vibration`, or `hammer`. Uses a hybrid TinyML + rule-based override approach for reliable real-world detection.

## Shared Stack

| Layer | Tool |
|-------|------|
| Board | Arduino Nano 33 BLE Sense |
| ML platform | Edge Impulse Studio |
| Data acquisition | Edge Impulse CLI (Data Forwarder) |
| Firmware | Arduino IDE |
| Display | SH1106 128×64 OLED (I2C) |

## Repository Structure

```
Tiny-ML/
├── water-pump-fault-detection/
│   ├── firmware/          # Arduino sketch + Edge Impulse library
│   ├── docs/              # Full project report (PDF)
│   └── README.md
├── smart-lock-tamper-detection/
│   ├── firmware/          # Arduino sketch + Edge Impulse library
│   ├── docs/              # Full project report (PDF)
│   └── README.md
├── LICENSE
└── README.md
```

## License

Released under the [MIT License](LICENSE).

---

Built by **Kunj Savani** — TinyML / Edge AI projects on Arduino Nano 33 BLE Sense.
