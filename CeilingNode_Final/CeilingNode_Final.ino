/*********************************************************************
 * Ceiling Node – Sensor Data Collector
 *********************************************************************/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <driver/i2s.h>

// ===========================
//  MAC address of Ground Node – CHANGE THIS!
// ===========================
uint8_t groundNodeMAC[] = { 0x78, 0x42, 0x1C, 0x19, 0xD0, 0x88 };

// ===========================
//  RISK THRESHOLDS (multi-sensor crowd risk algorithm)
//  Fix #7: every threshold raised by +5 points from the original spec.
// ===========================
#define NOISE_SAFE_THRESHOLD   40.0   // % normalized RMS  -> Safe        (was 35.0)
#define NOISE_DANGER_THRESHOLD 70.0   // % normalized RMS  -> Danger      (was 65.0)
#define DENSITY_SAFE           45.0   // %                 -> Safe        (was 40.0)
#define DENSITY_DANGER         75.0   // %                 -> Danger      (was 70.0)
#define TEMP_SAFE_MAX          37.0   // °C               -> Safe boundary (was 32.0)
#define TEMP_DANGER_MIN        40.0   // °C               -> Danger boundary (was 35.0)
#define HUM_SAFE_MAX           80.0   // %RH              -> Safe boundary (was 70.0)
#define HUM_DANGER_MIN         90.0   // %RH              -> Danger boundary (was 80.0)
#define PERSON_DISTANCE_CM     100.0  // cm                -> zone considered occupied
#define NOISE_FLOOR_DB         40.0   // dB SPL mapped to 0%
#define NOISE_CEIL_DB          90.0   // dB SPL mapped to 100%

enum RiskLevel { RISK_SAFE = 0, RISK_MODERATE = 1, RISK_DANGER = 2 };

// ===========================
//  Packet structure (must match Ground Node)
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
  float   noiseLevel;        // dB SPL (raw)
  float   noiseNormalized;   // 0–100 % mapped from dB SPL
  uint8_t occupiedZones;     // zones below PERSON_DISTANCE_CM
  uint8_t temperatureRisk;
  uint8_t humidityRisk;
  uint8_t environmentRisk;
  uint8_t noiseRisk;
  uint8_t densityRisk;
  uint32_t packetNumber;
} CeilingPacket;

CeilingPacket data;

// Sensor objects
Adafruit_BME280 bme;
SparkFun_VL53L5CX myImager;

// VL53L5CX INT pin (LPn is tied to 3.3V externally)
#define VL53L5CX_INT_PIN 5

// I2S pins
#define I2S_WS   15
#define I2S_SCK  4
#define I2S_SD   13

const int AUDIO_BUFFER_SIZE = 512;
int16_t audioBuffer[AUDIO_BUFFER_SIZE];

const int MAX_HISTORY = 10;
float distHistory[MAX_HISTORY];
int historyIndex = 0, historyCount = 0;
uint32_t packetCounter = 0;
bool tofDataValid = false;

void readBME280();
void readVL53L5CX();
void readNoise();
void computeMovementAndStagnation();
void sendData();
void haltWithError(const char *message);

uint8_t evaluateTemperatureRisk(float tempC);
uint8_t evaluateHumidityRisk(float humPct);
uint8_t evaluateNoiseRisk(float normalizedPct);
uint8_t evaluateDensityRisk(float densityPct);
void evaluateRisks();

// ===========================
//  ESP‑NOW send callback
// ===========================
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

void haltWithError(const char *message) {
  Serial.println(message);
  while (true) {
    delay(1000);
  }
}

// ===========================
//  SETUP
// ===========================
void setup() {
  Serial.begin(115200);

  // ---- BME280 ----
  Wire.begin(21, 22);
  if (!bme.begin(0x76)) {
    haltWithError("BME280 not found!");
  }

  // ---- VL53L5CX ----
  // INT is open-drain / active-low, so enable the internal pull-up.
  pinMode(VL53L5CX_INT_PIN, INPUT_PULLUP);

  if (!myImager.begin()) {
    haltWithError("VL53L5CX not found!");
  }
  myImager.setResolution(8 * 8);
  myImager.setRangingFrequency(15);
  myImager.startRanging();

  // ---- I2S Microphone ----
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num  = I2S_WS,
    .data_out_num = -1,
    .data_in_num  = I2S_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  // ---- WiFi & ESP‑NOW with fixed channel ----
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);   // Force channel 1

  if (esp_now_init() != ESP_OK) {
    haltWithError("ESP-NOW init failed");
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, groundNodeMAC, 6);
  peerInfo.channel = 1;          // same channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    haltWithError("Failed to add peer");
  }

  Serial.println("Ceiling Node ready.");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Ground MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", groundNodeMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

// ===========================
//  LOOP
// ===========================
void loop() {
  readBME280();
  readVL53L5CX();
  readNoise();
  computeMovementAndStagnation();
  evaluateRisks();

  data.nodeAlive = true;
  data.packetNumber = packetCounter++;

  sendData();

  Serial.printf("T=%.2f H=%.2f P=%.2f  Dist=%.1f  Dens=%.1f%%  Noise=%.1f  Move=%.1f  Stag=%.1f\n",
                data.temperature, data.humidity, data.pressure,
                data.avgDistance, data.densityPercent,
                data.noiseLevel, data.movementIndex, data.stagnationIndex);

  delay(1000);
}

// -----------------------------
//  Sensor reading functions (same as before)
// -----------------------------
void readBME280() {
  data.temperature = bme.readTemperature();
  data.humidity    = bme.readHumidity();
  data.pressure    = bme.readPressure() / 100.0F;
}

