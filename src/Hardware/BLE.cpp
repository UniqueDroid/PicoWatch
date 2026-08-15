#include "BLE.h"
#include <stdlib.h>

// Nordic UART Service - fixed UUIDs, NOT something we get to choose.
// Gadgetbridge's Bangle.js/Espruino coordinator scans for exactly this
// service UUID (plus a matching advertised device name, see config.h's
// BLE_NOTIFY_DEVICE_NAME) to auto-detect the device type. See project
// memory for the full researched protocol writeup.
//
// Property assignment verified directly against Gadgetbridge's
// BangleJSDeviceSupport source (14.08.2026): it WRITEs to ...0002 and
// enables NOTIFY on ...0003 as an unconditional part of its init
// handshake. An earlier version of this file had these two swapped
// (going by Nordic's own doc naming instead of Gadgetbridge's actual
// behavior) - Gadgetbridge's notify-enable on a WRITE-only characteristic
// silently no-ops, so every connection stalled forever at "connecting"
// regardless of bonding. See src/Hardware/BLE.h for the naming rationale.
#define SERVICE_UUID_NUS                "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_NUS_WRITE   "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // WRITE  - notifications arrive here
#define CHARACTERISTIC_UUID_NUS_NOTIFY  "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // NOTIFY - Gadgetbridge subscribes here (must exist w/ NOTIFY property)

bool notifyConnected = false;
// Updated on every connect and every incoming write - see
// BLE::msSinceLastActivity()'s header comment.
unsigned long lastActivityMs = 0;

// HTTP-over-BLE response state (see BLE::httpGet() et al. in the header) -
// set by NotifyTxCallback::_handleLine() when a "t":"http" reply arrives,
// polled by the caller's own timeout loop. Only one request is ever in
// flight at a time in this codebase (synchronous usage, like every other
// BLE operation here), so there's no request/response "id" matching -
// httpGet() just clears these, and the first "t":"http" reply that comes
// back is assumed to be the answer.
bool httpResponsePending = false;
bool httpGotResponse     = false;
bool httpSuccess         = false;
String httpBody;

// Reverse of decodeJsString() - encodes a plain string as a JS string
// literal safe to embed in the message we send Gadgetbridge (escapes the
// two characters that would otherwise break out of the quotes: backslash
// and the quote itself). URLs are the only thing this is used for, and
// are ASCII by construction (query params are already percent-encoded
// upstream), so unlike decodeJsString() there's no \xHH/atob() encoding
// to worry about on this direction.
static String encodeJsString(const String &s) {
  String out;
  out.reserve(s.length() + 2);
  out += '"';
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

class NotifyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    notifyConnected = true;
    lastActivityMs = millis();
  }
  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
    notifyConnected = false;
  }
};

// Decodes one JS string literal starting at s[pos] (must be '"'), Gadgetbridge-
// style: its jsonToStringInternal() (BangleJSDeviceSupport.java) hand-rolls
// this encoding rather than emitting strict JSON, so a real JSON parser
// (Arduino_JSON etc.) chokes on it - notably \xHH byte escapes and \v, both
// invalid in JSON. Appends the decoded bytes to out, returns the index just
// past the closing quote, or -1 if the string never closes.
static int decodeJsString(const String &s, int pos, String &out) {
  if (pos >= (int)s.length() || s[pos] != '"') return -1;
  pos++;
  while (pos < (int)s.length() && s[pos] != '"') {
    char c = s[pos];
    if (c == '\\' && pos + 1 < (int)s.length()) {
      char e = s[pos + 1];
      switch (e) {
        case 'n': out += '\n'; pos += 2; break;
        case 'r': out += '\r'; pos += 2; break;
        case 't': out += '\t'; pos += 2; break;
        case 'b': out += '\b'; pos += 2; break;
        case 'f': out += '\f'; pos += 2; break;
        case 'v': out += (char)0x0B; pos += 2; break;
        case '\\': out += '\\'; pos += 2; break;
        case '"': out += '"'; pos += 2; break;
        case '/': out += '/'; pos += 2; break;
        case 'x':
          if (pos + 3 < (int)s.length()) {
            const char hex[3] = {s[pos + 2], s[pos + 3], '\0'};
            out += (char)strtol(hex, nullptr, 16);
            pos += 4;
          } else {
            pos += 2;
          }
          break;
        default: out += e; pos += 2; break;
      }
    } else {
      out += c;
      pos++;
    }
  }
  if (pos >= (int)s.length()) return -1; // unterminated string
  return pos + 1;
}

