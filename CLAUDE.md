# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Deploy Commands

```bash
# Build firmware
pio run

# Upload firmware to device
pio run --target upload

# Upload LittleFS filesystem (web UI assets from data/)
pio run --target uploadfs

# Monitor serial output (115200 baud)
pio run --target monitor

# Build + upload + monitor in one step
pio run --target upload && pio run --target monitor
```

The `data/` directory is embedded into LittleFS at upload time. Edit files there directly — there is no frontend build step.

## Architecture Overview

Dual-core FreeRTOS on Adafruit Feather ESP32-S3. The two cores run entirely separate tasks with no shared mutex:

**Core 0 — `DataTaskcode()` (`src/telemetry_tasks.cpp`)**
- 100 Hz sampling loop driven by `vTaskDelayUntil()` (10 ms period, `SAMPLE_PERIOD_MS` in `include/config.h`)
- Reads LSM6DS3TRC IMU over I2C (400 kHz) and two suspension potentiometers via ADC
- Button state machine: Idle → Setup (calibration) → Recording → Idle
- On Setup: collects 50-sample gyro/accel bias, builds a rotation matrix (sensor → world frame, Z = gravity)
- Per sample: removes bias, applies rotation matrix, filters ADC with 20-sample σ-rejection (`stddevFilteredADC`)
- Buffers `SensorLine` structs in `sensorBuffer` (RAM), flushes to SD when full (`MAX_BUFFER_SIZE`)

**Core 1 — `WiFiTaskcode()` (`src/telemetry_tasks.cpp`)**
- Starts `ESPAsyncWebServer` and mDNS (`esp32.local` / `esp32-ap.local`)
- Reads MAX17048 battery gauge every 30 s and updates NeoPixel color
- Spawns a one-shot `uploadRunTask` (background FreeRTOS task) when upload is requested

## Key Files

| File | Purpose |
|------|---------|
| `include/config.h` | All pin definitions, `SAMPLE_PERIOD_MS`, backend URL — single source of truth |
| `include/globals.h` / `src/globals.cpp` | Shared state: `recording`, `batteryPercent`, LED helpers, WiFi event flags |
| `src/telemetry_tasks.cpp` | Both FreeRTOS task bodies; IMU calibration; `stddevFilteredADC`; `logDiagnostics`; `logBufferData` |
| `src/storage_manager.cpp` | `startNewRun()`, `flushSensorBuffer()` — SD card CSV creation and buffer flush |
| `src/network_manager.cpp` | Web routes, AP/STA WiFi switching, HTTPS multipart upload to Railway backend |
| `data/` | Static web UI (HTML/CSS/JS) served from LittleFS — edit directly, no build step |

## CSV Format

Columns in order: `gyro_x_world_mrads, gyro_y_world_mrads, gyro_z_world_mrads, accel_x_mg, accel_y_mg, accel_z_mg, rear_sus, front_sus`

- Gyro values: milli-rad/s in world frame (Z = down)
- Accel values: milli-g in world frame, gravity removed from Z axis
- Suspension: raw ADC counts (front = direct read, rear = `4095 - read`)

## Sensor Data Flow

```
IMU (104 Hz) ──► bias removal ──► rotation matrix ──► SensorLine.acc[0..5]
ADC (×20 burst) ──► σ-filter mean ──────────────────► SensorLine.{front,rear}_sus
SensorLine ──► sensorBuffer (RAM) ──► flushSensorBuffer() ──► SD card CSV
```

## WiFi / Web UI Flow

1. Boot → AP mode (`SD Squared Telemetry`, pw: `sdsquared`)
2. User POSTs credentials to `/connect` → ESP32 switches to STA mode
3. STA mode → `esp32.local` serves `connected.html` dashboard
4. Dashboard polls `GET /runs`, user selects run + fills metadata, POSTs to `/uploadRun`
5. Background `uploadRunTask` streams CSV as multipart HTTPS POST to Railway; deletes file on 200/201

## Hardware Pins (actual wiring — README has outdated values)

Authoritative source is `include/config.h`. Current assignments:
- Button: GPIO 9 (active LOW)
- RGB LED: R=10, G=11, B=12
- NeoPixel data: GPIO 33, power enable: GPIO 21
- SD CS: GPIO 5
- Front suspension ADC: GPIO 15 (A3)
- Rear suspension ADC: GPIO 14 (A4)
- Battery gauge I2C: SDA=3, SCL=4

## Coding Practices

- Declare variables only just before they are needed
- variable names and functions should be well named to inform the user of what they are doing
- Add vertical spacing to keep the code readable
- The code should read like a story
- Avoid nested sections, always abstract out functionality to seperate methods
- Each function should be responsible for one thing and one things only
- Add comments only where the code might be unreadable, your priority is to make readable code over everything else

**Embedded constraints:**
- No dynamic allocation (`new`/`malloc`) inside `DataTaskcode` — heap fragmentation causes hard-to-debug crashes
- No blocking calls on Core 0 — the 10 ms sampling budget must not be exceeded

**C++ discipline:**
- Use `const` for variables that don't change; use `static constexpr` for file-scope constants
- File-local helper functions must be declared `static`

**Prohibitions:**
- Never use `delay()` anywhere inside `DataTaskcode`
- Never add `Serial.print` calls inside helpers that run at 100 Hz (inside the sample loop)

**Serial logging convention:**
- All `Serial.printf` output must use a `[TAG]` prefix matching the existing pattern: `[DIAG]`, `[CAL]`, `[BATT]`, `[INFO]`, `[DATA]`

## Secrets

`include/secrets.h` is git-ignored. Copy from `include/secrets.h.example` and fill in `API_KEY`.
