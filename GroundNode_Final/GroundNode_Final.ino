/*********************************************************************
 * Ground Node – Crowd Risk Prediction System
 *********************************************************************/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#define LCD_ADDRESS 0x27
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// ===========================
//  PIN DEFINITIONS (per your table)
// ===========================
#define ENTRY_IR     32
#define EXIT_IR      33
#define GREEN_LED    27
#define YELLOW_LED   26
#define RED_LED      14
#define BUZZER       25

// ===========================
//  SYSTEM CONSTANTS
// ===========================
const int MAX_CAPACITY = 10;
const unsigned long SENSOR_TIMEOUT = 5000;
const unsigned long LCD_PAGE_TIME = 3000;
const unsigned long IR_COOLDOWN = 600;

// 3-level risk model (algorithm Steps 5–6)
enum RiskLevel { Risk_SAFE, Risk_MODERATE, Risk_DANGER };

// ===========================
//  MQTT / WiFi DASHBOARD CONFIG
// ===========================
#define ESPNOW_CHANNEL 1

const char* WIFI_SSID       = "MOTO";
const char* WIFI_PASSWORD   = "soumadri007";

const char* MQTT_BROKER     = "10.229.125.132";
const uint16_t MQTT_PORT    = 1883;
const char* MQTT_USER       = "";
const char* MQTT_PASSWORD   = "";
const char* MQTT_CLIENT_ID  = "GroundNode_CrowdRisk";
const char* MQTT_TOPIC_DATA   = "crowd/risk/data";
const char* MQTT_TOPIC_STATUS = "crowd/risk/status";

const unsigned long MQTT_PUBLISH_INTERVAL = 2000UL;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000UL;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastMQTTPublish = 0;
unsigned long lastMQTTReconnectAttempt = 0;
bool wifiDashboardEnabled = false;

// ===========================
//  PACKET FROM CEILING NODE (must match)
// ===========================
typedef struct {
  bool    nodeAlive;
  float   temperature;
  float   humidity;
  float   pressure;
  float   avgDistance;
  float   densityPercent;
  float   movementIndex;
  float   stagnationIndex;
  float   noiseLevel;
  float   noiseNormalized;
  uint8_t occupiedZones;
  uint8_t temperatureRisk;
  uint8_t humidityRisk;
  uint8_t environmentRisk;
  uint8_t noiseRisk;
  uint8_t densityRisk;
  uint32_t packetNumber;
} CeilingPacket;

CeilingPacket ceilingData;

// ===========================
//  PEOPLE COUNTER
// ===========================
volatile int peopleCount = 0;
volatile uint32_t entryCount = 0, exitCount = 0;
bool lastEntryState = HIGH, lastExitState = HIGH;
unsigned long lastEntryTime = 0, lastExitTime = 0;

bool ceilingOnline = false;
unsigned long lastPacketTime = 0;

float occupancyRatio = 0.0;
RiskLevel countRisk       = Risk_SAFE;
RiskLevel densityRiskG    = Risk_SAFE;
RiskLevel environmentRiskG= Risk_SAFE;
RiskLevel noiseRiskG      = Risk_SAFE;
RiskLevel currentRisk     = Risk_SAFE;
RiskLevel previousRisk    = Risk_SAFE;

byte currentPage = 0;
unsigned long lastLCDChange = 0;

bool buzzerState = false;

const unsigned long BUZZER_DURATION_MODERATE_MS = 1000UL;
const unsigned long BUZZER_DURATION_DANGER_MS   = 3000UL;
unsigned long riskLevelEnteredAt = 0;

const unsigned long PEOPLE_IDLE_TIMEOUT_MS = 5000UL;
unsigned long lastActivityTime = 0;
bool showPeoplePage = false;

// ===========================
//  FORWARD DECLARATIONS
// ===========================
void updatePeopleCounter();
RiskLevel evaluateCountRisk(int people);
RiskLevel maxRisk(RiskLevel a, RiskLevel b);
void calculateRisk();
void updateOutputs();
void updateLCD();
void updateBuzzer();
void updateLEDs();
String riskToString(RiskLevel level);
void monitorNode();
void resetRiskState();
void printStatus();
void connectWiFiAndSyncChannel();
void reconnectMQTT();
void publishToMQTT();

// ===========================
//  ESP‑NOW RECEIVE CALLBACK
// ===========================
void onDataReceive(const esp_now_recv_info_t *recvInfo,
                   const uint8_t *incomingData, int len) {
  if (len < sizeof(CeilingPacket)) return;
  memcpy(&ceilingData, incomingData, sizeof(CeilingPacket));
  if (!ceilingData.nodeAlive) {
    resetRiskState();
    ceilingOnline = false;
    return;
  }
  ceilingOnline = true;
  lastPacketTime = millis();
  Serial.println("Packet received");
}

