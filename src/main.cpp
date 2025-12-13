#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <vector>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <DFRobot_BMI160.h>
#include "Wire.h"


// --- Pin definitions ---
#define ONBOARD_LED_PIN 2
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
#define BLUE_LED_PIN 27
#define BUTTON_PIN 14
#define SD_CS_PIN 5
#define FRONT_SUS_PIN 34  // Analog pin for front suspension sensor
#define REAR_SUS_PIN 35   // Analog pin for rear suspension sensor

AsyncWebServer server(80);

// --- Wifi variables ---
volatile bool wifiConnected = false;
volatile bool startWiFiConnect = false;
String ssidInput = "";
String passwordInput = "";
const char* externalServerURL = "http://192.168.1.181:3001/api/s3/newRunFile";

// --- Recording variables ---
bool recording = false;
std::vector<std::vector<int>> allRecordings;
std::vector<int> currentRecording;

// --- SD Card Variables ---
struct SensorLine {
    int acc[6];      // 6 accelerometer readings
    int rear_sus;    // rear suspension
    int front_sus;   // front suspension
};

String currentRunFilePath = "";
std::vector<SensorLine> sensorBuffer;
const size_t MAX_BUFFER_SIZE = 512;  // Adjust as needed
unsigned long runStartTime = 0;

// --- BMI160 Sensor ---
// DFRobot_BMI160 bmi160;
// const int8_t ic2_add = 0x68;
// struct IMUData {
//     int16_t values[6];   // 0-2 gyro, 3-5 accel
//     bool valid;          // true if sensor read OK
// };

// --- Global Variables ---
enum LedMode {
    LED_OFF = 0,
    LED_BLINK = 1,
    LED_SOLID = 2
};
int currentLedMode = LED_OFF;
bool ledState = false;
unsigned long lastBlinkMillis = 0;
unsigned long blinkInterval = 500;

// Task handles
TaskHandle_t WiFiTask;
TaskHandle_t DataTask;
TaskHandle_t UploadTask;

void uploadRunTask(void *pvParameter) {
    Serial.println("[UPLOAD] Task started");

    char *runNameC = (char *)pvParameter;
    String csvPath = "/run.csv"; 
    if (runNameC != NULL) {
        csvPath = String(runNameC);
        free(runNameC);
    }

    if (!SPIFFS.begin(true) || !SD.begin(SD_CS_PIN)) {
        Serial.println("[UPLOAD] Storage mount failed");
        vTaskDelete(NULL);
        return;
    }

    File jsonFile = SPIFFS.open("/metadata.json", "r");
    File csvFile = SD.open(("/" + csvPath).c_str(), "r");

    if (!jsonFile || !csvFile) {
        Serial.println("[UPLOAD] File not found");
        if(jsonFile) jsonFile.close();
        if(csvFile) csvFile.close();
        vTaskDelete(NULL);
        return;
    }

    WiFiClient client;
    
    // --- 1. URL Parsing ---
    String url = externalServerURL;
    url.replace("http://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    String path = url.substring(slashIndex);
    int port = 80;
    int colonIndex = host.indexOf(':');
    if (colonIndex >= 0) {
        port = host.substring(colonIndex + 1).toInt();
        host = host.substring(0, colonIndex);
    }

    Serial.println("[UPLOAD] Connecting to " + host + ":" + String(port));

    if (!client.connect(host.c_str(), port)) {
        Serial.println("[UPLOAD] Connection failed");
        jsonFile.close();
        csvFile.close();
        vTaskDelete(NULL);
        return;
    }
    
    // Increased timeout prevents read failures on large files
    client.setTimeout(10); 

    String boundary = "----ESP32Boundary" + String(millis());

    // --- 2. PREPARE METADATA (FIXED) ---
    // We read the file into a String so we can send it as a FORM FIELD, not a file.
    // This satisfies @Body('metadata') on the server.
    String jsonString = "";
    while (jsonFile.available()) {
        jsonString += (char)jsonFile.read();
    }
    jsonFile.close(); // Done with this file

    // Header for the Metadata Field
    // Notice: NO 'filename=' and NO 'Content-Type'. This makes it a text field.
    String jsonPart = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"metadata\"\r\n\r\n" + 
        jsonString + "\r\n";

    // --- 3. PREPARE CSV HEADER (FIXED) ---
    String csvFileName = csvPath;
    int lastSlash = csvFileName.lastIndexOf('/');
    if (lastSlash >= 0) csvFileName = csvFileName.substring(lastSlash + 1);

    // Header for the CSV File
    // Notice: name="file" matches @UseInterceptors(FileInterceptor('file'))
    String csvHead = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + csvFileName + "\"\r\n" +
        "Content-Type: text/csv\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    // Calculate exact length
    size_t totalLen = jsonPart.length() + csvHead.length() + csvFile.size() + tail.length();

    Serial.printf("[UPLOAD] Sending %u bytes to %s\n", totalLen, path.c_str());

    // --- 4. SEND HTTP REQUEST ---
    client.print("POST " + path + " HTTP/1.1\r\n");
    client.print("Host: " + host + ":" + String(port) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(totalLen) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    // Send Metadata (Field)
    client.print(jsonPart);
    
    // Send CSV (File)
    client.print(csvHead);

    // Stream CSV Data from SD Card
    uint8_t buf[1024];
    size_t totalCsvSent = 0;
    while (csvFile.available()) {
        int r = csvFile.read(buf, sizeof(buf));
        if (r <= 0) break;
        
        size_t written = client.write(buf, r);
        if (written != r) {
            Serial.println("[UPLOAD] Network write failed!");
            break;
        }
        totalCsvSent += written;
        
        // Blink LED or print minimal progress
        if (totalCsvSent % 8192 == 0) Serial.print("."); 
    }
    
    client.print(tail);
    csvFile.close();

    Serial.println("\n[UPLOAD] Upload finished. Waiting for server...");

    // --- 5. READ RESPONSE ---
    String response = "";
    unsigned long timeout = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            response += (char)client.read();
            timeout = millis();
        }
        // 5 second read timeout
        if (millis() - timeout > 5000) break; 
    }

    Serial.println("[UPLOAD] Server Response:");
    Serial.println(response);

    client.stop();
    vTaskDelete(NULL);
}

