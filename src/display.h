// Screen, touch input and shared drawing helpers for the ESP32-2432S028R
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int HEADER_H = 30;
static const int MARGIN = 8;

static const uint16_t COLOR_HEADER = TFT_NAVY;
static const uint16_t COLOR_BUTTON = 0x3186;   // mid grey
static const uint16_t COLOR_OFF = 0x2104;      // dark grey
static const uint16_t COLOR_UNKNOWN = 0x7800;  // dark red
static const uint16_t COLOR_OK = 0x0400;       // dark green

extern TFT_eSPI tft;

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

void displayBegin();

// Returns true once per new tap, with the averaged raw touch-controller reading
bool touchPoll(int &rawX, int &rawY);
// Converts a raw reading to screen coordinates using the saved calibration
void touchToScreen(int rawX, int rawY, int &x, int &y);

// Title bar; with back=true a "<" back button is drawn in BACK_RECT
static const Rect BACK_RECT = { 0, 0, 50, HEADER_H };
void drawTitleBar(const char *title, bool back);
void drawButton(const Rect &r, const char *label, uint16_t bg = COLOR_BUTTON,
                uint16_t fg = TFT_WHITE, int font = 2);
void drawGear(int cx, int cy, uint16_t colour, uint16_t bg);
// Text centred in the area below the title bar
void drawMessage(const char *line1, const char *line2 = nullptr, const char *line3 = nullptr);
// Shortens text with ".." so it fits in maxWidth pixels
String fitText(const String &text, int maxWidth, int font);
