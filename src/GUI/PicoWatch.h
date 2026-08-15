#ifndef PICOWATCH_H
#define PICOWATCH_H

#include <Arduino.h>
// Must come before the WiFiManager.h include below - its own bundled
// wm_strings_en.h checks this at preprocess time and, if defined, swaps
// its HTTP_HELP constant for an empty string instead of the built-in
// "Available pages" table (full page-by-path/GitHub-credit block) that
// WiFiManager's stock Info page always appended - Jan wanted that gone
// (15.08.2026), and this is WiFiManager's own supported way to do it
// (not something achievable via the public setShow*() setters below,
// those only gate the Erase/Update buttons and Back link).
#define WM_NOHELP
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
// WiFi setup itself still uses WiFiManager (see PicoWatch.cpp); these back the
// post-connect status/File-Update/GitHub-Update web server instead.
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Arduino_JSON.h>
#include <GxEPD2_BW.h>
#include <Wire.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "DSEG7_Classic_Bold_53.h"
#include "Hardware/Display.h"
#include "Hardware/BLE.h"
#include "Hardware/bma.h"
#include "config.h"
#include "esp_chip_info.h"
#ifdef ARDUINO_ESP32S3_DEV
  #include "Hardware/PicoWatch32KRTC.h"
  #include "soc/rtc.h"
  #include "soc/rtc_io_reg.h"
  #include "soc/sens_reg.h"
  #include "esp_sleep.h"
  #include "rom/rtc.h"
  #include "soc/soc.h"
  #include "soc/rtc_cntl_reg.h"
  #include "time.h"
  #include "esp_sntp.h"
  #include "hal/rtc_io_types.h"
  #include "driver/rtc_io.h"
  #define uS_TO_S_FACTOR 1000000ULL  //Conversion factor for micro seconds to seconds
  #define ADC_VOLTAGE_DIVIDER ((360.0f+100.0f)/360.0f) //Voltage divider at battery ADC  
#else
  #include "Hardware/PicoWatchRTC.h"
#endif

typedef struct weatherData {
  int8_t temperature;
  int16_t weatherConditionCode;
  bool isMetric;
  String weatherDescription;
  bool external;
  tmElements_t sunrise;
  tmElements_t sunset;
} weatherData;

typedef struct picowatchSettings {
  // Weather Settings
  String cityID;
  String lat;
  String lon;
  String weatherAPIKey;
  String weatherURL;
  String weatherUnit;
  String weatherLang;
  int8_t weatherUpdateInterval;
  // NTP Settings
  String ntpServer;
  int gmtOffset;
  //
  bool vibrateOClock;
} picowatchSettings;

class PicoWatch {
public:
  #ifdef ARDUINO_ESP32S3_DEV
   static PicoWatch32KRTC RTC;
  #else
   static PicoWatchRTC RTC;
  #endif
  static GxEPD2_BW<PicoWatchDisplay, PicoWatchDisplay::HEIGHT> display;
  tmElements_t currentTime;
  picowatchSettings settings;

public:
  explicit PicoWatch(const picowatchSettings &s) : settings(s) {} // constructor
  void init(String datetime = "");
  void deepSleep();
  float getBatteryVoltage();
  uint8_t getBoardRevision();
  void vibMotor(uint8_t intervalMs = 100, uint8_t length = 20);

