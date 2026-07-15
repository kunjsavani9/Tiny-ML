//=========================================================
// SMART LOCKER TAMPER DETECTION
// FINAL HYBRID AI + RULE-BASED VERSION
//
// CONDITIONS:
// 0 = forced_vibration
// 1 = hammer
// 2 = idle
// 3 = normal_open
//
// RULES (in priority order):
// RULE 1 - IDLE:   door locked + vib < 0.20 + mic < 150
// RULE 2 - HAMMER: vib > 1.0 AND mic > 800
// RULE 3 - VIBRATION: vib > 0.40 AND mic < 800
// RULE 4 - NORMAL OPEN: door open + vib < 1.0
// RULE 5 - MODEL: fallback to AI prediction
//
// OLED LAYOUT (128x64):
// +--------------------------------+
// | SMART LOCKER       [LOCK/OPEN] |
// +--------------------------------+
// | CLASS NAME           [ALERT]   |
// +================================+  <- conf bar
// | RULE:x / AI MODEL   C: xx%     |
// +--------------------------------+
// | V: x.xx              M: xxxx   |
// +--------------------------------+
//=========================================================

#include <SmartLockTamperDetection_inferencing.h>

#include <Arduino_LSM9DS1.h>
#include <PDM.h>
#include <Wire.h>
#include <U8g2lib.h>

//=========================================================
// OLED
//=========================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C
u8g2(U8G2_R0, U8X8_PIN_NONE);

//=========================================================
// PINS
//=========================================================
#define LED_IDLE        2
#define LED_NORMAL      3
#define LED_VIBRATION   4
#define LED_HAMMER      5

#define BUZZER_PIN      6
#define SPRING_DO_PIN   7

//=========================================================
// THRESHOLDS
//=========================================================
#define CONFIDENCE_THRESHOLD  0.65f

#define IDLE_VIB_THRESHOLD    0.20f
#define IDLE_MIC_THRESHOLD    150.0f

#define HAMMER_VIB_THRESHOLD  1.0f
#define HAMMER_MIC_THRESHOLD  800.0f

#define VIBRATION_THRESHOLD   0.40f

#define BUZZER_FREQ           2500

//=========================================================
// LABELS
//=========================================================
const char* labels[4] = {
  "forced_vibration",
  "hammer",
  "idle",
  "normal_open"
};

const char* labelsShort[4] = {
  "VIBRATION",
  "HAMMER",
  "IDLE",
  "NORM OPEN"
};

const int classLED[4] = {
  LED_VIBRATION,
  LED_HAMMER,
  LED_IDLE,
  LED_NORMAL
};

const bool classAlarm[4] = {
  true,   // forced_vibration
  true,   // hammer
  false,  // idle
  false   // normal_open
};

//=========================================================
// MICROPHONE - double buffered
//=========================================================
#define MIC_BUF_SIZE 512

short micBufA[MIC_BUF_SIZE];
short micBufB[MIC_BUF_SIZE];

volatile short* activeBuf  = micBufA;
volatile short* processBuf = micBufB;

volatile int  micBufLen = 0;
volatile bool micReady  = false;

//=========================================================
// FEATURE BUFFER
//=========================================================
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
int   feature_ix = 0;

//=========================================================
// GLOBALS
//=========================================================
float g_peakVib   = 0;
float g_peakMic   = 0;
float g_avgMic    = 0;
int   g_door      = 1;
int   g_lastClass = 2;
int   g_ruleUsed  = 0;
// 0=AI model, 1=idle, 2=hammer, 3=vibration, 4=door

//=========================================================
// PDM CALLBACK
//=========================================================
void onPDMdata() {
  int bytes = PDM.available();
  if (bytes > 0 && bytes <= MIC_BUF_SIZE * 2) {
    PDM.read((short*)activeBuf, bytes);
    micBufLen = bytes / 2;
    micReady  = true;
    volatile short* tmp = activeBuf;
    activeBuf  = processBuf;
    processBuf = tmp;
  }
}

//=========================================================
// MIC RMS
//=========================================================
float calcMicRMS(int count) {
  if (count <= 0) return 0;
  float sum = 0;
  for (int i = 0; i < count; i++) {
    float s = (float)processBuf[i];
    sum += s * s;
  }
  return sqrt(sum / count);
}

