#include "network_manager.h"
#include "globals.h"
#include "config.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <SD.h>

void setupWebRoutes() {
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

    server.on("/file", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "Missing name");
            return;
        }
        String path = "/" + request->getParam("name")->value();
        if (!SD.exists(path)) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        request->send(SD, path, "text/csv");
    });

    server.on("/deleteRun", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("run", true) || request->hasParam("run")) {
            String runName = request->hasParam("run", true)
                ? request->getParam("run", true)->value()
                : request->getParam("run")->value();
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

    server.on("/battery", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{\"percent\":" + String(batteryPercent) + "}";
        request->send(200, "application/json", json);
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
}
