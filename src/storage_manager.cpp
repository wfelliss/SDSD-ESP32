#include "storage_manager.h"
#include "globals.h"
#include <SD.h>
#include <LittleFS.h>

std::vector<SensorLine> sensorBuffer;

bool initStorage() {
    bool lfs = LittleFS.begin(true);
    bool sd = SD.begin(SD_CS_PIN);
    return lfs && sd;
}

void startNewRun(const int initialAcc[6]) {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[ERROR] SD Card mount failed");
        setLedColor(0, 0, 1); // Blue Failed
        return;
    }

    // Determine next run number by scanning SD root for existing run_<n>.csv files
    int maxRun = 0;
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String fname = String(file.name());
                if (fname.startsWith("/")) fname = fname.substring(1);
                if (fname.startsWith("run_") && fname.endsWith(".csv")) {
                    int u = fname.indexOf('_');
                    int d = fname.lastIndexOf('.');
                    if (u >= 0 && d > u) {
                        String numStr = fname.substring(u + 1, d);
                        bool allDigits = true;
                        for (size_t i = 0; i < numStr.length(); ++i) {
                            if (!isDigit(numStr.charAt(i))) { allDigits = false; break; }
                        }
                        if (allDigits) {
                            int val = numStr.toInt();
                            if (val > maxRun) maxRun = val;
                        }
                    }
                }
            }
            file = root.openNextFile();
        }
        root.close();
    }
    int nextRun = maxRun + 1;
    currentRunFilePath = "/run_" + String(nextRun) + ".csv";

    File file = SD.open(currentRunFilePath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to create run file: " + currentRunFilePath);
        setLedColor(0, 0, 1); // Blue Failed
        return;
    }

    file.println("gyro_x_world_mrads,gyro_y_world_mrads,gyro_z_world_mrads,accel_x_world_mg,accel_y_world_mg,accel_z_world_mg,rear_sus,front_sus");
    for (int i = 0; i < 6; i++) {
        file.print(initialAcc[i]);
        file.print(",");
    }
    file.print(4095 - analogRead(REAR_SUS_PIN));
    file.print(",");
    file.println(analogRead(FRONT_SUS_PIN));
    file.close();

    sensorBuffer.clear();
    Serial.println("[INFO] New run started: " + currentRunFilePath);
}

void flushSensorBuffer() {
    if (sensorBuffer.empty() || currentRunFilePath == "") return;

    File file = SD.open(currentRunFilePath.c_str(), FILE_APPEND);
    if (!file) {
        Serial.println("[ERROR] Failed to open run file for writing");
        return;
    }

    for (const auto& line : sensorBuffer) {
        for (int i = 0; i < 6; i++) {
            file.print(line.acc[i]);
            file.print(",");
        }
        file.print(line.rear_sus);
        file.print(",");
        file.println(line.front_sus);
    }

    file.close();
    Serial.printf("[INFO] Flushed %u lines to SD card\n", (unsigned)sensorBuffer.size());
    sensorBuffer.clear();
}