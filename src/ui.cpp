#include "ui.h"

#include <WiFi.h>
#include <functional>

#include "display.h"
#include "keyboard.h"
#include "lights.h"
#include "settings.h"
#include "wifi_mgr.h"

enum Screen {
    SCR_MAIN,
    SCR_MENU,
    SCR_WIFI_LIST,
    SCR_CONNECTING,
    SCR_WIFI_ERROR,
    SCR_KEYBOARD,
    SCR_LIGHTS,
    SCR_LIGHT_EDIT,
    SCR_DISCOVER,
    SCR_CALIBRATE,
    SCR_CONFIRM_RESET,
};

static Screen screen = SCR_MAIN;

static void showMain();
static void showMenu();
static void showWifiList(bool rescan);
static void showLights();

// Opens the keyboard as the current screen
static void openKeyboard(const KeyboardOptions &opts, KeyboardDone done) {
    screen = SCR_KEYBOARD;
    keyboardOpen(opts, done);
}

// ---------------------------------------------------------------------------
// Shared list layout: 4 rows per page with Prev/Next buttons underneath

static const int LIST_ROWS = 4;
static const Rect PREV_RECT = { MARGIN, 200, 60, 34 };
static const Rect NEXT_RECT = { SCREEN_W - MARGIN - 60, 200, 60, 34 };
static const Rect FOOTER_MID_RECT = { MARGIN + 68, 200, SCREEN_W - 2 * MARGIN - 136, 34 };

static Rect listRow(int i) {
    return { MARGIN, 36 + i * 40, SCREEN_W - 2 * MARGIN, 36 };
}

static int pageCount(size_t items) {
    return max(1, (int)((items + LIST_ROWS - 1) / LIST_ROWS));
}

static void drawPager(int page, int pages) {
    if (page > 0) drawButton(PREV_RECT, "<", COLOR_BUTTON, TFT_WHITE, 4);
    else tft.fillRect(PREV_RECT.x, PREV_RECT.y, PREV_RECT.w, PREV_RECT.h, TFT_BLACK);
    if (page < pages - 1) drawButton(NEXT_RECT, ">", COLOR_BUTTON, TFT_WHITE, 4);
    else tft.fillRect(NEXT_RECT.x, NEXT_RECT.y, NEXT_RECT.w, NEXT_RECT.h, TFT_BLACK);
}

// Returns the item index tapped on this page, or -1
static int listHit(int x, int y, int page, size_t items) {
    for (int i = 0; i < LIST_ROWS; i++) {
        size_t idx = page * LIST_ROWS + i;
        if (idx < items && listRow(i).contains(x, y)) return idx;
    }
    return -1;
}

static void clearList() {
    tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, TFT_BLACK);
}

// ---------------------------------------------------------------------------
// Main screen: a button per light plus colour swatches

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
static const Rect GEAR_RECT = { SCREEN_W - 50, 0, 50, HEADER_H };

static Rect lightRect(size_t i) {
    int area = SCREEN_H - HEADER_H - SWATCH_H - 2 * MARGIN;
    int h = area / settings.numLights - MARGIN;
    return { MARGIN, HEADER_H + MARGIN + (int)i * (h + MARGIN), SCREEN_W - 2 * MARGIN, h };
}

static Rect swatchRect(size_t i) {
    int w = (SCREEN_W - (NUM_SWATCHES + 1) * MARGIN) / NUM_SWATCHES;
    int slack = SCREEN_W - (NUM_SWATCHES * w + (NUM_SWATCHES + 1) * MARGIN);
    return { slack / 2 + MARGIN + (int)i * (w + MARGIN), SCREEN_H - MARGIN - SWATCH_H, w, SWATCH_H };
}

