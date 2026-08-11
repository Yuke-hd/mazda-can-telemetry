#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

namespace {

constexpr char kApSsid[] = "CHANGE_ME";
constexpr char kApPassword[] = "CHANGE_ME";
ESP8266WebServer server(80);

void handleRoot() {
  server.send(200, "text/plain", "D1 Mini CAN simulator scaffold; no CAN output enabled.\n");
}

} // namespace

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(kApSsid, kApPassword);
  server.on("/", handleRoot);
  server.begin();
}

void loop() { server.handleClient(); }
