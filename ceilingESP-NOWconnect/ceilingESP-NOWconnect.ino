#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// =========================
// Data Structure
// =========================
typedef struct {
  float testValue;
  uint32_t packetID;
} TestPacket;

TestPacket packet;

uint32_t counter = 0;

// =========================
// Ground Node MAC Address
// 78:42:1C:19:D0:88
// =========================
uint8_t receiverMAC[] = {
  0x78,
  0x42,
  0x1C,
  0x19,
  0xD0,
  0x88
};

// =========================
// ESP-NOW Send Callback
// ESP32 Core 3.3.x Version
// =========================
void onDataSent(const wifi_tx_info_t *info,
                esp_now_send_status_t status)
{
  Serial.print("Send Status: ");

  if (status == ESP_NOW_SEND_SUCCESS)
  {
    Serial.println("SUCCESS");
  }
  else
  {
    Serial.println("FAILED");
  }
}

// =========================
// Setup
// =========================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);

  // Print MAC Address
  uint8_t mac[6];

  esp_wifi_get_mac(WIFI_IF_STA, mac);

  Serial.println();
  Serial.println("================================");
  Serial.println("CEILING NODE STARTED");

  Serial.printf("My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  // Register callback
  esp_now_register_send_cb(onDataSent);

  // Configure Peer
  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr,
         receiverMAC,
         6);

  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add Peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed To Add Peer");
    return;
  }

  Serial.println("Peer Added Successfully");
  Serial.println("================================");
}

// =========================
// Main Loop
// =========================
void loop()
{
  packet.packetID = counter++;

  packet.testValue =
      random(1000) / 10.0;

  esp_err_t result =
      esp_now_send(
          receiverMAC,
          (uint8_t *)&packet,
          sizeof(packet));

  if (result == ESP_OK)
  {
    Serial.print("Sent Packet ID: ");
    Serial.print(packet.packetID);

    Serial.print(" | Value: ");
    Serial.println(packet.testValue);
  }
  else
  {
    Serial.print("ESP-NOW Send Error: ");
    Serial.println(result);
  }

  delay(1000);
}