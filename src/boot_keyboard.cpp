#include "boot_keyboard.h"
#include <USB.h>

// Same encoding scheme as USBHIDKeyboard's _asciimap (see
// keyboardLayout/KeyboardLayout.h): values < 0x80 with the SHIFT/ALT_GR
// bits, non-printing keys as KEY_* (0x88+), modifiers as KEY_LEFT_CTRL..
// (0x80-0x87). Reused directly from USBHIDKeyboard.h.
#define SHIFT           0x80
#define ALT_GR          0x40
#define ISO_KEY         0x64
#define ISO_REPLACEMENT 0x32

static const uint8_t report_descriptor[] = {
  // No Report ID: this must be the ONLY HID device registered (see
  // boot_keyboard.h for why), producing the exact spec "boot keyboard"
  // shape with 8-byte, ID-less reports.
  TUD_HID_REPORT_DESC_KEYBOARD()
};

BootKeyboard::BootKeyboard() : hid(HID_ITF_PROTOCOL_KEYBOARD), _asciimap(KeyboardLayout_en_US) {
  memset(&_keyReport, 0, sizeof(_keyReport));
  hid.addDevice(this, sizeof(report_descriptor));
}

uint16_t BootKeyboard::_onGetDescriptor(uint8_t *dst) {
  memcpy(dst, report_descriptor, sizeof(report_descriptor));
  return sizeof(report_descriptor);
}

void BootKeyboard::begin(const uint8_t *layout) {
  _asciimap = layout;
  hid.begin();
}

void BootKeyboard::_onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) {
  // No Report ID in this descriptor, so the single LED output report
  // arrives as report_id==0 with the LED byte at buffer[0] (bit1 = CapsLock,
  // same bit layout USB HID uses everywhere: Numlock/CapsLock/ScrollLock/
  // Compose/Kana).
  if (len >= 1) {
    _capsLockOn = (buffer[0] & 0x02) != 0;
  }
}

void BootKeyboard::sendReport() {
  hid.SendReport(0, &_keyReport, sizeof(_keyReport));
}

size_t BootKeyboard::pressRaw(uint8_t k) {
  uint8_t i;
  if (k >= 0xE0 && k < 0xE8) {
    _keyReport.modifiers |= (1 << (k - 0xE0));
  } else if (k && k < 0xA5) {
    if (_keyReport.keys[0] != k && _keyReport.keys[1] != k && _keyReport.keys[2] != k && _keyReport.keys[3] != k && _keyReport.keys[4] != k
        && _keyReport.keys[5] != k) {
      for (i = 0; i < 6; i++) {
        if (_keyReport.keys[i] == 0x00) {
          _keyReport.keys[i] = k;
          break;
        }
      }
      if (i == 6) {
        return 0;
      }
    }
  } else if (_keyReport.modifiers == 0) {
    return 0;
  }
  sendReport();
  return 1;
}

size_t BootKeyboard::releaseRaw(uint8_t k) {
  uint8_t i;
  if (k >= 0xE0 && k < 0xE8) {
    _keyReport.modifiers &= ~(1 << (k - 0xE0));
  } else if (k && k < 0xA5) {
    for (i = 0; i < 6; i++) {
      if (0 != k && _keyReport.keys[i] == k) {
        _keyReport.keys[i] = 0x00;
      }
    }
  }
  sendReport();
  return 1;
}

size_t BootKeyboard::press(uint8_t k) {
  if (k >= 0x88) {
    k = k - 0x88;
  } else if (k >= 0x80) {
    _keyReport.modifiers |= (1 << (k - 0x80));
    k = 0;
  } else {
    k = _asciimap[k];
    if (!k) {
      return 0;
    }
    if ((k & SHIFT) == SHIFT) {
      _keyReport.modifiers |= 0x02;
      k &= ~SHIFT;
    }
    if ((k & ALT_GR) == ALT_GR) {
      _keyReport.modifiers |= 0x40;
      k &= ~ALT_GR;
    }
    if (k == ISO_REPLACEMENT) {
      k = ISO_KEY;
    }
  }
  return pressRaw(k);
}

size_t BootKeyboard::release(uint8_t k) {
  if (k >= 0x88) {
    k = k - 0x88;
  } else if (k >= 0x80) {
    _keyReport.modifiers &= ~(1 << (k - 0x80));
    k = 0;
  } else {
    k = _asciimap[k];
    if (!k) {
      return 0;
    }
    if ((k & SHIFT) == SHIFT) {
      _keyReport.modifiers &= ~(0x02);
      k &= ~SHIFT;
    }
    if ((k & ALT_GR) == ALT_GR) {
      _keyReport.modifiers &= ~(0x40);
      k &= ~ALT_GR;
    }
    if (k == ISO_REPLACEMENT) {
      k = ISO_KEY;
    }
  }
  return releaseRaw(k);
}

void BootKeyboard::releaseAll() {
  memset(&_keyReport, 0, sizeof(_keyReport));
  sendReport();
}

size_t BootKeyboard::write(uint8_t c) {
  uint8_t p = press(c);
  release(c);
  return p;
}

size_t BootKeyboard::write(const uint8_t *buffer, size_t size) {
  size_t n = 0;
  while (size--) {
    if (*buffer != '\r') {
      if (write(*buffer)) {
        n++;
      } else {
        break;
      }
    }
    buffer++;
  }
  return n;
}
