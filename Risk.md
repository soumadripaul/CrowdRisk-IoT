# Multi-Sensor Crowd Risk Prediction Algorithm

## Overview

The system computes a real-time crowd risk level by fusing five independent sensor-derived risk factors: occupancy (people count), ambient noise, temperature, humidity, and spatial density. Each factor is independently classified into one of three risk tiers — **Safe**, **Moderate**, or **Danger** — using fixed thresholds. The final risk level presented to the operator is the **maximum** of these five tiers, so that any single overloaded parameter is sufficient to escalate the overall alert, regardless of how the other parameters read.

Risk tiers are encoded numerically for comparison:

| Tier | Value |
|---|---|
| Safe | 0 |
| Moderate | 1 |
| Danger | 2 |

## Step 1 — Occupancy (People Count) Risk

Entries and exits are counted at the doorway using two IR break-beam sensors, giving a live occupant count *P* against a configured maximum capacity *C* (C = 10 in the deployed prototype).

$$
R_{count} =
\begin{cases}
\text{Safe} & P < C \\
\text{Moderate} & P = C \\
\text{Danger} & P > C
\end{cases}
$$

## Step 2 — Environmental Risk (Temperature + Humidity)

Temperature *T* (°C) and relative humidity *H* (%) are sampled from the ceiling-mounted BME280 sensor.

$$
R_{temp} =
\begin{cases}
\text{Safe} & T \le 32 \\
\text{Moderate} & 32 < T \le 35 \\
\text{Danger} & T > 35
\end{cases}
\qquad
R_{hum} =
\begin{cases}
\text{Safe} & H \le 70 \\
\text{Moderate} & 70 < H \le 80 \\
\text{Danger} & H > 80
\end{cases}
$$

The two are combined by taking the more severe of the pair:

$$
R_{env} = \max(R_{temp},\ R_{hum})
$$

## Step 3 — Noise Risk

A MEMS I²S microphone streams raw PCM samples. Sound pressure level is estimated from the RMS amplitude of each audio frame and converted to a normalized 0–100% scale before thresholding, so the same tier boundaries apply regardless of the microphone's raw dB range.

$$
dB_{SPL} = \text{clamp}\big(94 + 20\log_{10}(\text{RMS}),\ 40,\ 120\big)
$$

$$
N = \text{clamp}\left(\frac{dB_{SPL} - 40}{90 - 40} \times 100,\ 0,\ 100\right)\ (\%)
$$

$$
R_{noise} =
\begin{cases}
\text{Safe} & N < 35 \\
\text{Moderate} & 35 \le N < 65 \\
\text{Danger} & N \ge 65
\end{cases}
$$

## Step 4 — Spatial Density Risk

An 8×8 ToF depth sensor (VL53L5CX) divides the monitored area into 64 zones. A zone is marked **occupied** if the measured distance in that cell falls below a proximity threshold of 100 cm (i.e., something/someone is close enough beneath the sensor to register as a person rather than open floor).

$$
D = \frac{\text{OccupiedZones}}{64} \times 100\ (\%)
$$

$$
R_{density} =
\begin{cases}
\text{Safe} & D < 40 \\
\text{Moderate} & 40 \le D < 70 \\
\text{Danger} & D \ge 70
\end{cases}
$$

Two supplementary indices are derived from the same depth history but are used for trend/analytics display rather than feeding the risk fusion directly:

- **Movement Index** — magnitude of change in average distance between consecutive frames, scaled and clamped to 0–100.
- **Stagnation Index** — 100 minus the variance of the last 10 average-distance samples, clamped to 0–100 (a high value indicates a crowd that has stopped moving, e.g., a bottleneck).

## Step 5 — Risk Fusion

The five per-factor risk levels are combined by a **max-severity rule**: the overall risk equals the single most severe input. This is a deliberately conservative (worst-case) fusion strategy, since crowd-safety hazards are not mutually offsetting — e.g., a dangerously overcrowded but quiet, cool room is still dangerous.

$$
R_{overall} = \max\big(R_{count},\ R_{noise},\ R_{env},\ R_{density}\big)
$$

## Step 6 — Alert Mapping

The fused risk level drives the physical output layer at the ground node:

| Risk Level | LED | Buzzer Behavior |
|---|---|---|
| Safe | Green | Silent |
| Moderate | Yellow | Short intermittent beep (~1 Hz), auto-silences after 3 s at that level |
| Danger | Red | Continuous tone, auto-silences after 5 s at that level |

The buzzer timeout is re-armed only when the risk level *changes*, so a sustained Danger condition is audibly flagged once rather than indefinitely, while any escalation (e.g., Moderate → Danger) immediately re-triggers the alert.

## Pseudocode Summary

```
ALGORITHM ComputeCrowdRisk(peopleCount, temperature, humidity,
                            noiseNormalized, occupiedZones)

    // Step 1
    R_count    ← ClassifyCount(peopleCount)

    // Step 2
    R_temp     ← ClassifyThreshold(temperature, 32, 35)
    R_hum      ← ClassifyThreshold(humidity, 70, 80)
    R_env      ← max(R_temp, R_hum)

    // Step 3
    R_noise    ← ClassifyThreshold(noiseNormalized, 35, 65)

    // Step 4
    density    ← (occupiedZones / 64) × 100
    R_density  ← ClassifyThreshold(density, 40, 70)

    // Step 5 — Fusion
    R_overall  ← max(R_count, R_noise, R_env, R_density)

    RETURN R_overall
END ALGORITHM
```

## Design Rationale

- **Independent per-sensor evaluation** keeps the classification transparent and auditable — each tier boundary maps to a physically meaningful condition (e.g., WHO/ASHRAE-adjacent comfort bounds for temperature/humidity, a 40 cm/64-zone packing density for crowding).
- **Max-severity fusion** was chosen over a weighted/averaged score because averaging could mask an acute single-factor hazard (e.g., a spike in noise or density) behind otherwise-normal readings — undesirable in a safety-critical context.
- **Dual-node redundancy**: the ground node re-derives temperature, humidity, noise, and density risk locally from the raw values in each received packet, rather than trusting the ceiling node's pre-computed risk fields outright. This keeps the ground node's alerting logic self-contained if a future firmware revision on the ceiling node omits or corrupts a cached risk field.