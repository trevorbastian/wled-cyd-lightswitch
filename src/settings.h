// Persistent settings, stored in NVS as JSON using the same schema as config.json.
//
// Precedence: edits made on the device > config.json (embedded at build time)
// > built-in defaults. If config.json changes and is reflashed, it is
// re-applied over any on-device edits.
#pragma once

#include <Arduino.h>

static const size_t MAX_LIGHTS = 4;
static const size_t SSID_MAX_LEN = 32;
static const size_t PASSWORD_MAX_LEN = 63;
static const size_t NAME_MAX_LEN = 24;
static const size_t HOST_MAX_LEN = 63;

struct LightConfig {
    String name;
    String host;  // IP address or hostname of the WLED controller
};

// Raw XPT2046 readings at the screen edges (landscape)
struct TouchCal {
    int xMin = 200, xMax = 3700, yMin = 240, yMax = 3800;
};

struct Settings {
    String ssid;
    String password;
    LightConfig lights[MAX_LIGHTS];
    size_t numLights = 0;
    bool hasTouchCal = false;  // false until calibrated or set in config.json
    TouchCal touch;
};

extern Settings settings;

void settingsLoad();
void settingsSave();
// Restores config.json values (or built-in defaults if there was none)
void settingsFactoryReset();

bool settingsAddLight(const String &name, const String &host);
void settingsRemoveLight(size_t i);
void settingsSwapLights(size_t a, size_t b);
int settingsFindLight(const String &host);  // index or -1