static void drawMainHeader() {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
    tft.setTextColor(TFT_WHITE, COLOR_HEADER);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("WLED", MARGIN, HEADER_H / 2, 4);

    String status;
    uint16_t colour = TFT_RED;
    if (wifiConnected()) {
        status = WiFi.localIP().toString();
        colour = TFT_GREEN;
    } else {
        status = settings.ssid.isEmpty() ? "WiFi not set up" : "Connecting...";
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(colour, COLOR_HEADER);
    tft.drawString(status, GEAR_RECT.x - 4, HEADER_H / 2, 2);
    drawGear(GEAR_RECT.cx() + 6, GEAR_RECT.cy(), TFT_WHITE, COLOR_HEADER);
}

static void drawLight(size_t i) {
    Rect r = lightRect(i);
    const LightStatus &s = lightStatus[i];
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
            bg = COLOR_UNKNOWN; fg = TFT_WHITE; border = TFT_WHITE; label = "offline";
            break;
    }

    tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 10, border);
    tft.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 9, border);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MR_DATUM);
    int labelW = tft.drawString(label, r.x + r.w - 14, r.cy(), 4);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(fitText(settings.lights[i].name, r.w - labelW - 40, 4), r.x + 14, r.cy(), 4);
    lightStatus[i].dirty = false;
}

static void showMain() {
    screen = SCR_MAIN;
    tft.fillScreen(TFT_BLACK);
    drawMainHeader();
    if (settings.numLights == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("No lights yet", SCREEN_W / 2, 90, 4);
        tft.drawString("Tap the gear to add some", SCREEN_W / 2, 125, 2);
    }
    for (size_t i = 0; i < settings.numLights; i++) drawLight(i);
    for (size_t i = 0; i < NUM_SWATCHES; i++) {
        Rect r = swatchRect(i);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, tft.color565(SWATCHES[i].r, SWATCHES[i].g, SWATCHES[i].b));
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, TFT_DARKGREY);
    }
}

static void mainTap(int x, int y) {
    if (GEAR_RECT.contains(x, y)) {
        showMenu();
        return;
    }
    for (size_t i = 0; i < settings.numLights; i++) {
        Rect r = lightRect(i);
        if (r.contains(x, y)) {
            // Instant feedback; the button is redrawn when WLED reports the new state
            tft.drawRoundRect(r.x + 3, r.y + 3, r.w - 6, r.h - 6, 8, TFT_CYAN);
            lightsToggle(i);
            return;
        }
    }
    for (size_t i = 0; i < NUM_SWATCHES; i++) {
        if (swatchRect(i).contains(x, y)) {
            lightsSetColour(SWATCHES[i].r, SWATCHES[i].g, SWATCHES[i].b);
            return;
        }
    }
}

static void mainTick() {
    static bool lastConnected = false;
    bool connected = wifiConnected();
    if (connected != lastConnected) {
        lastConnected = connected;
        drawMainHeader();
    }
    for (size_t i = 0; i < settings.numLights; i++) {
        if (lightStatus[i].dirty) drawLight(i);
    }
}

// ---------------------------------------------------------------------------
// Touch calibration: tap a crosshair near each corner

static const int CAL_INSET = 20;
static const int CAL_TARGETS[4][2] = {
    { CAL_INSET, CAL_INSET },
    { SCREEN_W - CAL_INSET, CAL_INSET },
    { SCREEN_W - CAL_INSET, SCREEN_H - CAL_INSET },
    { CAL_INSET, SCREEN_H - CAL_INSET },
};

static int calStep;
static int calRaw[4][2];
static std::function<void()> calDone;

static void drawCalibrate() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Touch calibration", SCREEN_W / 2, 95, 4);
    tft.drawString("Tap the centre of the cross", SCREEN_W / 2, 125, 2);
    tft.drawString(String(calStep + 1) + " of 4", SCREEN_W / 2, 145, 2);

    int cx = CAL_TARGETS[calStep][0], cy = CAL_TARGETS[calStep][1];
    tft.drawFastHLine(cx - 15, cy, 31, TFT_WHITE);
    tft.drawFastVLine(cx, cy - 15, 31, TFT_WHITE);
    tft.drawCircle(cx, cy, 6, TFT_RED);
}

static void showCalibrate(std::function<void()> done) {
    screen = SCR_CALIBRATE;
    calDone = done;
    calStep = 0;
    drawCalibrate();
}

