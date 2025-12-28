#include "globals.h"
#include "config.h"

AsyncWebServer server(80);
volatile bool wifiConnected = false;
volatile bool startWiFiConnect = false;
volatile bool recording = false;

String ssidInput = "";
String passwordInput = "";
String currentRunFilePath = "";

int currentLedMode = LED_OFF;
bool ledState = false;
unsigned long lastBlinkMillis = 0;
unsigned long blinkInterval = 500;

TaskHandle_t WiFiTask = NULL;
TaskHandle_t DataTask = NULL;
TaskHandle_t UploadTask = NULL;

void updateOnBoardLed() {
    if (currentLedMode == LED_BLINK) {
        unsigned long now = millis();
        if (now - lastBlinkMillis >= blinkInterval) {
            lastBlinkMillis = now;
            ledState = !ledState;
            digitalWrite(ONBOARD_LED_PIN, ledState);
        }
    }
}

void setOnboardLed(int mode, unsigned long interval) {
    currentLedMode = mode;
    blinkInterval = interval;

    switch (mode) {
        case LED_OFF:
            ledState = false;
            digitalWrite(ONBOARD_LED_PIN, LOW);
            break;
        case LED_SOLID:
            ledState = true;
            digitalWrite(ONBOARD_LED_PIN, HIGH);
            break;
        case LED_BLINK:
            lastBlinkMillis = millis();
            break;
    }
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
    digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
    digitalWrite(BLUE_LED_PIN, blue ? HIGH : LOW);
}
