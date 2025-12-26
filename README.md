# 📡 SD Squared Telemetry: ESP32 Data Logger

This project is a high-frequency data logging system for vehicle suspension telemetry. It utilizes the ESP32’s dual-core architecture to capture sensor data at **100Hz** while maintaining a responsive web dashboard and background cloud uploads.

---

## 📖 Quick Start Instructions

1.  **Power On:** Connect the ESP32 to a power source. The RGB LED will turn **Purple** during boot.
2.  **Initial Setup:** * Search for the Wi-Fi network: **`SD Squared Telemetry`** (Password: `sdsquared`).
    * Once connected, open your browser and go to **`http://esp32-ap.local`** (or `192.168.4.1`).
    * Enter your local Wi-Fi credentials. The onboard LED will **blink** while connecting.
3.  **Operation:**
    * **Record:** Press the physical **Button (GPIO 14)**. The RGB LED turns **Red**. Data logs directly to the SD card.
    * **Stop:** Press the button again. The RGB LED returns to **Green**.
4.  **Manage Data:**
    * On your local network, visit **`http://esp32.local`**.
    * View recorded runs, add metadata (Track name/Comments), and sync them to the cloud.

---

## 🚀 Key Features

* **Dual-Core Execution:** * **Core 0:** Dedicated to 100Hz sensor sampling and SD card I/O to prevent data loss.
    * **Core 1:** Handles Wi-Fi, Async Web Server, and UI updates.
* **Storage:** Saves high-resolution CSV files to an **SD Card** (naming format: `run_TIMESTAMP.csv`).
* **Cloud Integration:** Background task streams CSV data + metadata to a Railway-hosted backend via `WiFiClientSecure`.
* **mDNS Support:** Access the device via `esp32.local` or `esp32-ap.local` instead of IP addresses.

---

## 🚥 Visual Feedback (LED Status)

| State | RGB LED | Onboard LED | Description |
| :--- | :--- | :--- | :--- |
| **Setup** | 🟣 Purple | Off | Initializing SPIFFS/SD/Tasks |
| **Ready/Idle** | 🟢 Green | Solid (if Wi-Fi ok) | System ready, not recording |
| **Recording** | 🔴 Red | Solid | Writing sensor data to SD card |
| **Error** | 🔵 Blue | Off | SD Card mount or SPIFFS failure |
| **Connecting** | State color | ⚪ Blinking | Attempting to join Wi-Fi network |
| **Connected** | State color | ⚪ Solid | Successfully joined Wi-Fi |

---

## ⚙️ MCU Task Allocation

| Core | Task Name | Responsibilities |
| :--- | :--- | :--- |
| **Core 0** | `DataTask` | Button debouncing, 10ms (100Hz) sampling, SD buffering/writing. |
| **Core 1** | `WiFiTask` | Web server management, mDNS responder, SoftAP configuration. |
| **Async** | `UploadTask` | Background HTTPS POST streaming of CSV data from SD to Cloud. |

---

## 🧩 Hardware Configuration

| Component | Pin | Notes |
| :--- | :--- | :--- |
| **Button** | GPIO 14 | Active LOW (Internal Pull-up) |
| **Onboard LED** | GPIO 2 | Wi-Fi Connection status |
| **RGB LED (R,G,B)** | 25, 26, 27 | Status indicators |
| **SD Card CS** | GPIO 5 | SPI Protocol for data storage |
| **Front Sus Sensor**| GPIO 34 | Analog input (Suspension travel) |
| **Rear Sus Sensor** | GPIO 35 | Analog input (Suspension travel) |

---

## 🌐 Web API Endpoints

The ESP32 hosts an `AsyncWebServer` with the following endpoints:

* **`GET /`**: Serves the configuration portal (AP mode) or dashboard (STA mode).
* **`POST /connect`**: Receives SSID and Password to switch from AP to Station mode.
* **`GET /runs`**: Returns a JSON list of all `.csv` files currently stored on the SD card.
* **`POST /uploadRun`**: Triggers a background task to upload a specific file with metadata (run name, track, comments).

---

## ⚠️ Notes & Technical Limits

* **HTTPS Uploads:** Uses `client.setInsecure()` to handle certificates without the overhead of root CA management on the MCU.
* **Buffer Management:** Data is captured in a 512-line RAM buffer before being flushed to the SD card to prevent I/O blocking.
* **mDNS on Android:** Android users should type the full `http://esp32.local/` in Chrome to ensure the address is resolved correctly.