static void calibrateTap(int rawX, int rawY) {
    calRaw[calStep][0] = rawX;
    calRaw[calStep][1] = rawY;
    if (++calStep < 4) {
        drawCalibrate();
        return;
    }

    // Average the two readings on each edge, then extrapolate to the screen edges
    float left = (calRaw[0][0] + calRaw[3][0]) / 2.0f;
    float right = (calRaw[1][0] + calRaw[2][0]) / 2.0f;
    float top = (calRaw[0][1] + calRaw[1][1]) / 2.0f;
    float bottom = (calRaw[2][1] + calRaw[3][1]) / 2.0f;

    if (fabsf(right - left) < 800 || fabsf(bottom - top) < 600) {
        drawMessage("That didn't look right", "Let's try again");
        delay(1500);
        calStep = 0;
        drawCalibrate();
        return;
    }

    float xPerPx = (right - left) / (SCREEN_W - 2 * CAL_INSET);
    float yPerPx = (bottom - top) / (SCREEN_H - 2 * CAL_INSET);
    settings.touch.xMin = left - CAL_INSET * xPerPx;
    settings.touch.xMax = settings.touch.xMin + SCREEN_W * xPerPx;
    settings.touch.yMin = top - CAL_INSET * yPerPx;
    settings.touch.yMax = settings.touch.yMin + SCREEN_H * yPerPx;
    settings.hasTouchCal = true;
    settingsSave();
    Serial.printf("touch calibrated: x %d..%d, y %d..%d\n", settings.touch.xMin, settings.touch.xMax,
                  settings.touch.yMin, settings.touch.yMax);
    calDone();
}

// ---------------------------------------------------------------------------
// Settings menu

static bool wifiFromMenu = false;

static void showConfirmReset();
static void showDiscover();

static Rect menuRow(int i) {
    return { MARGIN, 38 + i * 48, SCREEN_W - 2 * MARGIN, 40 };
}

static void showMenu() {
    screen = SCR_MENU;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("Settings", true);

    String labels[] = {
        "WiFi: " + (settings.ssid.isEmpty() ? String("not set up") : settings.ssid),
        "Lights (" + String(settings.numLights) + " of " + String(MAX_LIGHTS) + ")",
        "Calibrate touch",
        "Restore defaults",
    };
    for (int i = 0; i < 4; i++) drawButton(menuRow(i), labels[i].c_str(), COLOR_BUTTON, TFT_WHITE, 4);
}

static void menuTap(int x, int y) {
    if (BACK_RECT.contains(x, y)) {
        showMain();
    } else if (menuRow(0).contains(x, y)) {
        wifiFromMenu = true;
        showWifiList(true);
    } else if (menuRow(1).contains(x, y)) {
        showLights();
    } else if (menuRow(2).contains(x, y)) {
        showCalibrate(showMenu);
    } else if (menuRow(3).contains(x, y)) {
        showConfirmReset();
    }
}

// ---------------------------------------------------------------------------
// WiFi setup: network list -> password keyboard -> connecting -> error/success

static const Rect SCAN_RECT = { SCREEN_W - 72, 3, 66, HEADER_H - 6 };
static const Rect CANCEL_RECT = { (SCREEN_W - 120) / 2, 190, 120, 38 };
static const Rect ERR_LEFT_RECT = { MARGIN, 190, 96, 38 };
static const Rect ERR_MID_RECT = { (SCREEN_W - 96) / 2, 190, 96, 38 };
static const Rect ERR_RIGHT_RECT = { SCREEN_W - MARGIN - 96, 190, 96, 38 };
static const uint32_t CONNECT_TIMEOUT_MS = 15000;

static std::vector<Network> networks;
static bool scanning;
static int wifiPage;

static String pendingSsid, pendingPassword;
static bool pendingSecure;
static bool connectIsNew;  // testing new credentials (vs. reconnecting with saved ones)
static uint32_t connectStart;

static void drawSignal(int x, int y, int32_t rssi, uint16_t bg) {
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 4;
        tft.fillRect(x + i * 5, y + 16 - h, 3, h, i < bars ? TFT_WHITE : bg == TFT_BLACK ? COLOR_BUTTON : COLOR_OFF);
    }
}

static void drawLock(int x, int y, uint16_t colour) {
    tft.drawRoundRect(x + 2, y, 7, 9, 3, colour);
    tft.fillRect(x, y + 6, 11, 9, colour);
}

