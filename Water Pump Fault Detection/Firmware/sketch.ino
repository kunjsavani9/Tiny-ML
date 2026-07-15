// =======================================================
// EDGE IMPULSE + REAL SENSOR MODEL (FINAL VERSION)
// =======================================================

#include <WaterPumpFaultDetectionCustom_inferencing.h>
#include <Arduino_LSM9DS1.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <U8g2lib.h>

// =======================================================
// OLED DISPLAY
// =======================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// =======================================================
// ADS1115 ADC
// =======================================================
Adafruit_ADS1115 ads;

// =======================================================
// LED PINS
// =======================================================
#define LED_NORMAL     2
#define LED_IDLE       3
#define LED_BLOCKAGE   4
#define LED_CAVIT      5
#define LED_OVERLOAD   6

#define BUZZER_PIN     7

const int ALL_LEDS[5] = {
  LED_NORMAL,
  LED_IDLE,
  LED_BLOCKAGE,
  LED_CAVIT,
  LED_OVERLOAD
};

// =======================================================
// LABELS (MUST MATCH EDGE IMPULSE LABEL ORDER)
// =======================================================
const char* LABELS[5] = {
  "normal_run",
  "idle",
  "blockage",
  "cavitation",
  "overload"
};

const int CLASS_LED[5] = {
  LED_NORMAL,
  LED_IDLE,
  LED_BLOCKAGE,
  LED_CAVIT,
  LED_OVERLOAD
};

// =======================================================
// FEATURE BUFFER
// =======================================================
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
int fi = 0;

// =======================================================
// GLOBAL VARIABLES
// =======================================================
float g_current = 0;
float g_accel_rms = 0;

// ACS712 Offset Voltage
float offset = 2.5;

// =======================================================
// CALIBRATE ACS712 SENSOR
// =======================================================
void calibrateACS() {

  float sum = 0;

  Serial.println("Calibrating current sensor... Keep pump OFF");

  for (int i = 0; i < 200; i++) {

    float voltage = ads.computeVolts(
      ads.readADC_SingleEnded(0)
    );

    sum += voltage;

    delay(5);
  }

  offset = sum / 200.0;

  Serial.print("Calculated Offset: ");
  Serial.println(offset);
}

// =======================================================
// READ CURRENT FROM ACS712
// =======================================================
float readCurrent() {

  float voltage = ads.computeVolts(
    ads.readADC_SingleEnded(0)
  );

  // ACS712 5A version sensitivity = 185mV/A
  return (voltage - offset) / 0.185;
}

// =======================================================
// CONTROL LEDs
// =======================================================
void setLED(int cls) {

  for (int i = 0; i < 5; i++) {
    digitalWrite(ALL_LEDS[i], LOW);
  }

  digitalWrite(CLASS_LED[cls], HIGH);
}

// =======================================================
// BUZZER ALERT
// =======================================================
void buzzIfFault(int cls) {

  // 0 = normal_run
  // 1 = idle

  if (cls == 0 || cls == 1) {
    noTone(BUZZER_PIN);
  }
  else {
    tone(BUZZER_PIN, 2000, 200);
  }
}

// =======================================================
// OLED DISPLAY FUNCTION
// =======================================================
void drawOLED(int cls,
              float conf,
              float current,
              float accel) {

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "PUMP MONITOR");

  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(0, 28, LABELS[cls]);

  char buf[32];

  sprintf(buf, "Conf:%d%%", (int)(conf * 100));
  u8g2.drawStr(0, 45, buf);

  sprintf(buf, "I=%.2fA", current);
  u8g2.drawStr(0, 60, buf);

  sprintf(buf, "V=%.2fg", accel);
  u8g2.drawStr(70, 60, buf);

  u8g2.sendBuffer();
}

// =======================================================
// EDGE IMPULSE FEATURE CALLBACK
// =======================================================
int feature_get_data(size_t offset,
                     size_t length,
                     float *out_ptr) {

  memcpy(out_ptr,
         features + offset,
         length * sizeof(float));

  return 0;
}

// =======================================================
// SETUP
// =======================================================
void setup() {

  Serial.begin(115200);

  // ======================
  // LED Setup
  // ======================
  for (int i = 0; i < 5; i++) {
    pinMode(ALL_LEDS[i], OUTPUT);
  }

  pinMode(BUZZER_PIN, OUTPUT);

  // ======================
  // OLED INIT
  // ======================
  u8g2.begin();

  // ======================
  // IMU INIT
  // ======================
  if (!IMU.begin()) {

    Serial.println("IMU Initialization Failed");

    while (1);
  }

  // ======================
  // ADS1115 INIT
  // ======================
  ads.begin();

  ads.setGain(GAIN_ONE);

  // ======================
  // ACS Calibration
  // ======================
  calibrateACS();

  Serial.println("System Ready");
}

// =======================================================
// MAIN LOOP
// =======================================================
void loop() {

  fi = 0;

  float accel_sum = 0;

  // ===================================================
  // COLLECT SENSOR FEATURES
  // ===================================================
  while (fi < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 6) {

    float ax, ay, az;

    if (IMU.accelerationAvailable()) {

      IMU.readAcceleration(ax, ay, az);

      float current = readCurrent();

      float accel_rms = sqrt(ax * ax +
                             ay * ay +
                             az * az);

      // ===============================================
      // FEATURES (MUST MATCH TRAINING ORDER)
      // ===============================================
      features[fi++] = ax;
      features[fi++] = ay;
      features[fi++] = az;
      features[fi++] = current;
      features[fi++] = accel_rms;
      features[fi++] = fabs(current);

      accel_sum += accel_rms;
    }

    // 100 Hz Sampling
    delay(10);
  }

  // ===================================================
  // AVERAGE VALUES
  // ===================================================
  g_current = readCurrent();

  g_accel_rms = accel_sum / (fi / 6);

  // ===================================================
  // RUN EDGE IMPULSE CLASSIFIER
  // ===================================================
  signal_t signal;

  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &feature_get_data;

  ei_impulse_result_t result;

  run_classifier(&signal, &result, false);

  // ===================================================
  // FIND BEST CLASS
  // ===================================================
  int best = 0;
  float best_val = 0;

  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {

    if (result.classification[i].value > best_val) {

      best_val = result.classification[i].value;
      best = i;
    }
  }

  // ===================================================
  // OUTPUT ACTIONS
  // ===================================================
  setLED(best);

  buzzIfFault(best);

  drawOLED(best,
            best_val,
            g_current,
            g_accel_rms);

  // ===================================================
  // SERIAL MONITOR OUTPUT
  // ===================================================
  Serial.print("Class: ");
  Serial.print(LABELS[best]);

  Serial.print(" | Confidence: ");
  Serial.print(best_val * 100);
  Serial.print("% ");

  Serial.print(" | Current: ");
  Serial.print(g_current);
  Serial.print(" A");

  Serial.print(" | Accel RMS: ");
  Serial.println(g_accel_rms);

  delay(200);
}
