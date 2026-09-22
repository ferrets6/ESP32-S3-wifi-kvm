# ESP32-S3 WiFi KVM

Firmware for an **ESP32-S3 Super Mini** that turns the board into a USB HID
keyboard+mouse (native USB-C, TinyUSB), controllable from any device on the
local network through a web page with low-latency WebSocket input (touchpad,
text keyboard, special keys/combos). Personal DIY project: software KVM /
accessibility tool to control a PC from a phone or Raspberry Pi.

This board's own control panel (below) is self-contained and works standalone.
[remote-kvm](https://github.com/ferrets6/remote-kvm) wraps it (and
[unifying-cc2544-radiokey](https://github.com/ferrets6/unifying-cc2544-radiokey)) as one
of several pluggable HID drivers behind a single page that also shows live video from
the target machine.

## Hardware

- Chip: ESP32-S3 (4 MB flash, 2 MB PSRAM)
- USB: native USB-OTG (TinyUSB), same USB-C port used for power/flashing

## Build & flash

Requires [PlatformIO Core](https://platformio.org/install/cli).

```bash
pio run                # build
pio run -t upload      # build + flash (auto-detects the port)
pio device monitor      # serial debug, after upload
```

**Manual reset quirk**: this board has no dedicated USB-serial chip, so
auto-reset into/out of bootloader mode is unreliable.
- If upload fails ("Failed to connect"): hold **BOOT**, press and release
  **RESET**, release **BOOT**, retry the upload.
- After a successful upload, press **RESET** once (or unplug/replug) to
  start the new firmware.
- Only needed around a flash — normal power cycles boot straight into the
  last firmware.

## Two build environments

| Environment | USB exposes | Use when |
|---|---|---|
| `esp32-s3-supermini` (default) | keyboard + mouse | normal use, inside an already-booted OS |
| `esp32-s3-supermini-kbonly` | keyboard only | controlling BIOS/UEFI/GRUB, before an OS has started |

```bash
pio run -e esp32-s3-supermini-kbonly -t upload
```

BIOS/UEFI/GRUB expect the classic USB HID "boot keyboard" shape: one
interface, 8-byte reports, **no Report ID byte**. The default build shares
one HID interface between keyboard and mouse using a Report ID byte to tell
them apart — correct for a full OS, but many BIOS/bootloaders ignore it.
`kbonly` (`include/boot_keyboard.h`) drops the mouse and implements a
minimal Report-ID-less boot keyboard instead, matching a confirmed-working
reference device byte-for-byte (VID/PID, USB class, power attributes). It
is a best-effort reproduction, **not guaranteed** on every BIOS — see
[arduino-esp32 discussion #9281](https://github.com/espressif/arduino-esp32/discussions/9281)
for the underlying open issue.

This BIOS-compatible endpoint layout also needs two small changes to the
arduino-esp32 core itself (single IN endpoint instead of IN+OUT). They live
in `patches/framework/` and `tools/patch_framework.py` applies them
automatically, but **only** for the `kbonly` build, and only for the
duration of that build: the globally installed framework is patched right
before compiling and restored to stock right after, every time, including
after a build that hit the SCons cache and didn't need to recompile
anything. The default (keyboard+mouse) build never touches it — its code
path was verified byte-identical to upstream, so there's nothing to patch.
If a `kbonly` build is interrupted before its restore step runs, the very
next `pio run` (any environment) detects the leftover patch and heals it
automatically before doing anything else. The script is also pinned to the
exact framework version and file hashes it was verified against
(`patches/framework/manifest.json`); a framework update that changes either
one aborts the build instead of silently applying a stale patch, or
silently overwriting an unrelated modification it doesn't recognize.

## OTA updates

After the first USB flash, later firmware updates don't need physical
access: `pio run` (no `-t upload`) to build, then open
`http://<board-ip>/update`, upload `firmware.bin`, confirm — the board
reboots on its own.

## WiFi setup

On boot the board tries the WiFi credentials saved in flash; if that fails
(or none are saved) it opens its own open access point `ESP32-KVM-XXXX` at
`http://192.168.4.1/`. Connect to it and go to `/wifi` to save your home
network's SSID/password; the board then reboots and uses that network from
now on, falling back to the AP again if it's ever unreachable.

## Control panel

Open `http://<board-ip>/` from any device on the same network: touchpad
(drag to move, tap to click), a text field for typing (sends real per-key
HID presses, works with paste), special keys, and common combos
(Ctrl+C/V/X/A/Z/S, Alt+Tab, Win). Talks to the board over a WebSocket
(`/ws`) using a minimal text protocol.

Includes an Italian extended keymap (`it_keymap.cpp`) for accented
letters/symbols not covered by the core's built-in `it_IT` layout — requires
the target PC's OS keyboard layout to actually be set to Italian.

## Security

The web panel and WebSocket have **no authentication** — anyone on the
network can control the connected PC's keyboard/mouse. Fine for a trusted
home network; never expose the board to the internet or port-forward to it.
