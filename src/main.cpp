#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <ESP32S3_MPU6050_-_Predictive_Maintenance_inferencing.h>

// --- Configuration ---
char WIFI_SSID[] = "Wokwi-GUEST";
char WIFI_PASSWORD[] = "";
const char* token = "aocllMu7tWcRfWYZ3qCr"; // <-- Replace with your Access Token
const char* thingsboard_server = "thingsboard.cloud"; // <-- Replace with your ThingsBoard server address
// if DNS fail use demo.thingsboard.io[104.196.24.70] | thingsboard.cloud[44.194.165.190] | eu.thingsboard.cloud[3.69.110.78]
// or try ping <domain name> to get IP address from CMD

const int port = 1883;

const char STUDENT_ID[]      = "68083787";   // <-- Replace with your Student ID
const char FIRMWARE_VERSION[] = "1.0";

// --- [NEW] Model decision thresholds ---
#define UNBALANCE_THRESHOLD 0.5f   // unbalance prob >= this  -> fault = "unbalance"
#define ANOMALY_THRESHOLD   0.3f   // anomaly score  >  this  -> fault = "unknown_anomaly"

// --- [NEW] Accelerometer unit conversion ---
// MPU6050_light returns acceleration in g; the Edge Impulse dataset is in m/s^2.
// If results look wrong, try 1.0f.
#define ACC_SCALE 9.80665f

// --- Hardware Pins (from diagram.json — ESP32-S3) ---
#define pinLED 4       // LED cathode → GPIO 4 (active-LOW: sink to turn ON)
#define LED_ON  LOW    // LED wired 3V3 → R → Anode → Cathode → GPIO: LOW = ON
#define LED_OFF HIGH

bool ledState = false;  // current LED state (true = ON), reported as "led_state" attribute

#define pinSDA 8       // MPU6050 SDA → GPIO 8 (10kΩ pull-up to 3V3)
#define pinSCL 9       // MPU6050 SCL → GPIO 9 (10kΩ pull-up to 3V3)

// --- MPU6050 Configuration ---
#define NUM_MPUS 1
MPU6050 mpu[NUM_MPUS] = { MPU6050(Wire) };
const int AD0pin[NUM_MPUS] = {5};   // AD0 → GPIO 5

// --- Peripheral Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- [NEW] Inference buffer: 1 window = 200 samples x 3 axes = 600 values ---
static_assert(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME == 3, "Model expects 3 axes (accX, accY, accZ)");
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t featureIndex = 0;
unsigned long lastSampleMicros = 0;
const unsigned long sampleIntervalUs = (unsigned long)(EI_CLASSIFIER_INTERVAL_MS * 1000); // 100 Hz -> 10,000 us

// --- WiFi / MQTT non-blocking reconnect state ---
unsigned long lastWiFiRetry = 0;
const unsigned long wifiRetryInterval = 500;

unsigned long lastMQTTRetry = 0;
const unsigned long mqttRetryInterval = 5000;

// --- Prototypes ---
void onMessage(char* topic, byte* payload, unsigned int length);
void connectToWiFi();
void connectToMQTTBroker();
void setupMPU();
void SelectMPU(int selection);
void publishAttributes();
void sampleMPU();                 // [NEW]
void runInference();              // [NEW]
void setLed(bool on);             // [NEW]
void publishAnomalyTelemetry(const char* fault, float anomalyScore, float unbalanceProb); // [NEW]

// ====================================================================
//  setup()
// ====================================================================
void setup() {
  Serial.begin(115200);  // ESP32-S3 convention; matches boot ROM baud rate
  pinMode(pinLED, OUTPUT);
  digitalWrite(pinLED, LED_OFF);

  // --- Initialize I2C and MPU6050 ---
  setupMPU();

  // --- WiFi (non-blocking initial connect) ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);  // Wokwi-GUEST is on channel 6
  Serial.print("Connecting to WiFi");
  // Allow up to ~10s for initial connect, then fall through to loop()
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi");
  } else {
    Serial.println("\n⚠️ WiFi not connected yet — will retry in loop()");
  }

  client.setServer(thingsboard_server, port);
  client.setBufferSize(512);
  // *** set Callback receive from Server ***
  client.setCallback(onMessage);

  // [NEW] Print model info
  Serial.printf("🧠 Model: %d samples x %d axes @ %.0f Hz, %d classes\n",
                EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
                1000.0f / EI_CLASSIFIER_INTERVAL_MS, EI_CLASSIFIER_LABEL_COUNT);

  lastSampleMicros = micros();
}

