/*
  SMART RAIN-AWARE DRYING RACK
  Board   : ESP32 DevKit
  Sensor  : FC-37 + DHT11
  Actuator: Servo SG90 + Relay/Fan
  Broker  : EMQX Cloud, MQTT over TLS port 8883

  MQTT topics:
    Publish   smartrain/data
    Publish   smartrain/status
    Publish   smartrain/availability
    Subscribe smartrain/control

  Control JSON examples:
    {"mode":"AUTO"}
    {"mode":"MANUAL","servo":"CLOSED","fan":true}
    {"mode":"MANUAL","servo":"OPEN","fan":false}
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>

// =====================================================
// 1. WIFI AND MQTT CREDENTIALS
// =====================================================
const char* WIFI_SSID = "username WiFi";
const char* WIFI_PASSWORD = "password WiFi";

const char* MQTT_HOST = "BROKER HOST";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "username MQTT";
const char* MQTT_PASS = "password MQTT";

const char* TOPIC_DATA = "smartrain/data";
const char* TOPIC_STATUS = "smartrain/status";
const char* TOPIC_CONTROL = "smartrain/control";
const char* TOPIC_AVAIL = "smartrain/availability";

// =====================================================
// 2. CA CERTIFICATE (PEM)
// =====================================================
static const char ROOT_CA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)CERT";

// =====================================================
// 3. PIN AND DEVICE CONFIGURATION
// =====================================================
#define DHT_TYPE DHT11
const int PIN_RAIN = 34;
const int PIN_DHT = 4;
const int PIN_RELAY = 27;
const int PIN_SERVO = 18;

const bool RELAY_ACTIVE_LOW = true;
const int SERVO_OPEN = 0;
const int SERVO_CLOSE = 90;
const int RAIN_THRESHOLD = 2500;
const float HUM_ON = 52.0;
const float HUM_OFF = 48.0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long RAIN_CONFIRM = 5000;
const unsigned long DRY_CONFIRM = 30000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;

DHT dht(PIN_DHT, DHT_TYPE);
Servo servo;
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

int rainRaw = 4095;
float temperature = NAN;
float humidity = NAN;

bool isRaining = false;
bool rainPending = false;
bool pendingRainValue = false;
bool servoClosed = false;
bool fanOn = false;

String currentMode = "AUTO";
unsigned long rainPendingSince = 0;
unsigned long lastSensor = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

// =====================================================
// 4. ACTUATORS
// =====================================================
void setRelay(bool on) {
  // Interlock keselamatan: kipas tidak boleh ON jika atap terbuka.
  if (on && !servoClosed) on = false;
  const bool outputHigh = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(PIN_RELAY, outputHigh ? HIGH : LOW);
  fanOn = on;
}

void setServoClosed(bool closed) {
  servoClosed = closed;
  servo.write(closed ? SERVO_CLOSE : SERVO_OPEN);
  if (!servoClosed) setRelay(false);
}

// =====================================================
// 5. WIFI
// =====================================================
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttempt = millis();
  Serial.print("Connecting to WiFi");
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiAttempt = millis();
  }
}

// =====================================================
// 6. MQTT PUBLISH HELPERS
// =====================================================
void publishStatus() {
  if (!mqtt.connected()) return;

  StaticJsonDocument<160> doc;
  doc["mode"] = currentMode;
  doc["servo"] = servoClosed ? "CLOSED" : "OPEN";
  doc["fan"] = fanOn;

  char payload[160];
  serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(TOPIC_STATUS, payload, true);
}

void publishData() {
  if (!mqtt.connected()) return;

  StaticJsonDocument<256> doc;
  if (isnan(temperature)) doc["temperature"] = nullptr;
  else doc["temperature"] = temperature;

  if (isnan(humidity)) doc["humidity"] = nullptr;
  else doc["humidity"] = humidity;

  doc["rain"] = isRaining;
  doc["rain_raw"] = rainRaw;
  doc["roof"] = servoClosed ? "CLOSED" : "OPEN";
  doc["fan"] = fanOn;
  doc["mode"] = currentMode;

  char payload[256];
  serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(TOPIC_DATA, payload);
}

// =====================================================
// 7. MQTT CALLBACK - JSON CONTROL FROM WEBSITE
// =====================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_CONTROL) return;

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("Invalid control JSON: ");
    Serial.println(error.c_str());
    return;
  }

  const char* requestedMode = doc["mode"] | "";

  if (strcmp(requestedMode, "AUTO") == 0) {
    currentMode = "AUTO";
    Serial.println("Website command: AUTO");
    return;
  }

  if (strcmp(requestedMode, "MANUAL") != 0) {
    Serial.println("Unknown mode command");
    return;
  }

  currentMode = "MANUAL";

  // Jika field servo/fan tidak dikirim, pertahankan status sekarang.
  const char* requestedServo = doc["servo"] | (servoClosed ? "CLOSED" : "OPEN");
  const bool requestedFan = doc["fan"] | fanOn;
  const bool closeRoof = strcmp(requestedServo, "CLOSED") == 0;

  setServoClosed(closeRoof);
  setRelay(requestedFan && servoClosed);

  Serial.println("Website command: MANUAL state updated");
  publishStatus();
}

// =====================================================
// 8. MQTT CONNECTION
// =====================================================
void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL) return;

  lastMQTTAttempt = millis();
  String clientId = "SmartRainESP32_" + WiFi.macAddress();
  Serial.println("Connecting to MQTT...");

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                   TOPIC_AVAIL, 1, true, "offline")) {
    Serial.println("MQTT Connected!");
    mqtt.subscribe(TOPIC_CONTROL, 0);
    mqtt.publish(TOPIC_AVAIL, "online", true);
    publishStatus();
  } else {
    Serial.print("MQTT connection failed, state: ");
    Serial.println(mqtt.state());
  }
}

// =====================================================
// 9. SENSOR READING AND RAIN DEBOUNCE
// =====================================================
void readSensors() {
  rainRaw = analogRead(PIN_RAIN);

  const float h = dht.readHumidity();
  const float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
  } else {
    Serial.println("DHT read failed; keeping previous values");
  }
}

void updateRainState(unsigned long now) {
  const bool rainNow = rainRaw < RAIN_THRESHOLD;

  if (rainNow == isRaining) {
    rainPending = false;
    return;
  }

  if (!rainPending || rainNow != pendingRainValue) {
    rainPending = true;
    pendingRainValue = rainNow;
    rainPendingSince = now;
    return;
  }

  const unsigned long required = rainNow ? RAIN_CONFIRM : DRY_CONFIRM;
  if (now - rainPendingSince >= required) {
    isRaining = rainNow;
    rainPending = false;
  }
}

// =====================================================
// 10. AUTO / MANUAL LOGIC
// =====================================================
void applyAutoLogic() {
  setServoClosed(isRaining);

  if (!servoClosed) {
    setRelay(false);
  } else if (!isnan(humidity)) {
    if (!fanOn && humidity > HUM_ON) setRelay(true);
    else if (fanOn && humidity < HUM_OFF) setRelay(false);
  }
}

void printSerial() {
  Serial.printf(
    "RainRaw=%d | Status=%s | Temp=%.1fC | Hum=%.1f%% | Roof=%s | Fan=%s | Mode=%s\n",
    rainRaw,
    isRaining ? "RAIN" : "DRY",
    temperature,
    humidity,
    servoClosed ? "CLOSED" : "OPEN",
    fanOn ? "ON" : "OFF",
    currentMode.c_str()
  );
}

// =====================================================
// 11. SETUP AND LOOP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);
  fanOn = false;

  pinMode(PIN_RAIN, INPUT);
  dht.begin();

  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 500, 2400);
  servo.write(SERVO_OPEN);
  servoClosed = false;

  secureClient.setCACert(ROOT_CA);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(60);

  startWiFi();
  Serial.println("Smart Rain-Aware Drying Rack starting...");
}

void loop() {
  maintainWiFi();
  maintainMQTT();
  mqtt.loop();

  const unsigned long now = millis();
  if (now - lastSensor >= SENSOR_INTERVAL) {
    lastSensor = now;

    readSensors();
    updateRainState(now);

    if (currentMode == "AUTO") applyAutoLogic();

    publishData();
    publishStatus();
    printSerial();
  }
}
