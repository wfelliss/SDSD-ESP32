#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

// Variables to store user WiFi credentials
String ssidInput = "";
String passwordInput = "";

// Flag to indicate WiFi connection
bool wifiConnected = false;

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
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        wifiConnected = true;

        // Optional: Stop AP
        WiFi.softAPdisconnect(true);

    } else {
        Serial.println("\nFailed to connect. Restart ESP to retry.");
        wifiConnected = false;
    }
}

void setup() {
    Serial.begin(115200);

    // Mount SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return;
    }

    // Start in AP mode for WiFi setup
    WiFi.softAP("ESP32-Setup", "12345678");
    Serial.println("AP Started. Connect to WiFi 'ESP32-Setup' to configure");

    // Serve WiFi config page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) {
            request->redirect("/connected");
        } else {
            request->send(SPIFFS, "/index.html", "text/html");
        }
    });

    // Handle WiFi form submission
    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            ssidInput = request->getParam("ssid", true)->value();
            passwordInput = request->getParam("password", true)->value();
            request->send(200, "text/html", "<h1>Connecting...</h1><p>ESP will try to connect to WiFi</p>");
            connectToWiFi();
        } else {
            request->send(400, "text/plain", "Missing SSID or password");
        }
    });

    // Serve connected page
    server.on("/connected", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) {
            request->send(SPIFFS, "/connected.html", "text/html");
        } else {
            request->redirect("/");
        }
    });

    server.begin();
}

void loop() {
    // Nothing needed here; everything handled via async web server
}
