#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

typedef struct {
  float testValue;
  uint32_t packetID;
} TestPacket;

TestPacket incomingData;

// ESP-NOW Receive Callback
void onDataRecv(const esp_now_recv_info_t *recvInfo,
                const uint8_t *incomingDataBytes,
                int len)
{
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

  Serial.println("\n================================");
  Serial.println("PACKET RECEIVED");

  Serial.print("Packet ID: ");
  Serial.println(incomingData.packetID);

  Serial.print("Value: ");
  Serial.println(incomingData.testValue);

  char macStr[18];

  snprintf(macStr,
           sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           recvInfo->src_addr[0],
           recvInfo->src_addr[1],
           recvInfo->src_addr[2],
           recvInfo->src_addr[3],
           recvInfo->src_addr[4],
           recvInfo->src_addr[5]);

  Serial.print("From MAC: ");
  Serial.println(macStr);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);

  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);

  Serial.println();
  Serial.println("GROUND NODE STARTED");

  Serial.printf("My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP-NOW Receiver Ready");
}

void loop()
{
}