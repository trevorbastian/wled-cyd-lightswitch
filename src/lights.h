// Live connections to the configured WLED controllers.
//
// Keeps a WebSocket open to each one (ws://<host>/ws). WLED pushes its full
// state over it whenever anything changes, and we send JSON state commands
// (e.g. {"on":"t"} to toggle) back the same way.
#pragma once

#include <Arduino.h>
#include <vector>

#include "settings.h"

enum LightState { STATE_UNKNOWN, STATE_OFF, STATE_ON };

struct LightStatus {
    LightState state = STATE_UNKNOWN;
    uint8_t r = 255, g = 160, b = 0;  // primary colour of segment 0
    bool dirty = true;                // set on change; cleared by the UI after redrawing
};

extern LightStatus lightStatus[MAX_LIGHTS];

// (Re)connects to every light in settings; call after changing the light list
void lightsRestart();
void lightsLoop();

void lightsToggle(size_t i);
// Sets every light to this colour (turning it on, with the Solid effect so the
// colour isn't masked by an animation)
void lightsSetColour(uint8_t r, uint8_t g, uint8_t b);

struct FoundLight {
    String name;
    String host;
};

// Finds WLED controllers on the network via mDNS. Blocks for a few seconds.
std::vector<FoundLight> lightsDiscover();
// Asks a WLED controller for its configured name; empty if unreachable
String lightsFetchName(const String &host);
