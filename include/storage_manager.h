#pragma once
#include <vector>
#include "config.h"

struct SensorLine {
    int acc[6];
    int rear_sus;
    int front_sus;
};

extern std::vector<SensorLine> sensorBuffer;

bool initStorage();
void startNewRun();
void flushSensorBuffer();