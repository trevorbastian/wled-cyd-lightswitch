// Improv Wi-Fi over serial (https://www.improv-wifi.com/serial/).
//
// Lets the browser-based flasher (ESP Web Tools) send WiFi credentials right
// after installing, as an alternative to typing them on the touchscreen.
#pragma once

void improvLoop();
// True while connecting with credentials received from the browser
bool improvProvisioning();
