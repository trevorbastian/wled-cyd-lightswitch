// WLED on/off touch controller for the ESP32-2432S028R ("Cheap Yellow Display").
// Keeps a WebSocket open to each WLED controller (ws://<host>/ws). WLED pushes
// its full state over it whenever anything changes, and we send JSON state
// commands (e.g. {"on":"t"} to toggle) back the same way.

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"

// Touch controller is on its own SPI bus on the CYD
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Raw touch range, landscape; tweak if taps land in the wrong place
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int HEADER_H = 30;
static const int MARGIN = 8;
static const uint32_t WS_RECONNECT_MS = 3000;

static const uint16_t COLOR_OFF = 0x2104;      // dark grey
static const uint16_t COLOR_UNKNOWN = 0x7800;  // dark red

static const size_t NUM_LIGHTS = sizeof(LIGHTS) / sizeof(LIGHTS[0]);

struct Swatch { uint8_t r, g, b; };

// White, then six hues 60 degrees apart around the colour wheel
static const Swatch SWATCHES[] = {
    { 255, 255, 255 },  // white
    { 255,   0,   0 },  // red
    { 255, 255,   0 },  // yellow
    {   0, 255,   0 },  // green
    {   0, 255, 255 },  // cyan
    {   0,   0, 255 },  // blue
    { 255,   0, 255 },  // magenta
};
static const size_t NUM_SWATCHES = sizeof(SWATCHES) / sizeof(SWATCHES[0]);
static const int SWATCH_H = 40;

enum LightState { STATE_UNKNOWN, STATE_OFF, STATE_ON };

struct LightStatus {
    LightState state = STATE_UNKNOWN;
    uint8_t r = 255, g = 160, b = 0;  // primary colour of segment 0
};

static LightStatus status[NUM_LIGHTS];
static WebSocketsClient sockets[NUM_LIGHTS];

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static void buttonRect(size_t i, int &x, int &y, int &w, int &h) {
    int area = SCREEN_H - HEADER_H - SWATCH_H - 2 * MARGIN;
    h = area / NUM_LIGHTS - MARGIN;
    w = SCREEN_W - 2 * MARGIN;
    x = MARGIN;
    y = HEADER_H + MARGIN + i * (h + MARGIN);
}

static void swatchRect(size_t i, int &x, int &y, int &w, int &h) {
    w = (SCREEN_W - (NUM_SWATCHES + 1) * MARGIN) / NUM_SWATCHES;
    h = SWATCH_H;
    int slack = SCREEN_W - (NUM_SWATCHES * w + (NUM_SWATCHES + 1) * MARGIN);
    x = slack / 2 + MARGIN + i * (w + MARGIN);
    y = SCREEN_H - MARGIN - h;
}

static void drawSwatch(size_t i) {
    int x, y, w, h;
    swatchRect(i, x, y, w, h);
    tft.fillRoundRect(x, y, w, h, 6, tft.color565(SWATCHES[i].r, SWATCHES[i].g, SWATCHES[i].b));
    tft.drawRoundRect(x, y, w, h, 6, TFT_DARKGREY);
}

static void drawHeader() {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("WLED", MARGIN, HEADER_H / 2, 4);

    bool connected = WiFi.status() == WL_CONNECTED;
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(connected ? TFT_GREEN : TFT_RED, TFT_NAVY);
    tft.drawString(connected ? WiFi.localIP().toString() : String("No WiFi"),
                   SCREEN_W - MARGIN, HEADER_H / 2, 2);
}

static void drawButton(size_t i) {
    int x, y, w, h;
    buttonRect(i, x, y, w, h);

    const LightStatus &s = status[i];
    uint16_t lightColour = tft.color565(s.r, s.g, s.b);
    uint16_t bg, fg, border;
    const char *label;
    switch (s.state) {
        case STATE_ON: {
            // Dark text on bright colours, white text on dark ones (e.g. blue)
            int luma = (299 * s.r + 587 * s.g + 114 * s.b) / 1000;
            bg = lightColour;
            fg = luma > 128 ? TFT_BLACK : TFT_WHITE;
            border = TFT_WHITE;
            label = "ON";
            break;
        }
        case STATE_OFF:
            // Keep the colour visible as an outline so you can see what "on" will look like
            bg = COLOR_OFF; fg = TFT_WHITE; border = lightColour; label = "OFF";
            break;
        default:
            bg = COLOR_UNKNOWN; fg = TFT_WHITE; border = TFT_WHITE; label = "unreachable";
            break;
    }

    tft.fillRoundRect(x, y, w, h, 10, bg);
    tft.drawRoundRect(x, y, w, h, 10, border);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 9, border);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(LIGHTS[i].name, x + 14, y + h / 2, 4);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(label, x + w - 14, y + h / 2, 4);
}

