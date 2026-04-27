#include "telemetry_tasks.h"
#include "imu_handler.h"
#include "globals.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "suspension_cal.h"

// ─── Suspension ADC ───────────────────────────────────────────────────────────

#define SUS_NUM_SAMPLES 20
#define SUS_SIGMA_K     2.0f
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

    float sum = 0;
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) sum += samples[i];
    float mean = sum / SUS_NUM_SAMPLES;

    float variance = 0;
    for (int i = 0; i < SUS_NUM_SAMPLES; i++) {
        float diff = samples[i] - mean;
        variance += diff * diff;
    }
    float sigma = sqrtf(variance / SUS_NUM_SAMPLES);

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

// ─── Diagnostics ─────────────────────────────────────────────────────────────

static constexpr int REPORT_PERIOD = 100;

struct DiagState {
    uint32_t sampleCount  = 0;
    uint64_t accumLoopUs  = 0;
    uint64_t accumImuUs   = 0;
};

static void logDiagnostics(DiagState& diag) {
    float avgLoopMs = (diag.accumLoopUs / (float)REPORT_PERIOD) / 1000.0f;
    float avgImuMs  = (diag.accumImuUs  / (float)REPORT_PERIOD) / 1000.0f;
    Serial.printf("[DIAG] samples=%u avg_loop=%.3f ms avg_imu=%.3f ms buffer=%u\n",
                  diag.sampleCount, avgLoopMs, avgImuMs, (unsigned)sensorBuffer.size());
    diag.accumLoopUs = 0;
    diag.accumImuUs  = 0;
}


// ─── Button debounce ──────────────────────────────────────────────────────────

static constexpr unsigned long DEBOUNCE_DELAY_MS = 50;

struct ButtonState {
    int lastReading      = HIGH;
    int confirmedState   = HIGH;
    unsigned long lastDebounceTime = 0;
};

// Returns true once per physical press (LOW transition after debounce).
static bool checkForButtonPress(ButtonState& btn) {
    int reading = digitalRead(BUTTON_PIN);

    if (reading != btn.lastReading) {
        btn.lastDebounceTime = millis();
    }
    btn.lastReading = reading;

    if ((millis() - btn.lastDebounceTime) <= DEBOUNCE_DELAY_MS) return false;
    if (reading == btn.confirmedState) return false;

    btn.confirmedState = reading;
    return (reading == LOW);
}

// ─── Recording state machine ──────────────────────────────────────────────────

static void startRecordingSetup(ImuState& imu, TickType_t& xLastWakeTime) {
    setLedColor(0, 0, 0);  // off — collecting calibration data

    calibrateImu(imu);

    SensorLine initialLine = {};
    populateImuReadingIntoLine(imu, initialLine);
    startNewRun(initialLine.acc);

    setLedColor(255, 155, 0);  // yellow — calibration done, ready to record
    xLastWakeTime = xTaskGetTickCount();  // re-anchor after calibration delay
}

static void stopRecording() {
    setLedColor(0, 255, 0); // green — stopped, idle
    if (!sensorBuffer.empty()) flushSensorBuffer();
}

static void handleButtonPress(ImuState& imu, TickType_t& xLastWakeTime) {
    recording = (recording + 1) % 3;

    if (recording == 1) startRecordingSetup(imu, xLastWakeTime);
    else if (recording == 0) stopRecording();
}

// ─── Sample capture ───────────────────────────────────────────────────────────

static SensorLine captureSensorLine(ImuState& imu, uint64_t& accumImuUs) {
    SensorLine line = {};

    uint32_t tImuStart = micros();
    populateImuReadingIntoLine(imu, line);
    accumImuUs += micros() - tImuStart;

    int rawRear  = 4095 - stddevFilteredADC(REAR_SUS_PIN);
    int rawFront = stddevFilteredADC(FRONT_SUS_PIN);

    line.rear_sus  = correctSuspension(rawRear,  REAR_SUS_CAL,  REAR_SUS_CAL_SIZE);
    line.front_sus = correctSuspension(rawFront, FRONT_SUS_CAL, FRONT_SUS_CAL_SIZE);

    return line;
}

static void bufferSample(const SensorLine& line) {
    sensorBuffer.push_back(line);

    if (sensorBuffer.size() >= MAX_BUFFER_SIZE) {
        flushSensorBuffer();
    }
}

static void recordSample(ImuState& imu, DiagState& diag) {
    setLedColor(255, 0, 0); // red — recording

    uint32_t t0 = micros();

    SensorLine line = captureSensorLine(imu, diag.accumImuUs);
    bufferSample(line);

    diag.sampleCount++;
    diag.accumLoopUs += micros() - t0;

    if ((diag.sampleCount % REPORT_PERIOD) == 0) {
        logDiagnostics(diag);
    }
}

// ─── Task entry points ────────────────────────────────────────────────────────

void DataTaskcode(void* pvParameter) {
    sensorBuffer.reserve(MAX_BUFFER_SIZE);

    static ImuState imu;
    initImu(imu);

    ButtonState button;
    DiagState   diag;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        if (checkForButtonPress(button)) {
            handleButtonPress(imu, xLastWakeTime);
        }

        if (recording == 2) {
            recordSample(imu, diag);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

void WiFiTaskcode(void* pvParameter) {
    WiFi.softAP("SDSD", "SDSD1234");
    Serial.println("[WIFI] AP started — SSID: SDSD, IP: 192.168.4.1");
    setupWebRoutes();
    server.begin();

    unsigned long lastBatteryReadMs = millis() - BATTERY_READ_INTERVAL_MS;  // trigger immediate first read

    while (true) {
        updateOnBoardLed();

        unsigned long now = millis();
        if (now - lastBatteryReadMs >= BATTERY_READ_INTERVAL_MS) {
            lastBatteryReadMs = now;

            float pct     = maxlipo.cellPercent();
            float voltage = maxlipo.cellVoltage();

            pct = constrain(pct, 0.0f, 100.0f);
            batteryPercent = (int)pct;

            updateBatteryNeopixel();
            Serial.printf("[BATT] voltage=%.3fV percent=%d%%\n", voltage, batteryPercent);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
