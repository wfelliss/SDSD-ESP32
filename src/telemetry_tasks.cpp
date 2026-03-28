#include "telemetry_tasks.h"
#include "globals.h"
#include "storage_manager.h"
#include "network_manager.h"
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>

#define SUS_NUM_SAMPLES 20
#define SUS_SIGMA_K     2.0f   // reject samples beyond 2σ from the burst mean
static_assert(SUS_NUM_SAMPLES >= 5, "SUS_NUM_SAMPLES must be >= 5 for a meaningful sigma estimate");

// Takes SUS_NUM_SAMPLES rapid ADC reads, computes the burst mean and standard
// deviation, then averages only the samples within SUS_SIGMA_K * sigma of the
// mean. Outlier spikes separated from the cluster by >2σ are discarded.
// At N=20, sigma is stable enough that a single spike cannot inflate it enough
// to hide itself. Total time ~2 ms for both pins, well within the 10 ms window.
static int stddevFilteredADC(uint8_t pin) {
    int samples[SUS_NUM_SAMPLES];
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) {
        samples[i] = analogRead(pin);
    }

    // First pass: mean
    float sum = 0;
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) sum += samples[i];
    float mean = sum / SUS_NUM_SAMPLES;

    // Second pass: standard deviation
    float variance = 0;
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) {
        float diff = samples[i] - mean;
        variance += diff * diff;
    }
    float sigma = sqrtf(variance / SUS_NUM_SAMPLES);

    // Third pass: average inliers only
    float lo = mean - SUS_SIGMA_K * sigma;
    float hi = mean + SUS_SIGMA_K * sigma;
    float filteredSum = 0;
    int count = 0;
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) {
        if (samples[i] >= lo && samples[i] <= hi) {
            filteredSum += samples[i];
            count++;
        }
    }

    // Fallback: if every sample was rejected (degenerate — shouldn't happen),
    // return the unfiltered mean rather than divide-by-zero.
    if (count == 0) return (int)mean;
    return (int)(filteredSum / count);
}

