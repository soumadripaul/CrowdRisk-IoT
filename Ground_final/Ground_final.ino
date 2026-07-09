/*********************************************************************
 * Ground Node – Crowd Risk Prediction System
 *********************************************************************/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
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
  float   noiseLevel;        // dB SPL
  float   noiseNormalized;   // 0–100 %
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
RiskLevel countRisk     = Risk_SAFE;
RiskLevel currentRisk   = Risk_SAFE;
RiskLevel previousRisk  = Risk_SAFE;

byte currentPage = 0;
unsigned long lastLCDChange = 0;

bool buzzerState = false;
unsigned long buzzerTimer = 0;

// Buzzer timeouts per risk level
const unsigned long BUZZER_TIMEOUT_MODERATE_MS = 3000UL;  // 3 seconds
const unsigned long BUZZER_TIMEOUT_DANGER_MS   = 5000UL;  // 5 seconds
unsigned long riskLevelEnteredAt = 0;                      // when currentRisk last changed

// LCD behaviour: while people are actively passing, lock the LCD to a
// dedicated People/Entry/Exit/Risk page. After 5 s of no activity, resume
// the normal rotation.
const unsigned long PEOPLE_IDLE_TIMEOUT_MS = 5000UL;
unsigned long lastActivityTime = 0;       // updated on every entry/exit
bool showPeoplePage = false;              // true while lock-screen is active

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

  // ---- LCD ----
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Crowd Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing");

  // ---- IR sensors ----
  pinMode(ENTRY_IR, INPUT_PULLUP);
  pinMode(EXIT_IR,  INPUT_PULLUP);

  // ---- LEDs & Buzzer ----
  pinMode(GREEN_LED,  OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED,    OUTPUT);
  pinMode(BUZZER,     OUTPUT);
  digitalWrite(GREEN_LED,  LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED,    LOW);
  digitalWrite(BUZZER,     LOW);

  // Configure PWM tone driver on the buzzer pin (works for active & passive buzzers)
  // ESP32 Arduino core 3.x API: ledcAttach(pin, freq, resolution)
  ledcAttach(BUZZER, 2000, 8);    // 2 kHz, 8-bit resolution on the buzzer pin

  // ---- WiFi & ESP‑NOW with fixed channel ----
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);   // must match Ceiling

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP‑NOW init failed");
    while (true);
  }

  esp_now_register_recv_cb(onDataReceive);

  // Optionally add the Ceiling Node as a peer (not strictly needed for receiving)
  // but it can improve reliability.
  uint8_t ceilingMAC[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // replace with actual
  // You can omit this block if you don't know the ceiling MAC; receive works without it.
  // esp_now_peer_info_t peerInfo = {};
  // memcpy(peerInfo.peer_addr, ceilingMAC, 6);
  // peerInfo.channel = 1;
  // peerInfo.encrypt = false;
  // esp_now_add_peer(&peerInfo);

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

  // Lock the LCD to the people-activity page while someone is passing.
  if (activityThisCycle) {
    lastActivityTime = now;
    showPeoplePage = true;
    currentPage = 0;              // reset rotation so page 0 is the lock
    lastLCDChange = now;          // force immediate redraw
  }
}

// ===========================
//  RISK CALCULATION  (algorithm Steps 1–5)
// ===========================
RiskLevel evaluateCountRisk(int people) {
  // Step 1 – People Count thresholds: <10 SAFE, ==10 MODERATE, >10 DANGER
  if (people <  MAX_CAPACITY) return Risk_SAFE;
  if (people == MAX_CAPACITY) return Risk_MODERATE;
  return Risk_DANGER;
}

RiskLevel maxRisk(RiskLevel a, RiskLevel b) {
  return (a > b) ? a : b;
}

void calculateRisk() {
  // Step 1 – People Count risk
  countRisk = evaluateCountRisk(peopleCount);

  // Steps 2–4 – Per-sensor risks are also re-derived here from raw values
  // so the ground node stays self-contained if a packet is missing one
  // of the cached risk fields.
  RiskLevel temperatureR;
  if (ceilingData.temperature <= 32.0)       temperatureR = Risk_SAFE;
  else if (ceilingData.temperature <= 35.0)  temperatureR = Risk_MODERATE;
  else                                       temperatureR = Risk_DANGER;

  RiskLevel humidityR;
  if (ceilingData.humidity <= 70.0)          humidityR = Risk_SAFE;
  else if (ceilingData.humidity <= 80.0)     humidityR = Risk_MODERATE;
  else                                       humidityR = Risk_DANGER;

  RiskLevel environmentR = maxRisk(temperatureR, humidityR);

  RiskLevel noiseR;
  if (ceilingData.noiseNormalized < 35.0)        noiseR = Risk_SAFE;
  else if (ceilingData.noiseNormalized < 65.0)   noiseR = Risk_MODERATE;
  else                                           noiseR = Risk_DANGER;

  RiskLevel densityR;
  if (ceilingData.densityPercent < 40.0)       densityR = Risk_SAFE;
  else if (ceilingData.densityPercent < 70.0)  densityR = Risk_MODERATE;
  else                                          densityR = Risk_DANGER;

  // Step 5 – Fusion: OverallRisk = max of all sensor risks
  RiskLevel overall = countRisk;
  overall = maxRisk(overall, noiseR);
  overall = maxRisk(overall, environmentR);
  overall = maxRisk(overall, densityR);

  previousRisk = currentRisk;
  currentRisk  = overall;

  // Reset the buzzer timer whenever the risk level changes
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
    case Risk_SAFE:     digitalWrite(GREEN_LED,  HIGH); break;   // Step 6: green
    case Risk_MODERATE: digitalWrite(YELLOW_LED, HIGH); break;   // Step 6: yellow
    case Risk_DANGER:   digitalWrite(RED_LED,    HIGH); break;   // Step 6: red
  }
}

