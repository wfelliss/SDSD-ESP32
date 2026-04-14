#include "storage_manager.h"
#include "globals.h"
#include <SD.h>

std::vector<SensorLine> sensorBuffer;

bool initStorage() {
    return SD.begin(SD_CS_PIN);
}

static int parseRunNumber(const String& fname) {
    String name = fname.startsWith("/") ? fname.substring(1) : fname;

    if (!name.startsWith("run_") || !name.endsWith(".csv")) return -1;

    int underscore = name.indexOf('_');
    int dot = name.lastIndexOf('.');
    if (underscore < 0 || dot <= underscore) return -1;

    String numStr = name.substring(underscore + 1, dot);
    if (numStr.length() == 0) return -1;

    for (size_t i = 0; i < numStr.length(); ++i) {
        if (!isDigit(numStr.charAt(i))) return -1;
    }

    return numStr.toInt();
}

static int findNextRunNumber() {
    int maxRun = 0;

    File root = SD.open("/");
    if (!root) return 1;

    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            int runNum = parseRunNumber(String(entry.name()));
            if (runNum > maxRun) maxRun = runNum;
        }
        entry.close();
        entry = root.openNextFile();
    }

    root.close();
    return maxRun + 1;
}

static void writeRunHeader(File& file, const int initialAcc[6]) {
    file.println("gyro_x_world_mrads,gyro_y_world_mrads,gyro_z_world_mrads,accel_x_world_mg,accel_y_world_mg,accel_z_world_mg,rear_sus,front_sus");

    for (int i = 0; i < 6; i++) {
        file.print(initialAcc[i]);
        file.print(",");
    }
    file.print(4095 - analogRead(REAR_SUS_PIN));
    file.print(",");
    file.println(analogRead(FRONT_SUS_PIN));
}

void startNewRun(const int initialAcc[6]) {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[ERROR] SD Card mount failed");
        setLedColor(0, 0, 255);
        return;
    }

    int nextRun = findNextRunNumber();
    currentRunFilePath = "/run_" + String(nextRun) + ".csv";

    File file = SD.open(currentRunFilePath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to create run file: " + currentRunFilePath);
        setLedColor(0, 0, 255);
        return;
    }

    writeRunHeader(file, initialAcc);
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