void DataTaskcode(void * pvParameter) {
    // Setup one-time improvements
    sensorBuffer.reserve(MAX_BUFFER_SIZE);

    int lastButtonState = HIGH;
    int buttonState = HIGH;
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    // timing instrumentation
    const int REPORT_PERIOD = 100; // report every N samples
    uint32_t sampleCount = 0;
    uint64_t accumLoopUs = 0;
    uint64_t accumImuUs = 0;

    // --- IMU Initialization ---
    static Adafruit_LSM6DS3TRC imu;
    Wire.begin();
    Wire.setClock(400000);  // 400 kHz fast mode — must be after Wire.begin()
    bool imuOk = imu.begin_I2C();
    if (imuOk) {
        imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
        imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
        imu.setGyroDataRate(LSM6DS_RATE_104_HZ);
        imu.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
    }

    float gyroBiasX = 0.0f, gyroBiasY = 0.0f, gyroBiasZ = 0.0f;
    float accelBiasX = 0.0f, accelBiasY = 0.0f, accelBiasZ = 0.0f;

    // Rotation matrix: sensor frame → world frame (Z = down = gravity direction).
    // Identity until calibrated — safe fallback if IMU absent.
    float R[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    float gravMag = 9.806f; // updated at calibration

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        int reading = digitalRead(BUTTON_PIN);

        // Debounce (unchanged)
        if (reading != lastButtonState) lastDebounceTime = millis();
        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != buttonState) {
                buttonState = reading;
                if (buttonState == LOW) {
                    recording = (recording + 1) % 3; // 0, 1, 2 cycle
                    if (recording == 1) { // setup
                        setLedColor(0, 0, 0); // off — collecting calibration data

                        int initialAcc[6] = {};

                        if (imuOk) {
                            // Gyro + accel bias calibration: 50 samples × 5 ms = 250 ms
                            // Bike must be stationary — dynamic acceleration contaminates the bias
                            const int CAL_SAMPLES = 50;
                            double sumX = 0, sumY = 0, sumZ = 0;
                            double sumAX = 0, sumAY = 0, sumAZ = 0;
                            sensors_event_t calAccel, calGyro, calTemp;
                            for (int i = 0; i < CAL_SAMPLES; i++) {
                                imu.getEvent(&calAccel, &calGyro, &calTemp);
                                sumX  += calGyro.gyro.x;
                                sumY  += calGyro.gyro.y;
                                sumZ  += calGyro.gyro.z;
                                sumAX += calAccel.acceleration.x;
                                sumAY += calAccel.acceleration.y;
                                sumAZ += calAccel.acceleration.z;
                                vTaskDelay(pdMS_TO_TICKS(10));
                            }
                            gyroBiasX  = (float)(sumX  / CAL_SAMPLES);
                            gyroBiasY  = (float)(sumY  / CAL_SAMPLES);
                            gyroBiasZ  = (float)(sumZ  / CAL_SAMPLES);
                            accelBiasX = (float)(sumAX / CAL_SAMPLES);
                            accelBiasY = (float)(sumAY / CAL_SAMPLES);
                            accelBiasZ = (float)(sumAZ / CAL_SAMPLES);
                            Serial.printf("[CAL] gyro  bias x=%.4f y=%.4f z=%.4f rad/s\n",
                                          gyroBiasX, gyroBiasY, gyroBiasZ);
                            Serial.printf("[CAL] accel bias x=%.4f y=%.4f z=%.4f m/s2\n",
                                          accelBiasX, accelBiasY, accelBiasZ);

                            // Build rotation matrix: sensor frame → world frame
                            // World Z = down (gravity direction); X/Y are arbitrary horizontal axes.
                            gravMag = sqrtf(accelBiasX*accelBiasX + accelBiasY*accelBiasY + accelBiasZ*accelBiasZ);
                            if (gravMag > 0.5f) {
                                float wz[3] = { accelBiasX/gravMag, accelBiasY/gravMag, accelBiasZ/gravMag };

                                // Pick an arbitrary vector not parallel to wz for Gram-Schmidt
                                float tx = 1.0f, ty = 0.0f;
                                float d = tx*wz[0] + ty*wz[1]; // dot with wz (tz=0)
                                if (fabsf(d) > 0.9f) { tx = 0.0f; ty = 1.0f; d = wz[1]; }

                                // wx = normalise(tmp - (tmp·wz)*wz)
                                float wx[3] = { tx - d*wz[0], ty - d*wz[1], -d*wz[2] };
                                float s = sqrtf(wx[0]*wx[0] + wx[1]*wx[1] + wx[2]*wx[2]);
                                wx[0]/=s; wx[1]/=s; wx[2]/=s;

                                // wy = wz × wx  (right-handed, already unit length)
                                float wy[3] = {
                                    wz[1]*wx[2] - wz[2]*wx[1],
                                    wz[2]*wx[0] - wz[0]*wx[2],
                                    wz[0]*wx[1] - wz[1]*wx[0]
                                };

                                R[0][0]=wx[0]; R[0][1]=wx[1]; R[0][2]=wx[2];
                                R[1][0]=wy[0]; R[1][1]=wy[1]; R[1][2]=wy[2];
                                R[2][0]=wz[0]; R[2][1]=wz[1]; R[2][2]=wz[2];
                            } else {
                                // Sensor fault: reset to safe defaults and warn
                                gravMag = 9.806f;
                                R[0][0]=1; R[0][1]=0; R[0][2]=0;
                                R[1][0]=0; R[1][1]=1; R[1][2]=0;
                                R[2][0]=0; R[2][1]=0; R[2][2]=1;
                                Serial.printf("[CAL] ERROR gravMag=%.3f — sensor fault? R reset to identity\n", gravMag);
                            }
                            Serial.printf("[CAL] gravMag=%.3f R=[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]]\n",
                                          gravMag,
                                          R[0][0],R[0][1],R[0][2],
                                          R[1][0],R[1][1],R[1][2],
                                          R[2][0],R[2][1],R[2][2]);

                            // Take one world-frame reading for the initial CSV row
                            sensors_event_t a, g, t;
                            imu.getEvent(&a, &g, &t);
                            float igx = g.gyro.x - gyroBiasX, igy = g.gyro.y - gyroBiasY, igz = g.gyro.z - gyroBiasZ;
                            initialAcc[0] = (int)((R[0][0]*igx + R[0][1]*igy + R[0][2]*igz) * 1000.0f);
                            initialAcc[1] = (int)((R[1][0]*igx + R[1][1]*igy + R[1][2]*igz) * 1000.0f);
                            initialAcc[2] = (int)((R[2][0]*igx + R[2][1]*igy + R[2][2]*igz) * 1000.0f);
                            float iax = a.acceleration.x, iay = a.acceleration.y, iaz = a.acceleration.z;
                            initialAcc[3] = (int)((R[0][0]*iax + R[0][1]*iay + R[0][2]*iaz) / gravMag * 1000.0f);
                            initialAcc[4] = (int)((R[1][0]*iax + R[1][1]*iay + R[1][2]*iaz) / gravMag * 1000.0f);
                            initialAcc[5] = (int)((R[2][0]*iax + R[2][1]*iay + R[2][2]*iaz - gravMag) / gravMag * 1000.0f);
                        } else {
                            Serial.println("[CAL] IMU not found — recording with zero gyro bias");
                        }

                        startNewRun(initialAcc);
                        setLedColor(1, 1, 0); // yellow — calibration done, ready to record
                        xLastWakeTime = xTaskGetTickCount(); // re-anchor after 250 ms delay
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
            uint32_t t0 = micros();
            setLedColor(1, 0, 0); // red
            SensorLine line = {};

            uint32_t tImuStart = micros();
            sensors_event_t accel, gyro, temp;
            bool readOk = imuOk && imu.getEvent(&accel, &gyro, &temp);
            uint32_t tImuEnd = micros();
            accumImuUs += (tImuEnd - tImuStart);

            if (readOk) {
                // Rotate gyro (bias removed) and accel into world frame via R.
                // World Z = down (gravity direction); accel Z has gravity subtracted.
                float gx = gyro.gyro.x - gyroBiasX;
                float gy = gyro.gyro.y - gyroBiasY;
                float gz = gyro.gyro.z - gyroBiasZ;
                float ax = accel.acceleration.x;
                float ay = accel.acceleration.y;
                float az = accel.acceleration.z;

                // gyro world frame → milli-rad/s
                line.acc[0] = (int)((R[0][0]*gx + R[0][1]*gy + R[0][2]*gz) * 1000.0f);
                line.acc[1] = (int)((R[1][0]*gx + R[1][1]*gy + R[1][2]*gz) * 1000.0f);
                line.acc[2] = (int)((R[2][0]*gx + R[2][1]*gy + R[2][2]*gz) * 1000.0f);
                // accel world frame, gravity removed from Z → milli-g
                line.acc[3] = (int)((R[0][0]*ax + R[0][1]*ay + R[0][2]*az) / gravMag * 1000.0f);
                line.acc[4] = (int)((R[1][0]*ax + R[1][1]*ay + R[1][2]*az) / gravMag * 1000.0f);
                line.acc[5] = (int)((R[2][0]*ax + R[2][1]*ay + R[2][2]*az - gravMag) / gravMag * 1000.0f);
            } else {
                // IMU absent or read failed — zero-fill silently
                memset(line.acc, 0, sizeof(line.acc));
            }

            line.rear_sus = 4095 - stddevFilteredADC(REAR_SUS_PIN);
            line.front_sus = stddevFilteredADC(FRONT_SUS_PIN);

            sensorBuffer.push_back(line);

            if (sensorBuffer.size() >= MAX_BUFFER_SIZE) {
                flushSensorBuffer(); // okay — infrequent
            }

            sampleCount++;

            uint32_t t1 = micros();
            accumLoopUs += (t1 - t0);

            if ((sampleCount % REPORT_PERIOD) == 0) {
                float avgLoopMs = (accumLoopUs / (float)REPORT_PERIOD) / 1000.0f;
                float avgImuMs = (accumImuUs / (float)REPORT_PERIOD) / 1000.0f;
                Serial.printf("[DIAG] samples=%u avg_loop=%.3f ms avg_imu=%.3f ms buffer=%u\n",
                              sampleCount, avgLoopMs, avgImuMs, (unsigned)sensorBuffer.size());
                accumLoopUs = 0;
                accumImuUs = 0;
            }
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

    unsigned long lastBatteryReadMs = millis() - BATTERY_READ_INTERVAL_MS; // trigger immediate first read

    while (true) {
        if (startWiFiConnect) {
            startWiFiConnect = false;
            connectToWiFi();
        }
        updateOnBoardLed();

        unsigned long now = millis();
        if (now - lastBatteryReadMs >= BATTERY_READ_INTERVAL_MS) {
            lastBatteryReadMs = now;
            float pct = maxlipo.cellPercent();
            float voltage = maxlipo.cellVoltage();
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 100.0f) pct = 100.0f;
            batteryPercent = (int)pct;
            updateBatteryNeopixel();
            Serial.printf("[BATT] voltage=%.3fV percent=%d%%\n", voltage, batteryPercent);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}