//=========================================================
// VIBRATION
//=========================================================
float calcVibration(float ax, float ay, float az) {
  float dx = ax;
  float dy = ay;
  float dz = az - 1.0f;
  return sqrt(dx*dx + dy*dy + dz*dz);
}

//=========================================================
// DOOR
//=========================================================
int readDoor() {
  int votes = 0;
  for (int i = 0; i < 5; i++) {
    votes += digitalRead(SPRING_DO_PIN);
    delayMicroseconds(500);
  }
  return (votes >= 3) ? 1 : 0;
}

//=========================================================
// LED
//=========================================================
void setLED(int cls) {
  digitalWrite(LED_IDLE,      LOW);
  digitalWrite(LED_NORMAL,    LOW);
  digitalWrite(LED_VIBRATION, LOW);
  digitalWrite(LED_HAMMER,    LOW);
  digitalWrite(classLED[cls], HIGH);
}

//=========================================================
// BUZZER
//=========================================================
void alertBuzzer(int cls) {
  classAlarm[cls]
    ? tone(BUZZER_PIN, BUZZER_FREQ)
    : noTone(BUZZER_PIN);
}

//=========================================================
// OLED
//=========================================================
void drawOLED(
  int cls,
  float conf,
  float vib,
  float mic,
  int door,
  int ruleUsed
) {
  u8g2.clearBuffer();

  //-----------------------------------------------------
  // ROW 1 — title + door state
  //-----------------------------------------------------
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "SMART LOCKER");

  if (door == 1) {
    // Locked icon
    u8g2.drawBox(104, 3, 9, 6);
    u8g2.drawLine(106, 3, 106, 1);
    u8g2.drawLine(106, 1, 110, 1);
    u8g2.drawLine(110, 1, 110, 3);
    u8g2.drawStr(115, 10, "LK");
  } else {
    // Open icon
    u8g2.drawBox(104, 3, 9, 6);
    u8g2.drawLine(110, 1, 114, 1);
    u8g2.drawLine(114, 1, 114, 3);
    u8g2.drawStr(118, 10, "OP");
  }

  u8g2.drawLine(0, 13, 127, 13);

  //-----------------------------------------------------
  // ROW 2 — big class name + alert tag
  //-----------------------------------------------------
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(0, 27, labelsShort[cls]);

  if (classAlarm[cls]) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawFrame(91, 15, 36, 12);
    u8g2.drawStr(93, 25, "ALERT!");
  }

  //-----------------------------------------------------
  // Confidence bar — full width 5px
  //-----------------------------------------------------
  int barW = (int)(conf * 126);
  u8g2.drawFrame(0, 29, 127, 5);
  if (barW > 0) {
    u8g2.drawBox(1, 30, barW, 3);
  }

  //-----------------------------------------------------
  // ROW 3 — rule source + confidence %
  //-----------------------------------------------------
  u8g2.setFont(u8g2_font_6x10_tf);

  switch (ruleUsed) {
    case 1:  u8g2.drawStr(0, 44, "RULE:IDLE"); break;
    case 2:  u8g2.drawStr(0, 44, "RULE:HAMM"); break;
    case 3:  u8g2.drawStr(0, 44, "RULE:VIB");  break;
    case 4:  u8g2.drawStr(0, 44, "RULE:DOOR"); break;
    default: u8g2.drawStr(0, 44, "AI MODEL");  break;
  }

  char buf[12];
  sprintf(buf, "C:%d%%", (int)(conf * 100));
  u8g2.drawStr(90, 44, buf);

  u8g2.drawLine(0, 47, 127, 47);

  //-----------------------------------------------------
  // ROW 4 — sensor values
  //-----------------------------------------------------
  sprintf(buf, "V:%.2f", vib);
  u8g2.drawStr(0, 62, buf);

  sprintf(buf, "M:%.0f", mic);
  u8g2.drawStr(58, 62, buf);

  // Solid alert square bottom-right
  if (classAlarm[cls]) {
    u8g2.drawBox(120, 54, 7, 7);
  }

  u8g2.sendBuffer();
}

