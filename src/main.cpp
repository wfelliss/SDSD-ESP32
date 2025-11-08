#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

Preferences preferences;
AsyncWebServer server(80);

// ===== CONFIG =====
const char* ap_ssid = "ESP32_Config";
const char* ap_password = "12345678";

String ssid = "";
String password = "";

// HTML for Wi-Fi setup page
const char* configPage = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>WiFi Setup</title>
    <style>
      body { font-family: Arial; max-width: 400px; margin: 50px auto; text-align: center; }
      input { width: 100%; padding: 8px; margin: 8px 0; }
      button { padding: 10px 20px; }
    </style>
  </head>
  <body>
    <h2>Configure WiFi</h2>
    <form action="/save" method="POST">
      <input type="text" name="ssid" placeholder="WiFi SSID" required><br>
      <input type="password" name="pass" placeholder="WiFi Password" required><br>
      <button type="submit">Save & Connect</button>
    </form>
  </body>
</html>
)rawliteral";

// HTML for connected page
const char* connectedPage = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <title>Connected</title>
    <style>body { font-family: Arial; text-align: center; margin-top: 50px; }</style>
  </head>
  <body>
    <h2>✅ ESP32 Connected to WiFi!</h2>
  </body>
</html>
)rawliteral";

// ===== Function Declarations =====
void startAPMode();
void startSTAMode();

// ===== Setup WiFi in STA Mode =====
void startSTAMode() {
  Serial.printf("Connecting to SSID: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.reset();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(200, "text/html", connectedPage);
    });
    server.begin();
  } else {
    Serial.println("\nFailed to connect — reverting to AP mode.");
    startAPMode();
  }
}

// ===== Setup WiFi in AP Mode =====
void startAPMode() {
  Serial.println("Starting Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.reset();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", configPage);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {
    String newSSID, newPASS;
    if (request->hasParam("ssid", true)) newSSID = request->getParam("ssid", true)->value();
    if (request->hasParam("pass", true)) newPASS = request->getParam("pass", true)->value();

    if (newSSID != "" && newPASS != "") {
      ssid = newSSID;
      password = newPASS;

      preferences.putString("ssid", ssid);
      preferences.putString("pass", password);

      request->send(200, "text/html", "<h2>Saved! Connecting...</h2>");
      delay(2000);
      startSTAMode();
    } else {
      request->send(400, "text/plain", "Missing SSID or password!");
    }
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  preferences.begin("wifi", false);

  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");

  if (ssid.length() > 0 && password.length() > 0) {
    Serial.println("Found saved credentials. Trying to connect...");
    startSTAMode();
  } else {
    Serial.println("No saved credentials. Starting AP mode.");
    startAPMode();
  }
}

void loop() {
  // Nothing needed — handled asynchronously!
}
