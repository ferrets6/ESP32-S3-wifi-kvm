#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <USB.h>
#include <ElegantOTA.h>
#include <esp32-hal-rgb-led.h>
#include <esp_event.h>

#include "hid_bridge.h"
#include "keyboard_impl.h"
#include "web_ui.h"

#ifdef KVM_KEYBOARD_ONLY
// Keyboard-only composite: a single, Report-ID-less HID interface for
// maximum BIOS/UEFI/GRUB boot-keyboard compatibility (see boot_keyboard.h).
// No mouse on this build.
KeyboardClass Keyboard;
#else
// Normal composite USB HID device: keyboard + mouse over the same USB-C
// port, via the Arduino-ESP32 core's native TinyUSB stack (USB Mode:
// USB-OTG). Both devices share one HID interface multiplexed by Report ID.
#include <USBHIDMouse.h>
KeyboardClass Keyboard;
USBHIDMouse Mouse;
#endif

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

// No auth on /update: this board only ever sits on a trusted local network
// (open setup AP, no port-forwarding). Empty strings disable ElegantOTA's
// HTTP Basic Auth check entirely.
static const char *OTA_USERNAME = "";
static const char *OTA_PASSWORD = "";

String apSsid;
String currentStaSsid;
bool staConnected = false;

unsigned long restartAtMs = 0;

// Small in-memory event log for the /status page. There is no Serial-over-USB
// console once ARDUINO_USB_CDC_ON_BOOT is disabled (needed for BIOS/GRUB HID
// compatibility, see platformio.ini), and the board is often plugged into a
// host we have no other way to inspect (NAS, headless PC...), so this is the
// only window into what the USB stack and WiFi are actually doing.
static const int LOG_CAPACITY = 48;
String eventLog[LOG_CAPACITY];
int eventLogCount = 0;

void logEvent(const String &msg) {
  // Millisecond resolution matters here: BIOS/GRUB's whole USB negotiation
  // happens within the first second or two after power-on.
  eventLog[eventLogCount % LOG_CAPACITY] = "[" + String(millis()) + "ms] " + msg;
  eventLogCount++;
}

void usbEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  switch (id) {
    case ARDUINO_USB_STARTED_EVENT: logEvent("USB: host connected (enumerated)"); break;
    case ARDUINO_USB_STOPPED_EVENT: logEvent("USB: host disconnected"); break;
    case ARDUINO_USB_SUSPEND_EVENT: logEvent("USB: suspended by host"); break;
    case ARDUINO_USB_RESUME_EVENT: logEvent("USB: resumed by host"); break;
  }
}

// Low-level HID class control requests. A real boot-keyboard host (BIOS,
// GRUB) is expected to issue SET_PROTOCOL(BOOT) on the keyboard interface
// as part of actually using it; if that request never shows up here at all,
// the host isn't even attempting to treat our HID interface as a keyboard
// (as opposed to just power-enumerating the composite device and stopping).
// "instance" is the HID interface index in registration order: 0 = keyboard
// (USBHIDKeyboard Keyboard is declared/constructed first), 1 = mouse.
void hidProtocolEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  auto *p = (arduino_usb_hid_event_data_t *)data;
  if (id == ARDUINO_USB_HID_SET_PROTOCOL_EVENT) {
    logEvent(
      "HID: instance " + String(p->instance) + " SET_PROTOCOL -> " + (p->set_protocol.protocol == 0 ? "BOOT" : "REPORT")
    );
  } else if (id == ARDUINO_USB_HID_SET_IDLE_EVENT) {
    logEvent("HID: instance " + String(p->instance) + " SET_IDLE rate=" + String(p->set_idle.idle_rate));
  }
}

