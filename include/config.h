#pragma once
#include <Arduino.h>

// --- Pin Definitions ---
#define ONBOARD_LED_PIN 2
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
#define BLUE_LED_PIN 27
#define BUTTON_PIN 14
#define SD_CS_PIN 5
#define FRONT_SUS_PIN 34
#define REAR_SUS_PIN 35

// --- Constants ---
const unsigned long SAMPLE_PERIOD_MS = 10;
const unsigned int SAMPLE_FREQUENCY = 1000 / SAMPLE_PERIOD_MS;
const size_t MAX_BUFFER_SIZE = 512;
// const char* externalServerURL = "http://192.168.1.181:3001/api/s3/newRunFile";
static const char* EXTERNAL_SERVER_URL = "https://backend-production-68e1.up.railway.app/api/s3/newRunFile";