static void drawWifiList() {
    clearList();
    if (scanning) {
        drawMessage("Scanning...");
        return;
    }

    size_t items = networks.size() + 1;  // + "Other..."
    for (int i = 0; i < LIST_ROWS; i++) {
        size_t idx = wifiPage * LIST_ROWS + i;
        if (idx >= items) break;
        Rect r = listRow(i);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, COLOR_BUTTON);
        tft.setTextDatum(ML_DATUM);
        if (idx == networks.size()) {
            tft.setTextColor(TFT_WHITE, COLOR_BUTTON);
            tft.drawString("Other network...", r.x + 10, r.cy(), 4);
            continue;
        }
        const Network &n = networks[idx];
        bool current = n.ssid == settings.ssid && wifiConnected();
        tft.setTextColor(current ? TFT_GREEN : TFT_WHITE, COLOR_BUTTON);
        tft.drawString(fitText(n.ssid, r.w - 60, 4), r.x + 10, r.cy(), 4);
        if (n.secure) drawLock(r.x + r.w - 46, r.cy() - 8, TFT_WHITE);
        drawSignal(r.x + r.w - 28, r.cy() - 8, n.rssi, COLOR_BUTTON);
    }

    drawPager(wifiPage, pageCount(items));
    tft.fillRect(FOOTER_MID_RECT.x, FOOTER_MID_RECT.y, FOOTER_MID_RECT.w, FOOTER_MID_RECT.h, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(String(networks.size()) + " networks", FOOTER_MID_RECT.cx(), FOOTER_MID_RECT.cy(), 2);
}

static void showWifiList(bool rescan) {
    screen = SCR_WIFI_LIST;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("Choose WiFi", true);
    drawButton(SCAN_RECT, "Scan");
    if (rescan) {
        scanning = true;
        wifiPage = 0;
        wifiStartScan();
    }
    drawWifiList();
}

static void leaveWifiSetup() {
    if (!wifiConnected()) wifiConnectSaved();
    if (wifiFromMenu) showMenu();
    else showMain();
}

static void showConnecting() {
    screen = SCR_CONNECTING;
    connectStart = millis();
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("WiFi", false);
    drawMessage("Connecting to", pendingSsid.c_str());
    drawButton(CANCEL_RECT, "Cancel");
}

static void startConnect(const String &ssid, const String &password, bool isNew) {
    pendingSsid = ssid;
    pendingPassword = password;
    connectIsNew = isNew;
    wifiConnect(ssid, password);
    showConnecting();
}

static void askPassword(const String &initial) {
    KeyboardOptions opts;
    opts.title = "Password for " + pendingSsid;
    opts.initial = initial;
    opts.maxLen = PASSWORD_MAX_LEN;
    opts.password = true;
    openKeyboard(opts, [](bool ok, const String &text) {
        if (ok) startConnect(pendingSsid, text, true);
        else showWifiList(false);
    });
}

static void askHiddenNetwork() {
    KeyboardOptions opts;
    opts.title = "Network name";
    opts.maxLen = SSID_MAX_LEN;
    openKeyboard(opts, [](bool ok, const String &text) {
        if (!ok || text.isEmpty()) {
            showWifiList(false);
            return;
        }
        pendingSsid = text;
        pendingSecure = true;
        KeyboardOptions popts;
        popts.title = "Password (blank if none)";
        popts.maxLen = PASSWORD_MAX_LEN;
        popts.password = true;
        openKeyboard(popts, [](bool ok, const String &pw) {
            if (ok) startConnect(pendingSsid, pw, true);
            else showWifiList(false);
        });
    });
}

static void wifiListTap(int x, int y) {
    if (BACK_RECT.contains(x, y)) {
        leaveWifiSetup();
        return;
    }
    if (SCAN_RECT.contains(x, y)) {
        showWifiList(true);
        return;
    }
    if (scanning) return;

    size_t items = networks.size() + 1;
    if (PREV_RECT.contains(x, y) && wifiPage > 0) {
        wifiPage--;
        drawWifiList();
        return;
    }
    if (NEXT_RECT.contains(x, y) && wifiPage < pageCount(items) - 1) {
        wifiPage++;
        drawWifiList();
        return;
    }

    int idx = listHit(x, y, wifiPage, items);
    if (idx < 0) return;
    if (idx == (int)networks.size()) {
        askHiddenNetwork();
        return;
    }
    const Network &n = networks[idx];
    pendingSsid = n.ssid;
    pendingSecure = n.secure;
    if (n.secure) askPassword(n.ssid == settings.ssid ? settings.password : "");
    else startConnect(n.ssid, "", true);
}

