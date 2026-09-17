#pragma once
#include <Arduino.h>

// Extended Italian HID keymap.
//
// The Arduino-ESP32 core ships a built-in KeyboardLayout_it_IT table, but it
// only covers the 7-bit ASCII range (see USBHIDKeyboard.h / it_IT.cpp in the
// framework). Accented vowels (à è é ì ò ù), ç, °, §, £ and the euro sign are
// NOT ASCII and are not in that table, so USBHIDKeyboard::press()/write()
// cannot type them (and indexing that table past 127 would read out of
// bounds). This module fills that gap by sending raw HID usage codes
// (USBHIDKeyboard::pressRaw/releaseRaw) that reproduce exactly what a
// physical Italian keyboard sends for those characters, on a PC whose OS
// keyboard layout is set to Italian.
//
// Uppercase accented vowels (À È É Ì Ò Ù) don't have a dedicated HID usage:
// on a real Italian keyboard/OS layout they are produced by toggling
// CapsLock and pressing the accented key (this is standard behavior of the
// Windows/Linux "Italian" keyboard layout driver). This module tracks the
// host's actual CapsLock LED state (via the keyboard's OUTPUT report) and
// temporarily toggles it when needed, restoring it afterwards.

void itKeymapBegin();

// Types a single Unicode code point using the Italian layout rules above.
void itKeymapTypeCodepoint(uint32_t codepoint);
