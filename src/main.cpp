// WLED touch controller for the ESP32-2432S028R ("Cheap Yellow Display").
//
// Turns WLED lights on/off and sets their colour. WiFi, the light list and
// touch calibration are configured on the touchscreen, optionally starting
// from defaults in config.json (see config.example.json).

#include <Arduino.h>

#include "display.h"
#include "lights.h"
#include "settings.h"
#include "ui.h"
#include "wifi_mgr.h"

void setup() {
    Serial.begin(115200);
    settingsLoad();
    displayBegin();
    wifiBegin();
    lightsRestart();
    uiBegin();
}

void loop() {
    wifiLoop();
    lightsLoop();
    uiLoop();
    delay(5);
}
