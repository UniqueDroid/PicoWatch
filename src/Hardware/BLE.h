#ifndef _BLE_H_
#define _BLE_H_

#include "Arduino.h"

// NimBLE-Arduino instead of the ESP32 core's bundled classic (Bluedroid)
// BLE library - roughly half the flash/RAM footprint for the same
// functionality, which is the difference between the notification feature
// fitting or not fitting in PicoWatch V2's tight 1.9MB min_spiffs OTA
// slot (see project memory, 14.08.2026). Class names are NimBLE-prefixed
// (NimBLEServer instead of BLEServer, etc.) - not a drop-in header swap,
// the old BLE-OTA-specific code (dead/unused since WiFi OTA replaced it)
// was removed rather than ported, to keep the migration scoped to what's
// actually live.
#include <NimBLEDevice.h>

#include "config.h"

class BLE;

// Called once per received Gadgetbridge notification (src/title/body only -
// the other JSON fields like id/sender/actions aren't currently surfaced).
// Pointers are only valid for the duration of the call.
typedef void (*BleNotificationCallback)(const char *src, const char *title, const char *body);

class BLE {
public:
  BLE(void);
  ~BLE(void);

  // Gadgetbridge notification forwarding (Bangle.js/Espruino generic DIY
  // protocol - see config.h's BLE_NOTIFY_* comment for the researched
  // protocol details).
  bool beginNotify(const char *localName, BleNotificationCallback callback);
  void stopNotify();
  bool notifyClientConnected();
  // Milliseconds since the last connect/write - lets a caller end the
  // window once a connected phone has gone quiet instead of always
  // waiting out a fixed worst-case timeout (see PicoWatch::
  // _checkBleNotifications(), project memory 14.08.2026 battery
  // investigation).
  unsigned long msSinceLastActivity();

  // Gadgetbridge's HTTP-over-BLE proxy (its own {"t":"http",...} message
  // type - the phone performs the actual HTTP request over its own
  // network connection and hands the response back over this same link;
  // see project memory, 15.08.2026 for the researched wire format).
  // Fire-and-poll, not blocking: httpGet() only sends the request and
  // returns immediately (false if not connected to send on), the caller
  // polls httpResponseReady() in its own timeout loop, same pattern as
  // notifyClientConnected() - keeps all the "how long to wait" policy in
  // PicoWatch.cpp rather than duplicating timeout logic in this class.
  bool httpGet(const char *url);
  bool httpResponseReady();   // true once a matching reply (success or error) has arrived
  bool httpResponseSuccess(); // valid once httpResponseReady() - false means Gadgetbridge reported an error
  String httpResponseBody();  // valid once httpResponseReady() && httpResponseSuccess()

private:
  NimBLEServer *pServer = nullptr;

  NimBLEService *pNotifyService = nullptr;
  // Named after what Gadgetbridge's BangleJSDeviceSupport actually does with
  // each UUID (verified against its source, 14.08.2026) - NOT the "RX"/"TX"
  // labels Nordic's own UART service doc uses, which are easy to apply
  // backwards depending whose perspective (central vs peripheral) you read
  // them from. Getting this swapped is exactly why pairing always stalled:
  // Gadgetbridge unconditionally tries to enable NOTIFY on ...0003 as part
  // of its init handshake - if that characteristic doesn't have the NOTIFY
  // property (and therefore no CCCD), the enable silently no-ops and
  // Gadgetbridge's device never leaves the "connecting" state.
  NimBLECharacteristic *pWriteCharacteristic  = nullptr; // 6e400002... WRITE  - Gadgetbridge writes notification packets here
  NimBLECharacteristic *pNotifyCharacteristic = nullptr; // 6e400003... NOTIFY - Gadgetbridge subscribes here as part of its init handshake (console/REPL echo channel in the real protocol, unused for our one-way receipt but MUST exist with NOTIFY property or init never completes)
};

#endif
