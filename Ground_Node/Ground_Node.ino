#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define ENTRY_IR 32
#define EXIT_IR 33
#define GREEN_LED 27
#define YELLOW_LED 26
#define BLUE_LED 14
#define BUZZER 25

LiquidCrystal_I2C lcd(0x27, 16, 2);

typedef struct
{
  bool nodeAlive;
  float temperature;
  float humidity;
  float pressure;
} SensorData;

SensorData incomingData;

bool ceilingConnected = false;
unsigned long lastPacketTime = 0;
unsigned long lastMovementTime = 0;

int entryCount = 0;
int exitCount = 0;
int totalPeople = 0;

bool entryTriggered = false;
bool exitTriggered = false;

void OnDataRecv(const esp_now_recv_info *info,
                const uint8_t *incomingDataBytes,
                int len)
{
  if (len != sizeof(SensorData)) return;

  memcpy(&incomingData, incomingDataBytes, sizeof(SensorData));

  ceilingConnected = incomingData.nodeAlive;
  lastPacketTime = millis();

  Serial.printf("Temp: %.2f C\n", incomingData.temperature);
  Serial.printf("Humidity: %.2f %%\n", incomingData.humidity);
  Serial.printf("Pressure: %.2f hPa\n", incomingData.pressure);
}

void setRiskLED(String risk)
{
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(BLUE_LED, LOW);

  if (risk == "LOW") digitalWrite(GREEN_LED, HIGH);
  else if (risk == "MODERATE") digitalWrite(YELLOW_LED, HIGH);
  else if (risk == "HIGH") digitalWrite(BLUE_LED, HIGH);
}

void activateBuzzer()
{
  digitalWrite(BUZZER, HIGH);
  delay(1000);
  digitalWrite(BUZZER, LOW);
}

void showCrowdData()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("E:");
  lcd.print(entryCount);
  lcd.print(" X:");
  lcd.print(exitCount);

  lcd.setCursor(0,1);
  lcd.print("T:");
  lcd.print(totalPeople);
  lcd.print(" ");
  lcd.print(ceilingConnected ? "ON" : "OFF");
}

void evaluateCrowdRisk()
{
  String risk = "LOW";

  if (totalPeople > 10) risk = "HIGH";
  else if (totalPeople == 10) risk = "MODERATE";

  setRiskLED(risk);

  if (risk == "HIGH") activateBuzzer();

  showCrowdData();
}

void showBMEData()
{
  if (!ceilingConnected)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Ceiling Offline");
    delay(2000);
    showCrowdData();
    return;
  }

  float temp = incomingData.temperature;
  float hum = incomingData.humidity;
  float pressure = incomingData.pressure;

  String risk;

  if (temp > 35 || hum > 85) risk = "HIGH";
  else if (temp > 32 || hum > 75) risk = "MODERATE";
  else risk = "LOW";

  setRiskLED(risk);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("T:");
  lcd.print(temp,1);
  lcd.print(" H:");
  lcd.print(hum,0);

  lcd.setCursor(0,1);
  lcd.print("P:");
  lcd.print(pressure,0);

  delay(5000);
  showCrowdData();
}

void setup()
{
  Serial.begin(115200);

  pinMode(ENTRY_IR, INPUT);
  pinMode(EXIT_IR, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(21,22);

  lcd.init();
  lcd.backlight();

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    while(true);
  }

  esp_now_register_recv_cb(OnDataRecv);

  lastPacketTime = millis();
  lastMovementTime = millis();

  showCrowdData();
}

void loop()
{
  if (millis() - lastPacketTime > 5000)
    ceilingConnected = false;

  bool entryState = digitalRead(ENTRY_IR);
  bool exitState = digitalRead(EXIT_IR);

  if (entryState == LOW && !entryTriggered)
  {
    entryCount++;
    totalPeople++;
    entryTriggered = true;
    lastMovementTime = millis();
    evaluateCrowdRisk();
  }

  if (entryState == HIGH) entryTriggered = false;

  if (exitState == LOW && !exitTriggered)
  {
    exitCount++;
    if(totalPeople > 0) totalPeople--;
    exitTriggered = true;
    lastMovementTime = millis();
    evaluateCrowdRisk();
  }

  if (exitState == HIGH) exitTriggered = false;

  if (millis() - lastMovementTime > 10000)
  {
    showBMEData();
    lastMovementTime = millis();
  }
}