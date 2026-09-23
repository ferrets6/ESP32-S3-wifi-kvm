#include "hid_bridge.h"
#include "it_keymap.h"
#include "keyboard_impl.h"
#ifndef KVM_KEYBOARD_ONLY
#include <USBHIDMouse.h>
extern USBHIDMouse Mouse;  // defined in main.cpp
#endif

namespace {

#ifndef KVM_KEYBOARD_ONLY
uint8_t mouseButtonMask(const String &button) {
  if (button == "right") {
    return MOUSE_RIGHT;
  }
  if (button == "middle") {
    return MOUSE_MIDDLE;
  }
  return MOUSE_LEFT;
}
#endif

// Decodes one UTF-8 code point starting at s[i], advances i past it.
// Falls back to 1-byte advance / replacement-skip on malformed input so a
// bad byte never desyncs the rest of the string.
uint32_t decodeUtf8(const uint8_t *s, size_t len, size_t &i) {
  uint8_t b0 = s[i];
  if (b0 < 0x80) {
    i += 1;
    return b0;
  }
  int extra;
  uint32_t cp;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
  } else {
    i += 1;  // invalid leading byte, skip it
    return 0;
  }
  size_t j = i + 1;
  for (int k = 0; k < extra; k++) {
    if (j >= len || (s[j] & 0xC0) != 0x80) {
      i += 1;  // malformed continuation, skip just the lead byte
      return 0;
    }
    cp = (cp << 6) | (s[j] & 0x3F);
    j++;
  }
  i = j;
  return cp;
}

}  // namespace

void hidBegin() {
  Keyboard.begin(KeyboardLayout_it_IT);
#ifndef KVM_KEYBOARD_ONLY
  Mouse.begin();
#endif
  itKeymapBegin();
}

void hidMouseMove(int32_t dx, int32_t dy) {
#ifndef KVM_KEYBOARD_ONLY
  // HID relative mouse reports carry int8_t deltas; chunk larger swipes.
  while (dx != 0 || dy != 0) {
    int8_t stepX = (int8_t)constrain(dx, -127, 127);
    int8_t stepY = (int8_t)constrain(dy, -127, 127);
    Mouse.move(stepX, stepY);
    dx -= stepX;
    dy -= stepY;
  }
#endif
}

void hidMouseClick(const String &button) {
#ifndef KVM_KEYBOARD_ONLY
  Mouse.click(mouseButtonMask(button));
#endif
}

void hidMouseDown(const String &button) {
#ifndef KVM_KEYBOARD_ONLY
  Mouse.press(mouseButtonMask(button));
#endif
}

void hidMouseUp(const String &button) {
#ifndef KVM_KEYBOARD_ONLY
  Mouse.release(mouseButtonMask(button));
#endif
}

void hidMouseScroll(int8_t amount) {
#ifndef KVM_KEYBOARD_ONLY
  Mouse.move(0, 0, amount);
#endif
}

void hidTypeUtf8(const String &utf8Text) {
  const uint8_t *bytes = (const uint8_t *)utf8Text.c_str();
  size_t len = utf8Text.length();
  size_t i = 0;
  while (i < len) {
    uint32_t cp = decodeUtf8(bytes, len, i);
    if (cp != 0) {
      itKeymapTypeCodepoint(cp);
      // The HID interrupt endpoint is polled every 1ms, but TinyUSB only
      // buffers one outgoing report at a time: firing press+release for
      // consecutive characters back-to-back with no gap can overwrite a
      // report that hasn't actually gone out yet, silently dropping
      // characters (observed: typing "hello world" arrived as "hellwod").
      // A few ms per character is imperceptible for the whole-string
      // "kt:" case and keeps every keystroke on the wire.
      delay(25);
    }
  }
}

