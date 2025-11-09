#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include <ArduinoJson.h>


// --- Pin definitions ---
#define ONBOARD_LED_PIN 2
#define RED_LED_PIN 25
#define GREEN_LED_PIN 27
#define BLUE_LED_PIN 26
#define BUTTON_PIN 14

AsyncWebServer server(80);

// --- Wifi variables ---
volatile bool wifiConnected = false;
volatile bool startWiFiConnect = false;
String ssidInput = "";
String passwordInput = "";

// --- Recording variables ---
bool recording = false;
std::vector<std::vector<int>> allRecordings;
std::vector<int> currentRecording;

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
        DynamicJsonDocument doc(1024);
        JsonArray runsArray = doc.to<JsonArray>();

        for (size_t i = 0; i < allRecordings.size(); i++) {
            JsonArray runArray = runsArray.createNestedArray();
            for (size_t j = 0; j < allRecordings[i].size(); j++) {
                runArray.add(allRecordings[i][j]);
            }
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
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
    int lastButtonState = HIGH;
    int buttonState = HIGH;
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    while (true) {
        int reading = digitalRead(BUTTON_PIN);

        // Debounce logic
        if (reading != lastButtonState) lastDebounceTime = millis();

        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (reading != buttonState) {
                buttonState = reading;
                if (buttonState == LOW) {  // Button pressed
                    recording = !recording; // Toggle recording

                    if (recording) {
                        // Start a new session
                        currentRecording.clear();
                        Serial.println("Recording started (new session)");
                    } else {
                        // Stop recording, save session
                        allRecordings.push_back(currentRecording);
                        Serial.printf("Recording stopped, %d samples saved\n", currentRecording.size());
                        Serial.printf("Total runs so far: %d\n", allRecordings.size());
                    }
                }
            }
        }
        lastButtonState = reading;

        // --- Data capture ---
        if (recording) {
            int value = random(1, 101);  // Replace with actual data read
            currentRecording.push_back(value);
            setLedColor(1, 0, 0);  // Red while recording
        } else {
            setLedColor(0, 1, 0);  // Green when not recording
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);  // sample rate
    }
}

void setup() {
    Serial.begin(115200);

    setupPins();
    setLedColor(1, 0, 1);  // Purple while setting up

    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        setLedColor(0, 0, 1); // Blue Failed
        while(true) delay(1000); // halt here
    }

    // Start tasks on separate cores
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, &WiFiTask, 1);  // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, &DataTask, 0);  // Core 0
}

void loop() {
    // Nothing needed; tasks handle everything
}
