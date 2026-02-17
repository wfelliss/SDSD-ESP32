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
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(ONBOARD_LED_PIN, OUTPUT);

    setLedColor(1, 1, 1); // Loading state

    if (!initStorage()) {
        Serial.println("Storage Critical Failure");
        setLedColor(0, 0, 1);
        return;
    }

    // Launch Tasks
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, NULL, 1); // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, NULL, 0); // Core 0

    setLedColor(0, 1, 0); // Ready
}

void loop() {
    // FreeRTOS handles the tasks.
}