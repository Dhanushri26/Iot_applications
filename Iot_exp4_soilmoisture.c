#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// WiFi credentials
const char *ssid = "Dhanu";
const char *pass = "Dhanu!@26";

// Adafruit IO settings
#define MQTT_SERV "io.adafruit.com"
#define MQTT_PORT 1883
#define MQTT_NAME "Dhanu_26" // Your Adafruit IO username
#define MQTT_PASS ""

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, MQTT_SERV, MQTT_PORT, MQTT_NAME, MQTT_PASS);

// Adafruit IO Feed
Adafruit_MQTT_Publish MoistureFeed = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/Moisture");

// Sensor pin
const int moisturePin = A0;

void setup() {
  Serial.begin(9600);
  delay(10);

  // Connect to Wi-Fi
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");

  // Connect to MQTT server (Adafruit IO)
  MQTT_connect();
}

void loop() {
  MQTT_connect(); // Keep MQTT connected

  // Read soil moisture
  int sensorValue = analogRead(moisturePin);
  float moisturePercent = 100.0 - (sensorValue / 1023.0) * 100.0;

  Serial.print("Soil Moisture (%): ");
  Serial.println(moisturePercent);

  // Publish to Adafruit IO
  if (!MoistureFeed.publish(moisturePercent)) {
    Serial.println("Publish failed");
  } else {
    Serial.println("Published to Adafruit IO");
  }

  delay(10000); // wait 10 seconds before next read
}

void MQTT_connect() {
  if (mqtt.connected()) return;

  uint8_t retries = 3;
  while (mqtt.connect() != 0) {
    mqtt.disconnect();
    delay(5000);
    if (--retries == 0) while (1); // reboot if connection fails
  }
}
