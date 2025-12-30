# 📡 SD Squared Telemetry: ESP32 Data Logger

This project is a high-frequency data logging system for vehicle suspension telemetry. It utilizes the ESP32’s dual-core architecture to capture sensor data at **100Hz** while maintaining a responsive web dashboard and background cloud uploads.

---

## 📖 Quick Start Instructions

1.  **Power On**: Power on the ESP32. The RGB LED will turn **White/Purple** during boot.
2.  **Initial Setup**: 
    * Search for Wi-Fi: **`SD Squared Telemetry`** (Password: `sdsquared`).
    * Visit **`http://esp32-ap.local`** (or `192.168.4.1`) to enter your local Wi-Fi credentials - i.e. hotspot information.
      * If you are using a hotspot it is **highly** reccommended the SSID has no special characters or spaces in it
3.  **Operation**:
    * **Setup Run**: Press **Button (GPIO 14)** once. LED turns **Yellow**. A new run file is prepared.
    * **Record**: Press again. LED turns **Red**. Data logs at 100Hz.
    * **Stop**: Press again. LED returns to **Green**.
4.  **Sync**: Visit **`http://esp32.local`** on your local network to upload files to the cloud.

---

## User Feedback
### RGB LED (System State)
*Indicates the current operational mode of the telemetry system.*

| State | Color | Description |
| :--- | :--- | :--- |
| **Setup** | ⚪ White | Initializing storage and launching tasks. |
| **Ready/Idle** | 🟢 Green | System ready; waiting to start a run. |
| **Run Setup** | 🟡 Yellow | New file created on SD and unweighted values recorded; awaiting button press to record. |
| **Recording** | 🔴 Red | Actively logging sensor data to the SD card at 100Hz. |
| **Error** | 🔵 Blue | Fatal error: SD Card mount failed or file creation failed. |

### Onboard LED (Connectivity)
*Indicates WiFi status and background network activity via GPIO 2.*

| State | Pattern | Description |
| :--- | :--- | :--- |
| **AP Mode** | ⚪ Slow Blink | Acting as an Access Point (1000ms interval). |
| **Connecting** | ⚪ Fast Blink | Attempting to join a network (200ms interval). |
| **Connected** | ⚪ Solid | Successfully joined WiFi; Web Server is live. |
| **Disconnected**| Off | Not connected to any WiFi network. |

---

## 🚀 Key Features

* **Dual-Core Execution:**
   * **Core 0:** Dedicated to 100Hz sensor sampling and SD card I/O to prevent data loss.
    * **Core 1:** Handles Wi-Fi, Async Web Server, and UI updates.
* **Storage:** Saves high-resolution CSV files to an **SD Card** (naming format: `run_n+1.csv`).
* **Cloud Integration:** Background task streams CSV data + metadata to a Railway-hosted backend via `WiFiClientSecure`.
* **mDNS Support:** Access the device via `esp32.local` or `esp32-ap.local` instead of IP addresses.

---

## 📂 Project Structure

The codebase is modularized to separate hardware configuration, global state, storage logic, and networking tasks.

### ⚙️ Core Logic
* **`main.cpp`**: The entry point. It initializes hardware pins, mounts storage, and launches the FreeRTOS tasks on specific cores.
* **`config.h`**: The single source of truth for hardware. It contains pin definitions (LEDs, sensors, SD), sampling frequency, and the backend API URL.
* **`globals.h / .cpp`**: Manages the system's "brain." It stores shared variables like WiFi status and recording state, and handles the `updateOnBoardLed()` logic for non-blocking blinking.

### 💾 Data & Storage
* **`storage_manager.h / .cpp`**: Handles the heavy lifting for the **SD Card** and **LittleFS**. It manages the creation of new run files (e.g., `run_1.csv`) and flushes data buffers from RAM to the physical card.
* **`telemetry_tasks.h / .cpp`**: Contains the dual-core execution loops:
    * **Core 0 (`DataTask`)**: High-priority loop for 100Hz sensor sampling and physical button debouncing.
    * **Core 1 (`WiFiTask`)**: Manages the web server and system updates.

### 🌐 Networking & Web Interface
* **`network_manager.h / .cpp`**: Orchestrates WiFi connectivity (AP vs. Station mode) and defines all **Async Web Server** routes for the dashboard and data management.
* **`/data` Folder (Web Interface)**: Static assets served from LittleFS to provide the user interface:
    * **`index.html`**: The initial WiFi configuration portal used to connect the ESP32 to a local network.
    * **`connected.html`**: The main telemetry dashboard for viewing recorded runs and entering metadata.
    * **`style.css`**: The stylesheet providing a clean, responsive design for both mobile and desktop users.
    * **`script.js`**: Frontend logic that fetches the run list, handles metadata forms, and communicates with the ESP32 API.
* **Background Upload**: A specialized `uploadRunTask` that streams large CSV files from the SD card to a Railway backend via multipart HTTPS.

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
| **Button** | GPIO 14 | Active LOW; cycles Idle ➔ Setup ➔ Record. |
| **Onboard LED** | GPIO 2 | WiFi connection status. |
| **RGB LED** | 25, 26, 27 | R, G, B pins for status indicators. |
| **SD Card CS** | GPIO 5 | SPI Chip Select for storage. |
| **Suspension** | 34 (F), 35 (R) | Analog inputs for travel measurement. |

---

## 🌐 Web API Endpoints

The ESP32 hosts an `AsyncWebServer` with the following endpoints:

* **`GET /`**: Serves the configuration portal (AP mode) or dashboard (STA mode).
* **`POST /connect`**: Receives SSID and Password to switch from AP to Station mode.
* **`GET /runs`**: Returns a JSON list of all `.csv` files currently stored on the SD card.
* **`POST /uploadRun`**: Triggers a background task to upload a specific file with metadata (run name, track, comments).
* **`POST /deleteRun`**: Removes a specific file from the SD card.

---

## ⚠️ Notes & Technical Limits

* **HTTPS Uploads:** Uses `client.setInsecure()` to handle certificates without the overhead of root CA management on the MCU.
* **Buffer Management:** Data is captured in a 512-line RAM buffer before being flushed to the SD card to prevent I/O blocking.
* **mDNS on Android:** Android users should type the full `http://esp32.local/` in Chrome to ensure the address is resolved correctly.