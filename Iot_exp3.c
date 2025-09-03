#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// WiFi and Adafruit IO
const char *ssid = "Dhanu";       // WiFi Name
const char *pass = "Dhanu!@26";       // WiFi Password
#define MQTT_SERV "io.adafruit.com"
#define MQTT_PORT 1883
#define MQTT_NAME "Dhanu_26"         // Adafruit IO Username
#define MQTT_PASS ""     // Adafruit IO AIO Key

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, MQTT_SERV, MQTT_PORT, MQTT_NAME, MQTT_PASS);

// Adafruit IO Feeds
Adafruit_MQTT_Publish Moisture   = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/Moisture");
Adafruit_MQTT_Publish Temperature = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/Temperature");
Adafruit_MQTT_Publish Humidity    = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/Humidity");
Adafruit_MQTT_Publish SoilTemp    = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/SoilTemp");
Adafruit_MQTT_Publish WeatherData = Adafruit_MQTT_Publish(&mqtt, MQTT_NAME "/f/WeatherData");

Adafruit_MQTT_Subscribe LED  = Adafruit_MQTT_Subscribe(&mqtt, MQTT_NAME "/f/LED");
Adafruit_MQTT_Subscribe Pump = Adafruit_MQTT_Subscribe(&mqtt, MQTT_NAME "/f/Pump");

// OpenWeather API
const char server[] = "api.openweathermap.org";
String nameOfCity = "";
String apiKey = "";
String text;
const char* icon = "";
boolean startJson = false;
int jsonend = 0;

// Timings
unsigned long lastConnectionTime = 0;
const unsigned long postInterval = 600000; // 10 minutes
unsigned long previousTime = 0;
const unsigned long Interval = 50000; // 50 seconds

// Pins and sensors
#define dht_dpin D4
#define DHTTYPE DHT11
#define ONE_WIRE_BUS D2 // D2

const int ldrPin = D1;
const int ledPin = D0;
const int moisturePin = A0;
const int motorPin = D8;

DHT dht(dht_dpin, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

float moisturePercentage;
int temperature, humidity, soiltemp;

#define JSON_BUFF_DIMENSION 2500

void setup() {
  Serial.begin(115200);
  delay(10);
  
  dht.begin();
  sensors.begin();

  pinMode(ldrPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(motorPin, OUTPUT);
  digitalWrite(motorPin, LOW);
  digitalWrite(ledPin, HIGH);

  mqtt.subscribe(&LED);
  mqtt.subscribe(&Pump);

  text.reserve(JSON_BUFF_DIMENSION);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void loop() {
  MQTT_connect();
  unsigned long currentTime = millis();

  if (currentTime - lastConnectionTime > postInterval) {
    lastConnectionTime = currentTime;
    makehttpRequest();
  }

  // LDR logic
  int ldrStatus = analogRead(ldrPin);
  digitalWrite(ledPin, ldrStatus <= 200 ? HIGH : LOW);
  Serial.printf("LDR: %s (%d)\n", ldrStatus <= 200 ? "Dark" : "Bright", ldrStatus);

  // Moisture Sensor logic
  moisturePercentage = 100.0 - ((analogRead(moisturePin) / 1023.0) * 100.0);
  Serial.printf("Soil Moisture: %.2f%%\n", moisturePercentage);
  digitalWrite(motorPin, moisturePercentage < 35 ? HIGH : moisturePercentage > 38 ? LOW : digitalRead(motorPin));

  // Temperature and Humidity
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  sensors.requestTemperatures();
  soiltemp = sensors.getTempCByIndex(0);

  if (currentTime - previousTime >= Interval) {
    Moisture.publish(moisturePercentage);
    Temperature.publish(temperature);
    Humidity.publish(humidity);
    SoilTemp.publish(soiltemp);
    WeatherData.publish(icon);
    previousTime = currentTime;
  }

  // Handle subscriptions
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &LED) {
      const char *val = (char *)LED.lastread;
      Serial.printf("LED command: %s\n", val);
      digitalWrite(ledPin, !strcmp(val, "ON"));
    } else if (subscription == &Pump) {
      const char *val = (char *)Pump.lastread;
      Serial.printf("Pump command: %s\n", val);
      digitalWrite(motorPin, !strcmp(val, "OFF"));
    }
  }

  delay(9000);
}

void MQTT_connect() {
  if (mqtt.connected()) return;

  uint8_t retries = 3;
  while (mqtt.connect() != 0) {
    mqtt.disconnect();
    delay(5000);
    if (--retries == 0) while (1); // Let WDT reset
  }
}

void makehttpRequest() {
  client.stop();
  if (client.connect(server, 80)) {
    client.println("GET /data/2.5/forecast?q=" + nameOfCity + "&APPID=" + apiKey + "&mode=json&units=metric&cnt=2 HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("User-Agent: ArduinoWiFi/1.1");
    client.println("Connection: close");
    client.println();

    unsigned long timeout = millis();
    while (!client.available()) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    char c;
    while (client.available()) {
      c = client.read();
      if (c == '{') { startJson = true; jsonend++; }
      if (c == '}') { jsonend--; }
      if (startJson) text += c;
      if (jsonend == 0 && startJson) {
        parseJson(text.c_str());
        text = "";
        startJson = false;
      }
    }
  } else {
    Serial.println("Connection to weather API failed");
  }
}

void parseJson(const char * jsonString) {
  const size_t bufferSize = 2000;
  DynamicJsonBuffer jsonBuffer(bufferSize);
  JsonObject &root = jsonBuffer.parseObject(jsonString);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return;
  }

  JsonArray &list = root["list"];
  JsonObject &later = list[1];

  String weatherLater = later["weather"][0]["description"];
  Serial.printf("Weather: %s\n", weatherLater.c_str());

  if (weatherLater == "few clouds")      icon = "Few Clouds";
  else if (weatherLater == "rain")       icon = "Rain";
  else if (weatherLater == "broken clouds") icon = "Broken Clouds";
  else                                   icon = "Sunny";
}