namespace {

// Shared by hidSpecialKey() and hidModCombo(): resolves one of the special
// key names accepted over the WebSocket protocol to a USBHIDKeyboard KEY_*
// constant. Returns 0 for an unknown name.
uint8_t specialKeyCode(const String &name) {
  if (name == "ENTER") {
    return KEY_RETURN;
  } else if (name == "BACKSPACE") {
    return KEY_BACKSPACE;
  } else if (name == "TAB") {
    return KEY_TAB;
  } else if (name == "ESC") {
    return KEY_ESC;
  } else if (name == "DELETE") {
    return KEY_DELETE;
  } else if (name == "HOME") {
    return KEY_HOME;
  } else if (name == "END") {
    return KEY_END;
  } else if (name == "ARROW_UP") {
    return KEY_UP_ARROW;
  } else if (name == "ARROW_DOWN") {
    return KEY_DOWN_ARROW;
  } else if (name == "ARROW_LEFT") {
    return KEY_LEFT_ARROW;
  } else if (name == "ARROW_RIGHT") {
    return KEY_RIGHT_ARROW;
  } else if (name == "PAGE_UP") {
    return KEY_PAGE_UP;
  } else if (name == "PAGE_DOWN") {
    return KEY_PAGE_DOWN;
  } else if (name == "CAPSLOCK") {
    return KEY_CAPS_LOCK;
  } else if (name.length() >= 2 && name.length() <= 3 && name[0] == 'F') {
    int n = name.substring(1).toInt();
    if (n >= 1 && n <= 12) {
      return KEY_F1 + (n - 1);  // KEY_F1..KEY_F12 are contiguous
    }
  }
  return 0;
}

// Resolves one of the modifier names used by the "kg:" protocol command to
// a raw HID modifier usage code (see USBHIDKeyboard::press/pressRaw).
uint8_t modifierKeyCode(const String &name) {
  if (name == "CTRL") {
    return KEY_LEFT_CTRL;
  } else if (name == "ALT") {
    return KEY_LEFT_ALT;
  } else if (name == "ALTGR") {
    return KEY_RIGHT_ALT;
  } else if (name == "SHIFT") {
    return KEY_LEFT_SHIFT;
  } else if (name == "WIN") {
    return KEY_LEFT_GUI;
  }
  return 0;
}

}  // namespace

void hidSpecialKey(const String &name) {
  uint8_t key = specialKeyCode(name);
  if (!key) {
    return;
  }
  Keyboard.press(key);
  Keyboard.release(key);
}

void hidCombo(const String &name) {
  if (name == "CTRL_C") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('c');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "CTRL_V") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('v');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "CTRL_X") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('x');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "CTRL_A") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('a');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "CTRL_Z") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('z');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "CTRL_S") {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.write('s');
    Keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "ALT_TAB") {
    Keyboard.press(KEY_LEFT_ALT);
    Keyboard.press(KEY_TAB);
    delay(60);
    Keyboard.release(KEY_TAB);
    Keyboard.release(KEY_LEFT_ALT);
  } else if (name == "WIN") {
    Keyboard.press(KEY_LEFT_GUI);
    delay(30);
    Keyboard.release(KEY_LEFT_GUI);
  }
}

void hidModCombo(const String &mods, const String &key) {
  uint8_t modKeys[5];
  size_t modCount = 0;

  int start = 0;
  while (start < (int)mods.length() && modCount < 5) {
    int comma = mods.indexOf(',', start);
    String token = (comma < 0) ? mods.substring(start) : mods.substring(start, comma);
    uint8_t code = modifierKeyCode(token);
    if (code) {
      modKeys[modCount++] = code;
    }
    if (comma < 0) {
      break;
    }
    start = comma + 1;
  }

  for (size_t i = 0; i < modCount; i++) {
    Keyboard.press(modKeys[i]);
  }

  uint8_t special = specialKeyCode(key);
  if (special) {
    Keyboard.press(special);
    Keyboard.release(special);
  } else {
    // Single character (possibly multi-byte UTF-8): decode and type it
    // through the same path as regular typed text.
    const uint8_t *bytes = (const uint8_t *)key.c_str();
    size_t len = key.length();
    size_t i = 0;
    if (i < len) {
      uint32_t cp = decodeUtf8(bytes, len, i);
      if (cp != 0) {
        itKeymapTypeCodepoint(cp);
      }
    }
  }

  for (size_t i = modCount; i > 0; i--) {
    Keyboard.release(modKeys[i - 1]);
  }
}

namespace {
// Modifier state last applied by hidRawKey(), so only bits that actually
// change are sent (one HID report each).
uint8_t rawHeldModifiers = 0;

void applyRawModifiers(uint8_t modifiers) {
  uint8_t changed = modifiers ^ rawHeldModifiers;
  for (uint8_t bit = 0; bit < 8; bit++) {
    if (changed & (1 << bit)) {
      if (modifiers & (1 << bit)) {
        Keyboard.pressRaw(0xE0 + bit);
      } else {
        Keyboard.releaseRaw(0xE0 + bit);
      }
    }
  }
  rawHeldModifiers = modifiers;
}
}  // namespace

void hidRawKey(uint8_t modifiers, uint8_t usage, bool down) {
  if (usage >= 0xE0 && usage <= 0xE7) {
    uint8_t bit = 1 << (usage - 0xE0);
    modifiers = down ? (modifiers | bit) : (modifiers & ~bit);
    usage = 0;
  }
  // Modifiers go down before the key and come up after it, like a real
  // keyboard, so e.g. Shift+A never reaches the host as a bare "a".
  if (down) {
    applyRawModifiers(modifiers);
    if (usage) {
      Keyboard.pressRaw(usage);
    }
  } else {
    if (usage) {
      Keyboard.releaseRaw(usage);
    }
    applyRawModifiers(modifiers);
  }
}

void hidReleaseAllKeys() {
  Keyboard.releaseAll();
  rawHeldModifiers = 0;
}