// Standard-alphabet base64 decode - matches Android's
// Base64.encodeToString(bytes, Base64.NO_WRAP), which is what
// jsonToStringInternal() falls back to (as `atob("...")`) for long ASCII
// strings instead of a quoted literal (see BLE.cpp's file comment).
static String base64Decode(const String &in) {
  static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int lookup[256];
  for (int i = 0; i < 256; i++) lookup[i] = -1;
  for (int i = 0; i < 64; i++) lookup[(uint8_t)tbl[i]] = i;
  String out;
  int val = 0, bits = -8;
  for (size_t i = 0; i < in.length(); i++) {
    const uint8_t c = in[i];
    if (c == '=') break;
    const int d = lookup[c];
    if (d < 0) continue;
    val = (val << 6) + d;
    bits += 6;
    if (bits >= 0) {
      out += (char)((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out;
}

// Extracts one field's value from a Gadgetbridge GB(...) payload. Handles
// both encodings jsonToStringInternal() emits for a string value: a quoted
// JS literal (decodeJsString), or - for long ASCII-only strings, which
// covers most real notification bodies - `atob("<base64>")` instead of a
// literal, so a naive quoted-string-only parser silently fails on exactly
// the notifications worth showing. Order-independent key search, so field
// order in the payload doesn't matter.
static String extractField(const String &line, const char *key) {
  const String pattern = String("\"") + key + "\":";
  const int keyPos = line.indexOf(pattern);
  if (keyPos < 0) return "";
  int pos = keyPos + pattern.length();
  while (pos < (int)line.length() && line[pos] == ' ') pos++;
  if (pos >= (int)line.length()) return "";
  if (line[pos] == '"') {
    String out;
    decodeJsString(line, pos, out);
    return out;
  }
  if (line.startsWith("atob(", pos)) {
    String b64;
    decodeJsString(line, pos + 5, b64);
    return base64Decode(b64);
  }
  return "";
}

// Reassembles Gadgetbridge's chunked writes (MTU-sized, terminated by '\n')
// into full lines, strips the Espruino REPL wrapper ("\x10GB(...)"), and
// pulls out the fields we care about. See config.h's BLE_NOTIFY_* comment /
// project memory for the exact wire format this is reverse-engineered from -
// deliberately NOT run through a strict JSON parser, see extractField().
class NotifyTxCallback : public NimBLECharacteristicCallbacks {
public:
  explicit NotifyTxCallback(BleNotificationCallback callback) : _callback(callback) {}

  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
    lastActivityMs = millis();
    _buffer += pCharacteristic->getValue().c_str();
    int newlinePos;
    while ((newlinePos = _buffer.indexOf('\n')) >= 0) {
      _handleLine(_buffer.substring(0, newlinePos));
      _buffer.remove(0, newlinePos + 1);
    }
    // Guards against ever growing unbounded if a line never arrives
    // terminated (malformed sender, dropped packet, etc.) - a real
    // notification line is nowhere near this long.
    if (_buffer.length() > 1024) _buffer = "";
  }

private:
  void _handleLine(const String &line) {
    const int start = line.indexOf("GB(");
    if (start < 0) return;
    const String payload = line.substring(start + 3);
    const String type = extractField(payload, "t");
    if (type == "http") {
      // See BLE.h's httpGet()/httpResponseReady() comment - fire-and-poll,
      // this just records whatever comes back next while a request is
      // pending. "err" present means Gadgetbridge failed the request on
      // its end (bad URL, no internet access enabled in this Gadgetbridge
      // build, etc.) - either way this counts as "ready", the caller
      // checks httpResponseSuccess() to tell the two apart.
      if (!httpResponsePending) return;
      httpResponsePending = false;
      httpGotResponse = true;
      httpSuccess = extractField(payload, "err").length() == 0;
      httpBody = httpSuccess ? extractField(payload, "resp") : "";
      return;
    }
    if (type != "notify") return; // ignore notify- (deletion) and other message types
    const String src   = extractField(payload, "src");
    const String title = extractField(payload, "title");
    const String body  = extractField(payload, "body");
    if (_callback != nullptr) _callback(src.c_str(), title.c_str(), body.c_str());
  }

  BleNotificationCallback _callback;
  String _buffer;
};

//
// Constructor
BLE::BLE(void) {}

//
// Destructor
BLE::~BLE(void) {}

bool BLE::beginNotify(const char *localName, BleNotificationCallback callback) {
  notifyConnected = false;
  lastActivityMs  = millis();
  // A request from a previous, now-torn-down session must not be mistaken
  // for one made in this new session (see NotifyTxCallback::_handleLine()'s
  // "t":"http" branch - it has no id-matching, just a pending flag).
  httpResponsePending = false;
  httpGotResponse = false;

  NimBLEDevice::init(localName);

  // Deliberately no setSecurityAuth()/deleteAllBonds() here (tried and
  // reverted, 14.08.2026): calling deleteAllBonds() pulls NimBLE-Arduino
  // 2.5.1's ble_store_nvs.c bond-store code into the link, which on this
  // ESP32 core version (2.0.17 / IDF v4.4.7) hits an upstream library bug -
  // ble_store_nvs.c's peer-records restore path calls
  // ble_rpa_set_num_peer_dev_records(), guarded by the same
  // MYNEWT_VAL(BLE_HOST_BASED_PRIVACY) as its definition in
  // ble_hs_resolv.c, yet that definition doesn't make it into the final
  // link on this target - undefined reference, breaks the build on both
  // V2 and V3. Gadgetbridge's Bangle.js/Espruino coordinator uses
  // BONDING_STYLE_ASK (optional, not required, per project memory's
  // protocol research) and NimBLE's own default is already no-bonding, so
  // skipping this is not a functional loss - it was a speculative fix for
  // a bonding-key-mismatch theory that turned out not to be the real bug
  // (see the RX/TX characteristic swap fixed above, project memory).
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new NotifyServerCallbacks());

  pNotifyService = pServer->createService(SERVICE_UUID_NUS);

  // NimBLE automatically adds the CCCD descriptor for a NOTIFY-capable
  // characteristic - no manual BLE2902-equivalent needed, unlike the
  // classic Bluedroid-based library this replaced.
  pNotifyCharacteristic =
      pNotifyService->createCharacteristic(CHARACTERISTIC_UUID_NUS_NOTIFY, NIMBLE_PROPERTY::NOTIFY);

  pWriteCharacteristic = pNotifyService->createCharacteristic(
      CHARACTERISTIC_UUID_NUS_WRITE, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWriteCharacteristic->setCallbacks(new NotifyTxCallback(callback));

  pNotifyService->start();

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID_NUS);
  // NimBLEDevice::init(name) does NOT automatically put the device name in
  // the advertising payload - without this, scanners (Gadgetbridge
  // included) see an unnamed/"unknown" device and can't match it against
  // Gadgetbridge's Bangle.js/Espruino name regex either.
  //
  // The 128-bit NUS service UUID (18 bytes incl. header) plus a full name
  // like "Espruino PicoWatch" (21 bytes incl. header) plus flags (3 bytes)
  // add up to ~42 bytes - over BLE's 31-byte legacy advertising payload
  // limit, which would make setAdvertisementData() fail outright (checked
  // in NimBLEAdvertising.cpp) and advertising silently not start at all.
  // enableScanResponse(true) routes the name into the separate scan
  // response packet instead (standard practice for exactly this case -
  // primary packet stays small/filterable by service UUID, name lives in
  // the scan response, which active scanners like Android/Gadgetbridge
  // fetch automatically and combine with the primary packet).
  pAdvertising->enableScanResponse(true);
  pAdvertising->setName(localName);
  // Without this, NimBLE falls back to its zero-initialized advertising
  // params, which NimBLE's own ble_gap.c then defaults to
  // BLE_GAP_ADV_FAST_INTERVAL1 (30-60ms) - the "fast discovery" preset
  // meant for the first ~30s after a user taps "pair", not for repeatedly
  // sitting in an advertising window for up to a minute every few minutes.
  // 100-200ms is still well within normal discovery speed for a window
  // this long, but roughly halves the number of advertising radio bursts -
  // real, measurable battery draw, unlike the two flash-only "savings"
  // above (see project memory, 14.08.2026 battery investigation). Units
  // are 0.625ms per the underlying BLE HCI advertising interval field.
  pAdvertising->setMinInterval(160); // 100ms
  pAdvertising->setMaxInterval(320); // 200ms
  pAdvertising->start();

  return true;
}

void BLE::stopNotify() {
  // Fully tears down the BLE stack (radio + all GATT state) before
  // returning to deep sleep - leaving it running would both waste power
  // and leave stale service/characteristic pointers dangling into the
  // next beginNotify() call a few minutes later.
  NimBLEDevice::deinit(true);
  pServer               = nullptr;
  pNotifyService        = nullptr;
  pWriteCharacteristic  = nullptr;
  pNotifyCharacteristic = nullptr;
  notifyConnected       = false;
}

bool BLE::notifyClientConnected() { return notifyConnected; }

unsigned long BLE::msSinceLastActivity() { return millis() - lastActivityMs; }

bool BLE::httpGet(const char *url) {
  if (!notifyConnected || pNotifyCharacteristic == nullptr) return false;

  httpResponsePending = true;
  httpGotResponse = false;

  String json = "{\"t\":\"http\",\"url\":";
  json += encodeJsString(url);
  json += ",\"method\":\"get\"}";
  const String line = "\x10GB(" + json + ")\n";

  // Peripherals can only push data via NOTIFY, never WRITE (that
  // direction is reserved for the central/phone) - this is the reverse
  // of NotifyTxCallback's receive path, see BLE.h's sendMessage-related
  // comment. notify() sends its argument as a single GATT PDU with no
  // chunking of its own, so this manually splits to a conservative
  // 20-byte floor (BLE's universal minimum ATT MTU minus the 3-byte ATT
  // header, guaranteed usable even before/without MTU negotiation) -
  // matching, symmetrically, how Gadgetbridge's own uartTx() chunks its
  // writes to us.
  const int kChunk = 20;
  for (int i = 0; i < (int)line.length(); i += kChunk) {
    const int n = min(kChunk, (int)line.length() - i);
    if (!pNotifyCharacteristic->notify((const uint8_t *)line.c_str() + i, n)) {
      httpResponsePending = false;
      return false;
    }
  }
  return true;
}

bool BLE::httpResponseReady() { return httpGotResponse; }
bool BLE::httpResponseSuccess() { return httpSuccess; }
String BLE::httpResponseBody() { return httpBody; }
