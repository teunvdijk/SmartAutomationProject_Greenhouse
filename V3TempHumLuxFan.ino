#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <SimpleDHT.h>

// =======================
// SENSOR PIN DEFINITIONS
// =======================
#define LIGHT_SENSOR_PIN A1
#define FAN_PIN 5            // digitale pin, ventilator aan/uit
#define DHT22_PIN 2

// =======================
// WIFI + MQTT SETTINGS
// =======================
char ssid[] = "AndroidAP1A4C";
char pass[] = "xkae7138";

const char broker[] = "smartgreenhouse.cloud.shiftr.io";
int port = 1883;
const char mqttUser[] = "smartgreenhouse";
const char mqttPass[] = "qyakapmoX1AVEdFc";

// =======================
// OBJECTS
// =======================
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
SimpleDHT22 dht22;

// =======================
unsigned long lastPublish = 0;

// =======================
// FAN CONTROL PARAMETERS
// =======================
const float FAN_THRESHOLD_TEMP = 25.0; // °C

int fanState = 0; // 0 = uit, 1 = aan

// =======================
// MQTT PUBLISH FUNCTION
// =======================
void publishData(float temp, float hum, float lightLumen, float lightVolt, int fanSt) {
  String payload = "{";
  payload += "\"temperature\":" + String(temp, 1) + ",";
  payload += "\"humidity\":" + String(hum, 1) + ",";
  payload += "\"light_lumen\":" + String(lightLumen, 0) + ",";
  payload += "\"light_volt\":" + String(lightVolt, 2) + ",";
  payload += "\"fan_state\":" + String(fanSt);
  payload += "}";

  mqttClient.beginMessage("kas/data");
  mqttClient.print(payload);
  mqttClient.endMessage();

  Serial.print("MQTT published: ");
  Serial.println(payload);
}

// =======================
// SETUP
// =======================
void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // ventilator start uit

  Serial.println("Verbinden met WiFi...");
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ Verbonden met WiFi!");
  Serial.print("IP-adres: ");
  Serial.println(WiFi.localIP());

  mqttClient.setUsernamePassword(mqttUser, mqttPass);

  Serial.println("Verbinden met MQTT broker...");
  if (!mqttClient.connect(broker, port)) {
    Serial.print("❌ MQTT verbinding mislukt! Code: ");
    Serial.println(mqttClient.connectError());
    while (1);
  }

  Serial.println("✅ Verbonden met shiftr.io!");
}

// =======================
// LOOP
// =======================
void loop() {
  mqttClient.poll();

  // ====== LIGHT SENSOR ======
  int raw = analogRead(LIGHT_SENSOR_PIN);
  float voltage = (raw / 1023.0) * 3.3;

  float maxVolt = 0.57;
  float lumen = (voltage / maxVolt) * 300.0;
  if (lumen > 300) lumen = 301;

  // ====== READ DHT22 ======
  float temperature = 0;
  float humidity = 0;
  int err = dht22.read2(DHT22_PIN, &temperature, &humidity, NULL);

  if (err != SimpleDHTErrSuccess) {
    Serial.println("Fout bij uitlezen DHT22");
    return;
  }

  // ====== FAN CONTROL (ON/OFF) ======
  if (temperature >= FAN_THRESHOLD_TEMP) {
    fanState = 1;
    digitalWrite(FAN_PIN, HIGH); // ventilator aan
  } else {
    fanState = 0;
    digitalWrite(FAN_PIN, LOW);  // ventilator uit
  }

  // ====== LOGGING ======
  Serial.print("Temp: ");
  Serial.print(temperature);
  Serial.print(" °C | Hum: ");
  Serial.print(humidity);
  Serial.print(" % | Light: ");
  Serial.print(lumen);
  Serial.print(" lm | Volt: ");
  Serial.print(voltage, 2);
  Serial.print(" V | Fan: ");
  Serial.print(fanState ? "Aan" : "Uit");
  Serial.println();

  // ====== MQTT PUBLISH EVERY 5s ======
  if (millis() - lastPublish > 5000) {
    publishData(temperature, humidity, lumen, voltage, fanState);
    lastPublish = millis();
  }

  delay(1000);
}
