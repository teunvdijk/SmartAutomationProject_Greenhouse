#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <SimpleDHT.h>

// =======================
// SENSOR PIN DEFINITIONS
// =======================
#define LIGHT_SENSOR_PIN A1
#define FAN_PIN 5            
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
// NIEUW: GENERIEKE LOG FUNCTIE (ERROR & INFO)
// =======================
// We hebben 'level' toegevoegd zodat je ERROR en INFO kunt sturen
void publishLog(String level, String message) {
  // Als er geen wifi is, kunnen we niks sturen, dus check dat eerst
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long rawTime = WiFi.getTime();
  
  String payload = "{";
  payload += "\"timestamp\":" + String(rawTime) + ",";
  payload += "\"device_id\":\"PLC_SENSOR_NODE_01\","; 
  payload += "\"level\":\"" + level + "\","; // Hier komt nu ERROR of INFO
  payload += "\"source\":\"Arduino Sensor\",";
  payload += "\"message\":\"" + message + "\"";
  payload += "}";

  // We gebruiken hetzelfde topic 'kas/error' voor alle systeemlogs
  mqttClient.beginMessage("kas/error");
  mqttClient.print(payload);
  mqttClient.endMessage();

  Serial.print("📋 LOG [" + level + "]: ");
  Serial.println(message);
}

// =======================
// MQTT PUBLISH FUNCTION
// =======================
void publishData(float temp, float hum, float lightLumen, float lightVolt, int fanSt) {
  unsigned long rawTime = WiFi.getTime();

  String payload = "{";
  payload += "\"timestamp\":" + String(rawTime) + ","; 
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
// NIEUW: WIFI EN MQTT CHECK FUNCTIE
// =======================
void checkConnection() {
  // 1. Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi verbinding verbroken! Proberen te herverbinden...");
    
    // Blijf proberen tot we weer WiFi hebben
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      delay(5000); // Wacht 5 seconden per poging
      Serial.print(".");
    }
    Serial.println("\n✅ WiFi hersteld!");
  }

  // 2. Check MQTT (Als WiFi er is, maar MQTT is weggevallen)
  if (!mqttClient.connected()) {
    Serial.println("⚠️ MQTT verbinding verbroken! Opnieuw verbinden...");
    
    // Probeer opnieuw in te loggen
    if (mqttClient.connect(broker, port)) {
      Serial.println("✅ MQTT verbinding hersteld!");
      // Stuur direct een log naar de cloud dat we er weer zijn
      publishLog("INFO", "Systeem herverbonden met WiFi/MQTT na storing.");
    } else {
      Serial.print("❌ MQTT Herverbinden mislukt. Code: ");
      Serial.println(mqttClient.connectError());
      delay(2000);
    }
  }
}

// =======================
// SETUP
// =======================
void setup() {
  Serial.begin(115200);
  // while (!Serial); // Haal dit eventueel weg als je hem loskoppelt van de PC, anders wacht hij eeuwig

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); 

  Serial.println("Verbinden met WiFi...");
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ Verbonden met WiFi!");
  Serial.print("IP-adres: ");
  Serial.println(WiFi.localIP());

  Serial.println("Tijd synchroniseren...");
  int retry = 0;
  while (WiFi.getTime() == 0 && retry < 10) {
    delay(1000);
    retry++;
  }

  mqttClient.setUsernamePassword(mqttUser, mqttPass);

  Serial.println("Verbinden met MQTT broker...");
  if (!mqttClient.connect(broker, port)) {
    Serial.print("❌ MQTT verbinding mislukt! Code: ");
    Serial.println(mqttClient.connectError());
    while (1);
  }

  Serial.println("✅ Verbonden met shiftr.io!");

  // NIEUW: Loggen dat we zijn opgestart
  publishLog("INFO", "Arduino Sensor Node succesvol opgestart.");
}

// =======================
// LOOP
// =======================
void loop() {
  // NIEUW: Check elke ronde of we nog verbinding hebben
  checkConnection();

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
    Serial.print("Fout bij uitlezen DHT22, code: ");
    Serial.println(err);
    
    if (millis() - lastPublish > 5000) {
       // AANGEPAST: Gebruik nu de generieke log functie met level ERROR
       publishLog("ERROR", "DHT22 Sensor leesfout (Kabel los?)");
       lastPublish = millis(); 
    }
    delay(2000); 
    return; 
  }

  // ====== FAN CONTROL ======
  if (temperature >= FAN_THRESHOLD_TEMP) {
    fanState = 1;
    digitalWrite(FAN_PIN, HIGH); 
  } else {
    fanState = 0;
    digitalWrite(FAN_PIN, LOW); 
  }

  // ====== SERIAL MONITOR LOGGING ======
  Serial.print("[");
  Serial.print(WiFi.getTime()); 
  Serial.print("] Temp: ");
  Serial.print(temperature);
  Serial.print(" | Hum: ");
  Serial.print(humidity);
  Serial.print(" | Light: ");
  Serial.print(lumen);
  Serial.print(" | Fan: ");
  Serial.println(fanState ? "Aan" : "Uit");

  // ====== MQTT PUBLISH EVERY 15s ======
  if (millis() - lastPublish > 15000) {
    publishData(temperature, humidity, lumen, voltage, fanState);
    lastPublish = millis();
  }

  delay(1000);
}