// ===========================
//  SETUP
// ===========================
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Crowd Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing");

  pinMode(ENTRY_IR, INPUT_PULLUP);
  pinMode(EXIT_IR,  INPUT_PULLUP);

  pinMode(GREEN_LED,  OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED,    OUTPUT);
  pinMode(BUZZER,     OUTPUT);
  digitalWrite(GREEN_LED,  LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED,    LOW);
  digitalWrite(BUZZER,     LOW);

  ledcAttach(BUZZER, 2000, 8);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP‑NOW init failed");
    while (true);
  }

  esp_now_register_recv_cb(onDataReceive);

  uint8_t ceilingMAC[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

  // ---- WiFi + MQTT dashboard connection ----
  connectWiFiAndSyncChannel();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);   // FIX #2: default 256 bytes is too small for the full JSON payload

  Serial.println("-----------------------------------");
  Serial.println("GROUND NODE READY");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("-----------------------------------");
}

// ===========================
//  MAIN LOOP
// ===========================
void loop() {
  updatePeopleCounter();
  monitorNode();

  if (ceilingOnline) {
    calculateRisk();
  }

  updateOutputs();
  updateLCD();
  printStatus();
  publishToMQTT();
}

// ===========================
//  PEOPLE COUNTER
// ===========================
void updatePeopleCounter() {
  bool entryState = digitalRead(ENTRY_IR);
  bool exitState  = digitalRead(EXIT_IR);
  unsigned long now = millis();
  bool activityThisCycle = false;

  if (lastEntryState == HIGH && entryState == LOW && (now - lastEntryTime) > IR_COOLDOWN) {
    peopleCount++;
    entryCount++;
    lastEntryTime = now;
    activityThisCycle = true;
    Serial.println("ENTRY");
  }
  if (lastExitState == HIGH && exitState == LOW && (now - lastExitTime) > IR_COOLDOWN) {
    if (peopleCount > 0) {
      peopleCount--;
      exitCount++;
    }
    lastExitTime = now;
    activityThisCycle = true;
    Serial.println("EXIT");
  }
  lastEntryState = entryState;
  lastExitState = exitState;
  occupancyRatio = constrain((float)peopleCount / MAX_CAPACITY, 0.0, 1.0);

  countRisk = evaluateCountRisk(peopleCount);

  if (activityThisCycle) {
    lastActivityTime = now;
    showPeoplePage = true;
    currentPage = 0;
    lastLCDChange = now;
  }
}

// ===========================
//  RISK CALCULATION
// ===========================
RiskLevel evaluateCountRisk(int people) {
  if (people <  MAX_CAPACITY) return Risk_SAFE;
  if (people == MAX_CAPACITY) return Risk_MODERATE;
  return Risk_DANGER;
}

RiskLevel maxRisk(RiskLevel a, RiskLevel b) {
  return (a > b) ? a : b;
}

void calculateRisk() {
  countRisk = evaluateCountRisk(peopleCount);

  RiskLevel temperatureR;
  if (ceilingData.temperature <= 37.0)       temperatureR = Risk_SAFE;
  else if (ceilingData.temperature <= 40.0)  temperatureR = Risk_MODERATE;
  else                                       temperatureR = Risk_DANGER;

  RiskLevel humidityR;
  if (ceilingData.humidity <= 80.0)          humidityR = Risk_SAFE;
  else if (ceilingData.humidity <= 90.0)     humidityR = Risk_MODERATE;
  else                                       humidityR = Risk_DANGER;

  RiskLevel environmentR = maxRisk(temperatureR, humidityR);

  RiskLevel noiseR;
  if (ceilingData.noiseNormalized < 40.0)        noiseR = Risk_SAFE;
  else if (ceilingData.noiseNormalized < 70.0)   noiseR = Risk_MODERATE;
  else                                           noiseR = Risk_DANGER;

  RiskLevel densityR;
  if (ceilingData.densityPercent < 45.0)       densityR = Risk_SAFE;
  else if (ceilingData.densityPercent < 75.0)  densityR = Risk_MODERATE;
  else                                          densityR = Risk_DANGER;

  environmentRiskG = environmentR;
  noiseRiskG        = noiseR;
  densityRiskG      = densityR;

  RiskLevel overall = countRisk;
  overall = maxRisk(overall, noiseR);
  overall = maxRisk(overall, environmentR);
  overall = maxRisk(overall, densityR);

  previousRisk = currentRisk;
  currentRisk  = overall;

  if (currentRisk != previousRisk) {
    riskLevelEnteredAt = millis();
  }
}

// ===========================
//  OUTPUTS (LEDs, Buzzer)
// ===========================
void updateLEDs() {
  digitalWrite(GREEN_LED,  LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED,    LOW);

  switch (currentRisk) {
    case Risk_SAFE:     digitalWrite(GREEN_LED,  HIGH); break;
    case Risk_MODERATE: digitalWrite(YELLOW_LED, HIGH); break;
    case Risk_DANGER:   digitalWrite(RED_LED,    HIGH); break;
  }
}