void updateBuzzer() {
  unsigned long now = millis();

  // Enforce buzzer timeout for the current risk level
  bool buzzerTimedOut = false;
  if (currentRisk == Risk_MODERATE &&
      (now - riskLevelEnteredAt) >= BUZZER_TIMEOUT_MODERATE_MS) {
    buzzerTimedOut = true;
  }
  if (currentRisk == Risk_DANGER &&
      (now - riskLevelEnteredAt) >= BUZZER_TIMEOUT_DANGER_MS) {
    buzzerTimedOut = true;
  }

  if (buzzerTimedOut) {
    ledcWrite(BUZZER, 0);
    buzzerState = false;
    return;
  }

  switch (currentRisk) {
    case Risk_SAFE:
      ledcWrite(BUZZER, 0);               // silence
      buzzerState = false;
      break;
    case Risk_MODERATE:
      // Short warning beep (~one short tone per second)
      if (now - buzzerTimer > 1000) {
        buzzerTimer = now;
        buzzerState = !buzzerState;
        ledcWrite(BUZZER, buzzerState ? 128 : 0);   // 50% duty @ 2 kHz
      }
      break;
    case Risk_DANGER:
      // Continuous loud tone
      ledcWrite(BUZZER, 200);
      buzzerState = true;
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

  // While people are actively passing, lock the LCD to a dedicated activity page.
  if (showPeoplePage) {
    if (now - lastActivityTime >= PEOPLE_IDLE_TIMEOUT_MS) {
      // Idle window expired → release the lock and resume normal rotation
      showPeoplePage = false;
      currentPage = 0;
      lastLCDChange = now;
      lcd.clear();
    } else {
      // Redraw the lock screen, but only every LCD_PAGE_TIME to avoid flicker.
      if (now - lastLCDChange < LCD_PAGE_TIME) return;
      lastLCDChange = now;
      lcd.clear();

      lcd.setCursor(0, 0);
      lcd.print("P:"); lcd.print(peopleCount);
      lcd.print(" In:"); lcd.print(entryCount);
      lcd.print(" Out:"); lcd.print(exitCount);

      lcd.setCursor(0, 1);
      lcd.print(riskToString(currentRisk));
      return;
    }
  }

  if (now - lastLCDChange < LCD_PAGE_TIME) return;
  lastLCDChange = now;
  currentPage = (currentPage + 1) % 5;
  lcd.clear();

  switch (currentPage) {
    case 0:
      lcd.setCursor(0, 0); lcd.print("People:"); lcd.print(peopleCount);
      lcd.setCursor(0, 1); lcd.print(riskToString(currentRisk));
      break;
    case 1:
      lcd.setCursor(0, 0); lcd.print("Dens:"); lcd.print((int)ceilingData.densityPercent); lcd.print("%");
      lcd.setCursor(0, 1); lcd.print("Zone:"); lcd.print((int)ceilingData.occupiedZones);
      break;
    case 2:
      lcd.setCursor(0, 0); lcd.print("Noise:"); lcd.print((int)ceilingData.noiseNormalized); lcd.print("%");
      lcd.setCursor(0, 1); lcd.print("dB:"); lcd.print((int)ceilingData.noiseLevel);
      break;
    case 3:
      lcd.setCursor(0, 0); lcd.print("T:"); lcd.print(ceilingData.temperature, 1);
      lcd.print(" H:"); lcd.print(ceilingData.humidity, 0);
      lcd.setCursor(0, 1);
      lcd.print("Env:"); lcd.print(riskToString((RiskLevel)ceilingData.environmentRisk));
      break;
    case 4:
      lcd.setCursor(0, 0); lcd.print("Node:"); lcd.print(ceilingOnline ? "ONLINE" : "OFFLINE");
      lcd.setCursor(0, 1); lcd.print("Pkt:"); lcd.print(ceilingData.packetNumber);
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
  countRisk    = Risk_SAFE;
  currentRisk  = Risk_SAFE;
  previousRisk = Risk_SAFE;
  riskLevelEnteredAt = millis();   // restart timer so the (silent) SAFE window is fresh
}

// ===========================
//  SERIAL DEBUG
// ===========================
void printStatus() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < 1000) return;
  lastPrint = millis();

  Serial.println("====================================");
  Serial.print("People : "); Serial.println(peopleCount);
  Serial.print("Entry  : "); Serial.println(entryCount);
  Serial.print("Exit   : "); Serial.println(exitCount);
  Serial.print("Density: "); Serial.print(ceilingData.densityPercent); Serial.println("%");
  Serial.print("Zones  : "); Serial.println(ceilingData.occupiedZones);
  Serial.print("Noise  : "); Serial.print(ceilingData.noiseNormalized); Serial.print("% (");
  Serial.print(ceilingData.noiseLevel); Serial.println(" dB)");
  Serial.print("Temp   : "); Serial.println(ceilingData.temperature);
  Serial.print("Humid  : "); Serial.println(ceilingData.humidity);
  Serial.print("Press  : "); Serial.println(ceilingData.pressure);
  Serial.print("Count R: "); Serial.println(riskToString(countRisk));
  Serial.print("Noise R: "); Serial.println(riskToString((RiskLevel)ceilingData.noiseRisk));
  Serial.print("Env   R: "); Serial.println(riskToString((RiskLevel)ceilingData.environmentRisk));
  Serial.print("Dens  R: "); Serial.println(riskToString((RiskLevel)ceilingData.densityRisk));
  Serial.print("OVERALL: "); Serial.println(riskToString(currentRisk));
  Serial.print("Ceiling: "); Serial.println(ceilingOnline ? "ONLINE" : "OFFLINE");
}