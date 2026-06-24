#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);

  if (!bme.begin(0x76))
  {
    Serial.println("BME280 NOT FOUND");

    while (1);
  }

  Serial.println("BME280 READY");
}

void loop()
{
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0F;

  Serial.println("==========");

  Serial.print("Temp: ");
  Serial.print(temp);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.println(" %");

  Serial.print("Pressure: ");
  Serial.print(pressure);
  Serial.println(" hPa");

  delay(2000);
}