void updateBuzzer() {
  unsigned long elapsed = millis() - riskLevelEnteredAt;

  switch (currentRisk) {
    case Risk_SAFE:
      ledcWrite(BUZZER, 0);
      buzzerState = false;
      break;

    case Risk_MODERATE:
      if (elapsed < BUZZER_DURATION_MODERATE_MS) {
        ledcWrite(BUZZER, 128);
        buzzerState = true;
      } else {
        ledcWrite(BUZZER, 0);
        buzzerState = false;
      }
      break;

    case Risk_DANGER:
      if (elapsed < BUZZER_DURATION_DANGER_MS) {
        ledcWrite(BUZZER, 200);
        buzzerState = true;
      } else {
        ledcWrite(BUZZER, 0);
        buzzerState = false;
      }
      break;
  }
}

void updateOutputs() {
  updateLEDs();
  updateBuzzer();
}

// ===========================
//  LCD UPDATE
// ===========================
String riskToString(RiskLevel level) {
  switch (level) {
    case Risk_SAFE:     return "SAFE";
    case Risk_MODERATE: return "MODERATE";
    case Risk_DANGER:   return "DANGER";
  }
  return "UNKNOWN";
}

void updateLCD() {
  unsigned long now = millis();

  if (showPeoplePage) {
    if (now - lastActivityTime >= PEOPLE_IDLE_TIMEOUT_MS) {
      showPeoplePage = false;
      currentPage = 0;
      lastLCDChange = now;
      lcd.clear();
    } else {
      if (now - lastLCDChange < LCD_PAGE_TIME) return;
      lastLCDChange = now;
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("In:"); lcd.print(entryCount);
      lcd.print(" Out:"); lcd.print(exitCount);
      lcd.print(" P:"); lcd.print(peopleCount);

      lcd.setCursor(0, 1);
      lcd.print("OccRisk:"); lcd.print(riskToString(countRisk));
      return;
    }
  }

  if (now - lastLCDChange < LCD_PAGE_TIME) return;
  lastLCDChange = now;
  currentPage = (currentPage + 1) % 5;
  lcd.clear();

  switch (currentPage) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("In:"); lcd.print(entryCount);
      lcd.print(" Out:"); lcd.print(exitCount);
      lcd.print(" P:"); lcd.print(peopleCount);
      lcd.setCursor(0, 1);
      lcd.print("OccRisk:"); lcd.print(riskToString(countRisk));
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Dens:"); lcd.print((int)ceilingData.densityPercent); lcd.print("%");
      lcd.print(" Zone:"); lcd.print((int)ceilingData.occupiedZones);
      lcd.setCursor(0, 1);
      lcd.print("DenRisk:"); lcd.print(riskToString(densityRiskG));
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Noise:"); lcd.print((int)ceilingData.noiseNormalized); lcd.print("%");
      lcd.print(" dB:"); lcd.print((int)ceilingData.noiseLevel);
      lcd.setCursor(0, 1);
      lcd.print("NoiRisk:"); lcd.print(riskToString(noiseRiskG));
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print("T:"); lcd.print(ceilingData.temperature, 1);
      lcd.print(" H:"); lcd.print(ceilingData.humidity, 0);
      lcd.setCursor(0, 1);
      lcd.print("EnvRisk:"); lcd.print(riskToString(environmentRiskG));
      break;

    case 4:
      lcd.setCursor(0, 0);
      lcd.print("N:"); lcd.print(ceilingOnline ? "ONLINE" : "OFFLINE");
      lcd.print(" P:"); lcd.print(ceilingData.packetNumber);
      lcd.setCursor(0, 1);
      lcd.print("OvrRisk:"); lcd.print(riskToString(currentRisk));
      break;
  }
}

// ===========================
//  NODE HEALTH MONITOR
// ===========================
void monitorNode() {
  if (millis() - lastPacketTime > SENSOR_TIMEOUT) {
    if (ceilingOnline) {
      resetRiskState();
    }
    ceilingOnline = false;
  }
}

void resetRiskState() {
  countRisk         = Risk_SAFE;
  densityRiskG      = Risk_SAFE;
  environmentRiskG  = Risk_SAFE;
  noiseRiskG        = Risk_SAFE;
  currentRisk       = Risk_SAFE;
  previousRisk      = Risk_SAFE;
  riskLevelEnteredAt = millis();
}

