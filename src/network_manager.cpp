#include "network_manager.h"
#include "globals.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SD.h>

void setupWebRoutes() {
    // Setup async server
    server.onNotFound([](AsyncWebServerRequest *request){
        Serial.printf("NOT_FOUND: %s\n", request->url().c_str());
        
        // Option A: Redirect them to the main page (Best for Captive Portals)
        request->redirect("/");
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (wifiConnected) request->redirect("/connected");
        else request->send(LittleFS, "/index.html", "text/html");
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
        if (wifiConnected) request->send(LittleFS, "/connected.html", "text/html");
        else request->redirect("/");
    });

    server.on("/runs", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!SD.begin(SD_CS_PIN)) {
            request->send(500, "application/json", "{\"error\":\"SD Card mount failed\"}");
            return;
        }

        JsonDocument doc;
        JsonArray runsArray = doc.to<JsonArray>();

        File root = SD.open("/");
        File file = root.openNextFile();

        while (file) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                size_t fileSize = file.size();
                
                JsonObject fileObj = runsArray.add<JsonObject>();
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
        String track = "";
        String comments = "";
        int frontStroke = 0;
        int rearStroke = 0;

        // First check POST form body for run/track/comments
        if (request->hasParam("run", true)) runName = request->getParam("run", true)->value();
        else if (request->hasParam("run")) runName = request->getParam("run")->value();

        if (request->hasParam("track", true)) track = request->getParam("track", true)->value();
        else if (request->hasParam("track")) track = request->getParam("track")->value();

        if (request->hasParam("comments", true)) comments = request->getParam("comments", true)->value();
        else if (request->hasParam("comments")) comments = request->getParam("comments")->value();

        if (request->hasParam("front_stroke", true)) frontStroke = request->getParam("front_stroke", true)->value().toInt();
        else if (request->hasParam("front_stroke")) frontStroke = request->getParam("front_stroke")->value().toInt();

        if (request->hasParam("rear_stroke", true)) rearStroke = request->getParam("rear_stroke", true)->value().toInt();
        else if (request->hasParam("rear_stroke")) rearStroke = request->getParam("rear_stroke")->value().toInt();

        if (runName.length() == 0) {
            request->send(400, "text/plain", "Missing 'run' parameter");
            return;
        }

        // Allocate params struct and duplicate strings to heap for task
        UploadParams *params = (UploadParams *)malloc(sizeof(UploadParams));
        if (!params) {
            request->send(500, "text/plain", "Memory allocation failed");
            return;
        }
        params->runName = strdup(runName.c_str());
        params->track = strdup(track.c_str());
        params->comments = strdup(comments.c_str());
        params->frontStroke = frontStroke;
        params->rearStroke = rearStroke;

        if (!params->runName || !params->track || !params->comments) {
            if (params->runName) free(params->runName);
            if (params->track) free(params->track);
            if (params->comments) free(params->comments);
            free(params);
            request->send(500, "text/plain", "Memory allocation failed");
            return;
        }

        BaseType_t res = xTaskCreate(uploadRunTask, "UploadTask", 12000, params, 1, &UploadTask);
        if (res != pdPASS) {
            if (params->runName) free(params->runName);
            if (params->track) free(params->track);
            if (params->comments) free(params->comments);
            free(params);
            request->send(500, "text/plain", "Failed to create upload task");
            return;
        }

        request->send(200, "text/plain", "Upload started in background");
    });

    server.on("/deleteRun", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("run", true) || request->hasParam("run")) {
            String runName = request->hasParam("run", true) ? request->getParam("run", true)->value() : request->getParam("run")->value();
            if (!SD.begin(SD_CS_PIN)) {
                request->send(500, "text/plain", "SD Card mount failed");
                return;
            }
            if (SD.exists("/" + runName)) {
                SD.remove("/" + runName);
                Serial.println("Deleted run: " + runName);
                request->send(200, "text/plain", "Run deleted");
            } else {
                request->send(404, "text/plain", "Run not found");
            }
        } else {
            request->send(400, "text/plain", "Missing 'run' parameter");
        }
        
    });

    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/script.js", LittleFS, "/script.js");
}

