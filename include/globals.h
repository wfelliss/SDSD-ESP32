#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_MAX1704X.h>

extern AsyncWebServer server;
extern volatile int recording;

extern String currentRunFilePath;

extern TaskHandle_t WiFiTask;
extern TaskHandle_t DataTask;

extern int currentOnboardLedMode;
extern bool ledState;
extern unsigned long lastBlinkMillis;
extern unsigned long blinkInterval;

extern Adafruit_NeoPixel neopixel;
extern Adafruit_MAX17048 maxlipo;
extern volatile int batteryPercent;   // 0-100, or -1 if not yet read

// LED Modes
enum LedMode { LED_OFF = 0, LED_BLINK = 1, LED_SOLID = 2 };
void updateOnBoardLed();
void setOnboardLed(int mode, unsigned long interval = 500);
void initLedPwm();
void setLedColor(uint8_t red, uint8_t green, uint8_t blue);
void updateBatteryNeopixel();