// ===========================
//  SERIAL DEBUG
// ===========================
void printStatus() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < 1000) return;
  lastPrint = millis();

  Serial.println("====================================");
  Serial.print("In: "); Serial.print(entryCount);
  Serial.print("  Out: "); Serial.print(exitCount);
  Serial.print("  People: "); Serial.println(peopleCount);
  Serial.print("Occupied Risk : "); Serial.println(riskToString(countRisk));
  Serial.println("------------------------------------");
  Serial.print("Density : "); Serial.print(ceilingData.densityPercent); Serial.println("%");
  Serial.print("Zones Occupied: "); Serial.println(ceilingData.occupiedZones);
  Serial.print("Density Risk  : "); Serial.println(riskToString(densityRiskG));
  Serial.println("------------------------------------");
  Serial.print("Temperature : "); Serial.print(ceilingData.temperature); Serial.println(" C");
  Serial.print("Humidity    : "); Serial.print(ceilingData.humidity); Serial.println(" %RH");
  Serial.print("Pressure    : "); Serial.print(ceilingData.pressure); Serial.println(" hPa");
  Serial.print("Env Risk    : "); Serial.println(riskToString(environmentRiskG));
  Serial.println("------------------------------------");
  Serial.print("Noise (norm): "); Serial.print(ceilingData.noiseNormalized); Serial.println("%");
  Serial.print("Noise (dB)  : "); Serial.println(ceilingData.noiseLevel);
  Serial.print("Noise Risk  : "); Serial.println(riskToString(noiseRiskG));
  Serial.println("------------------------------------");
  Serial.print("Ceiling Node : "); Serial.println(ceilingOnline ? "ONLINE" : "OFFLINE");
  Serial.print("Packet Number: "); Serial.println(ceilingData.packetNumber);
  Serial.print("OVERALL RISK : "); Serial.println(riskToString(currentRisk));
  Serial.print("MQTT Dashboard: "); Serial.println(mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");
}

// ===========================
//  WIFI + MQTT DASHBOARD
// ===========================
void connectWiFiAndSyncChannel() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi for MQTT dashboard");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiDashboardEnabled = true;
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    esp_wifi_set_channel(WiFi.channel(), WIFI_SECOND_CHAN_NONE);
    Serial.print("ESP-NOW channel synced to: ");
    Serial.println(WiFi.channel());
  } else {
    wifiDashboardEnabled = false;
    Serial.println("WiFi connect failed - MQTT dashboard disabled, ESP-NOW stays on its fixed channel");
  }
}

// FIX #1: PubSubClient's connect(id, user, pass) sets the "username
// present" flag in the MQTT CONNECT packet even when user/pass are
// empty strings, which some brokers reject. Use the plain connect(id)
// overload whenever no credentials are configured, so the CONNECT
// packet carries no username/password fields at all.
void reconnectMQTT() {
  if (!wifiDashboardEnabled || WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastMQTTReconnectAttempt < MQTT_RECONNECT_INTERVAL) return;
  lastMQTTReconnectAttempt = now;

  Serial.print("Connecting to MQTT broker...");

  bool connected;
  if (strlen(MQTT_USER) > 0) {
    connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
  } else {
    connected = mqttClient.connect(MQTT_CLIENT_ID);   // anonymous connect
  }

  if (connected) {
    Serial.println("connected");
    mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
  }
}

void publishToMQTT() {
  if (!wifiDashboardEnabled || WiFi.status() != WL_CONNECTED) return;

  if (!mqttClient.connected()) {
    reconnectMQTT();
    if (!mqttClient.connected()) return;
  }
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastMQTTPublish < MQTT_PUBLISH_INTERVAL) return;
  lastMQTTPublish = now;

  char payload[420];
  snprintf(payload, sizeof(payload),
    "{"
      "\"people\":%d,\"entry\":%lu,\"exit\":%lu,\"occupiedRisk\":\"%s\","
      "\"densityPercent\":%.1f,\"zonesOccupied\":%d,\"densityRisk\":\"%s\","
      "\"temperature\":%.1f,\"humidity\":%.1f,\"envRisk\":\"%s\","
      "\"noisePercent\":%.1f,\"noiseDb\":%.1f,\"noiseRisk\":\"%s\","
      "\"ceilingOnline\":%s,\"packetNumber\":%lu,\"overallRisk\":\"%s\""
    "}",
    peopleCount, (unsigned long)entryCount, (unsigned long)exitCount, riskToString(countRisk).c_str(),
    ceilingData.densityPercent, (int)ceilingData.occupiedZones, riskToString(densityRiskG).c_str(),
    ceilingData.temperature, ceilingData.humidity, riskToString(environmentRiskG).c_str(),
    ceilingData.noiseNormalized, ceilingData.noiseLevel, riskToString(noiseRiskG).c_str(),
    ceilingOnline ? "true" : "false", (unsigned long)ceilingData.packetNumber, riskToString(currentRisk).c_str()
  );

  mqttClient.publish(MQTT_TOPIC_DATA, payload);
}