#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <math.h>

// Replace these with your own Wi-Fi credentials and MQTT ingest key.
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const char* MQTT_INGEST_KEY = "YOUR_INGEST_KEY";

// --- PUBLIC MQTT CONFIGURATION ---
const char* mqttBroker   = "broker.hivemq.com";
const int   mqttPort     = 1883;
const char* mqttTopic    = "drukalert/zone/telemetry";
const char* mqttCmdTopic = "drukalert/zone/cmd";

String clientId = "DrukAlert-MultiArray-" + String((uint32_t)ESP.getEfuseMac(), HEX);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// --- PIN DEFINITIONS ---
// FLOOD ZONE: Ultrasonic sensor + DHT11
const int floodTrigPin = 5;
const int floodEchoPin = 18;
const int floodDhtPin  = 21;
#define DHTTYPE DHT11
DHT floodDht(floodDhtPin, DHTTYPE);

// TRESPASSING ZONE: Ultrasonic sensor + PIR motion sensor
const int trespassTrigPin = 13;
const int trespassEchoPin = 14;
const int pirPin          = 19;

// Outputs: Alert LED & master buzzer
const int alertLedPin     = 23;
const int masterBuzzerPin = 25;

// --- FLOOD THRESHOLDS ---
const float FLOOD_HUM_MIN_PCT = 40.0;
const float FLOOD_DIST_MIN_CM = 2.0;
const float FLOOD_DIST_MAX_CM = 50.0;

// --- TRESPASSING DISTANCE RANGE ---
const float TRESPASS_MIN_CM = 10.0;
const float TRESPASS_MAX_CM = 90.0;

// Change threshold for rolling/passing object detection (cm)
const float OBJECT_DELTA_CM = 3.0;
float prevFloodDist = -1.0;

// --- BUZZER DURATIONS ---
const unsigned long FLOOD_BUZZ_MS    = 5000;
const unsigned long TRESPASS_BUZZ_MS = 13000;

const unsigned long TELEMETRY_INTERVAL_MS = 2500;
const unsigned long FAST_POLL_INTERVAL_MS = 80;

unsigned long floodRingUntilMs    = 0;
unsigned long trespassRingUntilMs = 0;

bool prevActiveFlood = false;
bool prevActiveTres  = false;

bool silencedFlood = false;
bool silencedTres  = false;

unsigned long lastMqttAttemptMs = 0;
unsigned long lastTelemetryMs   = 0;
unsigned long lastPollMs        = 0;

// Latest readings
float floodDist = -1.0;
float floodHum  = -1.0;
bool rollingFloodObjDetected = false;

float trespassDist = -1.0;
int motionDetected = LOW;

String floodEventState    = "NONE";
String trespassEventState = "NONE";

void publishTelemetryNow(bool floodAlert, bool trespassingAlert, bool forceSend = false) {
  unsigned long now = millis();
  if (!forceSend && (now - lastTelemetryMs < TELEMETRY_INTERVAL_MS)) return;

  String payload = "{";
  payload += "\"flood_dist\":" + String(floodDist, 1) + ",";
  payload += "\"flood_hum\":" + String(floodHum, 1) + ",";
  payload += "\"rolling_flood_obj\":" + String(rollingFloodObjDetected ? 1 : 0) + ",";
  payload += "\"trespass_dist\":" + String(trespassDist, 1) + ",";
  payload += "\"motion\":" + String(motionDetected) + ",";
  payload += "\"flood_alert\":" + String(floodAlert ? 1 : 0) + ",";
  payload += "\"trespass_alert\":" + String(trespassingAlert ? 1 : 0) + ",";
  payload += "\"flood_event\":\"" + floodEventState + "\",";
  payload += "\"trespass_event\":\"" + trespassEventState + "\",";
  payload += "\"key\":\"" + String(MQTT_INGEST_KEY) + "\"";
  payload += "}";

  lastTelemetryMs = now;
  if (floodEventState == "RESOLVED") floodEventState = "NONE";
  if (trespassEventState == "RESOLVED") trespassEventState = "NONE";

  if (mqtt.connected()) {
    mqtt.publish(mqttTopic, payload.c_str());
    Serial.println("[MQTT] " + payload);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (msg.indexOf("stop_buzzer") >= 0) {
    bool stopFlood = (msg.indexOf("flood") >= 0) || (msg == "stop_buzzer");
    bool stopTres  = (msg.indexOf("trespass") >= 0) || (msg == "stop_buzzer");

    if (stopFlood) {
      silencedFlood = true;
      floodRingUntilMs = 0;
      prevActiveFlood = false;
      floodEventState = "RESOLVED";
    }
    if (stopTres) {
      silencedTres = true;
      trespassRingUntilMs = 0;
      prevActiveTres = false;
      trespassEventState = "RESOLVED";
    }

    digitalWrite(alertLedPin, LOW);
    digitalWrite(masterBuzzerPin, LOW);
    Serial.println("[DASHBOARD CMD] Buzzer stopped.");
    publishTelemetryNow(false, false, true);
  }
}

void setup_wifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(200);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
  }
}