void uploadRunTask(void *pvParameter) {
    Serial.println("[UPLOAD] Task started (using LittleFS and SD)");

    // Extract parameters passed from the HTTP handler
    UploadParams *up = (UploadParams *)pvParameter;
    String csvPath = "/run.csv";
    String trackName = "";
    String comments = "";
    int frontStroke = 0;
    int rearStroke = 0;
    if (up) {
        if (up->runName) {
            csvPath = String(up->runName);
            free(up->runName);
            up->runName = NULL;
        }
        if (up->track) {
            trackName = String(up->track);
            free(up->track);
            up->track = NULL;
        }
        if (up->comments) {
            comments = String(up->comments);
            free(up->comments);
            up->comments = NULL;
        }
        if (up->frontStroke) {
            frontStroke = up->frontStroke;
        }
        if (up->rearStroke) {
            rearStroke = up->rearStroke;
        }
        free(up);
        up = NULL;
    }

    // --- 1. Mount LittleFS (true = format if mount fails) ---
    if (!LittleFS.begin(true) || !SD.begin(SD_CS_PIN)) {
        Serial.println("[UPLOAD] Storage mount failed (LittleFS or SD)");
        vTaskDelete(NULL);
        return;
    }

    // --- 2. Open CSV file on SD ---
    File csvFile = SD.open(("/" + csvPath).c_str(), "r");

    if (!csvFile) {
        Serial.println("[UPLOAD] CSV file not found on SD: " + csvPath);
        if(csvFile) csvFile.close();
        vTaskDelete(NULL);
        return;
    }

    // --- 3. Setup Secure Client ---
    WiFiClientSecure client;
    client.setInsecure(); // Required for Railway's SSL certificates
    
    // Parse URL for Host/Path
    String url = EXTERNAL_SERVER_URL;
    url.replace("https://", "");
    url.replace("http://", "");
    
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    String path = url.substring(slashIndex);
    
    int port = 443; // Always 443 for Railway HTTPS
    
    Serial.println("[UPLOAD] Connecting to: " + host + " (Port 443)");

    if (!client.connect(host.c_str(), port)) {
        Serial.println("[UPLOAD] HTTPS Connection failed");
        csvFile.close();
        vTaskDelete(NULL);
        return;
    }
    
    client.setTimeout(15); 

    // --- 4. Build Multipart Body ---
    String boundary = "----ESP32Boundary" + String(millis());

    // Count data lines in CSV (subtract header) to estimate run time.
    size_t newlineCount = 0;
    while (csvFile.available()) {
        char c = (char)csvFile.read();
        if (c == '\n') newlineCount++;
    }
    // Reset/close and re-open CSV for streaming later
    csvFile.close();
    csvFile = SD.open(("/" + csvPath).c_str(), "r");

    size_t dataLines = 0;
    if (newlineCount > 0) {
        // first line is header
        dataLines = (newlineCount > 0) ? (newlineCount - 1) : 0;
    }
    // sample period in ms (use SAMPLE_PERIOD_MS)

    JsonDocument metaDoc;
    String csvFileName = csvPath;
    if (csvFileName.lastIndexOf('/') >= 0) { // extracts filename from path
        csvFileName = csvFileName.substring(csvFileName.lastIndexOf('/') + 1);
    }
    int dotIndex = csvFileName.lastIndexOf('.');
    if (dotIndex > 0) { // remove extension
        csvFileName = csvFileName.substring(0, dotIndex);
    }

    metaDoc["run_name"] = csvFileName;
    metaDoc["run_comment"] = (comments.length() == 0) ? "NULL" : comments;
    metaDoc["location"] = (trackName.length() == 0) ? "NULL" : trackName;
    // Apply sensible defaults if not provided
    if (frontStroke == 0) frontStroke = 220; // max front pot travel
    if (rearStroke == 0) rearStroke = 80; // max rear pot travel
    metaDoc["front_stroke"] = frontStroke;
    metaDoc["rear_stroke"] = rearStroke;
    JsonObject sf = metaDoc["sample_frequency"].to<JsonObject>();
    sf["rear_sus"] = String(SAMPLE_FREQUENCY);
    sf["front_sus"] = String(SAMPLE_FREQUENCY);

    String jsonString;
    serializeJson(metaDoc, jsonString);

    String jsonPart = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"metadata\"\r\n\r\n" + 
        jsonString + "\r\n";

    String csvHead = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + csvFileName + "\"\r\n" +
        "Content-Type: text/csv\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";
    size_t totalLen = jsonPart.length() + csvHead.length() + csvFile.size() + tail.length();

    // --- 5. Send POST Request ---
    client.print("POST " + path + " HTTP/1.1\r\n");
    client.print("Host: " + host + "\r\n"); // Railway needs this header!
    client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
    client.print("Content-Length: " + String(totalLen) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    client.print(jsonPart);
    client.print(csvHead);

    // Stream CSV Data from SD Card
    uint8_t buf[1024];
    while (csvFile.available()) {
        int r = csvFile.read(buf, sizeof(buf));
        if (r <= 0) break;
        client.write(buf, r);
    }
    
    client.print(tail);
    csvFile.close();

    Serial.println("[UPLOAD] Data sent. Reading response...");

    // Ensure outgoing data is flushed from TCP buffers
    client.flush();

    // --- 6. Read Response ---
    String response = "";
    unsigned long lastReceive = millis();
    const unsigned long overallTimeoutMs = 15000; // wait up to 15s for server response

    // Wait for initial data with a short poll loop (keeps WiFi stack responsive)
    while (!client.available() && client.connected() && (millis() - lastReceive) < overallTimeoutMs) {
        delay(10);
    }

    // Read until the server closes the connection or we hit the overall timeout
    lastReceive = millis();
    while ((client.connected() || client.available()) && (millis() - lastReceive) < overallTimeoutMs) {
        while (client.available()) {
            char c = (char)client.read();
            response += c;
            lastReceive = millis();
        }
        // small delay to yield
        delay(10);
    }

    Serial.println("[UPLOAD] Server Response:");
    Serial.println(response);

    if (response.indexOf("HTTP/1.1 200") >= 0 || response.indexOf("HTTP/1.1 201") >= 0) {
        Serial.println("[UPLOAD] Success detected. Deleting local files...");

        // Delete CSV from SD card
        String fullCsvPath = (csvPath.startsWith("/") ? csvPath : "/" + csvPath);
        if (SD.remove(fullCsvPath.c_str())) {
            Serial.println("[UPLOAD] SD file deleted.");
        } else {
            Serial.println("[UPLOAD] Failed to delete SD file.");
        }
    } else {
        Serial.println("[UPLOAD] Upload failed or returned error. Keeping files for retry.");
    }

    client.stop();
    vTaskDelete(NULL);
}

void startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SD Squared Telemetry", "sdsquared");
    Serial.println("AP Started. Connect to WiFi 'SD Squared Telemetry' to configure");
    if (!MDNS.begin("esp32-ap")) {  // devices on the SoftAP can use esp32-ap.local
        Serial.println("Error starting mDNS responder on SoftAP");
    } else {
        Serial.println("mDNS responder started on SoftAP as esp32-ap.local");
    }
    setOnboardLed(LED_BLINK, 1000);
}
// WiFi connection function (blocking, but called in task loop)
void connectToWiFi() {
    setOnboardLed(LED_BLINK, 200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidInput.c_str(), passwordInput.c_str());

    Serial.print("Connecting to WiFi");
    const unsigned long wifiTimeoutMs = 30000; // 30s
    unsigned long startMillis = millis();
    int dotCounter = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startMillis) < wifiTimeoutMs) {
        updateOnBoardLed();
        delay(100); // keep loop responsive; print dot every 5th iteration (500ms)
        if (++dotCounter % 5 == 0) Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected!");
        Serial.println(WiFi.localIP());

        if (!MDNS.begin("esp32")) {  // esp32.local
            Serial.println("Error starting mDNS responder");
        } else {
            Serial.println("mDNS responder started at esp32.local");
        }

        wifiConnected = true;
        WiFi.softAPdisconnect(true);
        setOnboardLed(LED_SOLID);
    } else {
        Serial.println("\nFailed to connect (timeout).");
        wifiConnected = false;
        setOnboardLed(LED_OFF);

        // Restore SoftAP so user can reconnect and retry
        Serial.println("Restoring SoftAP for user configuration...");
        startAP();
    }
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi lost or Hotspot turned off.");
            
            // Only trigger a reconnect if we were previously successful
            // and aren't already trying to connect.
            if (wifiConnected) {
                wifiConnected = false;
                startWiFiConnect = true; 
                Serial.println("Triggering reconnection logic...");
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("Obtained IP: ");
            Serial.println(WiFi.localIP());
            wifiConnected = true;
            break;
            
        default: break;
    }
}