void setOnboardLed(int mode, unsigned long interval = 500) {
    currentLedMode = mode;
    blinkInterval = interval;

    switch (mode) {
        case LED_OFF:
            ledState = false;
            digitalWrite(ONBOARD_LED_PIN, LOW);
            break;
        case LED_SOLID:
            ledState = true;
            digitalWrite(ONBOARD_LED_PIN, HIGH);
            break;
        case LED_BLINK:
            lastBlinkMillis = millis();
            break;
    }
}
void updateOnboardLed() {
    if (currentLedMode == LED_BLINK) {
        unsigned long now = millis();
        if (now - lastBlinkMillis >= blinkInterval) {
            lastBlinkMillis = now;
            ledState = !ledState;
            digitalWrite(ONBOARD_LED_PIN, ledState);
        }
    }
}
// WiFi connection function (blocking, but called in task loop)
void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidInput.c_str(), passwordInput.c_str());

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected!");
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        WiFi.softAPdisconnect(true);
        setOnboardLed(LED_SOLID);
    } else {
        Serial.println("\nFailed to connect.");
        wifiConnected = false;
        setOnboardLed(LED_OFF);
    }
}

void setupPins() {
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    setOnboardLed(LED_OFF);
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(BLUE_LED_PIN, HIGH);
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
    digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
    digitalWrite(BLUE_LED_PIN, blue ? HIGH : LOW);
}

void startNewRun() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("[ERROR] SD Card mount failed");
        setLedColor(0, 0, 1); // Blue Failed
        return;
    }

    unsigned long timestamp = millis();
    currentRunFilePath = "/run_" + String(timestamp) + ".csv";

    File file = SD.open(currentRunFilePath.c_str(), FILE_WRITE);
    if (!file) {
        Serial.println("[ERROR] Failed to create run file: " + currentRunFilePath);
        setLedColor(0, 0, 1); // Blue Failed
        return;
    }

    file.println("acc1,acc2,acc3,acc4,acc5,acc6,rear_sus,front_sus");
    file.close();

    sensorBuffer.clear();
    runStartTime = millis();
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

// IMUData getAccelGyro() {
//     IMUData data;
//     data.valid = false;

//     int16_t raw[6] = {0};

//     int rslt = bmi160.getAccelGyroData(raw);

//     if (rslt == 0) {
//         for (int i = 0; i < 6; i++) {
//             data.values[i] = raw[i];
//         }
//         data.valid = true;
//     }

