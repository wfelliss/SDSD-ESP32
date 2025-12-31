#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

extern AsyncWebServer server;
extern volatile bool wifiConnected;
extern volatile bool startWiFiConnect;
extern volatile int recording;

extern String ssidInput;
extern String passwordInput;
extern String currentRunFilePath;

extern TaskHandle_t WiFiTask;
extern TaskHandle_t DataTask;
extern TaskHandle_t UploadTask;

extern int currentOnboardLedMode;
extern bool ledState;
extern unsigned long lastBlinkMillis;
extern unsigned long blinkInterval;

// LED Modes
enum LedMode { LED_OFF = 0, LED_BLINK = 1, LED_SOLID = 2 };
void updateOnBoardLed();
void setOnboardLed(int mode, unsigned long interval = 500);
void setLedColor(uint8_t red, uint8_t green, uint8_t blue);