  virtual void handleButtonPress();
  void showMenu(byte menuIndex, bool partialRefresh);
  void showFastMenu(byte menuIndex);
  // Settings submenu (About/Vibrate/Accelerometer/Set Time/WiFi/Sync NTP/Set
  // Timezone) - reached via the top-level "Settings" entry, same
  // list-rendering pattern as showMenu()/showFastMenu() but for
  // SETTINGS_MENU_LENGTH items and SETTINGS_MENU_STATE.
  void showSettingsMenu(byte settingsMenuIndex, bool partialRefresh);
  void showFastSettingsMenu(byte settingsMenuIndex);
  // Games submenu (Snake/Pong/Tetris/Flappy) - reached via the top-level
  // "Games" entry, same list-rendering pattern as showSettingsMenu() but
  // for GAMES_MENU_LENGTH items and GAMES_MENU_STATE. Fits on screen at
  // every font size without scrolling (only 4 entries).
  void showGamesMenu(byte gamesMenuIndex, bool partialRefresh);
  void showFastGamesMenu(byte gamesMenuIndex);
  void playSnake();
  void playPong();
  void playTetris();
  void playFlappy();
  // Time submenu (Set Time/Sync NTP/Set Timezone/Vibrate Window) - reached
  // via the Settings menu's "Time" entry, groups the previously separate
  // top-level Settings entries together. Same list-rendering pattern as
  // showGamesMenu() but for TIME_MENU_LENGTH items and TIME_MENU_STATE.
  void showTimeMenu(byte timeMenuIndex, bool partialRefresh);
  void showFastTimeMenu(byte timeMenuIndex);
  // Debug submenu (Vibrate Motor Test/Show Accelerometer) - reached via the
  // Settings menu's "Debug" entry, so those two rarely-used diagnostic
  // screens don't take up top-level Settings rows. Same list-rendering
  // pattern as showTimeMenu()/showFastTimeMenu() but for DEBUG_MENU_LENGTH
  // items and DEBUG_MENU_STATE.
  void showDebugMenu(byte debugMenuIndex, bool partialRefresh);
  void showFastDebugMenu(byte debugMenuIndex);
  // From-hour/To-hour range + on/off for the hourly vibrate-on-the-tick
  // feature (see PicoWatch::init()'s WATCHFACE_STATE tick handler),
  // persisted in flash (NVS). Same 3-field picker pattern as setAlarm().
  void showVibrateWindowSettings();
  void showAbout();
  void showBuzz();
  void showAccelerometer();
  void showSyncNTP();
  bool syncNTP();
  bool syncNTP(long gmt);
  bool syncNTP(long gmt, String ntpServer);
  void setTime();
  void setTimezone(); // interactive GMT offset picker, persisted in flash (NVS) - see PicoWatch.cpp
  void setWeatherCity(); // interactive 7-digit OpenWeatherMap city ID picker, persisted in flash (NVS)
  void setupWifi();
  bool connectWiFi();
  // Checks this repo's latest GitHub release and, if newer than the running
  // firmware, downloads + flashes GITHUB_OTA_ASSET_NAME (SHA256-verified
  // against the release asset's digest) and reboots. Blocking, shows
  // progress on the e-ink display. Shared by the Settings menu's "Update via
  // GitHub" item and the web UI's "GitHub Update" button.
  void updateFromGithub();
  // Menu/Back swap toggle, plus short/long-press action pickers for Up/Down
  // while on the watchface (see config.h WATCHFACE_ACTION_* / LONG_PRESS_MS).
  // Persisted in flash (NVS).
  void showButtonSettings();
  // Small/Default/Big font size for the top-level menu and Settings list
  // (see config.h UI_FONT_SIZE_*). Persisted in flash (NVS).
  void showFontSizeSettings();
  // White-on-black vs black-on-white for every list menu (main, Settings,
  // Games, Time, Debug, watchface picker). Persisted in flash (NVS).
  void showInvertMenuSettings();
  // How often (1-10 min) _checkBleNotifications() opens its BLE window -
  // battery/latency tradeoff, see config.h's BLE_NOTIFY_CHECK_INTERVAL_MIN
  // comment. Persisted in flash (NVS).
  void showNotifyIntervalSettings();
  // English/Deutsch UI language picker (see localization.h). Persisted in
  // flash (NVS), reloaded in init()'s reset path like the other settings.
  void showLanguageSettings();
  // WLAN vs BLE (Gadgetbridge phone proxy) for weather/time sync - see
  // config.h's INTERNET_ACCESS_*/BLE_HTTP_* and _httpViaBle(). Persisted
  // in flash (NVS).
  void showInternetAccessSettings();
  // Notification popup/icon controls (see config.h's NOTIFICATION_POPUP_*
  // comment) - popup on/off, popup duration, watchface icon on/off, icon
  // color. Persisted in flash (NVS).
  void showNotificationSettings();
  weatherData getWeatherData();

  // Top-level Stopwatch and Steps (Last 7 Days) apps - generic, not
  // per-watchface, so implemented directly here rather than as virtual
  // hooks. See PicoWatch.cpp for the step-history capture, which runs once per
  // minute regardless of which face is active (previously only happened to
  // run inside 7_SEG's own draw method).
  void showStopwatch();
  void showStepsHistory();
  // Today's step count so far - stepsBaseline (persisted, see PicoWatch.cpp)
  // plus the BMA423's own live counter, NOT just sensor.getCounter() alone.
  // Use this everywhere a watchface wants to show "steps today" instead of
  // reading the sensor directly, otherwise a reset/reflash silently drops
  // back to 0 (see PicoWatch.cpp's kPrefsStepsBaselineKey comment).
  uint32_t todaySteps();
  void setAlarm(); // Hour/Minute/Enabled picker, persisted in flash (NVS)
  void showWeatherForecast(); // 5-day forecast via OpenWeatherMap's free /forecast endpoint
  // Browses the last NOTIFICATION_COUNT phone notifications received via
  // Gadgetbridge (see _checkBleNotifications()) - Menu opens the full
  // title/body, Back returns to the main menu.
  void showNotifications();

  void showWatchFace(bool partialRefresh);
  virtual void drawWatchFace(); // override this method for different watch
                                // faces

  // Lets the user pick a different watchface design, called from the
  // "Change Watchface" menu item. No-op by default; only meaningful for a
  // PicoWatch subclass that actually holds more than one drawWatchFace()
  // implementation (see MultiFacePicoWatch). May return to either
  // WATCHFACE_STATE (design applied) or MAIN_MENU_STATE (cancelled) -
  // handleButtonPress() checks guiState afterward rather than assuming.
  virtual void changeWatchface() {}

