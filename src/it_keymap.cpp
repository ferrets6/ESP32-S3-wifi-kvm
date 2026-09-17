#include "it_keymap.h"
#include "keyboard_impl.h"
#include <initializer_list>

namespace {

volatile bool capsLockOn = false;

#ifndef KVM_KEYBOARD_ONLY
void keyboardEventCallback(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (id == ARDUINO_USB_HID_KEYBOARD_LED_EVENT) {
    auto *led = (arduino_usb_hid_keyboard_event_data_t *)data;
    capsLockOn = led->capslock;
  }
}
#endif

void tapRaw(uint8_t keycode) {
  Keyboard.pressRaw(keycode);
  Keyboard.releaseRaw(keycode);
}

void toggleCapsLock() {
  tapRaw(HID_KEY_CAPS_LOCK);
  capsLockOn = !capsLockOn;
  delay(15);  // give the host time to actually apply the toggle
}

// Presses `keycode` together with the given modifiers (raw HID modifier
// usage codes, e.g. HID_KEY_SHIFT_LEFT / HID_KEY_ALT_RIGHT), then releases
// everything in reverse order.
void tapWithMods(uint8_t keycode, std::initializer_list<uint8_t> mods) {
  for (uint8_t m : mods) {
    Keyboard.pressRaw(m);
  }
  Keyboard.pressRaw(keycode);
  Keyboard.releaseRaw(keycode);
  for (uint8_t m : mods) {
    Keyboard.releaseRaw(m);
  }
}

enum KeyMode {
  MODE_NONE,
  MODE_SHIFT,
  MODE_ALTGR,
  MODE_ALTGR_SHIFT,
  MODE_CAPS,
  MODE_CAPS_SHIFT,
};

struct ExtKey {
  uint32_t codepoint;
  uint8_t hidKey;
  KeyMode mode;
};

// Mapping reverse-engineered from the physical Italian ISO keyboard layout,
// cross-checked against the framework's KeyboardLayout_it_IT.cpp ASCII table
// (same physical keys, ASCII half of each key already handled by that table).
const ExtKey EXT_TABLE[] = {
  // e' / [ key
  {0x00E8 /* è */, HID_KEY_BRACKET_LEFT, MODE_NONE},
  {0x00E9 /* é */, HID_KEY_BRACKET_LEFT, MODE_SHIFT},
  {0x00C8 /* È */, HID_KEY_BRACKET_LEFT, MODE_CAPS},
  {0x00C9 /* É */, HID_KEY_BRACKET_LEFT, MODE_CAPS_SHIFT},
  {0x007B /* { */, HID_KEY_BRACKET_LEFT, MODE_ALTGR_SHIFT},

  // ] key
  {0x007D /* } */, HID_KEY_BRACKET_RIGHT, MODE_ALTGR_SHIFT},

  // o' / ; key
  {0x00F2 /* ò */, HID_KEY_SEMICOLON, MODE_NONE},
  {0x00E7 /* ç */, HID_KEY_SEMICOLON, MODE_SHIFT},
  {0x00D2 /* Ò */, HID_KEY_SEMICOLON, MODE_CAPS},
  {0x00C7 /* Ç */, HID_KEY_SEMICOLON, MODE_CAPS_SHIFT},

  // a' / ' key
  {0x00E0 /* à */, HID_KEY_APOSTROPHE, MODE_NONE},
  {0x00B0 /* ° */, HID_KEY_APOSTROPHE, MODE_SHIFT},
  {0x00C0 /* À */, HID_KEY_APOSTROPHE, MODE_CAPS},

  // i' / = key
  {0x00EC /* ì */, HID_KEY_EQUAL, MODE_NONE},
  {0x00CC /* Ì */, HID_KEY_EQUAL, MODE_CAPS},

  // u' / (ISO extra) key
  {0x00F9 /* ù */, HID_KEY_BACKSLASH, MODE_NONE},
  {0x00A7 /* § */, HID_KEY_BACKSLASH, MODE_SHIFT},
  {0x00D9 /* Ù */, HID_KEY_BACKSLASH, MODE_CAPS},

  // '3' key, shifted -> pound sign (not '#' like US)
  {0x00A3 /* £ */, HID_KEY_3, MODE_SHIFT},

  // AltGr+E -> euro sign
  {0x20AC /* € */, HID_KEY_E, MODE_ALTGR},
};

bool typeFromExtTable(uint32_t cp) {
  for (const ExtKey &k : EXT_TABLE) {
    if (k.codepoint != cp) {
      continue;
    }
    switch (k.mode) {
      case MODE_NONE: tapRaw(k.hidKey); break;
      case MODE_SHIFT: tapWithMods(k.hidKey, {HID_KEY_SHIFT_LEFT}); break;
      case MODE_ALTGR: tapWithMods(k.hidKey, {HID_KEY_ALT_RIGHT}); break;
      case MODE_ALTGR_SHIFT: tapWithMods(k.hidKey, {HID_KEY_ALT_RIGHT, HID_KEY_SHIFT_LEFT}); break;
      case MODE_CAPS:
      case MODE_CAPS_SHIFT: {
        bool wasOn = capsLockOn;
        if (!wasOn) {
          toggleCapsLock();
        }
        if (k.mode == MODE_CAPS_SHIFT) {
          tapWithMods(k.hidKey, {HID_KEY_SHIFT_LEFT});
        } else {
          tapRaw(k.hidKey);
        }
        if (!wasOn) {
          toggleCapsLock();
        }
        break;
      }
    }
    return true;
  }
  return false;
}

}  // namespace

void itKeymapBegin() {
#ifndef KVM_KEYBOARD_ONLY
  Keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, keyboardEventCallback);
#endif
  // BootKeyboard (KVM_KEYBOARD_ONLY build) has no such event: capsLockOn is
  // tracked purely by toggleCapsLock() below, which is the only thing that
  // ever toggles it in this KVM (no other keyboard input source exists).
}

void itKeymapTypeCodepoint(uint32_t cp) {
  if (cp == '\n') {
    Keyboard.write(KEY_RETURN);
    return;
  }
  if (cp == '\r') {
    return;  // ignore, \n already produces Enter
  }
  // Extended table first: a few characters are < 0x80 (e.g. { and }) but
  // need an AltGr+Shift combo the framework's built-in it_IT ASCII table
  // can't encode (it only has one modifier bit) and explicitly leaves
  // unmapped -- routing those through the ASCII fallback below would
  // silently send nothing. Checking the (short) extended table first for
  // every codepoint costs nothing measurable and avoids that trap.
  if (typeFromExtTable(cp)) {
    return;
  }
  if (cp < 0x80) {
    // Base ASCII: the framework's built-in Italian layout handles this
    // correctly (letters, digits, common punctuation).
    Keyboard.write((uint8_t)cp);
  }
  // Anything else (>= 0x80 and not in the extended table): unsupported,
  // skip it rather than typing garbage.
}
