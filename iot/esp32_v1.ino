#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <math.h>

const char* ssid = "";
const char* password = "";
const char* MQTT_INGEST_KEY = "";

// --- PUBLIC MQTT CONFIGURATION ---
const char* mqttBroker   = "broker.hivemq.com";
const int   mqttPort     = 1883;
const char* mqttTopic    = "drukalert/zone/telemetry";
const char* mqttCmdTopic = "drukalert/zone/cmd";

String clientId = "DrukAlert-MultiArray-" + String((uint32_t)ESP.getEfuseMac(), HEX);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// --- PIN DEFINITIONS FOR MULTI-SENSOR ARRAY ---
// ZONE 1: FLOOD (Ultrasonic 1 + DHT11)
const int floodTrigPin = 5;
const int floodEchoPin = 18;
const int floodDhtPin  = 21;
#define DHTTYPE DHT11
DHT floodDht(floodDhtPin, DHTTYPE);

// ZONE 2: LANDSLIDE (Ultrasonic 2 + DHT11)
const int landTrigPin = 16;
const int landEchoPin = 17;
const int landDhtPin  = 22;
DHT landDht(landDhtPin, DHTTYPE);

// ZONE 3: TRESPASSING (Ultrasonic 3 + PIR Motion Sensor)
const int trespassTrigPin = 13;
const int trespassEchoPin = 14;
const int pirPin          = 19;

// Outputs: Alert LED & Master Buzzer
const int alertLedPin     = 23;
const int masterBuzzerPin = 25;

// --- UPDATED ZONE THRESHOLDS ---
// Flood: humidity >= 40%, distance 4 cm - 60 cm, rolling/passing object delta
const float FLOOD_HUM_MIN_PCT = 40.0;
const float FLOOD_DIST_MIN_CM = 4.0;
const float FLOOD_DIST_MAX_CM = 60.0;

// Landslide: distance 2 cm - 50 cm
const float LAND_DIST_MIN_CM  = 2.0;
const float LAND_DIST_MAX_CM  = 50.0;

// Trespassing: human motion distance 10 cm - 90 cm
const float TRESPASS_MIN_CM   = 10.0;
const float TRESPASS_MAX_CM   = 90.0;

// Rolling / passing object change threshold (cm)
const float OBJECT_DELTA_CM   = 3.0;
float prevFloodDist = -1.0;
float prevLandDist  = -1.0;

// --- ZONE BUZZER DURATIONS ---
const unsigned long FLOOD_BUZZ_MS    = 5000;   // 5 seconds
const unsigned long LAND_BUZZ_MS     = 9000;   // 9 seconds
const unsigned long TRESPASS_BUZZ_MS = 13000;  // 13 seconds

const unsigned long TELEMETRY_INTERVAL_MS = 2500;
const unsigned long FAST_POLL_INTERVAL_MS   = 80;    // Fast detection poll

unsigned long floodRingUntilMs    = 0;
unsigned long landRingUntilMs     = 0;
unsigned long trespassRingUntilMs = 0;

bool prevActiveFlood = false;
bool prevActiveLand  = false;
bool prevActiveTres  = false;

bool silencedFlood = false;
bool silencedLand  = false;
bool silencedTres  = false;

unsigned long lastMqttAttemptMs = 0;
unsigned long lastTelemetryMs   = 0;
unsigned long lastPollMs        = 0;

// Latest sensor readings
float floodDist = -1.0;
float floodHum  = -1.0;
bool rollingFloodObjDetected = false;

float landDist = -1.0;
float landHum  = -1.0;
bool rollingLandObjDetected = false;

float trespassDist = -1.0;
int motionDetected = LOW;

String floodEventState     = "NONE";
String landslideEventState = "NONE";
String trespassEventState  = "NONE";

