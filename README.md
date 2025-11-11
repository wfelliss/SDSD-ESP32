# 📡 ESP32 WiFi Recording and Web Interface

This project implements a **dual-core ESP32 application** that lets you:

- Connect the ESP32 to a Wi-Fi network through a **local web portal**  
- Use a **button** to start and stop recording random (or sensor) data  
- Serve a **web dashboard** to view recorded runs  
- Provide visual feedback using **RGB LEDs** and the **onboard LED**

---

## 🚀 Features

- **Wi-Fi Setup Portal:**  
  When first powered on, the ESP32 starts a Wi-Fi Access Point (`ESP32-Setup`) where users can enter SSID and password via a web form.

- **Dual-Core Tasking:**  
  - **Core 1:** Handles Wi-Fi, web server, and LED status updates  
  - **Core 0:** Manages button input and recording logic  

- **Data Recording:**  
  Press the button to toggle recording on/off.  
  - While recording: random integers (or sensor data) are appended to the current session  
  - When stopped: the session is saved into memory (`allRecordings`)  
  - All runs can be viewed on `/connected`

- **LED Feedback:**

  | State | LED Color | Description |
  |--------|------------|-------------|
  | Setup | 🟣 Purple | SPIFFS and task initialization |
  | Recording | 🔴 Red | Actively capturing data |
  | Idle | 🟢 Green | Not recording |
  | SPIFFS Error | 🔵 Blue | Mounting failed |
  | Connecting Wi-Fi | ⚪ Blinking onboard LED |
  | Connected Wi-Fi | ⚪ Solid onboard LED |

- **SPIFFS File Hosting:**  
  Hosts HTML, CSS, and JS files for the Wi-Fi portal and connected dashboard.

---

## 🧠 Architecture Overview
# 💻 ESP32 Firmware Architecture Overview

This document outlines the allocation of tasks across the two CPU cores and the structure of the hosted web assets using the SPIFFS file system.

---

## ⚙️ MCU Core Task Allocation

The firmware utilizes the ESP32's dual-core capability to ensure real-time data handling (Core 0) is not interrupted by network activities (Core 1).

| Core | Task Group | Primary Responsibilities |
| :--- | :--- | :--- |
| **Core 1** | **WiFiTaskcode** | **Networking & Server** |
| | | - SoftAP Initialization (for ESP32-Setup) |
| | | - AsyncWebServer handling (Web UI) |
| | | - Wi-Fi Connection Management |
| | | - Onboard LED Control (Network Status) |
| **Core 0** | **DataTaskcode** | **Peripherals & Data Acquisition** |
| | | - Button Handling (with Debounce) |
| | | - Recording and Sampling Sensor Data |
| | | - RGB LED Status Feedback |

---

## 💾 SPIFFS (SPI Flash File System) Structure

The web interface assets are hosted from the onboard flash memory:

* **`/index.html`**: The main Wi-Fi setup page (used during initial configuration).
* **`/connected.html`**: The primary dashboard/data monitoring page.
* **`/style.css`**: Cascading Style Sheets for visual styling.
* **`/script.js`**: Client-side JavaScript for dynamic interaction.

## 🧩 Hardware Setup

| Component | Pin | Notes |
|------------|-----|-------|
| Button | GPIO 4 | Start/stop recording |
| RGB LED | GPIOs 15, 2, 0 | Status indicator |
| Onboard LED | GPIO 2 | Wi-Fi status |
| SPIFFS | — | File storage for HTML/JS/CSS |
| Power | 5V via USB | Recommended |

---

## 🌐 Accessing the Device

1. **First Boot:**  
   - The ESP32 starts in Access Point mode (`ESP32-Setup`).  
   - Connect your phone or PC to this Wi-Fi network.  
   - Visit **`192.168.4.1`** in your browser.  
   - Enter your Wi-Fi credentials and submit.

2. **After Connection:**  
   - The ESP32 reboots and connects to your Wi-Fi network.  
   - Once connected, the onboard LED turns solid.  
   - To view data runs, open **`http://192.168.1.99`** in your browser (replace with your router-assigned IP if different).

---

## 🧪 Testing & Usage

1. Power on the ESP32 via USB.  
2. Connect to `ESP32-Setup` Wi-Fi and configure credentials.  
3. Wait for connection confirmation (solid onboard LED).  
4. Press the **button** to start recording.  
   - The RGB LED turns red while recording.  
5. Press again to stop.  
   - The LED returns to green.  
6. Visit **`http://192.168.1.99`** to view the recorded runs.

---

## ⚙️ Requirements

- ESP32-DevKitC-32UE or similar  
- Arduino IDE / PlatformIO  
- Libraries:
  - `WiFi.h`
  - `AsyncTCP.h`
  - `ESPAsyncWebServer.h`
  - `SPIFFS.h`

---

## ⚠️ Notes & Limitations

- **Limited SPIFFS Storage:** Large logs may fill memory quickly; consider SD card integration for long recordings.  
- **Browser Caching:** After updates, clear browser cache or force refresh (`Ctrl + F5`).  
- **Static IP:** The IP `192.168.1.99` assumes static assignment — update if using DHCP.  
- **Power Supply:** USB 5V required for stable operation under Wi-Fi load.

---