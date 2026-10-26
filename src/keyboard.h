// Full-screen on-screen keyboard for text entry
#pragma once

#include <Arduino.h>
#include <functional>

using KeyboardDone = std::function<void(bool ok, const String &text)>;

struct KeyboardOptions {
    String title;
    String initial;
    size_t maxLen;
    bool password = false;      // adds a Show/Hide key
    bool startSymbols = false;  // open on the numbers/symbols layer (e.g. for IP addresses)
};

// Draws the keyboard; `done` is called when OK or Cancel is pressed
void keyboardOpen(const KeyboardOptions &opts, KeyboardDone done);
void keyboardTap(int x, int y);
