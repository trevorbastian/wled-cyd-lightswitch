#include "wifi_mgr.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>

#include "settings.h"

static const char *HOSTNAME = "wled-remote";

static volatile uint8_t lastReason = 0;
static volatile bool gotIp = false;
static bool mdnsStarted = false;

void wifiBegin() {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            // ASSOC_LEAVE is our own WiFi.disconnect(), not a failure
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason != WIFI_REASON_ASSOC_LEAVE) lastReason = reason;
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            gotIp = true;
        }
    });
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    wifiConnectSaved();
}

void wifiLoop() {
    // mDNS is needed for discovering WLED controllers; start it outside the event task
    if (gotIp && !mdnsStarted) {
        mdnsStarted = MDNS.begin(HOSTNAME);
    }
}

void wifiConnect(const String &ssid, const String &password) {
    lastReason = 0;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
}

void wifiConnectSaved() {
    if (settings.ssid.isEmpty()) {
        WiFi.disconnect();
        return;
    }
    wifiConnect(settings.ssid, settings.password);
}

bool wifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

const char *wifiFailReason() {
    switch (lastReason) {
        case 0:
            return "No response from network";
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "Wrong password?";
        default:
            return "Could not connect";
    }
}

void wifiStartScan() {
    // A pending connection attempt makes scans fail, so stop trying while we scan
    if (!wifiConnected()) WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
}

bool wifiScanDone() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);  // retry
        return false;
    }
    return n >= 0;
}

std::vector<Network> wifiScanResults() {
    std::vector<Network> nets;
    int n = WiFi.scanComplete();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // hidden network
        auto existing = std::find_if(nets.begin(), nets.end(), [&](const Network &x) { return x.ssid == ssid; });
        if (existing != nets.end()) {
            existing->rssi = max(existing->rssi, WiFi.RSSI(i));
            continue;
        }
        nets.push_back({ ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN });
    }
    std::sort(nets.begin(), nets.end(), [](const Network &a, const Network &b) { return a.rssi > b.rssi; });
    return nets;
}
