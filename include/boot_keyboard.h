#pragma once
#include <Arduino.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>  // reuse KEY_* constants and KeyboardLayout_it_IT[]

// Minimal single-purpose USB HID boot keyboard, used only by the
// KVM_KEYBOARD_ONLY build (see platformio.ini env esp32-s3-supermini-kbonly).
//
// USBHIDKeyboard (the normal core class) always tags its report with
// Report ID 1, because arduino-esp32's USBHID class multiplexes every
// registered HID device (keyboard AND mouse) onto a single shared USB HID
// interface, distinguishing them by Report ID within one combined report
// descriptor. That's fine for a full OS driver, but BIOS/UEFI and GRUB use a
// far more minimal USB-HID stack that (per a working reference: a Raspberry
// Pi USB gadget exposing a single keyboard function, see
// https://ohyaan.github.io/tips/how_to_turn_your_raspberry_pi_into_a_usb_hid_keyboard_or_mouse/)
// expects the classic USB HID spec "boot keyboard" shape: ONE HID interface,
// ONE collection, NO Report ID byte, plain 8-byte reports
// {modifiers, reserved, key1..key6}. This class reproduces exactly that by
// being the ONLY HID device registered in this build (no mouse) and by
// omitting the Report ID from both the descriptor and every sent report.
class BootKeyboard : public USBHIDDevice, public Print {
public:
  BootKeyboard();
  void begin(const uint8_t *layout = KeyboardLayout_en_US);
  size_t write(uint8_t k);
  size_t write(const uint8_t *buffer, size_t size);
  size_t press(uint8_t k);
  size_t release(uint8_t k);
  void releaseAll();
  size_t pressRaw(uint8_t k);
  size_t releaseRaw(uint8_t k);
  bool capsLockOn() const {
    return _capsLockOn;
  }

  // USBHIDDevice
  uint16_t _onGetDescriptor(uint8_t *buffer) override;
  void _onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) override;

private:
  USBHID hid;
  struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
  } _keyReport;
  const uint8_t *_asciimap;
  bool _capsLockOn = false;

  void sendReport();
};