//=========================================================
// EI CALLBACK
//=========================================================
int raw_feature_get_data(
  size_t offset,
  size_t length,
  float* out_ptr
) {
  memcpy(out_ptr,
         features + offset,
         length * sizeof(float));
  return 0;
}

//=========================================================
// SETUP
//=========================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_IDLE,      OUTPUT);
  pinMode(LED_NORMAL,    OUTPUT);
  pinMode(LED_VIBRATION, OUTPUT);
  pinMode(LED_HAMMER,    OUTPUT);
  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(SPRING_DO_PIN, INPUT_PULLUP);

  digitalWrite(LED_IDLE, HIGH);

  u8g2.begin();

  // Splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(18, 14, "SMART LOCKER");
  u8g2.drawLine(0, 17, 127, 17);
  u8g2.drawStr(8,  32, "Tamper Detection");
  u8g2.drawStr(22, 46, "Hybrid AI v3.0");
  u8g2.drawLine(0, 50, 127, 50);
  u8g2.drawStr(20, 63, "Initializing...");
  u8g2.sendBuffer();

  if (!IMU.begin()) {
    Serial.println("IMU FAILED");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_8x13B_tf);
    u8g2.drawStr(0, 32, "IMU FAILED!");
    u8g2.sendBuffer();
    while (1);
  }

  PDM.onReceive(onPDMdata);
  if (!PDM.begin(1, 16000)) {
    Serial.println("MIC FAILED");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_8x13B_tf);
    u8g2.drawStr(0, 32, "MIC FAILED!");
    u8g2.sendBuffer();
    while (1);
  }

  delay(500);
  micReady = false;

  int initDoor = readDoor();

  // Ready screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(18, 14, "SMART LOCKER");
  u8g2.drawLine(0, 17, 127, 17);
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(36, 38, "READY");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(16, 58,
    initDoor == 1 ? "Door: LOCKED" : "Door: OPEN");
  u8g2.sendBuffer();
  delay(1500);

  Serial.println("=====================================");
  Serial.println(" SMART LOCKER HYBRID AI v3.0");
  Serial.println("=====================================");
  Serial.println("THRESHOLDS:");
  Serial.print("  IDLE  vib<");
  Serial.print(IDLE_VIB_THRESHOLD);
  Serial.print(" mic<");
  Serial.println(IDLE_MIC_THRESHOLD);
  Serial.print("  HAMM  vib>");
  Serial.print(HAMMER_VIB_THRESHOLD);
  Serial.print(" mic>");
  Serial.println(HAMMER_MIC_THRESHOLD);
  Serial.print("  VIB   vib>");
  Serial.println(VIBRATION_THRESHOLD);
  Serial.println("=====================================");
  Serial.print("Boot door: ");
  Serial.println(initDoor == 1 ? "LOCKED" : "OPEN");
  Serial.println("=====================================");
}

