// WiFi connection, scanning and failure reporting
#pragma once

#include <Arduino.h>
#include <vector>

struct Network {
    String ssid;
    int32_t rssi;
    bool secure;
};

void wifiBegin();
void wifiLoop();

// Connects using the given credentials (does not save them)
void wifiConnect(const String &ssid, const String &password);
// Reconnects using the saved settings, if any
void wifiConnectSaved();
bool wifiConnected();
// True once connected to this particular network (not still to a previous one)
bool wifiConnectedTo(const String &ssid);
// Human-readable reason for the most recent failed connection attempt
const char *wifiFailReason();

// Asynchronous scan; wifiScanDone() returns true once results are ready
void wifiStartScan();
bool wifiScanDone();
// Unique networks, strongest first
std::vector<Network> wifiScanResults();