//     return data;
// }
// WiFi Task - Core 1
void WiFiTaskcode(void * pvParameter) {
    // Start AP
    WiFi.softAP("ESP32-Setup", "12345678");
    Serial.println("AP Started. Connect to WiFi 'ESP32-Setup' to configure");
    setOnboardLed(LED_BLINK, 1000);  // Start blinking once

    // Setup async server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->redirect("/connected");
        else request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            ssidInput = request->getParam("ssid", true)->value();
            passwordInput = request->getParam("password", true)->value();
            startWiFiConnect = true;
            setOnboardLed(LED_BLINK, 200);
            request->send(200, "text/html", "<h1>Connecting...</h1>");
        } else {
            request->send(400, "text/plain", "Missing SSID or password");
        }
    });

    server.on("/connected", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->send(SPIFFS, "/connected.html", "text/html");
        else request->redirect("/");
    });

    server.on("/runs", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!SD.begin(SD_CS_PIN)) {
            request->send(500, "application/json", "{\"error\":\"SD Card mount failed\"}");
            return;
        }

        DynamicJsonDocument doc(2048);
        JsonArray runsArray = doc.to<JsonArray>();

        File root = SD.open("/");
        File file = root.openNextFile();

        while (file) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                size_t fileSize = file.size();
                
                JsonObject fileObj = runsArray.createNestedObject();
                fileObj["name"] = fileName;
                fileObj["size"] = fileSize;
                Serial.println("Found file: " + fileName + " (" + String(fileSize) + " bytes)");
            }
            file = root.openNextFile();
        }

        root.close();

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server.on("/uploadRun", HTTP_POST, [](AsyncWebServerRequest *request){
        Serial.println("[ENDPOINT] /uploadRun POST received");

        String runName = "";
        // First check POST form body
        if (request->hasParam("run", true)) {
            runName = request->getParam("run", true)->value();
        } else if (request->hasParam("run")) {
            // fallback to query param
            runName = request->getParam("run")->value();
        }

        if (runName.length() == 0) {
            request->send(400, "text/plain", "Missing 'run' parameter");
            return;
        }

        // Duplicate string to heap for task parameter
        char *param = strdup(runName.c_str());
        if (!param) {
            request->send(500, "text/plain", "Memory allocation failed");
            return;
        }

        BaseType_t res = xTaskCreate(uploadRunTask, "UploadTask", 12000, param, 1, &UploadTask);
        if (res != pdPASS) {
            free(param);
            request->send(500, "text/plain", "Failed to create upload task");
            return;
        }

        request->send(200, "text/plain", "Upload started in background");
    });

    server.begin();

    while (true) {
        if (startWiFiConnect) {
            startWiFiConnect = false;
            connectToWiFi();
            // LED_SOLID when connected, LED_OFF when failed handled inside connectToWiFi()
        }

        // Just update LED timing each loop (do NOT reset mode)
        updateOnboardLed();

        vTaskDelay(50 / portTICK_PERIOD_MS);  // shorter delay for smoother blinking
    }
}

// Data Task - Core 0
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
                    recording = !recording;
                    if (recording) {
                        setLedColor(1, 0, 0);
                        startNewRun();
                    } else {
                        setLedColor(0, 1, 0);
                        if (!sensorBuffer.empty()) flushSensorBuffer();
                    }
                }
            }
        }
        lastButtonState = reading;

        // --- Capture data ---
        if (recording) {
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

            line.rear_sus = analogRead(REAR_SUS_PIN);
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

        // use vTaskDelayUntil for a stable period of 10 ms
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);

    setupPins();
    setLedColor(1, 1, 1);  // Purple while setting up

    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        setLedColor(0, 0, 1); // Blue Failed
        while(true) delay(1000); // halt here
    }
    // if(bmi160.softReset() != BMI160_OK){
    //     Serial.println("BMI160 reset failed");
    //     setLedColor(0, 0, 1); // Blue Failed
    //     while(true) delay(1000); // halt here
    // }
    // if(bmi160.I2cInit(ic2_add) != BMI160_OK){
    //     Serial.println("BMI160 I2C init failed");
    //     setLedColor(0, 0, 1); // Blue Failed
    //     while(true) delay(1000); // halt here
    // }

    server.serveStatic("/style.css", SPIFFS, "/style.css");
    server.serveStatic("/script.js", SPIFFS, "/script.js");

    // Start tasks on separate cores
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, &WiFiTask, 1);  // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, &DataTask, 0);  // Core 0

    setLedColor(0, 1, 0);  // Green ready
}

void loop() {
    // Nothing needed; tasks handle everything
}
