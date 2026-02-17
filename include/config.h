#pragma once
#include <Arduino.h>

// --- Pin Definitions ---

// Onboard Red LED (Fixed at Pin 13)
#define ONBOARD_LED_PIN 13 

// --- External LEDs (D Pins) ---
// We use D12, D11, and D10 for the LEDs.
#define RED_LED_PIN 12      // Digital Pin 12
#define GREEN_LED_PIN 11    // Digital Pin 11
#define BLUE_LED_PIN 10     // Digital Pin 10

// --- Button (D Pin) ---
// We use D9 (GPIO 9) for the button.
#define BUTTON_PIN 9        // Digital Pin 9

// --- SD Card (D Pin) ---
// Pin 5 is standard for SD Chip Select on Feathers.
#define SD_CS_PIN 5         // Digital Pin 5

// --- Analog Inputs (Keep on Analog Side) ---
// We keep these on the Analog header (A3/A4) because D5-D13 are digital.
// Note: You can technically use D-pins for analog on S3, but keeping them separate is cleaner.
#define FRONT_SUS_PIN 15    // A3
#define REAR_SUS_PIN 14     // A4

// --- Constants ---
const unsigned long SAMPLE_PERIOD_MS = 10;
const unsigned int SAMPLE_FREQUENCY = 1000 / SAMPLE_PERIOD_MS;
const size_t MAX_BUFFER_SIZE = 512;
static const char* LOCAL_SERVER_URL = "http://192.168.1.181:3001/api/s3/newRunFile";
static const char* EXTERNAL_SERVER_URL = "https://backend-production-68e1.up.railway.app/api/s3/newRunFile";