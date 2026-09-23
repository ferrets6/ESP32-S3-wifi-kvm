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

// Pure HID keyboard event, with no layout or keymap involved: the target
// OS resolves what the key means, exactly as with a real keyboard. Same
// shape as the Unifying dongle's SEND_KEY (modifier byte + usage + down).
//   modifiers: full HID modifier byte, applied as the new held state
//              (0x01 LCtrl, 0x02 LShift, 0x04 LAlt, 0x08 LGui,
//               0x10 RCtrl, 0x20 RShift, 0x40 RAlt/AltGr, 0x80 RGui)
//   usage:     HID Keyboard/Keypad page usage of the physical key (0x04 = A,
//              0x28 = Enter...); 0xE0-0xE7 act as the matching modifier bit;
//              0 updates only the modifiers
//   down:      true = press (held until the matching up; a repeated down
//              for a held key is ignored), false = release
// Up to 6 non-modifier keys can be held at once (boot report limit).
void hidRawKey(uint8_t modifiers, uint8_t usage, bool down);

// Releases every held key and modifier (e.g. when the client holding them goes away).
void hidReleaseAllKeys();
