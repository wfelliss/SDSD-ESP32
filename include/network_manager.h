#pragma once
#include <Arduino.h>

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