void reconnectMQTTNonBlocking() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastMqttAttemptMs < 4000) return;
  lastMqttAttemptMs = now;

  if (mqtt.connect(clientId.c_str())) {
    mqtt.subscribe(mqttCmdTopic);
    Serial.println("MQTT connected & subscribed.");
  }
}

float readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 15000);
  if (duration == 0) return -1.0;
  return (duration * 0.0343f) / 2.0f;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(floodTrigPin, OUTPUT);
  pinMode(floodEchoPin, INPUT);

  pinMode(trespassTrigPin, OUTPUT);
  pinMode(trespassEchoPin, INPUT);
  pinMode(pirPin, INPUT);

  pinMode(alertLedPin, OUTPUT);
  pinMode(masterBuzzerPin, OUTPUT);
  digitalWrite(alertLedPin, LOW);
  digitalWrite(masterBuzzerPin, LOW);

  floodDht.begin();

  setup_wifi();
  mqtt.setServer(mqttBroker, mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) setup_wifi();
  reconnectMQTTNonBlocking();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPollMs < FAST_POLL_INTERVAL_MS) return;
  lastPollMs = now;

  // --- 1. FLOOD ZONE ---
  floodDist = readDistance(floodTrigPin, floodEchoPin);
  float rawFloodHum = floodDht.readHumidity();
  if (!isnan(rawFloodHum)) floodHum = rawFloodHum;

  rollingFloodObjDetected = false;
  if (floodDist >= FLOOD_DIST_MIN_CM && floodDist <= FLOOD_DIST_MAX_CM) {
    if (prevFloodDist > 0 && fabs(floodDist - prevFloodDist) >= OBJECT_DELTA_CM) {
      rollingFloodObjDetected = true;
    }
  }
  if (floodDist > 0) prevFloodDist = floodDist;

  bool rawFlood = (floodHum >= FLOOD_HUM_MIN_PCT) &&
                  (floodDist >= FLOOD_DIST_MIN_CM && floodDist <= FLOOD_DIST_MAX_CM) &&
                  rollingFloodObjDetected;

  // --- 2. TRESPASSING ZONE ---
  motionDetected = digitalRead(pirPin);
  trespassDist = readDistance(trespassTrigPin, trespassEchoPin);

  bool rawTres = (motionDetected == HIGH) &&
                 (trespassDist >= TRESPASS_MIN_CM && trespassDist <= TRESPASS_MAX_CM);

  if (!rawFlood) silencedFlood = false;
  if (!rawTres)  silencedTres  = false;

  bool immediatePublish = false;

  if (rawFlood && !silencedFlood) {
    floodRingUntilMs = now + FLOOD_BUZZ_MS;
    if (!prevActiveFlood) {
      floodEventState = "TRIGGERED";
      immediatePublish = true;
    }
    prevActiveFlood = true;
  } else if (!rawFlood && prevActiveFlood && now >= floodRingUntilMs) {
    prevActiveFlood = false;
    floodEventState = "RESOLVED";
    immediatePublish = true;
  }

  if (rawTres && !silencedTres) {
    trespassRingUntilMs = now + TRESPASS_BUZZ_MS;
    if (!prevActiveTres) {
      trespassEventState = "TRIGGERED";
      immediatePublish = true;
    }
    prevActiveTres = true;
  } else if (!rawTres && prevActiveTres && now >= trespassRingUntilMs) {
    prevActiveTres = false;
    trespassEventState = "RESOLVED";
    immediatePublish = true;
  }

  bool floodActive    = (rawFlood || (now < floodRingUntilMs)) && !silencedFlood;
  bool trespassActive = (rawTres || (now < trespassRingUntilMs)) && !silencedTres;

  if (floodActive || trespassActive) {
    digitalWrite(alertLedPin, HIGH);
    digitalWrite(masterBuzzerPin, HIGH);
  } else {
    digitalWrite(alertLedPin, LOW);
    digitalWrite(masterBuzzerPin, LOW);
  }

  publishTelemetryNow(floodActive, trespassActive, immediatePublish);
}