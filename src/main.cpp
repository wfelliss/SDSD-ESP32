#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

// Shared variables
volatile bool wifiConnected = false;
volatile bool startWiFiConnect = false;

String ssidInput = "";
String passwordInput = "";

// Task handles
TaskHandle_t WiFiTask;
TaskHandle_t DataTask;

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
    } else {
        Serial.println("\nFailed to connect.");
        wifiConnected = false;
    }
}

// WiFi Task - Core 1
void WiFiTaskcode(void * pvParameter) {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        vTaskDelete(NULL);
        return;
    }

    // Start AP
    WiFi.softAP("ESP32-Setup", "12345678");
    Serial.println("AP Started. Connect to WiFi 'ESP32-Setup' to configure");

    // Setup async server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->redirect("/connected");
        else request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
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

    // Start tasks on separate cores
    xTaskCreatePinnedToCore(WiFiTaskcode, "WiFiTask", 12000, NULL, 1, &WiFiTask, 1);  // Core 1
    xTaskCreatePinnedToCore(DataTaskcode, "DataTask", 10000, NULL, 1, &DataTask, 0);  // Core 0
}

void loop() {
    // Nothing needed; tasks handle everything
}
