#pragma once
#include <Arduino.h>

struct UploadParams {
    char *runName;
    char *track;
    char *comments;
};

void startAP();
void connectToWiFi();
void setupWebRoutes();
void uploadRunTask(void *pvParameter); 