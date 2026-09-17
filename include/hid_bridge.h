#pragma once
#include <Arduino.h>

// Thin wrapper around the composite USB HID keyboard+mouse device
// (USBHIDKeyboard / USBHIDMouse from the Arduino-ESP32 core, TinyUSB-based,
// NOT the legacy USBHID library) plus the Italian keymap extension.
//
// All text/keys received over the WebSocket are dispatched through this
// module so main.cpp stays free of USB HID details.

void hidBegin();

// Relative mouse move. Values may exceed the HID int8 range (-127..127);
// they are chunked internally into multiple HID reports.
void hidMouseMove(int32_t dx, int32_t dy);

// button: "left" | "right" | "middle"
void hidMouseClick(const String &button);
void hidMouseDown(const String &button);
void hidMouseUp(const String &button);
void hidMouseScroll(int8_t amount);

// Types a UTF-8 encoded string as real HID key press/release events,
// character by character, using the Italian layout (see it_keymap.h).
void hidTypeUtf8(const String &utf8Text);

// name: ENTER | BACKSPACE | TAB | ESC | DELETE | HOME | END |
//       ARROW_UP | ARROW_DOWN | ARROW_LEFT | ARROW_RIGHT |
//       PAGE_UP | PAGE_DOWN | CAPSLOCK | F1..F12
void hidSpecialKey(const String &name);

// name: CTRL_C | CTRL_V | CTRL_X | CTRL_A | CTRL_Z | CTRL_S |
//       ALT_TAB | WIN
void hidCombo(const String &name);

// Generic held-modifier combo for the on-screen keyboard: holds any
// combination of modifiers while pressing one key, then releases
// everything. mods is a comma-separated subset of CTRL/ALT/ALTGR/SHIFT/WIN
// (e.g. "CTRL,SHIFT"). key is either a single literal character (sent as
// typed, e.g. Ctrl+"c") or one of the special names accepted by
// hidSpecialKey (e.g. Ctrl+Shift+"ARROW_LEFT" to select a word left).
void hidModCombo(const String &mods, const String &key);
