                                                                                                     # Crowd Risk Prediction & Safety Monitoring System

## An Edge-Enabled IoT Framework for Real-Time Crowd Risk Prediction Using Multi-Sensor Data Fusion

---

## 📖 Project Description

The **Crowd Risk Prediction & Safety Monitoring System** is a low-cost, privacy-preserving IoT solution designed to monitor crowd conditions in real time and predict potentially dangerous situations before they escalate into overcrowding incidents, congestion, crowd crushes, or stampedes.

Traditional crowd monitoring systems often rely on CCTV surveillance and manual observation, which are reactive in nature. This project adopts a proactive approach by combining multiple sensors and edge computing to continuously assess crowd conditions and generate early warnings.

The system uses two ESP32-based sensing nodes that collect and process crowd-related data locally. By leveraging multi-sensor data fusion and distributed edge processing, the system calculates a dynamic crowd risk score and triggers alerts through LEDs, buzzers, and a monitoring dashboard.

### Key Features

* Real-time crowd density monitoring
* Crowd flow and movement tracking
* Congestion and stagnation detection
* Multi-sensor data fusion
* Intelligent crowd risk prediction
* Edge-based processing using ESP32
* Wireless communication via ESP-NOW
* Dashboard visualization and data logging
* LED and buzzer-based warning system
* Privacy-preserving monitoring without cameras

---

# 🏗 Detailed Project Information

## System Components

### Ground Node

The Ground Node acts as the primary processing unit and is responsible for:

* Counting people entering and exiting monitored areas
* Receiving sensor data from the Ceiling Node
* Calculating crowd risk scores
* Triggering alerts
* Sending data to the monitoring dashboard

**Hardware Used**

* ESP32 DevKit V1
* 2 × FC-51 IR Sensors
* Buzzer
* Blue LED
* Yellow LED
* Green LED
* LCD Display

---

### Ceiling Node

The Ceiling Node collects environmental and density-related information.

**Hardware Used**

* ESP32 DevKit V1
* VL53L5X ToF Sensor
* BME280 Environmental Sensor
* INMP441 Digital Microphone

---

## Sensor Functions

### VL53L5X ToF Sensor

Used for:

* Crowd density estimation
* Occupancy detection
* Crowd concentration monitoring

### FC-51 IR Sensors

Used for:

* Entry counting
* Exit counting
* Crowd flow estimation

### BME280 Sensor

Measures:

* Temperature
* Humidity
* Atmospheric Pressure

### INMP441 Microphone

Used for:

* Abnormal noise detection
* Panic-related sound monitoring
* Sound intensity analysis

---

## System Architecture

```text
+------------------------------------------------+
|                Monitoring Dashboard            |
+------------------------------------------------+
                    ↑ Wi-Fi/MQTT
                    |
+------------------------------------------------+
|                  Ground Node                   |
|------------------------------------------------|
| ESP32                                          |
| FC-51 Entry Sensor                             |
| FC-51 Exit Sensor                              |
| Buzzer                                         |
| LEDs                                           |
| Risk Analysis Engine                           |
+------------------------------------------------+
                    ↑ ESP-NOW
                    |
+------------------------------------------------+
|                  Ceiling Node                  |
|------------------------------------------------|
| ESP32                                          |
| VL53L5X ToF Sensor                             |
| BME280 Sensor                                  |
| INMP441 Microphone                             |
+------------------------------------------------+
```

---

## Core System Workflow

1. Sensors collect real-time crowd and environmental data.
2. Ceiling Node processes sensor readings locally.
3. Sensor data is transmitted to the Ground Node via ESP-NOW.
4. Ground Node performs crowd risk analysis.
5. Risk score is calculated.
6. Alerts are generated when thresholds are exceeded.
7. Data is forwarded to a dashboard for visualization and logging.

---

# 🔌 Step-by-Step Connection Guide

## Ground Node Setup

### Step 1 – Prepare the ESP32

Mount the ESP32 DevKit V1 on a breadboard and power it using a power bank.

---

### Step 2 – Connect Entry IR Sensor

| FC-51 Pin | ESP32 Pin |
| --------- | --------- |
| VCC       | 3.3V      |
| GND       | GND       |
| OUT       | GPIO 32   |

This sensor counts incoming people.

---

### Step 3 – Connect Exit IR Sensor

| FC-51 Pin | ESP32 Pin |
| --------- | --------- |
| VCC       | 3.3V      |
| GND       | GND       |
| OUT       | GPIO 33   |

This sensor counts outgoing people.

---

### Step 4 – Connect LEDs

Use a 220Ω resistor in series with each LED.

#### Green LED

* GPIO 27 → Resistor → LED Anode
* LED Cathode → GND

#### Yellow LED

* GPIO 26 → Resistor → LED Anode
* LED Cathode → GND

#### Blue LED

* GPIO 14 → Resistor → LED Anode
* LED Cathode → GND

---

### Step 5 – Connect Buzzer

| Buzzer Pin | ESP32 Pin |
| ---------- | --------- |
| Positive   | GPIO 25   |
| Negative   | GND       |

