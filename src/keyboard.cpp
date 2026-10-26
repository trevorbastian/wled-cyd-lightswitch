#include "keyboard.h"

#include <vector>

#include "display.h"

// Layout: 10 key units across, 5 rows of 32px keys below the text field
static const int UNIT = SCREEN_W / 10;
static const int KEY_GAP = 3;
static const int KEYS_TOP = 60;
static const int ROW_PITCH = 36;
static const int KEY_H = ROW_PITCH - 4;
static const int TITLE_H = 22;
static const Rect FIELD = { 4, 26, SCREEN_W - 8, 30 };

static const uint16_t COLOR_KEY = 0x4208;
static const uint16_t COLOR_KEY_SPECIAL = 0x2945;
static const uint16_t COLOR_KEY_ACTIVE = 0x041F;
static const uint16_t COLOR_KEY_PRESSED = TFT_WHITE;

enum Layer { LOWER, UPPER, CAPS, SYM1, SYM2 };

// Rows 0-2 are full rows; row 3 sits between Shift and Delete
static const char *const LAYOUTS[][4] = {
    /* LOWER */ { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" },
    /* UPPER */ { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" },
    /* CAPS  */ { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" },
    /* SYM1  */ { "1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\",.?/\\" },
    /* SYM2  */ { "1234567890", "~`|<>!@#$%", "^&*()-_=+;", ":'\",.?/" },
};

enum Action { KEY_CHAR, KEY_SHIFT, KEY_DELETE, KEY_LAYER, KEY_SPACE, KEY_SHOW, KEY_CANCEL, KEY_OK };

struct Key {
    Rect r;
    Action action;
    char ch;
};

static KeyboardOptions options;
static KeyboardDone onDone;
static String text;
static Layer layer;
static bool hidden;
static std::vector<Key> keys;

static Rect keyRect(int row, float unitX, float units) {
    return { (int)(unitX * UNIT) + 1, KEYS_TOP + row * ROW_PITCH, (int)(units * UNIT) - KEY_GAP, KEY_H };
}

static void buildKeys() {
    keys.clear();
    const char *const *rows = LAYOUTS[layer];

    for (int row = 0; row < 3; row++) {
        int n = strlen(rows[row]);
        float offset = (10 - n) / 2.0f;
        for (int i = 0; i < n; i++) keys.push_back({ keyRect(row, offset + i, 1), KEY_CHAR, rows[row][i] });
    }

    keys.push_back({ keyRect(3, 0, 1.5), KEY_SHIFT, 0 });
    for (int i = 0; rows[3][i]; i++) keys.push_back({ keyRect(3, 1.5 + i, 1), KEY_CHAR, rows[3][i] });
    keys.push_back({ keyRect(3, 8.5, 1.5), KEY_DELETE, 0 });

    keys.push_back({ keyRect(4, 0, 1.5), KEY_LAYER, 0 });
    if (options.password) {
        keys.push_back({ keyRect(4, 1.5, 3.5), KEY_SPACE, ' ' });
        keys.push_back({ keyRect(4, 5, 1.5), KEY_SHOW, 0 });
    } else {
        keys.push_back({ keyRect(4, 1.5, 5), KEY_SPACE, ' ' });
    }
    keys.push_back({ keyRect(4, 6.5, 1.5), KEY_CANCEL, 0 });
    keys.push_back({ keyRect(4, 8, 2), KEY_OK, 0 });
}

static bool isSymbols() { return layer == SYM1 || layer == SYM2; }

static void drawKey(const Key &k, bool pressed) {
    String label;
    uint16_t bg = COLOR_KEY_SPECIAL;
    int font = 2;
    switch (k.action) {
        case KEY_CHAR:   label = String(k.ch); bg = COLOR_KEY; font = 4; break;
        case KEY_SPACE:  label = "space"; bg = COLOR_KEY; break;
        case KEY_DELETE: label = "Del"; break;
        case KEY_CANCEL: label = "Cancel"; break;
        case KEY_OK:     label = "OK"; bg = COLOR_OK; break;
        case KEY_SHOW:   label = hidden ? "Show" : "Hide"; break;
        case KEY_LAYER:  label = isSymbols() ? "abc" : "?123"; break;
        case KEY_SHIFT:
            if (isSymbols()) {
                label = layer == SYM1 ? "1/2" : "2/2";
            } else {
                label = layer == CAPS ? "CAPS" : "Shift";
                if (layer != LOWER) bg = COLOR_KEY_ACTIVE;
            }
            break;
    }
    if (pressed) bg = COLOR_KEY_PRESSED;
    uint16_t fg = pressed ? TFT_BLACK : TFT_WHITE;

    tft.fillRoundRect(k.r.x, k.r.y, k.r.w, k.r.h, 4, bg);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, k.r.cx(), k.r.cy() + (font == 4 ? 1 : 0), font);
}

static void drawKeys() {
    tft.fillRect(0, KEYS_TOP, SCREEN_W, SCREEN_H - KEYS_TOP, TFT_BLACK);
    for (const Key &k : keys) drawKey(k, false);
}

static void drawField() {
    String shown = text;
    if (options.password && hidden) {
        shown = "";
        for (size_t i = 0; i < text.length(); i++) shown += '*';
    }
    // Show the end of long text, where the user is typing
    int maxW = FIELD.w - 14;
    while (shown.length() > 0 && tft.textWidth(shown, 4) > maxW) shown.remove(0, 1);

    tft.fillRect(FIELD.x, FIELD.y, FIELD.w, FIELD.h, TFT_BLACK);
    tft.drawRect(FIELD.x, FIELD.y, FIELD.w, FIELD.h, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(shown, FIELD.x + 5, FIELD.cy() + 1, 4);
    int cursorX = FIELD.x + 6 + tft.textWidth(shown, 4);
    tft.fillRect(cursorX, FIELD.y + 4, 2, FIELD.h - 8, TFT_CYAN);
}

static void setLayer(Layer l) {
    layer = l;
    buildKeys();
    drawKeys();
}

void keyboardOpen(const KeyboardOptions &opts, KeyboardDone done) {
    options = opts;
    onDone = done;
    text = opts.initial;
    hidden = false;  // typing on a small resistive screen is hard enough without masking
    layer = opts.startSymbols ? SYM1 : LOWER;
    buildKeys();

    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCREEN_W, TITLE_H, COLOR_HEADER);
    tft.setTextColor(TFT_WHITE, COLOR_HEADER);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(fitText(opts.title, SCREEN_W - 2 * MARGIN, 2), MARGIN, TITLE_H / 2, 2);
    drawField();
    drawKeys();
}

static void finish(bool ok) {
    // Copy first: the callback may open another keyboard, replacing onDone
    KeyboardDone done = onDone;
    String result = text;
    done(ok, result);
}

void keyboardTap(int x, int y) {
    for (const Key &k : keys) {
        if (!k.r.contains(x, y)) continue;

        drawKey(k, true);
        delay(60);
        drawKey(k, false);

        switch (k.action) {
            case KEY_CHAR:
            case KEY_SPACE:
                if (text.length() < options.maxLen) {
                    text += k.ch;
                    drawField();
                }
                if (layer == UPPER) setLayer(LOWER);  // one-shot shift
                break;
            case KEY_DELETE:
                if (text.length()) {
                    text.remove(text.length() - 1);
                    drawField();
                }
                break;
            case KEY_SHIFT:
                // Letters: Shift -> CAPS lock -> off. Symbols: page 1 <-> 2
                switch (layer) {
                    case LOWER: setLayer(UPPER); break;
                    case UPPER: setLayer(CAPS); break;
                    case CAPS:  setLayer(LOWER); break;
                    case SYM1:  setLayer(SYM2); break;
                    case SYM2:  setLayer(SYM1); break;
                }
                break;
            case KEY_LAYER:
                setLayer(isSymbols() ? LOWER : SYM1);
                break;
            case KEY_SHOW:
                hidden = !hidden;
                drawField();
                drawKey(k, false);
                break;
            case KEY_CANCEL:
                finish(false);
                break;
            case KEY_OK:
                finish(true);
                break;
        }
        return;
    }
}