void publishTelemetryNow(bool floodAlert, bool landslideAlert, bool trespassingAlert, bool forceSend = false) {
  unsigned long now = millis();
  if (!forceSend && (now - lastTelemetryMs < TELEMETRY_INTERVAL_MS)) {
    return;
  }

  String payload = "{";
  payload += "\"flood_dist\":" + String(floodDist, 1) + ",";
  payload += "\"flood_hum\":" + String(floodHum, 1) + ",";
  payload += "\"rolling_flood_obj\":" + String(rollingFloodObjDetected ? 1 : 0) + ",";
  payload += "\"land_dist\":" + String(landDist, 1) + ",";
  payload += "\"land_hum\":" + String(landHum, 1) + ",";
  payload += "\"rolling_stone\":" + String(rollingLandObjDetected ? 1 : 0) + ",";
  payload += "\"trespass_dist\":" + String(trespassDist, 1) + ",";
  payload += "\"motion\":" + String(motionDetected) + ",";
  payload += "\"flood_alert\":" + String(floodAlert ? 1 : 0) + ",";
  payload += "\"landslide_alert\":" + String(landslideAlert ? 1 : 0) + ",";
  payload += "\"trespass_alert\":" + String(trespassingAlert ? 1 : 0) + ",";
  payload += "\"flood_event\":\"" + floodEventState + "\",";
  payload += "\"landslide_event\":\"" + landslideEventState + "\",";
  payload += "\"trespass_event\":\"" + trespassEventState + "\",";
  payload += "\"key\":\"" + String(MQTT_INGEST_KEY) + "\"";
  payload += "}";

  lastTelemetryMs = now;

  if (floodEventState == "RESOLVED") floodEventState = "NONE";
  if (landslideEventState == "RESOLVED") landslideEventState = "NONE";
  if (trespassEventState == "RESOLVED") trespassEventState = "NONE";

  if (mqtt.connected()) {
    mqtt.publish(mqttTopic, payload.c_str());
    Serial.println("[MQTT] " + payload);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (msg.indexOf("stop_buzzer") >= 0) {
    bool stopFlood = (msg.indexOf("flood") >= 0) || (msg == "stop_buzzer");
    bool stopLand  = (msg.indexOf("landslide") >= 0) || (msg == "stop_buzzer");
    bool stopTres  = (msg.indexOf("trespass") >= 0) || (msg == "stop_buzzer");

    if (stopFlood) {
      silencedFlood = true;
      floodRingUntilMs = 0;
      prevActiveFlood = false;
      floodEventState = "RESOLVED";
    }
    if (stopLand) {
      silencedLand = true;
      landRingUntilMs = 0;
      prevActiveLand = false;
      landslideEventState = "RESOLVED";
    }
    if (stopTres) {
      silencedTres = true;
      trespassRingUntilMs = 0;
      prevActiveTres = false;
      trespassEventState = "RESOLVED";
    }

    digitalWrite(alertLedPin, LOW);
    digitalWrite(masterBuzzerPin, LOW);
    Serial.println("[DASHBOARD CMD] Threat resolved -> Buzzer stopped, no Telegram alert.");
    publishTelemetryNow(false, false, false, true);
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

  pinMode(landTrigPin, OUTPUT);
  pinMode(landEchoPin, INPUT);

  pinMode(trespassTrigPin, OUTPUT);
  pinMode(trespassEchoPin, INPUT);
  pinMode(pirPin, INPUT);

  pinMode(alertLedPin, OUTPUT);
  pinMode(masterBuzzerPin, OUTPUT);
  digitalWrite(alertLedPin, LOW);
  digitalWrite(masterBuzzerPin, LOW);

  floodDht.begin();
  landDht.begin();

  setup_wifi();
  mqtt.setServer(mqttBroker, mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }
  reconnectMQTTNonBlocking();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPollMs < FAST_POLL_INTERVAL_MS) {
    return;
  }
  lastPollMs = now;

  // --- 1. FLOOD ZONE (Humidity >= 40%, Dist 4cm - 60cm, Rolling/Passing Object) ---
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

  // --- 2. LANDSLIDE ZONE (Dist 2cm - 50cm) ---
  landDist = readDistance(landTrigPin, landEchoPin);
  float rawLandHum = landDht.readHumidity();
  if (!isnan(rawLandHum)) landHum = rawLandHum;

  rollingLandObjDetected = false;
  if (landDist >= LAND_DIST_MIN_CM && landDist <= LAND_DIST_MAX_CM) {
    if (prevLandDist > 0 && fabs(landDist - prevLandDist) >= OBJECT_DELTA_CM) {
      rollingLandObjDetected = true;
    }
  }
  if (landDist > 0) prevLandDist = landDist;

  bool rawLand = (landDist >= LAND_DIST_MIN_CM && landDist <= LAND_DIST_MAX_CM) &&
                 (rollingLandObjDetected || landDist <= 15.0f);

  // --- 3. TRESPASSING ZONE (Human Motion, Dist 10cm - 90cm, 13s Ring) ---
  motionDetected = digitalRead(pirPin);
  trespassDist = readDistance(trespassTrigPin, trespassEchoPin);

  bool rawTres = (motionDetected == HIGH) &&
                 (trespassDist >= TRESPASS_MIN_CM && trespassDist <= TRESPASS_MAX_CM);

  if (!rawFlood) silencedFlood = false;
  if (!rawLand)  silencedLand  = false;
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

  if (rawLand && !silencedLand) {
    landRingUntilMs = now + LAND_BUZZ_MS;
    if (!prevActiveLand) {
      landslideEventState = "TRIGGERED";
      immediatePublish = true;
    }
    prevActiveLand = true;
  } else if (!rawLand && prevActiveLand && now >= landRingUntilMs) {
    prevActiveLand = false;
    landslideEventState = "RESOLVED";
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

  bool floodActive     = (rawFlood || (now < floodRingUntilMs)) && !silencedFlood;
  bool landslideActive = (rawLand || (now < landRingUntilMs)) && !silencedLand;
  bool trespassActive  = (rawTres || (now < trespassRingUntilMs)) && !silencedTres;

  if (floodActive || landslideActive || trespassActive) {
    digitalWrite(alertLedPin, HIGH);
    digitalWrite(masterBuzzerPin, HIGH);
  } else {
    digitalWrite(alertLedPin, LOW);
    digitalWrite(masterBuzzerPin, LOW);
  }

  publishTelemetryNow(floodActive, landslideActive, trespassActive, immediatePublish);
}