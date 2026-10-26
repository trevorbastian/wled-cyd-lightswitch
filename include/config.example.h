// Copy this file to config.h and fill in your details.
#pragma once

#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

struct LightConfig {
    const char *name;  // label shown on the button
    const char *host;  // WLED controller IP address or hostname
};

// One button is drawn per entry (up to ~4 fit comfortably).
static const LightConfig LIGHTS[] = {
    { "Living Room", "192.168.1.50" },
    // { "Bedroom",     "192.168.1.51" },
};