// ====================================================================
//  loop()
// ====================================================================
void loop() {
  // [CHANGED] Network handling no longer uses `return`,
  // so inference + Serial output keep running even when WiFi/MQTT is down.
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  } else if (!client.connected()) {
    connectToMQTTBroker();
  } else {
    client.loop();
  }

  // [NEW] Collect samples at 100 Hz; runs inference when a window is full
  sampleMPU();
}

// ====================================================================
//  [NEW] Sampling + Inference
// ====================================================================

// Read one sample every 10 ms (micros-based). When 200 samples are collected,
// run the classifier on the full window.
void sampleMPU() {
  unsigned long now = micros();
  if (now - lastSampleMicros < sampleIntervalUs) return;

  if (now - lastSampleMicros > 3 * sampleIntervalUs) {
    // Loop was blocked (e.g. MQTT reconnect / inference) — gap in data,
    // so start a fresh window to keep the signal continuous.
    featureIndex = 0;
    lastSampleMicros = now;
  } else {
    lastSampleMicros += sampleIntervalUs;
  }

  SelectMPU(0);
  mpu[0].update();
  // Axis order must match the impulse: accX, accY, accZ
  features[featureIndex++] = mpu[0].getAccX() * ACC_SCALE;
  features[featureIndex++] = mpu[0].getAccY() * ACC_SCALE;
  features[featureIndex++] = mpu[0].getAccZ() * ACC_SCALE;

  if (featureIndex >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
    featureIndex = 0;
    runInference();
  }
}

void runInference() {
  signal_t signal;
  int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  if (err != 0) {
    Serial.printf("❌ signal_from_buffer failed (%d)\n", err);
    return;
  }

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
  if (res != EI_IMPULSE_OK) {
    Serial.printf("❌ run_classifier failed (%d)\n", res);
    return;
  }

  // --- Read class probabilities ---
  float normalProb = 0.0f, unbalanceProb = 0.0f;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    const char* label = result.classification[i].label;
    if (strstr(label, "Unbalance"))   unbalanceProb = result.classification[i].value;
    else if (strstr(label, "Normal")) normalProb    = result.classification[i].value;
  }

  // --- Read anomaly score (K-means) ---
  float anomalyScore = 0.0f;
#if EI_CLASSIFIER_HAS_ANOMALY
  anomalyScore = result.anomaly;
#endif

  // --- Decide machine state ---
  const char* fault = nullptr;
  if (unbalanceProb >= UNBALANCE_THRESHOLD) {
    fault = "unbalance";
  } else if (anomalyScore > ANOMALY_THRESHOLD) {
    fault = "unknown_anomaly";        // looks like no pattern seen in training
  }
  bool anomalyNow = (fault != nullptr);

  // --- Homework 2: print state on Serial every inference ---
  if (anomalyNow) {
    Serial.printf("🔴 Machine state: anomaly | fault=%s | normal=%.3f unbalance=%.3f anomaly_score=%.3f\n",
                  fault, normalProb, unbalanceProb, anomalyScore);
  } else {
    Serial.printf("🟢 Machine state: normal  | normal=%.3f unbalance=%.3f anomaly_score=%.3f\n",
                  normalProb, unbalanceProb, anomalyScore);
  }

  // --- Homework 1: LED + telemetry only on anomaly ---
  setLed(anomalyNow);
  if (anomalyNow) {
    publishAnomalyTelemetry(fault, anomalyScore, unbalanceProb);
  }
  // Normal -> send nothing to ThingsBoard
}

// Turn LED on/off; report led_state attribute only when it changes.
void setLed(bool on) {
  if (on == ledState) return;
  ledState = on;
  digitalWrite(pinLED, ledState ? LED_ON : LED_OFF);
  Serial.print(F("💡 LED Status: "));
  Serial.println(ledState ? "ON" : "OFF");
  if (client.connected()) publishAttributes();
}

void publishAnomalyTelemetry(const char* fault, float anomalyScore, float unbalanceProb) {
  if (!client.connected()) {
    Serial.println(F("⚠️ MQTT not connected — anomaly telemetry not sent"));
    return;
  }

  JsonDocument doc;
  doc["machine_state"]  = "anomaly";
  doc["fault"]          = fault;
  doc["anomaly_score"]  = anomalyScore;
  doc["unbalance_prob"] = unbalanceProb;

  char payload[192];
  size_t len = serializeJson(doc, payload, sizeof(payload));
  if (len > 0 && client.publish("v1/devices/me/telemetry", payload)) {
    Serial.print(F("📨 Anomaly sent: "));
    Serial.println(payload);
  } else {
    Serial.println(F("❌ Telemetry publish failed"));
  }
}

// ====================================================================
//  MPU6050 Functions
// ====================================================================

