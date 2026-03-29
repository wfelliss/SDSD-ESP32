#pragma once
#include <Adafruit_LSM6DS3TRC.h>
#include "storage_manager.h"

// Holds all IMU hardware state and calibration results.
// Identity rotation matrix and standard gravity are safe defaults before calibration.
struct ImuState {
    ImuState() = default;
    ImuState(const ImuState&) = delete;
    ImuState& operator=(const ImuState&) = delete;

    Adafruit_LSM6DS3TRC device;
    bool ok          = false;
    float gyroBias[3]  = {};
    float accelBias[3] = {};
    float R[3][3]    = {{1,0,0},{0,1,0},{0,0,1}};
    float gravMag    = 9.806f;
};

void initImu(ImuState& imu);
void calibrateImu(ImuState& imu);
void populateImuReadingIntoLine(ImuState& imu, SensorLine& line);
