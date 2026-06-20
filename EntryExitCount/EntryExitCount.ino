#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define ENTRY_IR 32
#define EXIT_IR 33

LiquidCrystal_I2C lcd(0x27, 16, 2);

int occupancy = 0;

int lastEntryState = HIGH;
int lastExitState = HIGH;

unsigned long lastEntryTime = 0;
unsigned long lastExitTime = 0;

const unsigned long cooldown = 1000; // 1 second

void setup()
{
  Serial.begin(115200);

  pinMode(ENTRY_IR, INPUT);
  pinMode(EXIT_IR, INPUT);

  lcd.init();
  lcd.backlight();

  updateLCD();
}

void loop()
{
  int entryState = digitalRead(ENTRY_IR);
  int exitState = digitalRead(EXIT_IR);

  // ENTRY
  if (lastEntryState == HIGH &&
      entryState == LOW &&
      millis() - lastEntryTime > cooldown)
  {
    occupancy++;

    lastEntryTime = millis();

    Serial.print("ENTRY -> ");
    Serial.println(occupancy);

    updateLCD();
  }

  // EXIT
  if (lastExitState == HIGH &&
      exitState == LOW &&
      millis() - lastExitTime > cooldown)
  {
    occupancy--;

    if (occupancy < 0)
      occupancy = 0;

    lastExitTime = millis();

    Serial.print("EXIT -> ");
    Serial.println(occupancy);

    updateLCD();
  }

  lastEntryState = entryState;
  lastExitState = exitState;
}

void updateLCD()
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("People: ");
  lcd.print(occupancy);

  lcd.setCursor(0, 1);
  lcd.print("Monitoring");
}