  // Called once early during a true power-on reset (not a deep-sleep wake),
  // before the first showWatchFace(). No-op by default; MultiFacePicoWatch uses
  // it to load its persisted selectedFace from flash so a reset doesn't
  // silently fall back to face 0.
  virtual void onReset() {}

  // Web UI hooks for the "Watchface" settings page (see setupWifi()) - a
  // plain PicoWatch has exactly one, fixed watchface, so the default
  // (count 0) hides that page entirely. MultiFacePicoWatch overrides all
  // four to expose its face picker over the web too.
  virtual int webFaceCount() { return 0; }
  virtual const char *webFaceName(int index) { return ""; }
  virtual int webSelectedFace() { return 0; }
  virtual void webSetFace(int index) {}

protected:
  // Shared list-menu renderer (scrollable, width-truncated, respects
  // Settings -> Font Size) - see PicoWatch.cpp for the full comment.
  // Protected so a subclass's own list-style picker screens (e.g.
  // MultiFacePicoWatch::changeWatchface()) can use it too instead of
  // hand-rolling their own drawing loop with a hardcoded font.
  void uiRenderList(const char *const *items, int total, int selectedIndex, bool partialRefresh);

private:
  void _bmaConfig();
  // If a new day has started since the last check, records yesterday's step
  // count into the persisted 7-day history and resets the live counter.
  // Called once per minute from init()'s WATCHFACE_STATE tick handler,
  // regardless of which watchface is active.
  void _captureStepsAtMidnight();
  // Snapshots todaySteps() to NVS once per minute (skipped if unchanged
  // since the last snapshot) so a reset never loses more than ~1 minute of
  // progress - see kPrefsStepsBaselineKey's comment in PicoWatch.cpp.
  void _persistStepsProgress();
  // Every BLE_NOTIFY_CHECK_INTERVAL_MIN minutes, opens a short BLE
  // advertising window (BLE_NOTIFY_WINDOW_MS) so Gadgetbridge can connect
  // and flush any queued notifications - see config.h's BLE_NOTIFY_*
  // comment. Called from init()'s per-minute WATCHFACE_STATE tick handler.
  void _checkBleNotifications();
  // Vibrates + shows the most recent notification as a dismissable
  // overlay, called right after _checkBleNotifications() when a new one
  // arrived. Menu opens the full detail (_showNotificationDetail()), any
  // other button (or a timeout) dismisses back to the watchface.
  void _showNotificationPopup();
  bool _showNotificationDetail(int index); // true if Menu deleted it - see .cpp
  // Small envelope icon, top-center, drawn over every watchface (called
  // from the shared showWatchFace(), not per-face drawWatchFace()) while
  // hasUnreadNotification is set - Jan wanted an always-visible hint that
  // there's something to check without needing the popup to still be on
  // screen. See .cpp for why this is a drawn shape, not a bitmap.
  void _drawNotificationIndicator();
  // Blocking HTTP-over-BLE request via Gadgetbridge's phone proxy (see
  // BLE::httpGet() et al. and config.h's BLE_HTTP_*): opens a BLE window,
  // waits for a phone to connect and Gadgetbridge's own handshake grace
  // period, sends the request, waits for the response, tears the radio
  // back down. Returns false on any timeout/failure; on success `body`
  // holds Gadgetbridge's response body. Used by getWeatherData()/BLE time
  // sync when Settings -> "Internet Access" is set to BLE instead of WLAN.
  bool _httpViaBle(const String &url, String &body);
  // BLE-mode equivalent of syncNTP() - true NTP needs a UDP socket, which
  // Gadgetbridge's HTTP-only proxy can't carry, so this queries a public
  // HTTP time API (config.h's BLE_TIME_API_URL) via _httpViaBle() instead
  // and sets the RTC from its response. `gmt` is the offset in seconds,
  // same meaning as syncNTP(long)'s parameter.
  bool _syncTimeViaBle(long gmt);
  // On-demand 60s BLE advertising window for initial Gadgetbridge pairing
  // (the periodic _checkBleNotifications() window is too short/rare to
  // reliably catch manually) - reached via Menu on showNotifications()'s
  // empty-list screen.
  void _pairBluetooth();
  static void _configModeCallback(WiFiManager *myWiFiManager);
  static uint16_t _readRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len);
  static uint16_t _writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                 uint16_t len);
  weatherData _getWeatherData(String cityID, String lat, String lon, String units, String lang,
                             String url, String apiKey, uint8_t updateInterval);                                 
};

extern RTC_DATA_ATTR int guiState;
extern RTC_DATA_ATTR int menuIndex;
extern RTC_DATA_ATTR int settingsMenuIndex;
extern RTC_DATA_ATTR uint8_t alarmHour;
extern RTC_DATA_ATTR uint8_t alarmMinute;
extern RTC_DATA_ATTR bool alarmEnabled;
extern RTC_DATA_ATTR BMA423 sensor;
extern RTC_DATA_ATTR bool WIFI_CONFIGURED;
extern RTC_DATA_ATTR bool BLE_CONFIGURED;
extern RTC_DATA_ATTR bool USB_PLUGGED_IN;

#endif
