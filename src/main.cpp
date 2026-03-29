#include "config.h"
#include "globals.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "telemetry_tasks.h"

void setup() {
    Serial.begin(115200);

    unsigned long start = millis();
    while (!Serial && (millis() - start < 4000)) {
        delay(10);
    }

    WiFi.setTxPower(WIFI_POWER_8_5dBm); // Set WiFi transmit power to 8.5 dBm

    // Hardware Setup
    initLedPwm();
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(ONBOARD_LED_PIN, OUTPUT);

    // Enable NeoPixel power and initialise
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);
    neopixel.begin();
    neopixel.setBrightness(NEOPIXEL_BRIGHTNESS);
    updateBatteryNeopixel(); // show default green on boot

    // Initialise MAX17048 fuel gauge (I2C, address 0x36)
    if (!maxlipo.begin()) {
        Serial.println("[BATT] MAX17048 not found — battery % unavailable");
    } else {
        Serial.println("[BATT] MAX17048 found");
    }

    setLedColor(255, 255, 255); // Loading state — white

    if (!initStorage()) {
        Serial.println("Storage Critical Failure");
        setLedColor(0, 0, 255); // blue — critical failure
        return;
    }

    // Launch Tasks
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, NULL, 1); // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, NULL, 0); // Core 0

    setLedColor(0, 255, 0); // green — ready
}

void loop() {
    // FreeRTOS handles the tasks.
}