#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <SimpleDHT.h>
#include <Wire.h>              
#include <Adafruit_BMP280.h>   

// =======================
// PIN DEFINITIES
// =======================
#define LIGHT_SENSOR_PIN A1
#define FAN_PIN 5            // PWM pin
#define DHT22_PIN 2

// =======================
// WIFI + MQTT INSTELLINGEN
// =======================
char ssid[] = "AndroidAP1A4C";
char pass[] = "xkae7138";

const char broker[] = "smartgreenhouse.cloud.shiftr.io";
int port = 1883;
const char mqttUser[] = "smartgreenhouse";
const char mqttPass[] = "qyakapmoX1AVEdFc";

// =======================
// OBJECTEN
// =======================
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
SimpleDHT22 dht22;
Adafruit_BMP280 bmp; 

// =======================
unsigned long lastPublish = 0;

// =======================
// VENTILATOR INSTELLINGEN
// =======================
const float TEMP_MIN = 25.0; // Starttemperatuur
const float TEMP_MAX = 35.0; // Max snelheid temperatuur

// Instellingen voor de "Ramp"
const int PWM_MIN = 102;      // 40%
const int PWM_MAX = 255;     // 100%

int fanSpeedPercent = 0; 
bool fanIsRunning = false;   

// =======================
// LOG FUNCTIE
// =======================
void publishLog(String level, String message) {
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long rawTime = WiFi.getTime();
  
  String payload = "{";
  payload += "\"timestamp\":" + String(rawTime) + ",";
  payload += "\"device_id\":\"PLC_SENSOR_NODE_01\","; 
  payload += "\"level\":\"" + level + "\",";
  payload += "\"source\":\"Arduino Sensor\",";
  payload += "\"message\":\"" + message + "\"";
  payload += "}";

  mqttClient.beginMessage("kas/error");
  mqttClient.print(payload);
  mqttClient.endMessage();
}

// =======================
// DATA PUBLISH FUNCTIE
// =======================
void publishData(float avgTemp, float hum, float lightLumen, float lightVolt, int fanPerc, float pressure) {
  unsigned long rawTime = WiFi.getTime();

  String payload = "{";
  payload += "\"timestamp\":" + String(rawTime) + ","; 
  payload += "\"temperature\":" + String(avgTemp, 1) + ",";
  payload += "\"humidity\":" + String(hum, 1) + ",";
  payload += "\"pressure\":" + String(pressure, 1) + ",";
  payload += "\"light_lumen\":" + String(lightLumen, 0) + ",";
  payload += "\"light_volt\":" + String(lightVolt, 2) + ",";
  payload += "\"fan_state\":" + String(fanPerc); 
  payload += "}";

  mqttClient.beginMessage("kas/data");
  mqttClient.print(payload);
  mqttClient.endMessage();
  
  Serial.print(" >> MQTT VERZONDEN"); 
}

// =======================
// CONNECTIE CHECK (LOOP)
// =======================
void checkConnection() {
  // Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ WiFi weg! Herverbinden...");
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, pass);
      // AANGEPAST: Wacht nu 3 seconden per poging (was 5)
      delay(3000); 
    }
    Serial.println("✅ WiFi terug!");
  }

  // Check MQTT
  if (!mqttClient.connected()) {
    Serial.println("\n⚠️ MQTT weg! Herverbinden...");
    if (mqttClient.connect(broker, port)) {
      Serial.println("✅ MQTT terug!");
      publishLog("INFO", "Systeem herverbonden.");
    } else {
      delay(2000);
    }
  }
}

