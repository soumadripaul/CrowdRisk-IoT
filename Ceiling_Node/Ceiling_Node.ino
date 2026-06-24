#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

typedef struct
{
  bool nodeAlive;
  float temperature;
  float humidity;
  float pressure;
} SensorData;

SensorData data;

uint8_t groundNodeMAC[] =
{
  0x78, 0x42, 0x1C, 0x19, 0xD0, 0x88
};

void onDataSent(const wifi_tx_info_t *info,
                esp_now_send_status_t status)
{
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(21,22);

  if (!bme.begin(0x76))
  {
    Serial.println("BME280 NOT FOUND");
    while(1);
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    while(true);
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, groundNodeMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed To Add Peer");
    while(true);
  }

  Serial.println("Ceiling Node Ready");
}

void loop()
{
  data.nodeAlive = true;
  data.temperature = bme.readTemperature();
  data.humidity = bme.readHumidity();
  data.pressure = bme.readPressure() / 100.0F;

  esp_now_send(
    groundNodeMAC,
    (uint8_t *)&data,
    sizeof(data)
  );

  Serial.printf("T=%.2f H=%.2f P=%.2f\n",
                data.temperature,
                data.humidity,
                data.pressure);

  delay(1000);
}