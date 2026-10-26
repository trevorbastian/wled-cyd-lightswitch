#include "display.h"

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "settings.h"

// Touch controller is on its own SPI bus on the CYD
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Resistive panels flicker while pressed; ignore releases shorter than this
static const uint32_t RELEASE_DEBOUNCE_MS = 60;

TFT_eSPI tft;
static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

void displayBegin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSpi);
    touch.setRotation(1);
}

bool touchPoll(int &rawX, int &rawY) {
    static bool held = false;
    static uint32_t lastTouched = 0;

    if (touch.touched()) {
        lastTouched = millis();
        if (held) return false;
        held = true;

        // Let the reading settle, then average a few samples
        delay(15);
        long sx = 0, sy = 0;
        int n = 0;
        for (int i = 0; i < 8 && touch.touched(); i++) {
            TS_Point p = touch.getPoint();
            sx += p.x;
            sy += p.y;
            n++;
            delay(2);
        }
        if (n == 0) return false;
        rawX = sx / n;
        rawY = sy / n;
        return true;
    }

    if (held && millis() - lastTouched > RELEASE_DEBOUNCE_MS) held = false;
    return false;
}

void touchToScreen(int rawX, int rawY, int &x, int &y) {
    const TouchCal &c = settings.touch;
    x = constrain(map(rawX, c.xMin, c.xMax, 0, SCREEN_W), 0, SCREEN_W - 1);
    y = constrain(map(rawY, c.yMin, c.yMax, 0, SCREEN_H), 0, SCREEN_H - 1);
}

void drawTitleBar(const char *title, bool back) {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
    tft.setTextColor(TFT_WHITE, COLOR_HEADER);
    tft.setTextDatum(ML_DATUM);
    int x = MARGIN;
    if (back) {
        tft.drawString("<", MARGIN, HEADER_H / 2, 4);
        x = BACK_RECT.w;
    }
    tft.drawString(fitText(title, SCREEN_W - x - 60, 4), x, HEADER_H / 2, 4);
}

void drawButton(const Rect &r, const char *label, uint16_t bg, uint16_t fg, int font) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, bg);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, TFT_DARKGREY);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(fitText(label, r.w - 8, font), r.cx(), r.cy(), font);
}

void drawGear(int cx, int cy, uint16_t colour, uint16_t bg) {
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4;
        tft.fillCircle(cx + cos(a) * 8, cy + sin(a) * 8, 3, colour);
    }
    tft.fillCircle(cx, cy, 8, colour);
    tft.fillCircle(cx, cy, 3, bg);
}

void drawMessage(const char *line1, const char *line2, const char *line3) {
    tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    const char *lines[] = { line1, line2, line3 };
    int count = line3 ? 3 : line2 ? 2 : 1;
    int y = (HEADER_H + SCREEN_H) / 2 - (count - 1) * 15;
    for (int i = 0; i < count; i++, y += 30) {
        tft.drawString(fitText(lines[i], SCREEN_W - 2 * MARGIN, 4), SCREEN_W / 2, y, 4);
    }
}

String fitText(const String &text, int maxWidth, int font) {
    if (tft.textWidth(text, font) <= maxWidth) return text;
    String s = text;
    while (s.length() > 1 && tft.textWidth(s + "..", font) > maxWidth) s.remove(s.length() - 1);
    return s + "..";
}
