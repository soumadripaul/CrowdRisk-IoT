#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>

SparkFun_VL53L5CX myImager;
VL53L5CX_ResultsData measurementData;

// ===== TUNE THESE FOR YOUR INSTALLATION =====
const float CEILING_HEIGHT_MM = 3000.0;
const int OCCUPIED_THRESHOLD_MM = 2200;
// ============================================

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);

  Serial.println("Starting VL53L5CX...");

  if (!myImager.begin())
  {
    Serial.println("VL53L5CX NOT FOUND");
    while (1);
  }

  Serial.println("VL53L5CX FOUND");

  myImager.setResolution(8 * 8);

  myImager.startRanging();

  Serial.println("Ranging Started");
}

void loop()
{
  if (myImager.isDataReady())
  {
    if (myImager.getRangingData(&measurementData))
    {
      float sumDistance = 0;
      int validZones = 0;
      int occupiedZones = 0;

      for (int i = 0; i < 64; i++)
      {
        int distance = measurementData.distance_mm[i];

        if (distance > 0)
        {
          sumDistance += distance;
          validZones++;

          if (distance < OCCUPIED_THRESHOLD_MM)
          {
            occupiedZones++;
          }
        }
      }

      if (validZones == 0)
      {
        Serial.println("No Valid Zones");
        return;
      }

      // -------------------------
      // Average Distance
      // -------------------------
      float avgDistance =
          sumDistance / validZones;

      // -------------------------
      // Occupied Percentage
      // -------------------------
      float occupiedPercent =
          ((float)occupiedZones / 64.0) * 100.0;

      // -------------------------
      // Distance Factor
      // -------------------------
      float distanceFactor =
          ((CEILING_HEIGHT_MM - avgDistance)
           / CEILING_HEIGHT_MM) * 100.0;

      if (distanceFactor < 0)
        distanceFactor = 0;

      if (distanceFactor > 100)
        distanceFactor = 100;

      // -------------------------
      // Hybrid Density Score
      // -------------------------
      float densityScore =
          (0.7 * occupiedPercent) +
          (0.3 * distanceFactor);

      String densityStatus;

      if (densityScore <= 25)
        densityStatus = "LOW";
      else if (densityScore <= 50)
        densityStatus = "MODERATE";
      else if (densityScore <= 75)
        densityStatus = "HIGH";
      else
        densityStatus = "CRITICAL";

      // -------------------------
      // Print Results
      // -------------------------

      Serial.println("\n============================");

      Serial.print("Valid Zones: ");
      Serial.println(validZones);

      Serial.print("Occupied Zones: ");
      Serial.println(occupiedZones);

      Serial.print("Occupied %: ");
      Serial.print(occupiedPercent);
      Serial.println("%");

      Serial.print("Average Distance: ");
      Serial.print(avgDistance);
      Serial.println(" mm");

      Serial.print("Distance Factor: ");
      Serial.print(distanceFactor);
      Serial.println("%");

      Serial.print("Density Score: ");
      Serial.print(densityScore);
      Serial.println("%");

      Serial.print("Density Status: ");
      Serial.println(densityStatus);

      Serial.println("============================");
    }
  }

  delay(200);
}