// Initialize I2C on diagram-specified pins and set up all MPU6050 sensors.
void setupMPU() {
  Wire.begin(pinSDA, pinSCL);
  Wire.setTimeOut(1000);  // guard against a wedged I2C bus (avoids interrupt-WDT panic)

  for (int i = 0; i < NUM_MPUS; i++) {
    pinMode(AD0pin[i], OUTPUT);
    digitalWrite(AD0pin[i], HIGH);  // default HIGH → address 0x69 (deselected)
  }

  Serial.println(F("Initializing MPU-6050 sensors. Do not move the sensor."));
  delay(500);

  for (int i = 0; i < NUM_MPUS; i++) {
    SelectMPU(i);

    byte status = mpu[i].begin();
    Serial.print(F("Initializing MPU-6050 "));
    Serial.print(i);
    Serial.print(F(", error = "));
    Serial.print(status);
    if (status == 0)
      Serial.print(F(" (no error)"));
    Serial.println();

    // calcOffsets() runs a tight 500-iteration I2C polling loop that hangs
    // Wokwi's simulated I2C bus and trips the interrupt watchdog — skip it
    // here; offsets default to 0 (set in the MPU6050 constructor).
  }

  Serial.println(F("MPU-6050 initialization done"));
  Serial.println(F("Click on a MPU-6050 module and change the acceleration and gyro."));
}

// Choose a MPU by index — drives AD0 LOW on the selected sensor (address 0x68),
// HIGH on all others (address 0x69).
void SelectMPU(int selection) {
  for (int i = 0; i < NUM_MPUS; i++) {
    if (i == selection)
      digitalWrite(AD0pin[i], LOW);   // selected → 0x68
    else
      digitalWrite(AD0pin[i], HIGH);  // not selected → 0x69
  }
}

// ====================================================================
//  RPC Callback Function
// ====================================================================
// this function active when Postman/ThingsBoard send API
// Note: the model will override the LED on the next inference (every ~2 s).
void onMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("📩 Message arrived ["));
  Serial.print(topic);
  Serial.println("]");

  char responseTopic[128];
  strncpy(responseTopic, topic, sizeof(responseTopic) - 1);
  responseTopic[sizeof(responseTopic) - 1] = '\0';
  char* reqPtr = strstr(responseTopic, "request");
  if (reqPtr) {
    size_t tailLen = strlen(reqPtr + 7);
    memmove(reqPtr + 8, reqPtr + 7, tailLen + 1);
    memcpy(reqPtr, "response", 8);
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print(F("❌ JSON parse error: "));
    Serial.println(err.c_str());
    return;
  }

  const char* methodName = doc["method"];  // "setLed"
  bool params = doc["params"];             // true or false

  bool handled = false;
  if (methodName && strcmp(methodName, "setLed") == 0) {
    handled = true;
    ledState = params;
    digitalWrite(pinLED, ledState ? LED_ON : LED_OFF);
    Serial.print(F("💡 LED Status: "));
    Serial.println(ledState ? "ON" : "OFF");
  }

  JsonDocument resp;
  resp["success"] = handled;
  if (handled) resp["led_state"] = ledState;
  char respPayload[64];
  serializeJson(resp, respPayload, sizeof(respPayload));
  client.publish(responseTopic, respPayload);

  if (handled) publishAttributes();
}

// ====================================================================
//  Non-blocking WiFi Reconnect
// ====================================================================
void connectToWiFi() {
  if (millis() - lastWiFiRetry < wifiRetryInterval) return;
  lastWiFiRetry = millis();

  Serial.print(".");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Connected to WiFi");
  }
}

// ====================================================================
//  Non-blocking MQTT Connection
// ====================================================================
void connectToMQTTBroker() {
  if (millis() - lastMQTTRetry < mqttRetryInterval) return;
  lastMQTTRetry = millis();

  Serial.println(F("☁️ Connecting to ThingsBoard..."));
  if (client.connect("ESP32_Client", token, NULL)) {
    Serial.println(F("✅ Connected"));
    client.subscribe("v1/devices/me/rpc/request/+");
    publishAttributes();
  } else {
    Serial.print(F("❌ MQTT connect failed, rc="));
    Serial.println(client.state());
  }
}

// ====================================================================
//  ThingsBoard Attributes
// ====================================================================
void publishAttributes() {
  JsonDocument doc;
  doc["student_id"] = STUDENT_ID;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["led_state"] = ledState;

  char payload[128];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (len > 0 && client.publish("v1/devices/me/attributes", payload)) {
    Serial.print(F("📨 Attributes sent: "));
    Serial.println(payload);
  } else {
    Serial.println(F("❌ Attributes publish failed"));
  }
}