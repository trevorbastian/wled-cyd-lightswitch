#include "lights.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

static const uint32_t WS_RECONNECT_MS = 5000;
static const uint16_t HTTP_TIMEOUT_MS = 1500;

LightStatus lightStatus[MAX_LIGHTS];
static WebSocketsClient sockets[MAX_LIGHTS];
static size_t numActive = 0;

static void setStatus(size_t i, const LightStatus &s) {
    LightStatus &cur = lightStatus[i];
    if (cur.state != s.state || cur.r != s.r || cur.g != s.g || cur.b != s.b) {
        cur = s;
        cur.dirty = true;
    }
}

// WLED sends {"state":{...},"info":{...}} on connect and after every change
static void handleMessage(size_t i, const uint8_t *payload, size_t length) {
    JsonDocument filter;
    filter["state"]["on"] = true;
    filter["state"]["seg"][0]["col"][0] = true;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter))) return;
    JsonObject state = doc["state"];
    if (!state["on"].is<bool>()) return;

    LightStatus s = lightStatus[i];
    s.state = state["on"] ? STATE_ON : STATE_OFF;

    JsonArray col = state["seg"][0]["col"][0];
    if (col.size() >= 3) {
        // Fold the white channel of RGBW strips into the displayed colour
        uint8_t w = col.size() >= 4 ? col[3].as<uint8_t>() : 0;
        s.r = min(255, col[0].as<int>() + w);
        s.g = min(255, col[1].as<int>() + w);
        s.b = min(255, col[2].as<int>() + w);
    }
    setStatus(i, s);
}

static void onSocketEvent(size_t i, WStype_t type, uint8_t *payload, size_t length) {
    if (i >= numActive) return;
    switch (type) {
        case WStype_CONNECTED:
            Serial.printf("%s: connected\n", settings.lights[i].host.c_str());
            break;
        case WStype_DISCONNECTED:
            if (lightStatus[i].state != STATE_UNKNOWN) {
                Serial.printf("%s: disconnected\n", settings.lights[i].host.c_str());
                LightStatus s = lightStatus[i];
                s.state = STATE_UNKNOWN;
                setStatus(i, s);
            }
            break;
        case WStype_TEXT:
            handleMessage(i, payload, length);
            break;
        default:
            break;
    }
}

void lightsRestart() {
    numActive = 0;
    for (size_t i = 0; i < MAX_LIGHTS; i++) {
        sockets[i].disconnect();
        lightStatus[i] = LightStatus();
    }

    for (size_t i = 0; i < settings.numLights; i++) {
        sockets[i].begin(settings.lights[i].host, 80, "/ws");
        sockets[i].onEvent([i](WStype_t type, uint8_t *payload, size_t length) {
            onSocketEvent(i, type, payload, length);
        });
        sockets[i].setReconnectInterval(WS_RECONNECT_MS);
        // Ping every 15s; drop the connection if 2 pongs in a row are missed
        sockets[i].enableHeartbeat(15000, 3000, 2);
    }
    numActive = settings.numLights;
}

void lightsLoop() {
    if (WiFi.status() != WL_CONNECTED) return;
    for (size_t i = 0; i < numActive; i++) sockets[i].loop();
}

// Commands include "v":true so WLED replies straight away with the full new state
static void sendCommand(size_t i, const char *body) {
    if (i >= numActive || !sockets[i].isConnected()) return;
    sockets[i].sendTXT(body);
}

void lightsToggle(size_t i) {
    sendCommand(i, "{\"on\":\"t\",\"v\":true}");
}

void lightsSetColour(uint8_t r, uint8_t g, uint8_t b) {
    char body[96];
    snprintf(body, sizeof(body), "{\"on\":true,\"seg\":{\"fx\":0,\"col\":[[%u,%u,%u]]},\"v\":true}", r, g, b);
    for (size_t i = 0; i < numActive; i++) sendCommand(i, body);
}

String lightsFetchName(const String &host) {
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin("http://" + host + "/json/info")) return "";

    String name;
    if (http.GET() == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["name"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter))) {
            name = doc["name"] | "";
        }
    }
    http.end();
    return name.substring(0, NAME_MAX_LEN);
}

std::vector<FoundLight> lightsDiscover() {
    std::vector<FoundLight> found;
    if (WiFi.status() != WL_CONNECTED) return found;

    int n = MDNS.queryService("wled", "tcp");
    for (int i = 0; i < n; i++) {
        String host = MDNS.IP(i).toString();
        bool dup = false;
        for (auto &f : found) dup |= f.host == host;
        if (dup) continue;

        String name = lightsFetchName(host);
        if (name.isEmpty()) name = MDNS.hostname(i);
        found.push_back({ name, host });
    }
    return found;
}