void readVL53L5CX() {
  if (myImager.isDataReady()) {
    VL53L5CX_ResultsData results;
    myImager.getRangingData(&results);
    float sumDist = 0.0;
    int validZones = 0;
    int occupied = 0;
    for (int i = 0; i < 64; i++) {
      if (results.target_status[i] == 5) {
        float dist = results.distance_mm[i] / 10.0;   // mm -> cm
        sumDist += dist;
        validZones++;
        if (dist < PERSON_DISTANCE_CM) occupied++;
      }
    }
    data.occupiedZones = (uint8_t)occupied;
    if (validZones > 0) {
      data.avgDistance = sumDist / validZones;
      // DensityPercent uses occupied zones out of the 64-cell grid,
      // matching the algorithm spec: (OccupiedZones / 64) × 100
      data.densityPercent = (float)data.occupiedZones / 64.0 * 100.0;
      tofDataValid = true;
    } else {
      data.avgDistance = 0.0;
      data.densityPercent = 0.0;
      data.occupiedZones = 0;
      tofDataValid = false;
    }
    distHistory[historyIndex] = data.avgDistance;
    historyIndex = (historyIndex + 1) % MAX_HISTORY;
    if (historyCount < MAX_HISTORY) historyCount++;
  } else {
    data.avgDistance = 0.0;
    data.densityPercent = 0.0;
    data.occupiedZones = 0;
    tofDataValid = false;
  }
}

void readNoise() {
  size_t bytesRead;
  i2s_read(I2S_NUM_0, audioBuffer, AUDIO_BUFFER_SIZE * sizeof(int16_t), &bytesRead, portMAX_DELAY);
  int samples = bytesRead / sizeof(int16_t);
  if (samples <= 0) {
    data.noiseLevel = NOISE_FLOOR_DB;
    data.noiseNormalized = 0.0;
    return;
  }
  float sumSq = 0.0;
  for (int i = 0; i < samples; i++) {
    float val = audioBuffer[i] / 32768.0;
    sumSq += val * val;
  }
  float rms = sqrt(sumSq / samples);
  if (rms < 0.000001f) rms = 0.000001f;
  float dbSPL = 94.0 + 20.0 * log10(rms);
  dbSPL = constrain(dbSPL, 40.0, 120.0);
  data.noiseLevel = dbSPL;

  // Normalize to 0–100% using floor/ceiling dB SPL so the
  // 40% / 70% thresholds from the algorithm can be applied directly.
  float norm = (dbSPL - NOISE_FLOOR_DB) / (NOISE_CEIL_DB - NOISE_FLOOR_DB) * 100.0;
  data.noiseNormalized = constrain(norm, 0.0, 100.0);
}

void computeMovementAndStagnation() {
  if (!tofDataValid) {
    data.movementIndex = 0.0;
    data.stagnationIndex = 0.0;
    return;
  }

  if (historyCount >= 2) {
    float current = distHistory[(historyIndex - 1 + MAX_HISTORY) % MAX_HISTORY];
    float previous = distHistory[(historyIndex - 2 + MAX_HISTORY) % MAX_HISTORY];
    data.movementIndex = fabs(current - previous) * 2.0;
    data.movementIndex = constrain(data.movementIndex, 0.0, 100.0);
  } else {
    data.movementIndex = 0.0;
  }
  if (historyCount >= 3) {
    float mean = 0.0;
    for (int i = 0; i < historyCount; i++) mean += distHistory[i];
    mean /= historyCount;
    float var = 0.0;
    for (int i = 0; i < historyCount; i++) {
      float diff = distHistory[i] - mean;
      var += diff * diff;
    }
    var /= historyCount;
    data.stagnationIndex = constrain(100.0 - var, 0.0, 100.0);
  } else {
    data.stagnationIndex = 0.0;
  }
}

void sendData() {
  esp_err_t result = esp_now_send(groundNodeMAC, (uint8_t *)&data, sizeof(data));
  if (result != ESP_OK) {
    Serial.println("Send error");
  }
}

// ===========================
//  PER-SENSOR RISK EVALUATION
//  (matches algorithm Steps 2–4, thresholds raised +5 per fix #7)
// ===========================
uint8_t evaluateTemperatureRisk(float tempC) {
  if (tempC <= TEMP_SAFE_MAX)   return RISK_SAFE;
  if (tempC <= TEMP_DANGER_MIN) return RISK_MODERATE;
  return RISK_DANGER;
}

uint8_t evaluateHumidityRisk(float humPct) {
  if (humPct <= HUM_SAFE_MAX)   return RISK_SAFE;
  if (humPct <= HUM_DANGER_MIN) return RISK_MODERATE;
  return RISK_DANGER;
}

uint8_t evaluateNoiseRisk(float normalizedPct) {
  if (normalizedPct < NOISE_SAFE_THRESHOLD)   return RISK_SAFE;
  if (normalizedPct < NOISE_DANGER_THRESHOLD) return RISK_MODERATE;
  return RISK_DANGER;
}

uint8_t evaluateDensityRisk(float densityPct) {
  if (densityPct < DENSITY_SAFE)   return RISK_SAFE;
  if (densityPct < DENSITY_DANGER) return RISK_MODERATE;
  return RISK_DANGER;
}

void evaluateRisks() {
  data.temperatureRisk = evaluateTemperatureRisk(data.temperature);
  data.humidityRisk    = evaluateHumidityRisk(data.humidity);
  // EnvironmentRisk = HighestRisk(TemperatureRisk, HumidityRisk)
  data.environmentRisk = (data.temperatureRisk > data.humidityRisk)
                            ? data.temperatureRisk
                            : data.humidityRisk;

  data.noiseRisk    = evaluateNoiseRisk(data.noiseNormalized);
  data.densityRisk  = evaluateDensityRisk(data.densityPercent);
}