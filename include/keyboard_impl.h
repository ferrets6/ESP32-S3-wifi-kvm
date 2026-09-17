#pragma once

// Selects which keyboard HID implementation the rest of the firmware uses.
// Build the KVM_KEYBOARD_ONLY variant (env esp32-s3-supermini-kbonly in
// platformio.ini) to test/use on hosts whose BIOS/UEFI or bootloader needs
// to see the keyboard before an OS is running (see boot_keyboard.h for why
// that needs a different USB descriptor than the normal keyboard+mouse
// composite).
#ifdef KVM_KEYBOARD_ONLY
#include "boot_keyboard.h"
typedef BootKeyboard KeyboardClass;
#else
#include <USBHIDKeyboard.h>
typedef USBHIDKeyboard KeyboardClass;
#endif

extern KeyboardClass Keyboard;