// Onboard addressable RGB LED, used purely as an "a command just arrived"
// activity indicator: a brief, dim flash on every WebSocket message,
// regardless of what it contains (so it also works as a safe connectivity
// test that never touches the HID keyboard/mouse). GPIO21 confirmed via
// the "lt:<gpio>" diagnostic command (see handleWsMessage / tools/led-test.ps1)
// on this specific "ESP32-S3 Super Mini" board; other clones may differ.
static const uint8_t LED_PIN = 21;
static const uint8_t LED_FLASH_BRIGHTNESS = 12;  // out of 255, deliberately dim
static const uint16_t LED_FLASH_MS = 40;
unsigned long ledOffAtMs = 0;
bool ledEnabled = true;  // toggled from the web UI, persisted in NVS

// State for the "lt:<gpio>" diagnostic command (see handleWsMessage).
uint8_t ledTestPin = 0;
unsigned long ledTestOffAtMs = 0;

void ledFlash() {
  if (!ledEnabled) {
    return;
  }
  rgbLedWrite(LED_PIN, 0, 0, LED_FLASH_BRIGHTNESS);  // dim blue
  ledOffAtMs = millis() + LED_FLASH_MS;
}

// ---------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------

void deriveApSsid() {
  // Read the factory MAC straight from eFuse instead of WiFi.macAddress()/
  // softAPmacAddress(): those return whatever the WiFi driver's interface
  // MAC happens to be at the moment of the call, which is unreliable (and
  // was observed to be all-zero / a different derived value) before the
  // driver has fully started. ESP.getEfuseMac() has no such dependency.
  // Only used to tell multiple boards apart by SSID; the AP itself is open
  // (see startAccessPoint()) since it only ever serves the one-time /wifi
  // setup page.
  uint64_t efuseMac = ESP.getEfuseMac();
  uint8_t mac[6];
  for (int i = 0; i < 6; i++) {
    mac[i] = (efuseMac >> (8 * i)) & 0xFF;
  }
  char ssidBuf[24];
  snprintf(ssidBuf, sizeof(ssidBuf), "ESP32-KVM-%02X%02X", mac[4], mac[5]);
  apSsid = ssidBuf;
}

bool tryConnectStation(const String &ssid, const String &pass, uint32_t timeoutMs) {
  if (ssid.isEmpty()) {
    return false;
  }
  Serial.printf("[wifi] connecting to '%s'...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP: %s\n", WiFi.localIP().toString().c_str());
    logEvent("WiFi: connected to '" + ssid + "', IP " + WiFi.localIP().toString());
    return true;
  }
  Serial.println("[wifi] station connect failed");
  logEvent("WiFi: failed to connect to '" + ssid + "', falling back to AP");
  return false;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str());  // open network: only used for the one-time /wifi setup
  Serial.printf("[wifi] AP mode: SSID='%s' (open) IP=%s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  logEvent("WiFi: AP mode '" + apSsid + "' (open), IP " + WiFi.softAPIP().toString());
}

void setupWiFi() {
  deriveApSsid();
  currentStaSsid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  staConnected = tryConnectStation(currentStaSsid, pass, 12000);
  if (!staConnected) {
    startAccessPoint();
  }
}

// ---------------------------------------------------------------------
// HTTP routes
// ---------------------------------------------------------------------

void setupHttpRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", INDEX_HTML);
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    String html = FPSTR(WIFI_HTML);
    html.replace("%SSID%", currentStaSsid);
    html.replace("%APSSID%", apSsid);
    html.replace("%LEDCHECKED%", ledEnabled ? "checked" : "");
    req->send(200, "text/html", html);
  });

  // Our own OTA upload page (dark theme, no third-party branding), served
  // instead of ElegantOTA's bundled page. Must be registered before
  // ElegantOTA.begin() so it wins the route match for GET /update;
  // ElegantOTA's own /ota/start and /ota/upload endpoints (the actual
  // upload logic, untouched) still power the upload underneath.
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", OTA_HTML);
  });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *req) {
    String ssid = req->hasParam("ssid", true) ? req->getParam("ssid", true)->value() : "";
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    req->send(200, "text/html",
               "<html><body style='font-family:sans-serif;background:#121316;color:#e8e8ea;padding:24px'>"
               "Saved. Restarting...</body></html>");
    restartAtMs = millis() + 1200;
  });

  server.on("/led", HTTP_POST, [](AsyncWebServerRequest *req) {
    ledEnabled = req->hasParam("enabled", true);
    prefs.putBool("ledEnabled", ledEnabled);
    logEvent(String("Settings: LED activity flash turned ") + (ledEnabled ? "on" : "off"));
    AsyncWebServerResponse *res = req->beginResponse(303);
    res->addHeader("Location", "/wifi");
    req->send(res);
  });

  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    logEvent("Settings: reboot requested from web UI");
    req->send(200, "text/html",
               "<html><body style='font-family:sans-serif;background:#121316;color:#e8e8ea;padding:24px'>"
               "Restarting...</body></html>");
    restartAtMs = millis() + 800;
  });

  // Plain-text diagnostics page: uptime, WiFi/USB state, and a rolling
  // event log. There is no Serial-over-USB console on this build (CDC is
  // disabled for BIOS/GRUB HID compatibility, see platformio.ini) and the
  // board is often plugged into a host with no other way to inspect it
  // (NAS, headless PC...), so this is the only window into what's going on.
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    String html = "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta http-equiv='refresh' content='3'>";
    html += "<title>ESP32 KVM - Status</title>";
    html += "<style>body{background:#121316;color:#e8e8ea;font:13px/1.5 monospace;padding:20px;"
            "white-space:pre-wrap;max-width:700px;margin:0 auto}a{color:#4f8cff}"
            "h1{font-size:15px}</style></head><body>";
    html += "<h1>ESP32 KVM Status</h1>";
    html += "Uptime: " + String(millis() / 1000) + "s\n";
    html += "WiFi: " + String(staConnected ? ("client on '" + currentStaSsid + "', IP " + WiFi.localIP().toString())
                                            : ("open AP '" + apSsid + "', IP " + WiFi.softAPIP().toString()));
    html += "\nUSB connected to a host: " + String((bool)USB ? "yes" : "no");
    html += "\nCommand LED: " + String(ledEnabled ? "on" : "off");
    html += "\nWebSocket clients connected: " + String(ws.count());
    html += "\n\n-- Event log (auto-refreshes every 3s) --\n";
    int n = min(eventLogCount, LOG_CAPACITY);
    int start = eventLogCount > LOG_CAPACITY ? eventLogCount % LOG_CAPACITY : 0;
    if (n == 0) {
      html += "(no events yet)";
    }
    for (int i = 0; i < n; i++) {
      html += eventLog[(start + i) % LOG_CAPACITY] + "\n";
    }
    html += "\n<a href='/'>&larr; Control panel</a>";
    html += "</body></html>";
    req->send(200, "text/html", html);
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "Not found");
  });
}

// ---------------------------------------------------------------------
// WebSocket protocol: "<cmd>:<payload>"
//   mm:dx:dy        relative mouse move
//   mc:<button>     mouse click        (left|right|middle)
//   md:<button>     mouse button down
//   mu:<button>     mouse button up
//   kt:<utf8 text>  type text (real per-character key press/release)
//   kk:<name>       special key        (see hid_bridge.h)
//   kc:<name>       key combo          (see hid_bridge.h)
//   kg:<mods>:<key> generic held-modifier combo (see hidModCombo)
// ---------------------------------------------------------------------