static void wifiListTick() {
    if (scanning && wifiScanDone()) {
        networks = wifiScanResults();
        scanning = false;
        drawWifiList();
    }
}

static void showWifiError() {
    screen = SCR_WIFI_ERROR;
    String reason = wifiFailReason();
    // Stop retrying the new credentials and fall back to the saved network
    if (connectIsNew) wifiConnectSaved();

    tft.fillScreen(TFT_BLACK);
    drawTitleBar("WiFi", false);
    drawMessage("Couldn't connect to", pendingSsid.c_str(), reason.c_str());
    if (connectIsNew) {
        if (pendingSecure) drawButton(ERR_LEFT_RECT, "Password");
        drawButton(ERR_RIGHT_RECT, "Networks");
    } else {
        drawButton(ERR_LEFT_RECT, "Retry");
        drawButton(ERR_MID_RECT, "Change WiFi");
        drawButton(ERR_RIGHT_RECT, "Continue");
    }
}

static void connectingTap(int x, int y) {
    if (!CANCEL_RECT.contains(x, y)) return;
    if (connectIsNew) {
        wifiConnectSaved();
        showWifiList(false);
    } else {
        showMain();
    }
}

static void connectingTick() {
    if (wifiConnected()) {
        if (connectIsNew) {
            settings.ssid = pendingSsid;
            settings.password = pendingPassword;
            settingsSave();
        }
        showMain();
    } else if (millis() - connectStart > CONNECT_TIMEOUT_MS) {
        showWifiError();
    }
}

static void wifiErrorTap(int x, int y) {
    if (connectIsNew) {
        if (pendingSecure && ERR_LEFT_RECT.contains(x, y)) askPassword(pendingPassword);
        else if (ERR_RIGHT_RECT.contains(x, y)) showWifiList(false);
    } else {
        if (ERR_LEFT_RECT.contains(x, y)) {
            startConnect(settings.ssid, settings.password, false);
        } else if (ERR_MID_RECT.contains(x, y)) {
            wifiFromMenu = false;
            showWifiList(true);
        } else if (ERR_RIGHT_RECT.contains(x, y)) {
            showMain();  // keeps retrying in the background
        }
    }
}

static void wifiErrorTick() {
    // Saved network came back (e.g. the router finished booting)
    if (!connectIsNew && wifiConnected()) showMain();
}

// ---------------------------------------------------------------------------
// Light management

static const Rect FIND_RECT = { MARGIN, 200, 148, 34 };
static const Rect ADD_RECT = { SCREEN_W - MARGIN - 148, 200, 148, 34 };
static const Rect EDIT_RECTS[] = {
    { MARGIN, 80, 148, 44 },                      // rename
    { SCREEN_W - MARGIN - 148, 80, 148, 44 },     // change address
    { MARGIN, 136, 148, 44 },                     // move up
    { SCREEN_W - MARGIN - 148, 136, 148, 44 },    // remove
};
static size_t editIndex;

static void applyLightChanges() {
    settingsSave();
    lightsRestart();
}

static void drawLightRow(const Rect &r, const String &name, const String &host, int checkbox) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, COLOR_BUTTON);
    int x = r.x + 10;
    if (checkbox >= 0) {
        tft.drawRect(x, r.cy() - 9, 18, 18, TFT_WHITE);
        if (checkbox) tft.fillRect(x + 4, r.cy() - 5, 10, 10, TFT_GREEN);
        x += 28;
    }
    tft.setTextColor(TFT_LIGHTGREY, COLOR_BUTTON);
    tft.setTextDatum(MR_DATUM);
    int hostW = tft.drawString(host, r.x + r.w - 10, r.cy(), 2);
    tft.setTextColor(TFT_WHITE, COLOR_BUTTON);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(fitText(name, r.x + r.w - x - hostW - 20, 4), x, r.cy(), 4);
}

static void showLights() {
    screen = SCR_LIGHTS;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("Lights", true);
    if (settings.numLights == 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("No lights yet", SCREEN_W / 2, 110, 4);
    }
    for (size_t i = 0; i < settings.numLights; i++) {
        drawLightRow(listRow(i), settings.lights[i].name, settings.lights[i].host, -1);
    }
    bool full = settings.numLights >= MAX_LIGHTS;
    drawButton(FIND_RECT, "Find lights");
    drawButton(ADD_RECT, "Add by address", full ? COLOR_OFF : COLOR_BUTTON, full ? TFT_DARKGREY : TFT_WHITE);
}

