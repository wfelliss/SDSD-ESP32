#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// --- Pin definitions ---
#define RED_LED_PIN 25
#define GREEN_LED_PIN 27
#define BLUE_LED_PIN 26
// #define BUTTON_PIN 0

AsyncWebServer server(80);

// --- Shared variables ---
volatile bool wifiConnected = false;
volatile bool startWiFiConnect = false;

String ssidInput = "";
String passwordInput = "";

// Task handles
TaskHandle_t WiFiTask;
TaskHandle_t DataTask;

void setupLEDs() {
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(BLUE_LED_PIN, OUTPUT);
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(BLUE_LED_PIN, HIGH);
}

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
    digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
    digitalWrite(BLUE_LED_PIN, blue ? HIGH : LOW);
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
        setLedColor(0, 1, 0); // Green connected
    } else {
        Serial.println("\nFailed to connect.");
        wifiConnected = false;
        setLedColor(1, 0, 0); // Red failed
    }
}

// WiFi Task - Core 1
void WiFiTaskcode(void * pvParameter) {
    setLedColor(1, 0, 0);  // Red initializing

    // Start AP
    WiFi.softAP("ESP32-Setup", "12345678");
    Serial.println("AP Started. Connect to WiFi 'ESP32-Setup' to configure");
    setLedColor(0, 1, 0); // Green AP mode started

    // Setup async server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->redirect("/connected");
        else request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            setLedColor(1, 1, 0); // Yellow connecting
            ssidInput = request->getParam("ssid", true)->value();
            passwordInput = request->getParam("password", true)->value();
            startWiFiConnect = true;  // flag to connect in task loop
            request->send(200, "text/html", "<h1>Connecting...</h1>");
        } else {
            request->send(400, "text/plain", "Missing SSID or password");
        }
    });

    server.on("/connected", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->send(SPIFFS, "/connected.html", "text/html");
        else request->redirect("/");
    });

    server.begin();

    while (true) {
        if (startWiFiConnect) {
            startWiFiConnect = false;
            connectToWiFi();
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

// Data Task - Core 0
void DataTaskcode(void * pvParameter) {
    while (true) {
        // Example: read sensors, log to SD, etc.
        printf("Data Task running. WiFi connected: %s\n", wifiConnected ? "Yes" : "No");

        // Replace with real sensor reads / SD writes
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);

    setupLEDs();
    setLedColor(1, 0, 0);  // Red while setting up

    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        setLedColor(0, 0, 1); // Blue
        while(true) delay(1000); // halt here
    }

    // Start tasks on separate cores
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, &WiFiTask, 1);  // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, &DataTask, 0);  // Core 0
}

void loop() {
    // Nothing needed; tasks handle everything
}