static void drawAll() {
    tft.fillScreen(TFT_BLACK);
    drawHeader();
    for (size_t i = 0; i < NUM_LIGHTS; i++) drawButton(i);
    for (size_t i = 0; i < NUM_SWATCHES; i++) drawSwatch(i);
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

    LightStatus &s = status[i];
    s.state = state["on"] ? STATE_ON : STATE_OFF;

    JsonArray col = state["seg"][0]["col"][0];
    if (col.size() >= 3) {
        // Fold the white channel of RGBW strips into the displayed colour
        uint8_t w = col.size() >= 4 ? col[3].as<uint8_t>() : 0;
        s.r = min(255, col[0].as<int>() + w);
        s.g = min(255, col[1].as<int>() + w);
        s.b = min(255, col[2].as<int>() + w);
    }
    drawButton(i);
}

static void onSocketEvent(size_t i, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.printf("%s: connected\n", LIGHTS[i].host);
            break;
        case WStype_DISCONNECTED:
            if (status[i].state != STATE_UNKNOWN) {
                Serial.printf("%s: disconnected\n", LIGHTS[i].host);
                status[i].state = STATE_UNKNOWN;
                drawButton(i);
            }
            break;
        case WStype_TEXT:
            handleMessage(i, payload, length);
            break;
        default:
            break;
    }
}

// Commands include "v":true so WLED replies straight away with the full new state
static void sendCommand(size_t i, const char *body) {
    if (!sockets[i].isConnected()) return;
    sockets[i].sendTXT(body);
}

static void toggle(size_t i) {
    // Instant feedback; the button is redrawn when WLED reports the new state
    int x, y, w, h;
    buttonRect(i, x, y, w, h);
    tft.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 8, TFT_CYAN);

    sendCommand(i, "{\"on\":\"t\",\"v\":true}");
}

// Sets every light to the swatch colour (turning it on, with the Solid effect
// so the colour isn't masked by an animation)
static void applyColour(size_t s) {
    char body[96];
    snprintf(body, sizeof(body), "{\"on\":true,\"seg\":{\"fx\":0,\"col\":[[%u,%u,%u]]},\"v\":true}",
             SWATCHES[s].r, SWATCHES[s].g, SWATCHES[s].b);
    for (size_t i = 0; i < NUM_LIGHTS; i++) sendCommand(i, body);
}

static bool inRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

// Returns the index of the light button at (x, y), or -1
static int buttonAt(int px, int py) {
    for (size_t i = 0; i < NUM_LIGHTS; i++) {
        int x, y, w, h;
        buttonRect(i, x, y, w, h);
        if (inRect(px, py, x, y, w, h)) return i;
    }
    return -1;
}

// Returns the index of the colour swatch at (x, y), or -1
static int swatchAt(int px, int py) {
    for (size_t i = 0; i < NUM_SWATCHES; i++) {
        int x, y, w, h;
        swatchRect(i, x, y, w, h);
        if (inRect(px, py, x, y, w, h)) return i;
    }
    return -1;
}

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSpi);
    touch.setRotation(1);

    tft.fillScreen(TFT_BLACK);
    drawHeader();
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to WiFi...", SCREEN_W / 2, SCREEN_H / 2, 4);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(200);

    drawAll();

    for (size_t i = 0; i < NUM_LIGHTS; i++) {
        sockets[i].begin(LIGHTS[i].host, 80, "/ws");
        sockets[i].onEvent([i](WStype_t type, uint8_t *payload, size_t length) {
            onSocketEvent(i, type, payload, length);
        });
        sockets[i].setReconnectInterval(WS_RECONNECT_MS);
        // Ping every 15s; drop the connection if 2 pongs in a row are missed
        sockets[i].enableHeartbeat(15000, 3000, 2);
    }
}

void loop() {
    static bool wasTouched = false;
    static wl_status_t lastWifi = WL_IDLE_STATUS;

    for (size_t i = 0; i < NUM_LIGHTS; i++) sockets[i].loop();

    bool touched = touch.tirqTouched() && touch.touched();
    if (touched && !wasTouched) {
        TS_Point p = touch.getPoint();
        int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
        int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
        int i;
        if ((i = buttonAt(x, y)) >= 0) {
            toggle(i);
        } else if ((i = swatchAt(x, y)) >= 0) {
            applyColour(i);
        }
    }
    wasTouched = touched;

    wl_status_t wifi = WiFi.status();
    if (wifi != lastWifi) {
        lastWifi = wifi;
        drawHeader();
    }

    delay(10);
}
