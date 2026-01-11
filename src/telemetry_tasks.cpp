#include "telemetry_tasks.h"
#include "globals.h"
#include "storage_manager.h"
#include "network_manager.h"

void DataTaskcode(void * pvParameter) {
    // Setup one-time improvements
    sensorBuffer.reserve(MAX_BUFFER_SIZE);
    const float ACC_SCALE = 1.0f / 16384.0f;

    int lastButtonState = HIGH;
    int buttonState = HIGH;
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    // timing instrumentation
    const int REPORT_PERIOD = 100; // report every N samples
    uint32_t sampleCount = 0;
    uint64_t accumLoopUs = 0;
    uint64_t accumImuUs = 0;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        uint32_t t0 = micros();

        int reading = digitalRead(BUTTON_PIN);

        // Debounce (unchanged)
        if (reading != lastButtonState) lastDebounceTime = millis();
        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != buttonState) {
                buttonState = reading;
                if (buttonState == LOW) {
                    recording = (recording + 1) % 3; // 0, 1, 2 cycle
                    if (recording == 1) { // setup
                        // set led to yellow
                        setLedColor(1, 1, 0);
                        startNewRun();
                    } else if (recording == 0) { // stopped recording
                        setLedColor(0, 1, 0);
                        if (!sensorBuffer.empty()) flushSensorBuffer();
                    }
                }
            }
        }
        lastButtonState = reading;

        // --- Capture data ---
        if (recording == 2) { // actively recording
            setLedColor(1, 0, 0); // red
            SensorLine line;

            uint32_t tImuStart = micros();
            // IMUData imuData = getAccelGyro();
            uint32_t tImuEnd = micros();
            accumImuUs += (tImuEnd - tImuStart);

            if (true) {
                // gyro (raw)
                for (int i = 0; i < 3; i++) {
                    line.acc[i] = 0; // imuData.values[i];
                }
                // accel -> scaled to integer-ish (if you really want floats, change type)
                for (int i = 3; i < 6; i++) {
                    // multiply instead of divide
                    float a = 0; // imuData.values[i] * ACC_SCALE;
                    line.acc[i] = (int)(a * 1000.0f); // optional: quantize to milli-g to keep int
                }
            } else {
                // avoid frequent prints — only log every REPORT_PERIOD
                if ((sampleCount % REPORT_PERIOD) == 0) {
                    Serial.println("Failed to read IMU data");
                }
            }

            line.rear_sus = 4095 - analogRead(REAR_SUS_PIN);
            line.front_sus = analogRead(FRONT_SUS_PIN);

            sensorBuffer.push_back(line);

            if (sensorBuffer.size() >= MAX_BUFFER_SIZE) {
                flushSensorBuffer(); // okay — infrequent
            }

            sampleCount++;
        }

        uint32_t t1 = micros();
        uint32_t loopUs = t1 - t0;
        accumLoopUs += loopUs;

        if ((sampleCount > 0) && ((sampleCount % REPORT_PERIOD) == 0)) {
            float avgLoopMs = (accumLoopUs / (float)REPORT_PERIOD) / 1000.0f;
            float avgImuMs = (accumImuUs / (float)REPORT_PERIOD) / 1000.0f;
            Serial.printf("[DIAG] samples=%u avg_loop=%.3f ms avg_imu=%.3f ms buffer=%u\n",
                          sampleCount, avgLoopMs, avgImuMs, (unsigned)sensorBuffer.size());
            accumLoopUs = 0;
            accumImuUs = 0;
        }

        // use vTaskDelayUntil for a stable period defined by SAMPLE_PERIOD_MS
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}


void WiFiTaskcode(void * pvParameter) {
    startAP();
    setupWebRoutes();
    server.begin();
    WiFi.onEvent(WiFiEvent);
    while (true) {
        if (startWiFiConnect) {
            startWiFiConnect = false;
            connectToWiFi();
        }
        updateOnBoardLed();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}