static void showLightEdit(size_t i) {
    screen = SCR_LIGHT_EDIT;
    editIndex = i;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar(settings.lights[i].name.c_str(), true);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Address: " + settings.lights[i].host, MARGIN, 52, 2);
    drawButton(EDIT_RECTS[0], "Rename");
    drawButton(EDIT_RECTS[1], "Change address");
    if (i > 0) drawButton(EDIT_RECTS[2], "Move up");
    drawButton(EDIT_RECTS[3], "Remove", COLOR_UNKNOWN);
}

static void addByAddress() {
    KeyboardOptions opts;
    opts.title = "Light address (e.g. 192.168.1.50)";
    opts.maxLen = HOST_MAX_LEN;
    opts.startSymbols = true;
    openKeyboard(opts, [](bool ok, const String &text) {
        String host = text;
        host.trim();
        if (ok && !host.isEmpty() && settingsFindLight(host) < 0) {
            tft.fillScreen(TFT_BLACK);
            drawTitleBar("Lights", false);
            drawMessage("Checking", host.c_str());
            String name = lightsFetchName(host);
            if (name.isEmpty()) name = "Light " + String(settings.numLights + 1);
            settingsAddLight(name, host);
            applyLightChanges();
        }
        showLights();
    });
}

static void lightsTap(int x, int y) {
    if (BACK_RECT.contains(x, y)) {
        showMenu();
    } else if (FIND_RECT.contains(x, y)) {
        showDiscover();
    } else if (ADD_RECT.contains(x, y)) {
        if (settings.numLights < MAX_LIGHTS) addByAddress();
    } else {
        int idx = listHit(x, y, 0, settings.numLights);
        if (idx >= 0) showLightEdit(idx);
    }
}

static void lightEditTap(int x, int y) {
    size_t i = editIndex;
    if (BACK_RECT.contains(x, y)) {
        showLights();
    } else if (EDIT_RECTS[0].contains(x, y)) {
        KeyboardOptions opts;
        opts.title = "Name";
        opts.initial = settings.lights[i].name;
        opts.maxLen = NAME_MAX_LEN;
        openKeyboard(opts, [i](bool ok, const String &text) {
            if (ok && !text.isEmpty()) {
                settings.lights[i].name = text;
                settingsSave();
            }
            showLightEdit(i);
        });
    } else if (EDIT_RECTS[1].contains(x, y)) {
        KeyboardOptions opts;
        opts.title = "Address";
        opts.initial = settings.lights[i].host;
        opts.maxLen = HOST_MAX_LEN;
        opts.startSymbols = true;
        openKeyboard(opts, [i](bool ok, const String &text) {
            String host = text;
            host.trim();
            if (ok && !host.isEmpty()) {
                settings.lights[i].host = host;
                applyLightChanges();
            }
            showLightEdit(i);
        });
    } else if (EDIT_RECTS[2].contains(x, y) && i > 0) {
        settingsSwapLights(i, i - 1);
        applyLightChanges();
        showLightEdit(i - 1);
    } else if (EDIT_RECTS[3].contains(x, y)) {
        settingsRemoveLight(i);
        applyLightChanges();
        showLights();
    }
}

// ---------------------------------------------------------------------------
// Discovery: tick WLED controllers found on the network

static std::vector<FoundLight> found;
static int discoverPage;

static void drawDiscoverList() {
    clearList();
    if (found.empty()) {
        drawMessage("No WLED lights found");
    }
    for (int i = 0; i < LIST_ROWS; i++) {
        size_t idx = discoverPage * LIST_ROWS + i;
        if (idx >= found.size()) break;
        bool selected = settingsFindLight(found[idx].host) >= 0;
        drawLightRow(listRow(i), found[idx].name, found[idx].host, selected);
    }
    drawPager(discoverPage, pageCount(found.size()));

    String label = "Search again (" + String(settings.numLights) + "/" + String(MAX_LIGHTS) + ")";
    drawButton(FOOTER_MID_RECT, label.c_str());
}

