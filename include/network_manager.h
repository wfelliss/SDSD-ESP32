#pragma once
#include <Arduino.h>
#include <WiFi.h>

typedef arduino_event_id_t WiFiEvent_t;

struct UploadParams {
    char *runName;
    char *track;
    char *comments;
    int frontStroke;
    int rearStroke;
};

void startAP();
void connectToWiFi();
void setupWebRoutes();
void uploadRunTask(void *pvParameter); 
void WiFiEvent(WiFiEvent_t event);