void handleWsMessage(const String &msg) {
  int c1 = msg.indexOf(':');
  String cmd = (c1 < 0) ? msg : msg.substring(0, c1);
  String rest = (c1 < 0) ? "" : msg.substring(c1 + 1);

  if (cmd != "mm") {
    // Every command except mouse-move (way too frequent during a drag,
    // would flood the ring buffer) so /status shows exactly what was sent
    // and when, alongside the USB-level events.
    logEvent("CMD: " + msg);
  }

  if (cmd == "mm") {
    int c2 = rest.indexOf(':');
    if (c2 < 0) {
      return;
    }
    int32_t dx = rest.substring(0, c2).toInt();
    int32_t dy = rest.substring(c2 + 1).toInt();
    hidMouseMove(dx, dy);
  } else if (cmd == "mc") {
    hidMouseClick(rest);
  } else if (cmd == "md") {
    hidMouseDown(rest);
  } else if (cmd == "mu") {
    hidMouseUp(rest);
  } else if (cmd == "kt") {
    hidTypeUtf8(rest);
  } else if (cmd == "kk") {
    hidSpecialKey(rest);
  } else if (cmd == "kc") {
    hidCombo(rest);
  } else if (cmd == "kg") {
    int c2 = rest.indexOf(':');
    if (c2 < 0) {
      return;
    }
    hidModCombo(rest.substring(0, c2), rest.substring(c2 + 1));
  } else if (cmd == "lt") {
    // Diagnostic only: "lt:<gpio>" flashes that pin as a WS2812 RGB LED
    // (dim blue, ~300ms) so the correct onboard-LED pin can be found
    // without reflashing. Never touches the keyboard/mouse.
    uint8_t pin = (uint8_t)rest.toInt();
    Serial.printf("[led-test] flashing GPIO%u\n", pin);
    rgbLedWrite(pin, 0, 0, 40);
    ledTestPin = pin;
    ledTestOffAtMs = millis() + 300;
  } else if (cmd == "le") {
    // "le:1" / "le:0": enable/disable the activity-flash LED, persisted.
    ledEnabled = (rest == "1");
    prefs.putBool("ledEnabled", ledEnabled);
    if (!ledEnabled && ledOffAtMs) {
      rgbLedWrite(LED_PIN, 0, 0, 0);
      ledOffAtMs = 0;
    }
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[ws] client #%u connected\n", client->id());
    logEvent("WS: client #" + String(client->id()) + " connected from " + client->remoteIP().toString());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[ws] client #%u disconnected\n", client->id());
    logEvent("WS: client #" + String(client->id()) + " disconnected");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      ledFlash();
      auto buf = std::unique_ptr<char[]>(new char[len + 1]);
      memcpy(buf.get(), data, len);
      buf[len] = '\0';
      handleWsMessage(String(buf.get()));
    }
  }
}

// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  hidBegin();  // Keyboard.begin(it_IT) + Mouse.begin()
#ifdef KVM_KEYBOARD_ONLY
  // Reproduce, in every field this API lets us control, the exact device
  // descriptor of a confirmed-working reference: a Linux USB gadget
  // (configfs) exposing a single boot-protocol keyboard function, verified
  // working on this same PC's BIOS setup screen on the same USB-A ports
  // (see README "Two build environments" for the full writeup and
  // https://ohyaan.github.io/tips/how_to_turn_your_raspberry_pi_into_a_usb_hid_keyboard_or_mouse/).
  //
  //   - idVendor/idProduct: 0x1d6b/0x0104 is the Linux Foundation's
  //     sanctioned VID for generic USB gadgets -- not a real vendor's
  //     identity, just what the reference used.
  //   - bDeviceClass/SubClass/Protocol = 0 ("defined at interface level"):
  //     the core defaults these to Miscellaneous/Common/IAD (0xEF/0x02/0x01),
  //     the standard signal for a composite device using an Interface
  //     Association Descriptor -- wrong for a single plain HID interface
  //     with no IAD actually present, and plausibly why some very minimal
  //     BIOS/GRUB USB-HID stacks skip the device instead of reading the
  //     interface's own (correctly-set) boot-keyboard class.
  //   - strings and bcdDevice: byte-for-byte the same as the reference.
  //
  // Still under investigation with the board's owner: this exact symptom
  // (ESP32-S3 HID keyboard not recognized as a boot-protocol device, e.g.
  // by a KVM switch) is an open, unresolved report against arduino-esp32
  // itself (github.com/espressif/arduino-esp32 discussion #9281), so this
  // is our best-effort reproduction of the one setup known to work, not a
  // guaranteed fix.
  USB.VID(0x1d6b);
  USB.PID(0x0104);
  USB.firmwareVersion(0x0100);
  USB.manufacturerName("Raspberry Pi");
  USB.productName("Virtual Keyboard");
  USB.serialNumber("0123456789");
  USB.usbClass(0);
  USB.usbSubClass(0);
  USB.usbProtocol(0);
  // Found by diffing lsusb -v against TWO independently-confirmed-working
  // boot keyboards on the same target BIOS (a real Logitech K120 and the
  // board owner's own from-scratch CC2544-based dongle): both report
  // themselves as USB 1.10 (not 2.00), bus-powered (not self-powered --
  // which the core defaults to for every USB device regardless of how it's
  // actually powered), and a low, old-style power budget (90-100mA, not
  // the default 500mA max). Our board genuinely draws more than that once
  // WiFi is active, but this field is only the enumeration-time budget
  // request, not an enforced cap -- most hosts don't police it strictly at
  // this stage, and matching what a real minimal boot keyboard declares is
  // worth it if it helps a BIOS that's already inclined to distrust
  // anything that doesn't look like one of those.
  USB.usbVersion(0x0110);
  USB.usbAttributes(0xA0);  // bus-powered + remote wakeup, like the Logitech
  USB.usbPower(100);
#else
  // Same device-descriptor fields as the kbonly build above (VID/PID,
  // class/subclass/protocol, USB version, power attributes, strings) --
  // best-effort application of the working reference's identity to this
  // build too. Does NOT fix BIOS/GRUB boot-keyboard recognition on its own:
  // with the mouse also registered, the keyboard's HID report still carries
  // a Report ID byte (see boot_keyboard.h / README), which is the actual
  // blocker. Kept here anyway since it's free and can't hurt enumeration.
  USB.VID(0x1d6b);
  USB.PID(0x0104);
  USB.firmwareVersion(0x0100);
  USB.manufacturerName("Tecknet");
  USB.productName("Mini Wireless Combo");
  USB.serialNumber("1464357899");
  USB.usbClass(0);
  USB.usbSubClass(0);
  USB.usbProtocol(0);
  USB.usbVersion(0x0110);
  USB.usbAttributes(0xA0);
  USB.usbPower(100);
#endif
  USB.onEvent(usbEventHandler);
  esp_event_handler_register(ARDUINO_USB_HID_EVENTS, ESP_EVENT_ANY_ID, hidProtocolEventHandler, NULL);
  USB.begin();

  prefs.begin("wifi", false);
  ledEnabled = prefs.getBool("ledEnabled", true);
  setupWiFi();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  setupHttpRoutes();
  ElegantOTA.begin(&server, OTA_USERNAME, OTA_PASSWORD);
  server.begin();

  String host = staConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  Serial.printf("[http] control panel: http://%s/\n", host.c_str());
  Serial.printf("[http] OTA update page: http://%s/update (user '%s')\n", host.c_str(), OTA_USERNAME);
}

void loop() {
  ws.cleanupClients();
  ElegantOTA.loop();
  if (ledOffAtMs && millis() > ledOffAtMs) {
    rgbLedWrite(LED_PIN, 0, 0, 0);
    ledOffAtMs = 0;
  }
  if (ledTestOffAtMs && millis() > ledTestOffAtMs) {
    rgbLedWrite(ledTestPin, 0, 0, 0);
    ledTestOffAtMs = 0;
  }
  if (restartAtMs && millis() > restartAtMs) {
    ESP.restart();
  }
}
