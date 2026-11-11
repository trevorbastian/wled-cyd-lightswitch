// Screens and navigation: main controls, settings menu, WiFi setup, light
// management, touch calibration and factory reset
#pragma once

#include <Arduino.h>

void uiBegin();
void uiLoop();

// Called by the Improv Serial handler when the browser sends WiFi settings
void uiImprovStarted(const String &ssid);
void uiImprovFinished(bool ok);