static void showDiscover() {
    screen = SCR_DISCOVER;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("Find lights", true);
    if (!wifiConnected()) {
        found.clear();
        drawMessage("Not connected to WiFi");
        return;
    }
    drawMessage("Searching...");
    found = lightsDiscover();
    discoverPage = 0;
    drawDiscoverList();
}

static void discoverTap(int x, int y) {
    if (BACK_RECT.contains(x, y)) {
        showLights();
        return;
    }
    if (!wifiConnected()) return;
    if (FOOTER_MID_RECT.contains(x, y)) {
        showDiscover();
        return;
    }
    if (PREV_RECT.contains(x, y) && discoverPage > 0) {
        discoverPage--;
        drawDiscoverList();
        return;
    }
    if (NEXT_RECT.contains(x, y) && discoverPage < pageCount(found.size()) - 1) {
        discoverPage++;
        drawDiscoverList();
        return;
    }

    int idx = listHit(x, y, discoverPage, found.size());
    if (idx < 0) return;
    int existing = settingsFindLight(found[idx].host);
    if (existing >= 0) {
        settingsRemoveLight(existing);
    } else if (!settingsAddLight(found[idx].name, found[idx].host)) {
        return;  // already at MAX_LIGHTS
    }
    applyLightChanges();
    drawDiscoverList();
}

// ---------------------------------------------------------------------------
// Factory reset

static const Rect RESET_CANCEL_RECT = { MARGIN, 190, 148, 40 };
static const Rect RESET_OK_RECT = { SCREEN_W - MARGIN - 148, 190, 148, 40 };

static void showConfirmReset() {
    screen = SCR_CONFIRM_RESET;
    tft.fillScreen(TFT_BLACK);
    drawTitleBar("Restore defaults", true);
    drawMessage("Undo all changes made", "on this device and use", "the flashed settings?");
    drawButton(RESET_CANCEL_RECT, "Cancel");
    drawButton(RESET_OK_RECT, "Restore", COLOR_UNKNOWN);
}

static void confirmResetTap(int x, int y) {
    if (BACK_RECT.contains(x, y) || RESET_CANCEL_RECT.contains(x, y)) {
        showMenu();
    } else if (RESET_OK_RECT.contains(x, y)) {
        settingsFactoryReset();
        drawMessage("Restarting...");
        delay(500);
        ESP.restart();
    }
}

// ---------------------------------------------------------------------------

static void afterCalibration() {
    if (settings.ssid.isEmpty()) {
        wifiFromMenu = false;
        showWifiList(true);
    } else if (wifiConnected()) {
        showMain();
    } else {
        pendingSsid = settings.ssid;
        pendingPassword = settings.password;
        connectIsNew = false;
        showConnecting();
    }
}

void uiBegin() {
    if (!settings.hasTouchCal) showCalibrate(afterCalibration);
    else afterCalibration();
}

void uiLoop() {
    int rawX, rawY;
    if (touchPoll(rawX, rawY)) {
        if (screen == SCR_CALIBRATE) {
            calibrateTap(rawX, rawY);
        } else {
            int x, y;
            touchToScreen(rawX, rawY, x, y);
            switch (screen) {
                case SCR_MAIN:          mainTap(x, y); break;
                case SCR_MENU:          menuTap(x, y); break;
                case SCR_WIFI_LIST:     wifiListTap(x, y); break;
                case SCR_CONNECTING:    connectingTap(x, y); break;
                case SCR_WIFI_ERROR:    wifiErrorTap(x, y); break;
                case SCR_KEYBOARD:      keyboardTap(x, y); break;
                case SCR_LIGHTS:        lightsTap(x, y); break;
                case SCR_LIGHT_EDIT:    lightEditTap(x, y); break;
                case SCR_DISCOVER:      discoverTap(x, y); break;
                case SCR_CONFIRM_RESET: confirmResetTap(x, y); break;
                default: break;
            }
        }
    }

    switch (screen) {
        case SCR_MAIN:       mainTick(); break;
        case SCR_WIFI_LIST:  wifiListTick(); break;
        case SCR_CONNECTING: connectingTick(); break;
        case SCR_WIFI_ERROR: wifiErrorTick(); break;
        default: break;
    }
}