---

### Step 6 – Connect LCD Display

| LCD Pin | ESP32 Pin |
| ------- | --------- |
| VCC     | 3.3V      |
| GND     | GND       |
| SDA     | GPIO 21   |
| SCL     | GPIO 22   |

---

### Step 7 – Add Stabilization Capacitor

Connect a **100µF capacitor** between:

* 3.3V
* GND

Place it close to the ESP32 power pins.

---

### Step 8 – Power the Ground Node

Connect the ESP32 to a dedicated power bank.

---

# Ceiling Node Setup

### Step 1 – Prepare the ESP32

Mount the ESP32 on a breadboard and power it using a separate power bank.

---

### Step 2 – Connect VL53L5X ToF Sensor

| VL53L5X Pin | ESP32 Pin         |
| ----------- | ----------------- |
| VIN         | 3.3V              |
| GND         | GND               |
| SDA         | GPIO 21           |
| SCL         | GPIO 22           |
| INT         | GPIO 5 (Optional) |
| LPn         | 3.3V (Optional) |

Mount the sensor facing downward toward the crowd.

---

### Step 3 – Connect BME280 Sensor

| BME280 Pin | ESP32 Pin |
| ---------- | --------- |
| VIN        | 3.3V      |
| GND        | GND       |
| SDA        | GPIO 21   |
| SCL        | GPIO 22   |

The BME280 shares the same I²C bus as the VL53L5X.

---

### Step 4 – Connect INMP441 Microphone

| INMP441 Pin | ESP32 Pin |
| ----------- | --------- |
| VDD         | 3.3V      |
| GND         | GND       |
| WS          | GPIO 15   |
| SCK         | GPIO 4    |
| SD          | GPIO 13   |
| L/R         | GND       |

---

### Step 5 – Add Stabilization Capacitor

Connect a **100µF capacitor** between:

* 3.3V
* GND

Place it near the sensor power lines.

---

### Step 6 – Power the Ceiling Node

Connect the ESP32 to a separate power bank.

---

# 📋 Ground Node Sensor Connection Table

| Component              | ESP32 Pin | Purpose                |
| ---------------------- | --------- | ---------------------- |
| LCD SDA                | GPIO 21   | I²C Data               |
| LCD SCL                | GPIO 22   | I²C Clock              |
| Buzzer                 | GPIO 25   | Critical risk alert    |
| Yellow LED             | GPIO 26   | Caution indication     |
| Green LED              | GPIO 27   | Safe indication        |
| Blue LED               | GPIO 14   | Danger indication      |
| FC-51 Entry Sensor OUT | GPIO 32   | Entry counting         |
| FC-51 Exit Sensor OUT  | GPIO 33   | Exit counting          |
| Power Rail             | 3.3V      | Power supply           |
| Ground Rail            | GND       | Common ground          |

---

# 📋 Ceiling Node Sensor Connection Table

| Component   | ESP32 Pin | Purpose            |
| ----------- | --------- | ------------------ |
| VL53L5X SDA | GPIO 21   | I²C Data           |
| VL53L5X SCL | GPIO 22   | I²C Clock          |
| VL53L5X INT | GPIO 5    | Optional interrupt |
| VL53L5X LPn | 3.3V      | Optional reset     |
| BME280 SDA  | GPIO 21   | Shared I²C Data    |
| BME280 SCL  | GPIO 22   | Shared I²C Clock   |
| INMP441 WS  | GPIO 15   | I²S Word Select    |
| INMP441 SCK | GPIO 4    | I²S Clock          |
| INMP441 SD  | GPIO 13   | I²S Data           |
| INMP441 L/R | GND       | Left channel       |
| Power Rail  | 3.3V      | Power supply       |
| Ground Rail | GND       | Common ground      |

---

# 📡 Communication Flow

```text
Sensors
   ↓
Ceiling Node ESP32
   ↓ ESP-NOW
Ground Node ESP32
   ↓
Risk Analysis Engine
   ↓
LED / Buzzer Alerts
   ↓
Wi-Fi / MQTT
   ↓
Monitoring Dashboard
```

---

# 🎯 Practical Applications

* Railway Stations
* Bus Terminals
* Ferry Terminals
* Stadiums
* Shopping Malls
* Religious Gatherings
* Educational Institutions
* Concerts and Festivals
* Public Events
* Smart City Deployments

---

# 🚀 Future Enhancements

* Machine Learning-based crowd prediction
* AI-powered anomaly detection
* Cloud analytics integration
* Mobile application support
* Heatmap visualization
* Multi-floor monitoring
* Additional ESP32 sensing nodes
* Smart city integration

---

# 📈 Expected Outcomes

The system aims to provide:

* Accurate real-time crowd monitoring
* Early detection of dangerous crowd conditions
* Reduced risk of crowd-related accidents
* Improved situational awareness
* Low-cost deployment capability
* Scalable and portable architecture
* Privacy-preserving crowd safety management

By combining edge computing, multi-sensor fusion, and predictive analytics, this project provides a practical framework for improving crowd safety in public environments.