// =======================
// SETUP
// =======================
void setup() {
  Serial.begin(115200);
  
  pinMode(FAN_PIN, OUTPUT);
  analogWrite(FAN_PIN, 0); 

  if (!bmp.begin(0x76)) {
    Serial.println("❌ BMP280 niet gevonden (Check 0x76/0x77)!");
  }

  // ==========================================
  // WIFI VERBINDEN MET 3 SEC TIMEOUT
  // ==========================================
  Serial.println("Starten met WiFi verbinden...");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Poging verbinden met SSID: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, pass);

    // AANGEPAST: Maximaal 3 seconden wachten (6 x 500ms)
    int pogingen = 0;
    while (pogingen < 6 && WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      pogingen++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Verbonden met WiFi!");
    } else {
      Serial.println("\n❌ Mislukt na 3 seconden. Opnieuw proberen...");
      delay(1000);
    }
  }
  
  Serial.print("IP-adres: ");
  Serial.println(WiFi.localIP());

  while (WiFi.getTime() == 0) { delay(1000); }

  mqttClient.setUsernamePassword(mqttUser, mqttPass);
  if (!mqttClient.connect(broker, port)) {
    Serial.println("❌ MQTT connectie mislukt.");
    while (1); 
  }
  Serial.println("✅ Shiftr.io verbonden!");
  publishLog("INFO", "Opstarten compleet.");
  
  Serial.println("\n[TIJD]       AVG_TEMP  VOCHT   DRUK      LICHT    FAN");
}

// =======================
// LOOP
// =======================
void loop() {
  checkConnection();
  mqttClient.poll();

  // --- 1. LICHT ---
  int rawLight = analogRead(LIGHT_SENSOR_PIN);
  float lightVolt = (rawLight / 1023.0) * 3.3;
  float lumen = (lightVolt / 0.57) * 300.0;
  if (lumen > 300) lumen = 301;

  // --- 2. TEMPERATUUR & VOCHT (DHT22) ---
  float dhtTemp = 0;
  float humidity = 0;
  int err = dht22.read2(DHT22_PIN, &dhtTemp, &humidity, NULL);
  if (err != SimpleDHTErrSuccess) {
    delay(2000); return; 
  }

  // --- 3. DRUK & TEMP (BMP280) ---
  float bmpTemp = bmp.readTemperature();
  float pressure = bmp.readPressure() / 100.0F;

  // --- 4. GEMIDDELDE TEMP ---
  float avgTemp = dhtTemp;
  if (bmpTemp != 0.0 && !isnan(bmpTemp)) {
    avgTemp = (dhtTemp + bmpTemp) / 2.0;
  }

  // --- 5. FAN CONTROL (KICKSTART 3s + RAMP) ---
  int targetPWM = 0;

  if (avgTemp <= TEMP_MIN) {
    targetPWM = 0;
    fanSpeedPercent = 0;
    fanIsRunning = false;
  } 
  else {
    if (!fanIsRunning) {
      Serial.print(" [KICKSTART 3s!] ");
      analogWrite(FAN_PIN, 255); // Vol vermogen
      
      // AANGEPAST: Vasthouden voor 3 seconden (3000ms)
      delay(3000);                
      
      fanIsRunning = true;       
    }

    if (avgTemp >= TEMP_MAX) {
      targetPWM = PWM_MAX; 
    } else {
      targetPWM = map(avgTemp * 10, TEMP_MIN * 10, TEMP_MAX * 10, PWM_MIN, PWM_MAX);
    }
    fanSpeedPercent = map(targetPWM, 0, 255, 0, 100);
  }

  analogWrite(FAN_PIN, targetPWM);


  // --- 6. SERIAL MONITOR ---
  Serial.print("[");
  Serial.print(WiFi.getTime());
  Serial.print("] ");
  
  Serial.print("Avg: "); Serial.print(avgTemp, 1); Serial.print("C | ");
  Serial.print("Hum: "); Serial.print(humidity, 1); Serial.print("% | ");
  Serial.print("Press: "); Serial.print(pressure, 0); Serial.print("hPa | ");
  Serial.print("Light: "); Serial.print(lumen, 0); Serial.print("lm | ");
  
  Serial.print("Fan: "); Serial.print(fanSpeedPercent); Serial.print("%");
  if(targetPWM > 0) { Serial.print(" (PWM "); Serial.print(targetPWM); Serial.print(")"); }

  // --- 7. VERSTUREN ---
  if (millis() - lastPublish > 15000) {
    publishData(avgTemp, humidity, lumen, lightVolt, fanSpeedPercent, pressure);
    lastPublish = millis();
  } else {
    Serial.println();
  }

  delay(1000);
}