//=========================================================
// LOOP
//=========================================================
void loop() {

  feature_ix = 0;
  g_peakVib  = 0;
  g_peakMic  = 0;
  g_ruleUsed = 0;

  float micSum = 0;
  int   micN   = 0;

  g_door = readDoor();

  float ax, ay, az;
  float gx, gy, gz;
  float currentMic = 0;

  //-----------------------------------------------------
  // COLLECT WINDOW
  //-----------------------------------------------------
  while (feature_ix <
         EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 8) {

    if (IMU.accelerationAvailable() &&
        IMU.gyroscopeAvailable()) {

      IMU.readAcceleration(ax, ay, az);
      IMU.readGyroscope(gx, gy, gz);

      float vib = calcVibration(ax, ay, az);
      if (vib > g_peakVib) g_peakVib = vib;

      if (micReady) {
        int len  = micBufLen;
        micReady = false;
        currentMic = calcMicRMS(len);
        if (currentMic > g_peakMic)
          g_peakMic = currentMic;
        micSum += currentMic;
        micN++;
      }

      features[feature_ix++] = ax;
      features[feature_ix++] = ay;
      features[feature_ix++] = az;
      features[feature_ix++] = gx;
      features[feature_ix++] = gy;
      features[feature_ix++] = gz;
      features[feature_ix++] = currentMic;
      features[feature_ix++] = (float)g_door;

      delay(10);
    }
  }

  g_avgMic = (micN > 0) ? micSum / micN : 0;

  //-----------------------------------------------------
  // RUN INFERENCE
  //-----------------------------------------------------
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data     = &raw_feature_get_data;

  ei_impulse_result_t result;

  EI_IMPULSE_ERROR res =
    run_classifier(&signal, &result, false);

  if (res != EI_IMPULSE_OK) {
    Serial.println("Inference failed");
    return;
  }

  //-----------------------------------------------------
  // BEST CLASS FROM MODEL
  //-----------------------------------------------------
  int   bestClass = 0;
  float bestValue = 0;

  for (int i = 0;
       i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > bestValue) {
      bestValue = result.classification[i].value;
      bestClass = i;
    }
  }

  //-----------------------------------------------------
  // RULE-BASED OVERRIDES
  //-----------------------------------------------------

  //===================================================
  // RULE 1 - IDLE
  // Door locked + very still + very quiet
  //===================================================
  if (g_door    == 1                  &&
      g_peakVib <  IDLE_VIB_THRESHOLD &&
      g_peakMic <  IDLE_MIC_THRESHOLD) {
    bestClass  = 2;
    bestValue  = 0.99f;
    g_ruleUsed = 1;
  }

  //===================================================
  // RULE 2 - HAMMER
  // Very high vibration + very loud sound
  //===================================================
  else if (g_peakVib > HAMMER_VIB_THRESHOLD &&
           g_peakMic > HAMMER_MIC_THRESHOLD) {
    bestClass  = 1;
    bestValue  = 0.99f;
    g_ruleUsed = 2;
  }

  //===================================================
  // RULE 3 - FORCED VIBRATION
  // High vibration but quiet
  //===================================================
  else if (g_peakVib > VIBRATION_THRESHOLD  &&
           g_peakMic < HAMMER_MIC_THRESHOLD) {
    bestClass  = 0;
    bestValue  = 0.95f;
    g_ruleUsed = 3;
  }

  //===================================================
  // RULE 4 - NORMAL OPEN
  // Door open + calm
  //===================================================
  else if (g_door    == 0    &&
           g_peakVib <  1.0f) {
    bestClass  = 3;
    bestValue  = 0.95f;
    g_ruleUsed = 4;
  }

  //===================================================
  // RULE 5 - AI MODEL fallback
  //===================================================
  else {
    g_ruleUsed = 0;
    if (bestValue >= CONFIDENCE_THRESHOLD) {
      g_lastClass = bestClass;
    } else {
      bestClass = g_lastClass;
      bestValue = 0.50f;
    }
  }

  if (g_ruleUsed > 0) {
    g_lastClass = bestClass;
  }

  //-----------------------------------------------------
  // OUTPUTS
  //-----------------------------------------------------
  setLED(bestClass);
  alertBuzzer(bestClass);
  drawOLED(bestClass, bestValue,
           g_peakVib, g_peakMic,
           g_door, g_ruleUsed);

  //-----------------------------------------------------
  // SERIAL DEBUG
  //-----------------------------------------------------
  Serial.print("CLASS: ");
  Serial.print(labels[bestClass]);

  switch (g_ruleUsed) {
    case 1: Serial.print(" [RULE:IDLE]"); break;
    case 2: Serial.print(" [RULE:HAMM]"); break;
    case 3: Serial.print(" [RULE:VIB]");  break;
    case 4: Serial.print(" [RULE:DOOR]"); break;
    default:
      Serial.print(bestValue < CONFIDENCE_THRESHOLD
        ? " [AI:LOW]" : " [AI:MODEL]");
      break;
  }

  Serial.print(" | CONF: ");
  Serial.print(bestValue * 100, 1);
  Serial.print("%");

  Serial.print(" | VIB: ");
  Serial.print(g_peakVib, 3);

  Serial.print(" | SOUND: ");
  Serial.print(g_peakMic, 1);

  Serial.print(" | DOOR: ");
  Serial.println(g_door == 1 ? "LOCKED" : "OPEN");

  delay(100);
}

