#include "PicoWatch.h"
#include "localization.h"
#include "MenuIcons.h"
#include <Preferences.h>
#include <mbedtls/sha256.h> // SHA256 verification for GitHub OTA - see PicoWatch::updateFromGithub()
#include <Fonts/FreeMonoBold12pt7b.h> // "Big" menu font size - see PicoWatch::showFontSizeSettings()

// Cached copy of the persisted alarm (see setAlarm()/onReset()) - survives
// deep sleep like guiState/menuIndex; loaded from flash once on reset rather
// than re-reading NVS every single per-minute tick. Declared here (ahead of
// the anonymous namespace below) since loadAlarm()/saveAlarm() reference it.
RTC_DATA_ATTR uint8_t alarmHour;
RTC_DATA_ATTR uint8_t alarmMinute;
RTC_DATA_ATTR bool alarmEnabled;

// Weather city ID actually used at runtime (OpenWeatherMap numeric ID, see
// https://openweathermap.org/current#cityid) - defaults to settings.cityID
// but can be overridden on-device via PicoWatch::setWeatherCity() without a
// recompile. Can't just mutate settings.cityID directly: settings is a
// regular (non-RTC) object member reconstructed from the compile-time
// default on every wake from deep sleep, so an override stored there would
// vanish after the very next sleep cycle.
RTC_DATA_ATTR char weatherCityID[12];

// Button remapping (see PicoWatch::showButtonSettings()) - cached copies of
// what's persisted in flash (NVS), loaded once on reset like the alarm
// above rather than re-reading NVS on every single button press.
RTC_DATA_ATTR bool menuBackSwapped;
RTC_DATA_ATTR uint8_t watchfaceUpShortAction;
RTC_DATA_ATTR uint8_t watchfaceUpLongAction;
RTC_DATA_ATTR uint8_t watchfaceDownShortAction;
RTC_DATA_ATTR uint8_t watchfaceDownLongAction;

// Menu/Settings-list font size (see PicoWatch::showFontSizeSettings()) -
// same caching rationale as above.
RTC_DATA_ATTR uint8_t uiFontSize;

// Random WiFi-setup AP password, generated once per true power-on reset (see
// init()'s "default: reset" case) and shown on-screen in _configModeCallback
// - matches pfsense-status-esp32's actual approach (main.cpp's
// generateApPassword(): an 8-char password from a fixed charset via
// esp_random(), regenerated on boot, never persisted) instead of a hardcoded
// AP password anyone reading the source could use.
RTC_DATA_ATTR char wifiApPassword[9];

namespace {
// Manual timezone override, persisted in flash (NVS) so it survives power
// loss, not just deep sleep (unlike the RTC_DATA_ATTR globals below). Once
// set via PicoWatch::setTimezone(), the weather-fetch code stops overwriting
// gmtOffset with the queried city's timezone (see the weather API handling
// further down) - that auto-overwrite was clobbering manually-corrected
// times whenever weather/NTP synced.
constexpr const char *kPrefsNamespace = "picowatch";
constexpr const char *kPrefsManualKey = "tzManual";
constexpr const char *kPrefsOffsetKey = "gmtOffset";
constexpr long kGmtOffsetStepSec = 15 * 60;       // 15 minutes - covers every real-world UTC offset (e.g. UTC+5:45)
constexpr long kGmtOffsetMinSec = -12 * 3600;      // UTC-12
constexpr long kGmtOffsetMaxSec = 14 * 3600;       // UTC+14

bool loadManualGmtOffset(long &outOffsetSec) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  bool manual = prefs.getBool(kPrefsManualKey, false);
  if (manual) {
    outOffsetSec = prefs.getLong(kPrefsOffsetKey, 0);
  }
  prefs.end();
  return manual;
}

void saveManualGmtOffset(long offsetSec) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putBool(kPrefsManualKey, true);
  prefs.putLong(kPrefsOffsetKey, offsetSec);
  prefs.end();
}

// Web menu login password (see PicoWatch::setupWifi()'s connected-webserver
// block) - falls back to WEB_MENU_PASSWORD_DEFAULT until the user changes it
// via the "Change Password" form on that page.
constexpr const char *kPrefsWebPassKey = "webPass";

String loadWebMenuPassword() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  String pass = prefs.getString(kPrefsWebPassKey, WEB_MENU_PASSWORD_DEFAULT);
  prefs.end();
  return pass;
}

void saveWebMenuPassword(const String &pass) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putString(kPrefsWebPassKey, pass);
  prefs.end();
}

// Step-count history for the last 7 complete days, persisted in flash (NVS)
// so it survives a reset. history[0] is the most recently completed day,
// history[6] the oldest. Captured once per real day at 00:00 regardless of
// which watchface is active (see PicoWatch::init()'s WATCHFACE_STATE tick
// handler) - previously this reset only happened to run because 7_SEG's own
// draw method checked for midnight, so any other active face silently never
// reset (or recorded) the daily step count at all.
constexpr const char *kPrefsStepsHistKey = "stepsHist";
constexpr const char *kPrefsStepsDayKey = "stepsDay";
constexpr int kStepsHistoryDays = 7;

// Not a real calendar day count, just a cheap monotonically-increasing
// per-day number good enough to detect "a new day started since we last
// looked" - exact calendar math isn't needed here.
long dayNumber(const tmElements_t &t) {
  return (long)tmYearToCalendar(t.Year) * 372L + (long)t.Month * 31L + (long)t.Day;
}

void loadStepsHistory(int32_t (&history)[kStepsHistoryDays]) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  size_t got = prefs.getBytes(kPrefsStepsHistKey, history, sizeof(int32_t) * kStepsHistoryDays);
  prefs.end();
  if (got != sizeof(int32_t) * kStepsHistoryDays) {
    for (int i = 0; i < kStepsHistoryDays; i++) history[i] = 0;
  }
}

constexpr const char *kPrefsAlarmHourKey = "alarmH";
constexpr const char *kPrefsAlarmMinuteKey = "alarmM";
constexpr const char *kPrefsAlarmEnabledKey = "alarmOn";

void loadAlarm() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  alarmHour = prefs.getUChar(kPrefsAlarmHourKey, 7);
  alarmMinute = prefs.getUChar(kPrefsAlarmMinuteKey, 0);
  alarmEnabled = prefs.getBool(kPrefsAlarmEnabledKey, false);
  prefs.end();
}

void saveAlarm() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsAlarmHourKey, alarmHour);
  prefs.putUChar(kPrefsAlarmMinuteKey, alarmMinute);
  prefs.putBool(kPrefsAlarmEnabledKey, alarmEnabled);
  prefs.end();
}

constexpr const char *kPrefsBtnSwapKey = "btnSwap";
constexpr const char *kPrefsUpShortKey = "upShort";
constexpr const char *kPrefsUpLongKey = "upLong";
constexpr const char *kPrefsDownShortKey = "dnShort";
constexpr const char *kPrefsDownLongKey = "dnLong";

void loadButtonSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  menuBackSwapped = prefs.getBool(kPrefsBtnSwapKey, false);
  watchfaceUpShortAction = prefs.getUChar(kPrefsUpShortKey, WATCHFACE_ACTION_NONE);
  watchfaceUpLongAction = prefs.getUChar(kPrefsUpLongKey, WATCHFACE_ACTION_NONE);
  watchfaceDownShortAction = prefs.getUChar(kPrefsDownShortKey, WATCHFACE_ACTION_NONE);
  watchfaceDownLongAction = prefs.getUChar(kPrefsDownLongKey, WATCHFACE_ACTION_NONE);
  prefs.end();
}

void saveButtonSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putBool(kPrefsBtnSwapKey, menuBackSwapped);
  prefs.putUChar(kPrefsUpShortKey, watchfaceUpShortAction);
  prefs.putUChar(kPrefsUpLongKey, watchfaceUpLongAction);
  prefs.putUChar(kPrefsDownShortKey, watchfaceDownShortAction);
  prefs.putUChar(kPrefsDownLongKey, watchfaceDownLongAction);
  prefs.end();
}

// Same 65-char set pfsense-status-esp32 uses (upper/lower letters minus the
// ambiguous I/O/l/o, digits 2-9, a handful of symbols) - see main.cpp's
// generateApPassword().
void generateWifiApPassword() {
  static const char kCharset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%&*";
  constexpr size_t kCharsetLen = sizeof(kCharset) - 1;
  for (int i = 0; i < 8; i++) {
    wifiApPassword[i] = kCharset[esp_random() % kCharsetLen];
  }
  wifiApPassword[8] = '\0';
}

const char *watchfaceActionName(uint8_t action) {
  switch (action) {
  case WATCHFACE_ACTION_SETTINGS: return PW_ACTION_SETTINGS;
  case WATCHFACE_ACTION_CHANGE_WATCHFACE: return PW_ACTION_CHANGE_WATCHFACE;
  case WATCHFACE_ACTION_WEATHER: return PW_ACTION_WEATHER;
  case WATCHFACE_ACTION_STOPWATCH: return PW_ACTION_STOPWATCH;
  case WATCHFACE_ACTION_ALARM: return PW_ACTION_ALARM;
  default: return PW_ACTION_NONE;
  }
}

constexpr const char *kPrefsFontSizeKey = "uiFont";

void loadFontSize() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  uiFontSize = prefs.getUChar(kPrefsFontSizeKey, UI_FONT_SIZE_DEFAULT);
  prefs.end();
}

void saveFontSize() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsFontSizeKey, uiFontSize);
  prefs.end();
}

constexpr const char *kPrefsLanguageKey = "uiLang";

void loadLanguage() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  picowatchLanguage = prefs.getUChar(kPrefsLanguageKey, PICOWATCH_LANG);
  prefs.end();
}

void saveLanguage() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsLanguageKey, picowatchLanguage);
  prefs.end();
}

const char *fontSizeName(uint8_t size) {
  switch (size) {
  case UI_FONT_SIZE_SMALL: return PW_FONT_SIZE_SMALL;
  case UI_FONT_SIZE_BIG: return PW_FONT_SIZE_BIG;
  default: return PW_FONT_SIZE_DEFAULT;
  }
}

// Scoped to just the top-level menu and Settings list (see config.h) - the
// built-in classic GFX font (nullptr) is genuinely smaller than any bundled
// FreeMono size, so it stands in for "Small" rather than trying to find/ship
// a smaller mono font.
const GFXfont *uiMenuFont() {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: return nullptr;
  case UI_FONT_SIZE_BIG: return &FreeMonoBold12pt7b;
  default: return &FreeMonoBold9pt7b;
  }
}

// Row spacing and highlight-box padding tuned per size - chosen so 6 rows
// (MENU_LENGTH, the fixed top-level menu) always fit within the 200px
// display regardless of size (16*6=96, 25*6=150, 32*6=192).
int uiMenuRowHeight() {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: return 16;
  case UI_FONT_SIZE_BIG: return 32;
  default: return MENU_HEIGHT;
  }
}

// How many Settings-list rows fit edge-to-edge in DISPLAY_HEIGHT at the
// current row height, capped at the list's actual length (no point
// reserving scroll space that will never be used). Previously a fixed
// SETTINGS_MENU_VISIBLE_ROWS=6 left a visibly empty black bar at the
// bottom with the Default row height (6*25=150 of 200px) - computing this
// fills the screen instead.
int uiSettingsVisibleRows() {
  const int rows = DISPLAY_HEIGHT / uiMenuRowHeight();
  return min(rows, (int)SETTINGS_MENU_LENGTH);
}

// Fixed regardless of font size - the icon itself stays 12x12 at every
// size, only the surrounding row height/font changes.
constexpr int16_t kMenuIconX = 2;
constexpr int16_t kMenuIconTextX = 18;

void uiMenuHighlightPadding(int16_t &yOffset, uint16_t &heightPad) {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: yOffset = -3; heightPad = 6; break;
  case UI_FONT_SIZE_BIG: yOffset = -12; heightPad = 18; break;
  default: yOffset = -10; heightPad = 15; break;
  }
}

constexpr const char *kPrefsCityIdKey = "cityId";

// Loads the saved city ID into weatherCityID, falling back to (and
// persisting) the compile-time settings.cityID default the first time this
// runs on a given device.
void loadWeatherCityID(const String &defaultCityID) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  String saved = prefs.getString(kPrefsCityIdKey, "");
  prefs.end();
  if (saved.length() == 0) saved = defaultCityID;
  strncpy(weatherCityID, saved.c_str(), sizeof(weatherCityID) - 1);
  weatherCityID[sizeof(weatherCityID) - 1] = '\0';
}

void saveWeatherCityID(const char *cityID) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putString(kPrefsCityIdKey, cityID);
  prefs.end();
}

// Same weather-condition-code ranges as draw7SegWeather() in the AllFaces
// example (https://openweathermap.org/weather-conditions), just labelled
// text instead of an icon bitmap.
const char *weatherConditionLabel(int code) {
  if (code > 801) return PW_WEATHER_COND_CLOUDY;
  if (code == 801) return PW_WEATHER_COND_FEW_CLOUDS;
  if (code == 800) return PW_WEATHER_COND_CLEAR;
  if (code >= 700) return PW_WEATHER_COND_HAZE;
  if (code >= 600) return PW_WEATHER_COND_SNOW;
  if (code >= 500) return PW_WEATHER_COND_RAIN;
  if (code >= 300) return PW_WEATHER_COND_DRIZZLE;
  if (code >= 200) return PW_WEATHER_COND_STORM;
  return PW_WEATHER_COND_UNKNOWN;
}

struct DayForecastEntry {
  char date[11] = ""; // "YYYY-MM-DD"
  int temp = 0;
  int conditionCode = 0;
};
}  // namespace

#ifdef ARDUINO_ESP32S3_DEV
  PicoWatch32KRTC PicoWatch::RTC;
  #define ACTIVE_LOW 0
#else
  PicoWatchRTC PicoWatch::RTC;
  #define ACTIVE_LOW 1
#endif
GxEPD2_BW<PicoWatchDisplay, PicoWatchDisplay::HEIGHT> PicoWatch::display(
    PicoWatchDisplay{});

RTC_DATA_ATTR int guiState;
RTC_DATA_ATTR int menuIndex;
RTC_DATA_ATTR int settingsMenuIndex;
RTC_DATA_ATTR BMA423 sensor;
RTC_DATA_ATTR bool WIFI_CONFIGURED;
RTC_DATA_ATTR bool BLE_CONFIGURED;
RTC_DATA_ATTR weatherData currentWeather;
RTC_DATA_ATTR int weatherIntervalCounter = -1;
RTC_DATA_ATTR long gmtOffset = 0;
RTC_DATA_ATTR bool alreadyInMenu         = true;
RTC_DATA_ATTR bool USB_PLUGGED_IN = false;
RTC_DATA_ATTR tmElements_t bootTime;
RTC_DATA_ATTR uint32_t lastIPAddress;
RTC_DATA_ATTR char lastSSID[30];

void PicoWatch::init(String datetime) {
  setCpuFrequencyMhz(CPU_FREQ_MHZ); // see config.h - keep >=80 for WiFi/BT
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause(); // get wake up reason
  #ifdef ARDUINO_ESP32S3_DEV
    Wire.begin(PICOWATCH_V3_SDA, PICOWATCH_V3_SCL);     // init i2c
  #else
    Wire.begin(SDA, SCL);                         // init i2c
  #endif
  RTC.init();
  // Init the display since is almost sure we will use it
  display.epd2.initPicoWatch();

  switch (wakeup_reason) {
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_TIMER: // RTC Alarm
  #else
  case ESP_SLEEP_WAKEUP_EXT0: // RTC Alarm
  #endif
    RTC.read(currentTime);
    switch (guiState) {
    case WATCHFACE_STATE:
      showWatchFace(true); // partial updates on tick
      if (settings.vibrateOClock) {
        if (currentTime.Minute == 0) {
          // The RTC wakes us up once per minute
          vibMotor(75, 4);
        }
      }
      _captureStepsAtMidnight();
      if (alarmEnabled && currentTime.Hour == alarmHour && currentTime.Minute == alarmMinute) {
        vibMotor(150, 10); // longer/more pulses than the o'clock vibration, so it's distinguishable
      }
      break;
    case MAIN_MENU_STATE:
      // Return to watchface if in menu for more than one tick
      if (alreadyInMenu) {
        guiState = WATCHFACE_STATE;
        showWatchFace(false);
      } else {
        alreadyInMenu = true;
      }
      break;
    }
    break;
  case ESP_SLEEP_WAKEUP_EXT1: // button Press
    handleButtonPress();
    break;
  #ifdef ARDUINO_ESP32S3_DEV
  case ESP_SLEEP_WAKEUP_EXT0: // USB plug in
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    if(guiState == WATCHFACE_STATE){
      RTC.read(currentTime);
      showWatchFace(true);
    }
    break;
  #endif
  default: // reset
    onReset();
    RTC.config(datetime);
    _bmaConfig();
    #ifdef ARDUINO_ESP32S3_DEV
    pinMode(USB_DET_PIN, INPUT);
    USB_PLUGGED_IN = (digitalRead(USB_DET_PIN) == 1);
    #endif    
    {
      long manualOffset;
      gmtOffset = loadManualGmtOffset(manualOffset) ? manualOffset : settings.gmtOffset;
    }
    loadAlarm();
    loadWeatherCityID(settings.cityID);
    loadButtonSettings();
    loadFontSize();
    loadLanguage();
    generateWifiApPassword();
    RTC.read(currentTime);
    RTC.read(bootTime);
    showWatchFace(false); // full update on reset
    vibMotor(75, 4);
    // For some reason, seems to be enabled on first boot
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    break;
  }
  deepSleep();
}
void PicoWatch::deepSleep() {
  display.hibernate();
  RTC.clearAlarm();        // resets the alarm flag in the RTC
  #ifdef ARDUINO_ESP32S3_DEV
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USB_DET_PIN, USB_PLUGGED_IN ? LOW : HIGH); //// enable deep sleep wake on USB plug in/out
  rtc_gpio_set_direction((gpio_num_t)USB_DET_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)USB_DET_PIN);

  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_LOW); // enable deep sleep wake on button press
  rtc_gpio_set_direction((gpio_num_t)UP_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)UP_BTN_PIN);

  rtc_clk_32k_enable(true);
  //rtc_clk_slow_freq_set(RTC_SLOW_FREQ_32K_XTAL);
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  int secToNextWake = 60 - timeinfo.tm_sec; // next full minute, as before
  // NIGHT_SLEEP_AFTER_HOUR > NIGHT_SLEEP_BEFORE_HOUR means the night window
  // spans midnight (e.g. 23 > 5 covers 23:00-04:59).
  bool isNight = NIGHT_SLEEP_AFTER_HOUR > NIGHT_SLEEP_BEFORE_HOUR
                     ? (timeinfo.tm_hour >= NIGHT_SLEEP_AFTER_HOUR ||
                        timeinfo.tm_hour < NIGHT_SLEEP_BEFORE_HOUR)
                     : (timeinfo.tm_hour >= NIGHT_SLEEP_AFTER_HOUR &&
                        timeinfo.tm_hour < NIGHT_SLEEP_BEFORE_HOUR);
  if (isNight && NIGHT_SLEEP_FOR_M > 1) {
    // Align to minutes-since-midnight (not minutes-within-the-hour) so
    // NIGHT_SLEEP_FOR_M=45 always lands on 00:00 exactly, regardless of
    // which hour sleep started in - 00:00 is always a multiple of any
    // interval, keeping _captureStepsAtMidnight()'s exact-minute check
    // reliable even though most other wakes get skipped.
    int minutesSinceMidnight = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int extraMinutes =
        NIGHT_SLEEP_FOR_M - 1 - (minutesSinceMidnight % NIGHT_SLEEP_FOR_M);
    // Never sleep past an enabled alarm's target time, so it still fires
    // even if it falls inside the night window.
    if (alarmEnabled) {
      int alarmMinutesSinceMidnight = alarmHour * 60 + alarmMinute;
      int minutesUntilAlarm = alarmMinutesSinceMidnight - minutesSinceMidnight;
      if (minutesUntilAlarm <= 0) minutesUntilAlarm += 24 * 60;
      if (minutesUntilAlarm - 1 < extraMinutes) {
        extraMinutes = minutesUntilAlarm - 1;
      }
    }
    secToNextWake += extraMinutes * 60;
  }
  esp_sleep_enable_timer_wakeup(secToNextWake * uS_TO_S_FACTOR);
  #else
  // Set GPIOs 0-39 to input to avoid power leaking out
  const uint64_t ignore = 0b11110001000000110000100111000010; // Ignore some GPIOs due to resets
  for (int i = 0; i < GPIO_NUM_MAX; i++) {
    if ((ignore >> i) & 0b1)
      continue;
    pinMode(i, INPUT);
  }
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_INT_PIN,
                               0); // enable deep sleep wake on RTC interrupt
  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_HIGH); // enable deep sleep wake on button press
  #endif
  esp_deep_sleep_start();
}

namespace {
// Top-level menu (case 0-3, MAIN_MENU_STATE) and Settings submenu (case 0-6,
// SETTINGS_MENU_STATE) selections share this shape across both the
// single-press switch and its fast-menu-loop duplicate below, so it's
// factored out once instead of copy-pasted four times.
void dispatchTopMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->changeWatchface();
    break;
  case 1:
    w->showStopwatch();
    break;
  case 2:
    w->showStepsHistory();
    break;
  case 3:
    w->setAlarm();
    break;
  case 4:
    w->showWeatherForecast();
    break;
  case 5:
    w->showSettingsMenu(settingsMenuIndex, false);
    break;
  default:
    break;
  }
}

void dispatchSettingsMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->showAbout();
    break;
  case 1:
    w->showBuzz();
    break;
  case 2:
    w->showAccelerometer();
    break;
  case 3:
    w->setTime();
    break;
  case 4:
    w->setupWifi();
    break;
  /*case 5:
    w->showUpdateFW();
    break;*/
  case 5:
    w->showSyncNTP();
    break;
  case 6:
    w->setTimezone();
    break;
  case 7:
    w->setWeatherCity();
    break;
  case 8:
    w->updateFromGithub();
    break;
  case 9:
    w->showButtonSettings();
    break;
  case 10:
    w->showFontSizeSettings();
    break;
  case 11:
    w->showLanguageSettings();
    break;
  default:
    break;
  }
}

// Dispatches a watchface-screen Up/Down press (short or long) to whatever
// action the user assigned it via showButtonSettings() - see config.h's
// WATCHFACE_ACTION_* constants. ACTION_NONE is a deliberate no-op.
void dispatchWatchfaceAction(PicoWatch *w, uint8_t action) {
  switch (action) {
  case WATCHFACE_ACTION_SETTINGS:
    w->showSettingsMenu(settingsMenuIndex, false);
    break;
  case WATCHFACE_ACTION_CHANGE_WATCHFACE:
    w->changeWatchface();
    break;
  case WATCHFACE_ACTION_WEATHER:
    w->showWeatherForecast();
    break;
  case WATCHFACE_ACTION_STOPWATCH:
    w->showStopwatch();
    break;
  case WATCHFACE_ACTION_ALARM:
    w->setAlarm();
    break;
  default:
    break;
  }
}
}  // namespace

void PicoWatch::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();
  // Menu/Back swap (see showButtonSettings()) - only affects top-level menu
  // navigation (this function); individual screens like setTime()/setAlarm()
  // still read the physical MENU_BTN_PIN/BACK_BTN_PIN directly for their own
  // field-to-field navigation, unaffected by this setting.
  const uint64_t logicalMenuMask = menuBackSwapped ? BACK_BTN_MASK : MENU_BTN_MASK;
  const uint64_t logicalBackMask = menuBackSwapped ? MENU_BTN_MASK : BACK_BTN_MASK;
  const int logicalMenuPin = menuBackSwapped ? BACK_BTN_PIN : MENU_BTN_PIN;
  const int logicalBackPin = menuBackSwapped ? MENU_BTN_PIN : BACK_BTN_PIN;
  // Menu Button
  if (wakeupBit & logicalMenuMask) {
    if (guiState ==
        WATCHFACE_STATE) { // enter menu state if coming from watch face
      showMenu(menuIndex, false);
    } else if (guiState == MAIN_MENU_STATE) { // if already in menu, then select menu item
      dispatchTopMenu(this, menuIndex);
      // changeWatchface() (case 0) may go straight back to WATCHFACE_STATE
      // instead of the usual MAIN_MENU_STATE - the fast-menu loop below only
      // handles MAIN_MENU_STATE/APP_STATE/FW_UPDATE_STATE/
      // SETTINGS_MENU_STATE, so falling through into it while already on
      // WATCHFACE_STATE would leave the watch appearing frozen (ignoring all
      // button input) for up to 5 seconds. Return immediately in that case,
      // same as the "Back while already on WATCHFACE_STATE" case below.
      if (guiState == WATCHFACE_STATE) return;
    } else if (guiState == SETTINGS_MENU_STATE) { // select settings submenu item
      dispatchSettingsMenu(this, settingsMenuIndex);
    } /*else if (guiState == FW_UPDATE_STATE) {
      updateFWBegin();
    }*/
  }
  // Back Button
  else if (wakeupBit & logicalBackMask) {
    if (guiState == MAIN_MENU_STATE) { // exit to watch face if already in menu
      RTC.read(currentTime);
      showWatchFace(false);
    } else if (guiState == SETTINGS_MENU_STATE) { // exit to top menu if already in settings
      showMenu(menuIndex, false);
    } else if (guiState == APP_STATE) {
      showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in a settings app
    } else if (guiState == FW_UPDATE_STATE) {
      showSettingsMenu(settingsMenuIndex, false);
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Up Button
  else if (wakeupBit & UP_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // increment menu index
      menuIndex--;
      if (menuIndex < 0) {
        menuIndex = MENU_LENGTH - 1;
      }
      showMenu(menuIndex, true);
    } else if (guiState == SETTINGS_MENU_STATE) { // increment settings menu index
      settingsMenuIndex--;
      if (settingsMenuIndex < 0) {
        settingsMenuIndex = SETTINGS_MENU_LENGTH - 1;
      }
      showSettingsMenu(settingsMenuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      // User-assignable short/long press action (see showButtonSettings()).
      // Measure how long the button that just woke us stays held, then
      // dispatch - same "return immediately, don't fall into the fast-menu
      // loop" reasoning as the Menu/Back WATCHFACE_STATE cases above, since
      // the dispatched action manages its own guiState/rendering.
      pinMode(UP_BTN_PIN, INPUT);
      unsigned long pressStart = millis();
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
      const bool isLong = (millis() - pressStart) >= LONG_PRESS_MS;
      dispatchWatchfaceAction(this, isLong ? watchfaceUpLongAction : watchfaceUpShortAction);
      return;
    }
  }
  // Down Button
  else if (wakeupBit & DOWN_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // decrement menu index
      menuIndex++;
      if (menuIndex > MENU_LENGTH - 1) {
        menuIndex = 0;
      }
      showMenu(menuIndex, true);
    } else if (guiState == SETTINGS_MENU_STATE) { // decrement settings menu index
      settingsMenuIndex++;
      if (settingsMenuIndex > SETTINGS_MENU_LENGTH - 1) {
        settingsMenuIndex = 0;
      }
      showSettingsMenu(settingsMenuIndex, true);
    } else if (guiState == WATCHFACE_STATE) {
      // See the identical Up-button case just above for why this measures
      // press duration instead of returning immediately.
      pinMode(DOWN_BTN_PIN, INPUT);
      unsigned long pressStart = millis();
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) delay(10);
      const bool isLong = (millis() - pressStart) >= LONG_PRESS_MS;
      dispatchWatchfaceAction(this, isLong ? watchfaceDownLongAction : watchfaceDownShortAction);
      return;
    }
  }

  /***************** fast menu *****************/
  bool timeout     = false;
  long lastTimeout = millis();
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  while (!timeout) {
    if (millis() - lastTimeout > 5000) {
      timeout = true;
    } else {
      if (digitalRead(logicalMenuPin) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // if already in menu, then select menu item
          dispatchTopMenu(this, menuIndex);
          // See the identical comment in the single-press handler above.
          if (guiState == WATCHFACE_STATE) break;
        } else if (guiState == SETTINGS_MENU_STATE) {
          dispatchSettingsMenu(this, settingsMenuIndex);
        } /*else if (guiState == FW_UPDATE_STATE) {
          updateFWBegin();
        }*/
      } else if (digitalRead(logicalBackPin) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // exit to watch face if already in menu
          RTC.read(currentTime);
          showWatchFace(false);
          break; // leave loop
        } else if (guiState == SETTINGS_MENU_STATE) {
          showMenu(menuIndex, false); // exit to top menu if already in settings
        } else if (guiState == APP_STATE) {
          showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in a settings app
        } else if (guiState == FW_UPDATE_STATE) {
          showSettingsMenu(settingsMenuIndex, false);
        }
      } else if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // increment menu index
          menuIndex--;
          if (menuIndex < 0) {
            menuIndex = MENU_LENGTH - 1;
          }
          showFastMenu(menuIndex);
        } else if (guiState == SETTINGS_MENU_STATE) {
          settingsMenuIndex--;
          if (settingsMenuIndex < 0) {
            settingsMenuIndex = SETTINGS_MENU_LENGTH - 1;
          }
          showFastSettingsMenu(settingsMenuIndex);
        }
      } else if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // decrement menu index
          menuIndex++;
          if (menuIndex > MENU_LENGTH - 1) {
            menuIndex = 0;
          }
          showFastMenu(menuIndex);
        } else if (guiState == SETTINGS_MENU_STATE) {
          settingsMenuIndex++;
          if (settingsMenuIndex > SETTINGS_MENU_LENGTH - 1) {
            settingsMenuIndex = 0;
          }
          showFastSettingsMenu(settingsMenuIndex);
        }
      }
    }
  }
}

void PicoWatch::showMenu(byte menuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(uiMenuFont());

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;
  const int rowHeight = uiMenuRowHeight();
  int16_t highlightYOffset;
  uint16_t highlightHeightPad;
  uiMenuHighlightPadding(highlightYOffset, highlightHeightPad);

  const char *menuItems[] = {PW_MENU_CHANGE_WATCHFACE, PW_MENU_STOPWATCH, PW_MENU_STEPS,
                             PW_MENU_ALARM,             PW_MENU_WEATHER,   PW_MENU_SETTINGS};
  const uint8_t *const menuIcons[] = {iconWatchface, iconStopwatch, iconSteps,
                                      iconAlarm,      iconWeather,   iconSettings};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = rowHeight + (rowHeight * i);
    display.setCursor(kMenuIconTextX, yPos);
    display.getTextBounds(menuItems[i], kMenuIconTextX, yPos, &x1, &y1, &w, &h);
    uint16_t color;
    if (i == menuIndex) {
      display.fillRect(0, y1 + highlightYOffset, DISPLAY_WIDTH, h + highlightHeightPad, GxEPD_WHITE);
      color = GxEPD_BLACK;
    } else {
      color = GxEPD_WHITE;
    }
    display.setTextColor(color);
    display.println(menuItems[i]);
    display.drawBitmap(kMenuIconX, y1 + ((int16_t)h - MENU_ICON_HEIGHT) / 2, menuIcons[i],
                        MENU_ICON_WIDTH, MENU_ICON_HEIGHT, color);
  }

  display.display(partialRefresh);

  guiState = MAIN_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastMenu(byte menuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(uiMenuFont());

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;
  const int rowHeight = uiMenuRowHeight();
  int16_t highlightYOffset;
  uint16_t highlightHeightPad;
  uiMenuHighlightPadding(highlightYOffset, highlightHeightPad);

  const char *menuItems[] = {PW_MENU_CHANGE_WATCHFACE, PW_MENU_STOPWATCH, PW_MENU_STEPS,
                             PW_MENU_ALARM,             PW_MENU_WEATHER,   PW_MENU_SETTINGS};
  const uint8_t *const menuIcons[] = {iconWatchface, iconStopwatch, iconSteps,
                                      iconAlarm,      iconWeather,   iconSettings};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = rowHeight + (rowHeight * i);
    display.setCursor(kMenuIconTextX, yPos);
    display.getTextBounds(menuItems[i], kMenuIconTextX, yPos, &x1, &y1, &w, &h);
    uint16_t color;
    if (i == menuIndex) {
      display.fillRect(0, y1 + highlightYOffset, DISPLAY_WIDTH, h + highlightHeightPad, GxEPD_WHITE);
      color = GxEPD_BLACK;
    } else {
      color = GxEPD_WHITE;
    }
    display.setTextColor(color);
    display.println(menuItems[i]);
    display.drawBitmap(kMenuIconX, y1 + ((int16_t)h - MENU_ICON_HEIGHT) / 2, menuIcons[i],
                        MENU_ICON_WIDTH, MENU_ICON_HEIGHT, color);
  }

  display.display(true);

  guiState = MAIN_MENU_STATE;
}

namespace {
const char *const kSettingsMenuItems[] = {
    PW_SETTINGS_ABOUT,  PW_SETTINGS_VIBRATE, PW_SETTINGS_ACCELEROMETER,
    PW_SETTINGS_SET_TIME, PW_SETTINGS_SETUP_WIFI, /*"Update Firmware",*/
    PW_SETTINGS_SYNC_NTP, PW_SETTINGS_SET_TIMEZONE, PW_SETTINGS_SET_CITY,
    PW_SETTINGS_UPDATE_GITHUB, PW_SETTINGS_BUTTON_SETTINGS, PW_SETTINGS_FONT_SIZE,
    PW_SETTINGS_LANGUAGE};
const uint8_t *const kSettingsMenuIcons[] = {
    iconAbout,  iconVibrate, iconAccel,
    iconTime,   iconWifi,
    iconSync,   iconTimezone, iconCity,
    iconUpdate, iconButtons, iconFontSize, iconLanguage};

// Fitting all SETTINGS_MENU_LENGTH items on screen at once made the rows too
// cramped to read. Instead this shows a scrolling window (sized by
// uiSettingsVisibleRows(), which fills the display edge-to-edge at the
// current font size) at comfortable spacing, keeping the current selection
// centered where possible and clamped at the top/bottom of the full list.
// Stateless - recomputed fresh from settingsMenuIndex every render, no
// separate scroll-position variable to keep in sync.
int settingsMenuScrollOffset(int index, int visibleRows) {
  if (SETTINGS_MENU_LENGTH <= visibleRows) return 0;
  const int maxOffset = SETTINGS_MENU_LENGTH - visibleRows;
  int offset = index - visibleRows / 2;
  if (offset < 0) offset = 0;
  if (offset > maxOffset) offset = maxOffset;
  return offset;
}
}  // namespace

void PicoWatch::showSettingsMenu(byte settingsMenuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(uiMenuFont());

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;
  const int rowHeight = uiMenuRowHeight();
  int16_t highlightYOffset;
  uint16_t highlightHeightPad;
  uiMenuHighlightPadding(highlightYOffset, highlightHeightPad);

  const int visibleRows = uiSettingsVisibleRows();
  const int scrollOffset = settingsMenuScrollOffset(settingsMenuIndex, visibleRows);
  const int visibleCount = min(visibleRows, (int)SETTINGS_MENU_LENGTH - scrollOffset);
  for (int row = 0; row < visibleCount; row++) {
    const int i = scrollOffset + row;
    yPos = rowHeight + (rowHeight * row);
    display.setCursor(kMenuIconTextX, yPos);
    display.getTextBounds(kSettingsMenuItems[i], kMenuIconTextX, yPos, &x1, &y1, &w, &h);
    uint16_t color;
    if (i == settingsMenuIndex) {
      display.fillRect(0, y1 + highlightYOffset, DISPLAY_WIDTH, h + highlightHeightPad, GxEPD_WHITE);
      color = GxEPD_BLACK;
    } else {
      color = GxEPD_WHITE;
    }
    display.setTextColor(color);
    display.println(kSettingsMenuItems[i]);
    display.drawBitmap(kMenuIconX, y1 + ((int16_t)h - MENU_ICON_HEIGHT) / 2, kSettingsMenuIcons[i],
                        MENU_ICON_WIDTH, MENU_ICON_HEIGHT, color);
  }

  display.display(partialRefresh);

  guiState = SETTINGS_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastSettingsMenu(byte settingsMenuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(uiMenuFont());

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;
  const int rowHeight = uiMenuRowHeight();
  int16_t highlightYOffset;
  uint16_t highlightHeightPad;
  uiMenuHighlightPadding(highlightYOffset, highlightHeightPad);

  const int visibleRows = uiSettingsVisibleRows();
  const int scrollOffset = settingsMenuScrollOffset(settingsMenuIndex, visibleRows);
  const int visibleCount = min(visibleRows, (int)SETTINGS_MENU_LENGTH - scrollOffset);
  for (int row = 0; row < visibleCount; row++) {
    const int i = scrollOffset + row;
    yPos = rowHeight + (rowHeight * row);
    display.setCursor(kMenuIconTextX, yPos);
    display.getTextBounds(kSettingsMenuItems[i], kMenuIconTextX, yPos, &x1, &y1, &w, &h);
    uint16_t color;
    if (i == settingsMenuIndex) {
      display.fillRect(0, y1 + highlightYOffset, DISPLAY_WIDTH, h + highlightHeightPad, GxEPD_WHITE);
      color = GxEPD_BLACK;
    } else {
      color = GxEPD_WHITE;
    }
    display.setTextColor(color);
    display.println(kSettingsMenuItems[i]);
    display.drawBitmap(kMenuIconX, y1 + ((int16_t)h - MENU_ICON_HEIGHT) / 2, kSettingsMenuIcons[i],
                        MENU_ICON_WIDTH, MENU_ICON_HEIGHT, color);
  }

  display.display(true);

  guiState = SETTINGS_MENU_STATE;
}

void PicoWatch::_captureStepsAtMidnight() {
  if (!(currentTime.Hour == 0 && currentTime.Minute == 0)) return;

  const long today = dayNumber(currentTime);

  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  const long lastDay = prefs.getLong(kPrefsStepsDayKey, 0);
  prefs.end();

  if (lastDay == today) return; // already captured for this day (multiple ticks at 00:00 are possible)

  int32_t history[kStepsHistoryDays];
  loadStepsHistory(history);
  for (int i = kStepsHistoryDays - 1; i > 0; i--) history[i] = history[i - 1];
  history[0] = (int32_t)sensor.getCounter();

  Preferences writePrefs;
  writePrefs.begin(kPrefsNamespace, false);  // read-write
  writePrefs.putBytes(kPrefsStepsHistKey, history, sizeof(history));
  writePrefs.putLong(kPrefsStepsDayKey, today);
  writePrefs.end();

  sensor.resetStepCounter();
}

void PicoWatch::showStopwatch() {
  guiState = APP_STATE;

  bool running = false;
  unsigned long startMillis = 0;
  unsigned long elapsedMillis = 0;
  unsigned long lastShownSecond = 9999999; // force the first draw

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);

  // Same button-bounce hazard as setTimezone()/changeWatchface(): Menu was
  // just used to select "Stopwatch" from the menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  while (1) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      if (running) {
        elapsedMillis += millis() - startMillis;
      } else {
        startMillis = millis();
      }
      running = !running;
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10); // one press = one toggle
    }
    if (!running && digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      elapsedMillis = 0;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
    }

    const unsigned long totalMillis = elapsedMillis + (running ? (millis() - startMillis) : 0);
    const unsigned long totalSeconds = totalMillis / 1000;

    // Partial e-ink refreshes roughly once a second while running is plenty
    // for a stopwatch and keeps the display from being hammered; still
    // redraws right away on every Menu/Up press above for input feedback.
    if (totalSeconds != lastShownSecond) {
      lastShownSecond = totalSeconds;
      display.fillScreen(GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);

      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(20, 25);
      display.println(PW_STOPWATCH_TITLE);

      const unsigned long hh = totalSeconds / 3600;
      const unsigned long mm = (totalSeconds % 3600) / 60;
      const unsigned long ss = totalSeconds % 60;

      // Big MM:SS in the same font/size the default watchface uses for
      // HH:MM (guaranteed to fit the 200px width) - hours only shown as a
      // small prefix above when the stopwatch has actually run that long,
      // since cramming HH:MM:SS into one line at this font size would run
      // off the right edge.
      if (hh > 0) {
        display.setFont(&FreeMonoBold9pt7b);
        char hbuf[8];
        snprintf(hbuf, sizeof(hbuf), "%luh", hh);
        display.setCursor(5, 60);
        display.println(hbuf);
      }

      char buf[8];
      snprintf(buf, sizeof(buf), "%02lu:%02lu", mm, ss);
      display.setFont(&DSEG7_Classic_Bold_53);
      display.setCursor(5, 53 + 60);
      display.println(buf);

      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(20, 160);
      display.println(running ? PW_STOPWATCH_MENU_STOP : PW_STOPWATCH_MENU_START);
      if (!running) {
        display.setCursor(20, 185);
        display.println(PW_STOPWATCH_UP_RESET);
      }
      display.display(true); // partial refresh
    }
  }

  showMenu(menuIndex, false);
}

void PicoWatch::showStepsHistory() {
  guiState = APP_STATE;

  int32_t history[kStepsHistoryDays];
  loadStepsHistory(history);

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 30);
  display.println(PW_STEPS_TITLE);

  const char *const *labels = pwDayLabels();
  for (int i = 0; i < kStepsHistoryDays; i++) {
    display.setCursor(5, 55 + i * 20);
    char buf[32];
    snprintf(buf, sizeof(buf), "%-11s %6ld", labels[i], (long)history[i]);
    display.println(buf);
  }
  display.display(false); // full refresh

  pinMode(BACK_BTN_PIN, INPUT);
  while (digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
    delay(50);
  }
  while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10); // wait for release

  showMenu(menuIndex, false);
}

void PicoWatch::setAlarm() {
  guiState = APP_STATE;

  uint8_t hour = alarmHour;
  uint8_t minute = alarmMinute;
  bool enabled = alarmEnabled;

  int8_t setIndex = SET_ALARM_HOUR;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as setTimezone()/showStopwatch(): Menu was
  // just used to select "Alarm" from the menu, and (unlike Set Time's 5
  // fields) only 3 fields separate that press from an accidental save+exit.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_ALARM_ENABLED) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_ALARM_HOUR) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_ALARM_HOUR:
        hour == 23 ? (hour = 0) : hour++;
        break;
      case SET_ALARM_MINUTE:
        minute == 59 ? (minute = 0) : minute++;
        break;
      case SET_ALARM_ENABLED:
        enabled = !enabled;
        break;
      default:
        break;
      }
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_ALARM_HOUR:
        hour == 0 ? (hour = 23) : hour--;
        break;
      case SET_ALARM_MINUTE:
        minute == 0 ? (minute = 59) : minute--;
        break;
      case SET_ALARM_ENABLED:
        enabled = !enabled;
        break;
      default:
        break;
      }
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 25);
    display.println(PW_ALARM_TITLE);

    display.setCursor(5, 80);
    if (setIndex == SET_ALARM_HOUR) {
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (hour < 10) display.print("0");
    display.print(hour);

    display.setTextColor(GxEPD_WHITE);
    display.print(":");

    if (setIndex == SET_ALARM_MINUTE) {
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (minute < 10) display.print("0");
    display.println(minute);

    display.setTextColor(GxEPD_WHITE);
    display.setCursor(5, 140);
    display.print(PW_ALARM_ENABLED_LABEL);
    if (setIndex == SET_ALARM_ENABLED) {
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    display.println(enabled ? PW_YES : PW_NO);

    display.display(true); // partial refresh
  }

  alarmHour = hour;
  alarmMinute = minute;
  alarmEnabled = enabled;
  saveAlarm();

  showMenu(menuIndex, false);
}

void PicoWatch::showButtonSettings() {
  guiState = APP_STATE;

  bool swapped = menuBackSwapped;
  uint8_t upShort = watchfaceUpShortAction;
  uint8_t upLong = watchfaceUpLongAction;
  uint8_t downShort = watchfaceDownShortAction;
  uint8_t downLong = watchfaceDownLongAction;

  int8_t setIndex = 0;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as the other "Menu confirms immediately"
  // screens - Menu was just used to select this from the settings menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool cancelled = false;
  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > BUTTON_SETTINGS_FIELD_COUNT - 1) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != 0) {
        setIndex--;
      } else {
        // Back on the first field exits without saving - see the identical
        // fix in setWeatherCity() for why this matters.
        cancelled = true;
        break;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case 0: swapped = !swapped; break;
      case 1: upShort = (upShort + 1) % WATCHFACE_ACTION_COUNT; break;
      case 2: upLong = (upLong + 1) % WATCHFACE_ACTION_COUNT; break;
      case 3: downShort = (downShort + 1) % WATCHFACE_ACTION_COUNT; break;
      case 4: downLong = (downLong + 1) % WATCHFACE_ACTION_COUNT; break;
      default: break;
      }
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case 0: swapped = !swapped; break;
      case 1: upShort = (upShort + WATCHFACE_ACTION_COUNT - 1) % WATCHFACE_ACTION_COUNT; break;
      case 2: upLong = (upLong + WATCHFACE_ACTION_COUNT - 1) % WATCHFACE_ACTION_COUNT; break;
      case 3: downShort = (downShort + WATCHFACE_ACTION_COUNT - 1) % WATCHFACE_ACTION_COUNT; break;
      case 4: downLong = (downLong + WATCHFACE_ACTION_COUNT - 1) % WATCHFACE_ACTION_COUNT; break;
      default: break;
      }
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 20);
    display.println(PW_BUTTON_SETTINGS_TITLE);

    const char *labels[BUTTON_SETTINGS_FIELD_COUNT] = {
        PW_BUTTON_SETTINGS_SWAP, PW_BUTTON_SETTINGS_UP_SHORT, PW_BUTTON_SETTINGS_UP_LONG,
        PW_BUTTON_SETTINGS_DOWN_SHORT, PW_BUTTON_SETTINGS_DOWN_LONG};
    const char *values[BUTTON_SETTINGS_FIELD_COUNT] = {
        swapped ? PW_YES : PW_NO, watchfaceActionName(upShort), watchfaceActionName(upLong),
        watchfaceActionName(downShort), watchfaceActionName(downLong)};

    for (int i = 0; i < BUTTON_SETTINGS_FIELD_COUNT; i++) {
      const int yPos = 55 + i * 28;
      display.setTextColor(GxEPD_WHITE);
      display.setCursor(0, yPos);
      display.println(labels[i]);
      display.setCursor(10, yPos + 14);
      if (i == setIndex) {
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
      }
      display.println(values[i]);
    }

    display.display(true); // partial refresh
  }

  if (!cancelled) {
    menuBackSwapped = swapped;
    watchfaceUpShortAction = upShort;
    watchfaceUpLongAction = upLong;
    watchfaceDownShortAction = downShort;
    watchfaceDownLongAction = downLong;
    saveButtonSettings();
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showFontSizeSettings() {
  guiState = APP_STATE;

  uint8_t size = uiFontSize;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool cancelled = false;
  bool confirmed = false;
  while (!confirmed) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      confirmed = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      cancelled = true;
      break;
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      size = (size + 1) % UI_FONT_SIZE_COUNT;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      size = (size + UI_FONT_SIZE_COUNT - 1) % UI_FONT_SIZE_COUNT;
    }

    // Preview the actual font/spacing this size will use, not the screen's
    // own fixed font - otherwise you can't tell what you're picking.
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(10, 20);
    display.println(PW_FONT_SIZE_TITLE);
    display.setCursor(10, 40);
    display.println(PW_FONT_SIZE_SUBTITLE);

    display.setFont(size == UI_FONT_SIZE_SMALL     ? nullptr
                     : size == UI_FONT_SIZE_BIG    ? &FreeMonoBold12pt7b
                                                    : &FreeMonoBold9pt7b);
    display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    int16_t x1, y1;
    uint16_t w, h;
    const char *name = fontSizeName(size);
    display.getTextBounds(name, 0, 90, &x1, &y1, &w, &h);
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, GxEPD_WHITE);
    display.setCursor(10, 90);
    display.println(name);

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    uiFontSize = size;
    saveFontSize();
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showLanguageSettings() {
  guiState = APP_STATE;

  uint8_t language = picowatchLanguage;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool cancelled = false;
  bool confirmed = false;
  while (!confirmed) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      confirmed = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      cancelled = true;
      break;
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      language = (language + 1) % PW_LANG_COUNT;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      language = (language + PW_LANG_COUNT - 1) % PW_LANG_COUNT;
    }

    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(10, 20);
    display.println(PW_SETTINGS_LANGUAGE);

    // Language names are deliberately never translated (see
    // pwLanguageName()) - a German speaker still needs to recognize
    // "Deutsch" while the UI is currently showing English strings.
    display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    int16_t x1, y1;
    uint16_t w, h;
    const char *name = pwLanguageName(language);
    display.getTextBounds(name, 0, 60, &x1, &y1, &w, &h);
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, GxEPD_WHITE);
    display.setCursor(10, 60);
    display.println(name);

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    picowatchLanguage = language;
    saveLanguage();
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showWeatherForecast() {
  guiState = APP_STATE;

  static constexpr int kForecastDays = 5;
  DayForecastEntry days[kForecastDays];
  int dayCount = 0;
  bool fetchOk = false;

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 30);
  display.println(PW_WEATHER_LOADING);
  display.display(false); // full refresh

  if (connectWiFi()) {
    HTTPClient http;
    http.setConnectTimeout(3000); // 3 second max timeout
    // Free "5 Day / 3 Hour" forecast endpoint - same query-string shape as
    // the current-weather URL (settings.weatherURL), just a different path,
    // so the existing cityID/lat-lon template logic still applies.
    String url = settings.weatherURL;
    url.replace("data/2.5/weather", "data/2.5/forecast");
    if (weatherCityID[0] != '\0') {
      url.replace("{cityID}", weatherCityID);
    } else {
      url.replace("{lat}", settings.lat);
      url.replace("{lon}", settings.lon);
    }
    url.replace("{units}", settings.weatherUnit);
    url.replace("{lang}", settings.weatherLang);
    url.replace("{apiKey}", settings.weatherAPIKey);

    http.begin(url.c_str());
    const int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
      const String payload = http.getString();
      JSONVar responseObject = JSON.parse(payload);
      JSONVar list = responseObject["list"];
      // Response is 3-hour steps ("dt_txt": "YYYY-MM-DD HH:MM:SS"); take one
      // reading per calendar date, preferring the one closest to midday once
      // it shows up so the temperature is roughly representative of the day.
      for (int i = 0; i < list.length() && dayCount < kForecastDays; i++) {
        const char *dtTxt = (const char *)list[i]["dt_txt"];
        char date[11];
        strncpy(date, dtTxt, 10);
        date[10] = '\0';
        const int hour = atoi(dtTxt + 11);

        const bool isNewDay = dayCount == 0 || strcmp(date, days[dayCount - 1].date) != 0;
        if (isNewDay) {
          strncpy(days[dayCount].date, date, sizeof(days[dayCount].date) - 1);
          days[dayCount].date[sizeof(days[dayCount].date) - 1] = '\0';
          days[dayCount].temp = (int)list[i]["main"]["temp"];
          days[dayCount].conditionCode = (int)list[i]["weather"][0]["id"];
          dayCount++;
        } else if (hour == 12) {
          days[dayCount - 1].temp = (int)list[i]["main"]["temp"];
          days[dayCount - 1].conditionCode = (int)list[i]["weather"][0]["id"];
        }
      }
      fetchOk = dayCount > 0;
    }
    http.end();
    // turn off radios
    WiFi.mode(WIFI_OFF);
    btStop();
  }

  display.fillScreen(GxEPD_BLACK);
  display.setCursor(5, 25);
  display.println(fetchOk ? "Weather - 5 Days" : "Forecast failed");
  if (fetchOk) {
    const char unitLetter = settings.weatherUnit == String("metric") ? 'C' : 'F';
    for (int i = 0; i < dayCount; i++) {
      display.setCursor(5, 55 + i * 28);
      char buf[40];
      // days[i].date is "YYYY-MM-DD" - "+5" skips the year, showing "MM-DD".
      snprintf(buf, sizeof(buf), "%s  %3d%c  %s", days[i].date + 5, days[i].temp, unitLetter,
               weatherConditionLabel(days[i].conditionCode));
      display.println(buf);
    }
  } else {
    display.setCursor(5, 60);
    display.println(PW_WEATHER_CHECK_WIFI);
    display.println(PW_WEATHER_CHECK_API_KEY);
  }
  display.display(false); // full refresh

  pinMode(BACK_BTN_PIN, INPUT);
  while (digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
    delay(50);
  }
  while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10); // wait for release

  showMenu(menuIndex, false);
}

void PicoWatch::showAbout() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 20);

  display.print(PW_ABOUT_LIBVER);
  display.println(PICOWATCH_LIB_VER);

  display.print(PW_ABOUT_REV);
  display.println(getBoardRevision());

  display.print(PW_ABOUT_BATT);
  float voltage = getBatteryVoltage();
  display.print(voltage);
  display.println(PW_ABOUT_VOLT_UNIT);

  #ifndef ARDUINO_ESP32S3_DEV
  display.print(PW_ABOUT_UPTIME);
  RTC.read(currentTime);
  time_t b = makeTime(bootTime);
  time_t c = makeTime(currentTime);
  int totalSeconds = c-b;
  //int seconds = (totalSeconds % 60);
  int minutes = (totalSeconds % 3600) / 60;
  int hours = (totalSeconds % 86400) / 3600;
  int days = (totalSeconds % (86400 * 30)) / 86400;
  display.print(days);
  display.print(PW_ABOUT_DAYS);
  display.print(hours);
  display.print(PW_ABOUT_HOURS);
  display.print(minutes);
  display.println(PW_ABOUT_MINUTES);
  #endif

  if(WIFI_CONFIGURED){
    display.print(PW_ABOUT_SSID);
    display.println(lastSSID);
    display.print(PW_ABOUT_IP);
    display.println(IPAddress(lastIPAddress).toString());
  }else{
    display.println(PW_ABOUT_WIFI_NOT_CONNECTED);
  }
  display.display(false); // full refresh

  guiState = APP_STATE;
}

// Step 5/5 of re-adding the WiFi/Update feature in isolated, individually
// testable pieces (see project memory). Checks this repo's latest GitHub
// release and, if newer than the running firmware, downloads + flashes
// GITHUB_OTA_ASSET_NAME (SHA256-verified against the release asset's
// digest) and reboots. Blocking, shows progress on the e-ink display.
// Shared by the Settings menu's "Update via GitHub" item and the web UI's
// "GitHub Update" button.
void PicoWatch::updateFromGithub() {
  guiState = APP_STATE;
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println(PW_GITHUB_CHECKING);
  display.display(false);

  auto showResultAndReturn = [&](const char *line1, const char *line2 = nullptr) {
    display.fillScreen(GxEPD_BLACK);
    display.setCursor(0, 30);
    display.println(line1);
    if (line2) display.println(line2);
    display.display(false);
    delay(2500);
    display.epd2.setBusyCallback(PicoWatchDisplay::busyCallback);
  };

  if (!connectWiFi()) {
    showResultAndReturn(PW_GITHUB_WIFI_NOT_CONNECTED);
    return;
  }

  WiFiClientSecure apiClient;
  apiClient.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(5000);
  String apiUrl = String("https://api.github.com/repos/") + GITHUB_OTA_OWNER + "/" +
                  GITHUB_OTA_REPO + "/releases/latest";
  https.begin(apiClient, apiUrl);
  https.addHeader("User-Agent", "PicoWatch");
  https.addHeader("Accept", "application/vnd.github+json");
  const int code = https.GET();
  if (code != 200) {
    https.end();
    showResultAndReturn(PW_GITHUB_NO_RELEASE, PW_GITHUB_NETWORK_ERROR);
    return;
  }
  const String payload = https.getString();
  https.end();

  JSONVar release = JSON.parse(payload);
  if (JSON.typeof(release) == "undefined") {
    showResultAndReturn(PW_GITHUB_BAD_DATA);
    return;
  }

  const String tag = (const char *)release["tag_name"];
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;
  sscanf(tag.c_str(), "v%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  const bool newer =
      (latestMajor > SOFTWARE_VERSION_MAJOR) ||
      (latestMajor == SOFTWARE_VERSION_MAJOR && latestMinor > SOFTWARE_VERSION_MINOR) ||
      (latestMajor == SOFTWARE_VERSION_MAJOR && latestMinor == SOFTWARE_VERSION_MINOR &&
       latestPatch > SOFTWARE_VERSION_PATCH);

  if (!newer) {
    display.fillScreen(GxEPD_BLACK);
    display.setCursor(0, 30);
    display.println(PW_GITHUB_ALREADY_LATEST_1);
    display.println(PW_GITHUB_ALREADY_LATEST_2);
    display.println(tag);
    display.display(false);
    delay(2500);
    display.epd2.setBusyCallback(PicoWatchDisplay::busyCallback);
    return;
  }

  JSONVar assets = release["assets"];
  String downloadUrl;
  String digestHex; // hex part of the asset's "sha256:<hex>" digest field, if present
  for (int i = 0; i < assets.length(); i++) {
    const char *name = (const char *)assets[i]["name"];
    if (strcmp(name, GITHUB_OTA_ASSET_NAME) == 0) {
      downloadUrl = (const char *)assets[i]["browser_download_url"];
      if (JSON.typeof(assets[i]["digest"]) != "undefined") {
        const String digest = (const char *)assets[i]["digest"];
        const int colon = digest.indexOf(':');
        digestHex = (colon >= 0) ? digest.substring(colon + 1) : digest;
      }
      break;
    }
  }

  if (downloadUrl.length() == 0) {
    showResultAndReturn(PW_GITHUB_ASSET_NOT_FOUND, PW_GITHUB_IN_LATEST_RELEASE);
    return;
  }

  display.fillScreen(GxEPD_BLACK);
  display.setCursor(0, 30);
  display.println(PW_GITHUB_DOWNLOADING);
  display.println(tag);
  display.println(" ");
  display.println("0%");
  display.display(false);

  WiFiClientSecure dlClient;
  dlClient.setInsecure();
  HTTPClient dl;
  dl.setConnectTimeout(5000);
  dl.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  dl.begin(dlClient, downloadUrl);
  dl.addHeader("User-Agent", "PicoWatch");
  const int dlCode = dl.GET();
  if (dlCode != 200) {
    dl.end();
    showResultAndReturn(PW_GITHUB_DOWNLOAD_FAILED);
    return;
  }

  const int total = dl.getSize(); // -1 if unknown
  if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) {
    dl.end();
    showResultAndReturn(PW_GITHUB_NOT_ENOUGH_SPACE, PW_GITHUB_FOR_UPDATE);
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);

  WiFiClient *stream = dl.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  int lastShownPercent = -1;
  unsigned long lastByteAt = millis();
  while (dl.connected() && (total < 0 || written < total)) {
    const size_t avail = stream->available();
    if (avail) {
      const size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      const size_t r = stream->readBytes(buf, toRead);
      if (r == 0) break;
      Update.write(buf, r);
      mbedtls_sha256_update(&sha, buf, r);
      written += r;
      lastByteAt = millis();
      if (total > 0) {
        const int percent = (written * 100) / total;
        if (percent != lastShownPercent) {
          lastShownPercent = percent;
          display.fillRect(0, 60, 200, 20, GxEPD_BLACK);
          display.setCursor(0, 75);
          display.print(percent);
          display.println("%");
          display.display(true); // partial refresh
        }
      }
    } else {
      if (millis() - lastByteAt > 15000) break; // stalled
      delay(2);
    }
  }
  dl.end();

  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);

  const bool sizeOk = (total <= 0) || (written == total);
  bool digestOk = true;
  if (digestHex.length() == 64) {
    uint8_t expected[32];
    for (int i = 0; i < 32; i++) {
      expected[i] = (uint8_t)strtoul(digestHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
    }
    digestOk = (memcmp(hash, expected, 32) == 0);
  }

  if (!sizeOk || !digestOk) {
    Update.abort();
    showResultAndReturn(PW_GITHUB_VERIFY_1, PW_GITHUB_VERIFY_2);
    return;
  }

  if (!Update.end(true) || Update.hasError()) {
    showResultAndReturn(PW_GITHUB_FAILED_1, PW_GITHUB_FAILED_2);
    return;
  }

  display.fillScreen(GxEPD_BLACK);
  display.setCursor(0, 30);
  display.println(PW_GITHUB_VERIFIED);
  display.println(PW_GITHUB_REBOOTING);
  display.display(false);
  delay(1000);
  ESP.restart();
}

void PicoWatch::showBuzz() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(70, 80);
  display.println(PW_BUZZ);
  display.display(false); // full refresh
  vibMotor();
  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::vibMotor(uint8_t intervalMs, uint8_t length) {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  bool motorOn = false;
  for (int i = 0; i < length; i++) {
    motorOn = !motorOn;
    digitalWrite(VIB_MOTOR_PIN, motorOn);
    delay(intervalMs);
  }
}

void PicoWatch::setTime() {

  guiState = APP_STATE;

  RTC.read(currentTime);

  #ifdef ARDUINO_ESP32S3_DEV
  uint8_t minute = currentTime.Minute;
  uint8_t hour   = currentTime.Hour;
  uint8_t day    = currentTime.Day;
  uint8_t month  = currentTime.Month;
  uint8_t year   = currentTime.Year;  
  #else
  int8_t minute = currentTime.Minute;
  int8_t hour   = currentTime.Hour;
  int8_t day    = currentTime.Day;
  int8_t month  = currentTime.Month;
  int8_t year   = tmYearToY2k(currentTime.Year);
  #endif

  int8_t setIndex = SET_HOUR;

  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  display.setFullWindow();

  while (1) {

    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_DAY) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_HOUR) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 23 ? (hour = 0) : hour++;
        break;
      case SET_MINUTE:
        minute == 59 ? (minute = 0) : minute++;
        break;
      case SET_YEAR:
        year == 99 ? (year = 0) : year++;
        break;
      case SET_MONTH:
        month == 12 ? (month = 1) : month++;
        break;
      case SET_DAY:
        day == 31 ? (day = 1) : day++;
        break;
      default:
        break;
      }
    }

    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_HOUR:
        hour == 0 ? (hour = 23) : hour--;
        break;
      case SET_MINUTE:
        minute == 0 ? (minute = 59) : minute--;
        break;
      case SET_YEAR:
        year == 0 ? (year = 99) : year--;
        break;
      case SET_MONTH:
        month == 1 ? (month = 12) : month--;
        break;
      case SET_DAY:
        day == 1 ? (day = 31) : day--;
        break;
      default:
        break;
      }
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if (setIndex == SET_HOUR) { // blink hour digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (hour < 10) {
      display.print("0");
    }
    display.print(hour);

    display.setTextColor(GxEPD_WHITE);
    display.print(":");

    display.setCursor(108, 80);
    if (setIndex == SET_MINUTE) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (minute < 10) {
      display.print("0");
    }
    display.print(minute);

    display.setTextColor(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(45, 150);
    if (setIndex == SET_YEAR) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    display.print(2000 + year);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if (setIndex == SET_MONTH) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (month < 10) {
      display.print("0");
    }
    display.print(month);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if (setIndex == SET_DAY) { // blink minute digits
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if (day < 10) {
      display.print("0");
    }
    display.print(day);
    display.display(true); // partial refresh
  }

  tmElements_t tm;
  tm.Month  = month;
  tm.Day    = day;
  #ifdef ARDUINO_ESP32S3_DEV
  tm.Year   = year;
  #else
  tm.Year   = y2kYearToTm(year);
  #endif
  tm.Hour   = hour;
  tm.Minute = minute;
  tm.Second = 0;

  RTC.set(tm);

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::setTimezone() {
  guiState = APP_STATE;

  long offsetSec = gmtOffset;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Unlike Set Time (5 fields, Menu just advances) or the About/Buzz/etc.
  // screens (exit via Back), Menu here confirms AND exits immediately - so a
  // still-bouncing/held Menu press (the same one that just selected "Set
  // Timezone" from the list) would otherwise instantly exit again before the
  // screen is ever usable. Wait for a clean release first.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool save = true;
  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      save = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      save = false;  // single field - Back cancels instead of stepping back
      break;
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      offsetSec = (offsetSec + kGmtOffsetStepSec > kGmtOffsetMaxSec) ? kGmtOffsetMinSec
                                                                      : offsetSec + kGmtOffsetStepSec;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      offsetSec = (offsetSec - kGmtOffsetStepSec < kGmtOffsetMinSec) ? kGmtOffsetMaxSec
                                                                      : offsetSec - kGmtOffsetStepSec;
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 100);
    char buf[16];
    long absOffset = offsetSec < 0 ? -offsetSec : offsetSec;
    snprintf(buf, sizeof(buf), "GMT%c%02ld:%02ld", offsetSec < 0 ? '-' : '+', absOffset / 3600,
             (absOffset % 3600) / 60);
    display.print(buf);
    display.display(true);  // partial refresh
  }

  if (save) {
    gmtOffset = offsetSec;
    saveManualGmtOffset(offsetSec);
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::setWeatherCity() {
  guiState = APP_STATE;

  static constexpr int kDigitCount = 7;
  uint8_t digits[kDigitCount] = {0};
  {
    // Right-align the current city ID into the digit fields (e.g. "5128581"
    // -> digits[0..6] = 5,1,2,8,5,8,1; a shorter ID is left-padded with 0).
    const int len = strlen(weatherCityID);
    for (int i = 0; i < len && i < kDigitCount; i++) {
      const char c = weatherCityID[len - 1 - i];
      digits[kDigitCount - 1 - i] = (c >= '0' && c <= '9') ? (c - '0') : 0;
    }
  }

  int8_t setIndex = 0;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as the other "Menu confirms immediately"
  // screens (setTimezone()/changeWatchface()/showStopwatch()) doesn't apply
  // here in quite the same way (Menu just advances a digit, like Set Time),
  // but a lingering press on entry would still skip straight past digit 0 -
  // wait for a clean release to be safe.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool cancelled = false;
  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > kDigitCount - 1) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != 0) {
        setIndex--;
      } else {
        // Back on the first digit exits without saving - previously Back
        // only ever moved between digits, so there was no way to leave
        // this screen without brute-forcing through all 7 digits via Menu.
        cancelled = true;
        break;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      digits[setIndex] = (digits[setIndex] + 1) % 10;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      digits[setIndex] = (digits[setIndex] + 9) % 10;
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 25);
    display.println(PW_SET_CITY_TITLE);

    for (int i = 0; i < kDigitCount; i++) {
      display.setCursor(15 + i * 24, 90);
      if (i == setIndex) {
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
      } else {
        display.setTextColor(GxEPD_WHITE);
      }
      display.print(digits[i]);
    }

    // Shifted up 15px from the original 150/170/190 - the last line was
    // sitting right at the 200px display edge and getting clipped/hard to
    // read.
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(5, 135);
    display.println(PW_SET_CITY_FIND_1);
    display.setCursor(5, 155);
    display.println("openweathermap.org"); // real URL, not translatable
    display.setCursor(5, 175);
    display.println("/current#cityid"); // real URL path, not translatable

    display.display(true); // partial refresh
  }

  if (cancelled) {
    showSettingsMenu(settingsMenuIndex, false);
    return;
  }

  char buf[kDigitCount + 1];
  for (int i = 0; i < kDigitCount; i++) buf[i] = '0' + digits[i];
  buf[kDigitCount] = '\0';

  // Strip leading zeros (but keep at least one digit) so the ID matches
  // what OpenWeatherMap actually expects, e.g. "0005128581" -> "5128581".
  char *trimmed = buf;
  while (trimmed[0] == '0' && trimmed[1] != '\0') trimmed++;

  strncpy(weatherCityID, trimmed, sizeof(weatherCityID) - 1);
  weatherCityID[sizeof(weatherCityID) - 1] = '\0';
  saveWeatherCityID(weatherCityID);
  weatherIntervalCounter = -1; // force a fresh weather fetch for the new city

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showAccelerometer() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);

  Accel acc;

  long previousMillis = 0;
  long interval       = 200;

  guiState = APP_STATE;

  pinMode(BACK_BTN_PIN, INPUT);

  while (1) {

    unsigned long currentMillis = millis();

    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }

    if (currentMillis - previousMillis > interval) {
      previousMillis = currentMillis;
      // Get acceleration data
      bool res          = sensor.getAccel(acc);
      uint8_t direction = sensor.getDirection();
      display.fillScreen(GxEPD_BLACK);
      display.setCursor(0, 30);
      if (res == false) {
        display.println(PW_ACCEL_FAIL);
      } else {
        display.print("  X:");
        display.println(acc.x);
        display.print("  Y:");
        display.println(acc.y);
        display.print("  Z:");
        display.println(acc.z);

        display.setCursor(30, 130);
        switch (direction) {
        case DIRECTION_DISP_DOWN:
          display.println(PW_ACCEL_FACE_DOWN);
          break;
        case DIRECTION_DISP_UP:
          display.println(PW_ACCEL_FACE_UP);
          break;
        case DIRECTION_BOTTOM_EDGE:
          display.println(PW_ACCEL_BOTTOM_EDGE);
          break;
        case DIRECTION_TOP_EDGE:
          display.println(PW_ACCEL_TOP_EDGE);
          break;
        case DIRECTION_RIGHT_EDGE:
          display.println(PW_ACCEL_RIGHT_EDGE);
          break;
        case DIRECTION_LEFT_EDGE:
          display.println(PW_ACCEL_LEFT_EDGE);
          break;
        default:
          display.println(PW_ACCEL_ERROR);
          break;
        }
      }
      display.display(true); // full refresh
    }
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showWatchFace(bool partialRefresh) {
  display.setFullWindow();
  // At this point it is sure we are going to update
  display.epd2.asyncPowerOn();
  drawWatchFace();
  display.display(partialRefresh); // partial refresh
  guiState = WATCHFACE_STATE;
}

void PicoWatch::drawWatchFace() {
  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(5, 53 + 60);
  if (currentTime.Hour < 10) {
    display.print("0");
  }
  display.print(currentTime.Hour);
  display.print(":");
  if (currentTime.Minute < 10) {
    display.print("0");
  }
  display.println(currentTime.Minute);
}

weatherData PicoWatch::getWeatherData() {
  return _getWeatherData(weatherCityID, settings.lat, settings.lon,
    settings.weatherUnit, settings.weatherLang, settings.weatherURL,
    settings.weatherAPIKey, settings.weatherUpdateInterval);
}

weatherData PicoWatch::_getWeatherData(String cityID, String lat, String lon, String units, String lang,
                                   String url, String apiKey,
                                   uint8_t updateInterval) {
  currentWeather.isMetric = units == String("metric");
  if (weatherIntervalCounter < 0) { //-1 on first run, set to updateInterval
    weatherIntervalCounter = updateInterval;
  }
  if (weatherIntervalCounter >=
      updateInterval) { // only update if WEATHER_UPDATE_INTERVAL has elapsed
                        // i.e. 30 minutes
    if (connectWiFi()) {
      HTTPClient http; // Use Weather API for live data if WiFi is connected
      http.setConnectTimeout(3000); // 3 second max timeout
      String weatherQueryURL = url;
      if(cityID != ""){
        weatherQueryURL.replace("{cityID}", cityID);
      }else{
        weatherQueryURL.replace("{lat}", lat);
        weatherQueryURL.replace("{lon}", lon);
      }
      weatherQueryURL.replace("{units}", units);
      weatherQueryURL.replace("{lang}", lang);
      weatherQueryURL.replace("{apiKey}", apiKey);
      http.begin(weatherQueryURL.c_str());
      int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        String payload             = http.getString();
        JSONVar responseObject     = JSON.parse(payload);
        currentWeather.temperature = int(responseObject["main"]["temp"]);
        currentWeather.weatherConditionCode =
            int(responseObject["weather"][0]["id"]);
        currentWeather.weatherDescription =
		        JSONVar::stringify(responseObject["weather"][0]["main"]);
	      currentWeather.external = true;
		        breakTime((time_t)(int)responseObject["sys"]["sunrise"], currentWeather.sunrise);
		        breakTime((time_t)(int)responseObject["sys"]["sunset"], currentWeather.sunset);
        // sync NTP during weather API call and use timezone of lat & lon,
        // unless the user has set a timezone manually (see setTimezone()) -
        // otherwise this would silently overwrite their choice on every sync.
        {
          long manualOffset;
          if (!loadManualGmtOffset(manualOffset)) {
            gmtOffset = int(responseObject["timezone"]);
          }
        }
        syncNTP(gmtOffset);
      } else {
        // http error
      }
      http.end();
      // turn off radios
      WiFi.mode(WIFI_OFF);
      btStop();
    } else { // No WiFi, use internal temperature sensor
      uint8_t temperature = sensor.readTemperature(); // celsius
      if (!currentWeather.isMetric) {
        temperature = temperature * 9. / 5. + 32.; // fahrenheit
      }
      currentWeather.temperature          = temperature;
      currentWeather.weatherConditionCode = 800;
      currentWeather.external             = false;
    }
    weatherIntervalCounter = 0;
  } else {
    weatherIntervalCounter++;
  }
  return currentWeather;
}

float PicoWatch::getBatteryVoltage() {
  #ifdef ARDUINO_ESP32S3_DEV
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * ADC_VOLTAGE_DIVIDER;
  #else
  if (RTC.rtcType == DS3231) {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f *
           2.0f; // Battery voltage goes through a 1/2 divider.
  } else {
    return analogReadMilliVolts(BATT_ADC_PIN) / 1000.0f * 2.0f;
  }
  #endif
}

uint8_t PicoWatch::getBoardRevision() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  if(chip_info.model == CHIP_ESP32){ //Revision 1.0 - 2.0
    Wire.beginTransmission(0x68); //v1.0 has DS3231
    if (Wire.endTransmission() == 0){
      return 10;
    }
    delay(1);
    Wire.beginTransmission(0x51); //v1.5 and v2.0 have PCF8563
    if (Wire.endTransmission() == 0){
        pinMode(35, INPUT);
        if(digitalRead(35) == 0){
          return 20; //in rev 2.0, pin 35 is BTN 3 and has a pulldown
        }else{
          return 15; //in rev 1.5, pin 35 is the battery ADC
        }
    }
  }
  if(chip_info.model == CHIP_ESP32S3){ //Revision 3.0
    return 30;
  }
  return -1;
}

uint16_t PicoWatch::_readRegister(uint8_t address, uint8_t reg, uint8_t *data,
                               uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)address, (uint8_t)len);
  uint8_t i = 0;
  while (Wire.available()) {
    data[i++] = Wire.read();
  }
  return 0;
}

uint16_t PicoWatch::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data, len);
  return (0 != Wire.endTransmission());
}

void PicoWatch::_bmaConfig() {

  if (sensor.begin(_readRegister, _writeRegister, delay) == false) {
    // fail to init BMA
    return;
  }

  // Accel parameter structure
  Acfg cfg;
  /*!
      Output data rate in Hz, Optional parameters:
          - BMA4_OUTPUT_DATA_RATE_0_78HZ
          - BMA4_OUTPUT_DATA_RATE_1_56HZ
          - BMA4_OUTPUT_DATA_RATE_3_12HZ
          - BMA4_OUTPUT_DATA_RATE_6_25HZ
          - BMA4_OUTPUT_DATA_RATE_12_5HZ
          - BMA4_OUTPUT_DATA_RATE_25HZ
          - BMA4_OUTPUT_DATA_RATE_50HZ
          - BMA4_OUTPUT_DATA_RATE_100HZ
          - BMA4_OUTPUT_DATA_RATE_200HZ
          - BMA4_OUTPUT_DATA_RATE_400HZ
          - BMA4_OUTPUT_DATA_RATE_800HZ
          - BMA4_OUTPUT_DATA_RATE_1600HZ
  */
  cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
  /*!
      G-range, Optional parameters:
          - BMA4_ACCEL_RANGE_2G
          - BMA4_ACCEL_RANGE_4G
          - BMA4_ACCEL_RANGE_8G
          - BMA4_ACCEL_RANGE_16G
  */
  cfg.range = BMA4_ACCEL_RANGE_2G;
  /*!
      Bandwidth parameter, determines filter configuration, Optional parameters:
          - BMA4_ACCEL_OSR4_AVG1
          - BMA4_ACCEL_OSR2_AVG2
          - BMA4_ACCEL_NORMAL_AVG4
          - BMA4_ACCEL_CIC_AVG8
          - BMA4_ACCEL_RES_AVG16
          - BMA4_ACCEL_RES_AVG32
          - BMA4_ACCEL_RES_AVG64
          - BMA4_ACCEL_RES_AVG128
  */
  cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;

  /*! Filter performance mode , Optional parameters:
      - BMA4_CIC_AVG_MODE
      - BMA4_CONTINUOUS_MODE
  */
  cfg.perf_mode = BMA4_CONTINUOUS_MODE;

  // Configure the BMA423 accelerometer
  sensor.setAccelConfig(cfg);

  // Enable BMA423 accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  // Warning : Need to use feature, you must first enable the accelerometer
  sensor.enableAccel();

  struct bma4_int_pin_config config;
  config.edge_ctrl = BMA4_LEVEL_TRIGGER;
  config.lvl       = BMA4_ACTIVE_HIGH;
  config.od        = BMA4_PUSH_PULL;
  config.output_en = BMA4_OUTPUT_ENABLE;
  config.input_en  = BMA4_INPUT_DISABLE;
  // The correct trigger interrupt needs to be configured as needed
  sensor.setINTPinConfig(config, BMA4_INTR1_MAP);

  struct bma423_axes_remap remap_data;
  remap_data.x_axis      = 1;
  remap_data.x_axis_sign = 0xFF;
  remap_data.y_axis      = 0;
  remap_data.y_axis_sign = 0xFF;
  remap_data.z_axis      = 2;
  remap_data.z_axis_sign = 0xFF;
  // Need to raise the wrist function, need to set the correct axis
  sensor.setRemapAxes(&remap_data);

  // Enable BMA423 isStepCounter feature
  sensor.enableFeature(BMA423_STEP_CNTR, true);
  // Enable BMA423 isTilt feature
  sensor.enableFeature(BMA423_TILT, true);
  // Enable BMA423 isDoubleClick feature
  sensor.enableFeature(BMA423_WAKEUP, true);

  // Reset steps
  sensor.resetStepCounter();

  // Turn on feature interrupt
  sensor.enableStepCountInterrupt();
  sensor.enableTiltInterrupt();
  // It corresponds to isDoubleClick interrupt
  sensor.enableWakeupInterrupt();
}

// Step 3/5 of re-adding the WiFi/Update feature in isolation: the new
// join-page + AP-idle-timeout flow, WITHOUT File/GitHub Update yet (those
// are steps 4-5) - see project memory for why this is being done piece by
// piece instead of as one commit.
namespace {
// Same dark navy/cyan theme pfsense-status-esp32 layers on top of
// WiFiManager's own default pages via setCustomHeadElement() - injected
// after WiFiManager's stock <style> block, so equal-specificity rules here
// win without needing !important. No logo (PicoWatch doesn't have one to
// serve), just the color/shape theme - see project memory on reusing the
// reference's design, not just its logic.
constexpr const char *kWifiPortalTheme =
    "<style>"
    "body{background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;"
    "color:#e7f3fb;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif}"
    "h1,h2,h3{color:#e7f3fb}"
    "a{color:#4fc3f7}a:hover{color:#7dd8fb}"
    "input,select{background:#101823;border:1px solid #22303f;color:#e7f3fb;border-radius:10px;margin:8px 0}"
    "button,input[type='button'],input[type='submit']{background:linear-gradient(135deg,#4fc3f7,#0f6fa8);"
    "color:#0d1015;font-weight:600;border-radius:999px;margin:6px 0}"
    "button.D{background:#dc3630;color:#fff}"
    ".msg{background:#101823;border:1px solid #22303f;border-left-width:5px;border-radius:10px;color:#a9bcca}"
    ".msg.P{border-left-color:#4fc3f7}.msg.D{border-left-color:#dc3630}.msg.S{border-left-color:#4ade80}"
    "hr{border:none;border-top:1px solid #22303f;margin:18px 0}"
    "</style>";

// Wraps a status/update page body in the same theme, for the small
// WebServer that runs once WiFi Setup is actually connected (step 4/5 of
// re-adding this feature - see project memory).
String themedPage(const String &title, const String &bodyHtml) {
  String html;
  html.reserve(bodyHtml.length() + 400);
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>"
          "<title>";
  html += title;
  html += "</title>";
  html += kWifiPortalTheme;
  html += "</head><body style='text-align:center'>"
          "<div style='display:inline-block;text-align:left;min-width:260px;max-width:500px'>"
          "<h1>PicoWatch</h1><h3>";
  html += title;
  html += "</h3>";
  html += bodyHtml;
  html += "</div></body></html>";
  return html;
}
}  // namespace

void PicoWatch::setupWifi() {
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy
  WiFiManager wifiManager;
  wifiManager.setAPCallback(_configModeCallback);
  wifiManager.setCustomHeadElement(kWifiPortalTheme);

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println(PW_WIFI_CONNECTING);
  display.display(false);

  // This now matches pfsense-status-esp32's ACTUAL control flow, read
  // directly from their main.cpp/config_portal.cpp rather than
  // approximated - two earlier attempts at this each had a real bug
  // (see project memory), so this one is a deliberately literal port
  // instead of an inspired-by rewrite:
  //   1. Try a direct WiFi.begin() + our own wait loop FIRST, bypassing
  //      WiFiManager's autoConnect() machinery entirely for the common
  //      case (main.cpp:190-196, config_portal.cpp:908-913).
  //   2. Only if that fails, fall back to WiFiManager's own config portal
  //      via startConfigPortal() - NOT autoConnect() - password-protected
  //      with the random per-boot key (config_portal.cpp:412-416).
  //   3. Afterward, regardless of which path connected,
  //      ALWAYS call startWebPortal() (main.cpp:196-199: "Keep local menu
  //      available even after normal STA reconnect") - this is what
  //      actually creates/binds WiFiManager's own server so our custom
  //      routes + setCustomMenuHTML() have something to attach to, even
  //      when no portal was ever shown.
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  unsigned long waitStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - waitStart < 8000) {
    delay(50);
  }

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (!connected) {
    display.fillScreen(GxEPD_BLACK);
    display.setCursor(0, 20);
    display.println(PW_WIFI_CONNECT_PHONE_TO);
    display.println(WIFI_AP_SSID);
    display.println(PW_WIFI_PASSWORD_NEXT_1);
    display.println(PW_WIFI_PASSWORD_NEXT_2);
    display.display(false);
    // Password-protected with a random key (generateWifiApPassword(),
    // called once per true reset) shown on-screen in _configModeCallback -
    // matches pfsense-status-esp32's actual approach instead of a
    // hardcoded key anyone reading the source could use.
    connected = wifiManager.startConfigPortal(WIFI_AP_SSID, wifiApPassword);
  }

  display.fillScreen(GxEPD_BLACK);
  display.setCursor(0, 30);
  if (!connected) { // WiFi setup failed
    display.println(PW_WIFI_SETUP_FAILED);
    display.println(PW_WIFI_TIMED_OUT);
  } else {
    display.println(PW_WIFI_CONNECTED_TO);
    display.println(WiFi.SSID());
    display.println(PW_WIFI_OPEN_IN_BROWSER);
    display.println(WiFi.localIP());
    display.println(" ");
    display.println(PW_WIFI_BACK_TO_DISCONNECT);
    weatherIntervalCounter = -1; // Reset to force weather to be read again
    lastIPAddress = WiFi.localIP();
    WiFi.SSID().toCharArray(lastSSID, 30);
  }
  display.display(false); // full refresh

  if (connected) {
    // Keep the connection (and IP) actually reachable for a while instead
    // of tearing it straight back down - otherwise the display still says
    // "Connected to..." but the radio is already off underneath it.
    // Exits early on Back, or after WIFI_STAY_CONNECTED_TIMEOUT seconds of
    // nobody touching it.
    pinMode(BACK_BTN_PIN, INPUT);

    // Always call this, even when we connected directly via our own
    // WiFi.begin() above with no portal ever shown - it's what actually
    // creates/binds wifiManager's own server (matches
    // main.cpp:196-199's "Keep local menu available even after normal STA
    // reconnect").
    wifiManager.startWebPortal();

    unsigned long authUntil = 0;
    IPAddress authIp;
    String webMenuPassword = loadWebMenuPassword();

    if (wifiManager.server) {
      WebServer &server = *wifiManager.server;
      auto isAuthed = [&]() {
        return authUntil != 0 && millis() <= authUntil && server.client().remoteIP() == authIp;
      };
      auto requireAuth = [&]() {
        if (isAuthed()) return true;
        server.sendHeader("Location", "/login", true);
        server.send(302, "text/plain", "");
        return false;
      };
      auto loginForm = [](const char *notice) {
        String body;
        if (notice) {
          body += "<div class='msg D'>";
          body += notice;
          body += "</div>";
        }
        body += "<form method='POST' action='/login'>"
                "<input type='password' name='p' placeholder='Password'>"
                "<button type='submit'>Login</button></form>";
        return body;
      };

      server.on("/login", HTTP_GET, [&]() {
        server.send(200, "text/html", themedPage("Login", loginForm(nullptr)));
      });
      server.on("/login", HTTP_POST, [&]() {
        if (server.arg("p") == webMenuPassword) {
          authUntil = millis() + WEB_MENU_SESSION_MS;
          authIp = server.client().remoteIP();
          server.sendHeader("Location", "/", true);
          server.send(302, "text/plain", "");
        } else {
          server.send(200, "text/html", themedPage("Login", loginForm("Wrong password.")));
        }
      });
      server.on("/change-password", HTTP_GET, [&]() {
        if (!requireAuth()) return;
        server.send(200, "text/html",
                     themedPage("Change Password",
                                "<form method='POST' action='/change-password'>"
                                "<input type='password' name='p' placeholder='New password (min 8 chars)'>"
                                "<button type='submit'>Save</button></form>"
                                "<hr><a href='/'>Back</a>"));
      });
      server.on("/change-password", HTTP_POST, [&]() {
        if (!requireAuth()) return;
        const String newPass = server.arg("p");
        if (newPass.length() < 8) {
          server.send(200, "text/html",
                       themedPage("Change Password",
                                  "<div class='msg D'>Password must be at least 8 characters.</div>"
                                  "<form method='POST' action='/change-password'>"
                                  "<input type='password' name='p' placeholder='New password (min 8 chars)'>"
                                  "<button type='submit'>Save</button></form>"
                                  "<hr><a href='/'>Back</a>"));
          return;
        }
        webMenuPassword = newPass;
        saveWebMenuPassword(newPass);
        server.send(200, "text/html",
                     themedPage("Change Password",
                                "<div class='msg S'>Password changed.</div><hr><a href='/'>Back</a>"));
      });
      server.on("/logout", HTTP_GET, [&]() {
        authUntil = 0;
        server.sendHeader("Location", "/login", true);
        server.send(302, "text/plain", "");
      });
      server.on("/github-update", HTTP_POST, [&]() {
        if (!requireAuth()) return;
        server.send(200, "text/html",
                     themedPage("Online Update",
                                "<div class='msg P'>Starting GitHub update check&hellip;<br/>"
                                "Watch the device screen for progress.</div>"));
        updateFromGithub(); // blocking; reboots on success, falls through here on failure
      });
      server.on(
          "/file-update", HTTP_POST,
          [&]() {
            if (!requireAuth()) return;
            server.sendHeader("Connection", "close");
            const bool ok = !Update.hasError();
            server.send(200, "text/html",
                        themedPage("File Update",
                                   ok ? "<div class='msg S'><strong>Update OK</strong><br/>Rebooting&hellip;</div>"
                                      : "<div class='msg D'><strong>Update failed.</strong></div>"));
            if (ok) {
              delay(500);
              ESP.restart();
            }
          },
          [&]() {
            // Runs (and streams straight into the OTA partition) BEFORE
            // the main handler above gets a chance to run requireAuth() -
            // checking auth only there would let an unauthenticated
            // request's file bytes reach Update.write() regardless of the
            // final response. Gate it here too.
            static bool uploadAuthorized = false;
            HTTPUpload &upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
              uploadAuthorized = isAuthed();
              if (!uploadAuthorized) return;
              display.fillScreen(GxEPD_BLACK);
              display.setCursor(0, 30);
              display.println(PW_WIFI_RECEIVING_UPDATE_1);
              display.println(PW_WIFI_RECEIVING_UPDATE_2);
              display.display(false);
              Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
              if (uploadAuthorized) Update.write(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
              if (uploadAuthorized) Update.end(true);
            }
          });

      // Once connected, WiFiManager's own root page ("/") becomes our
      // app's home screen: a minimal stock menu (just "wifi") plus our own
      // buttons injected via setCustomMenuHTML() - the actual mechanism
      // pfsense-status-esp32 uses (buildCustomMenuHtml() there).
      static std::vector<const char *> kConnectedMenuIds = {"wifi"};
      wifiManager.setMenu(kConnectedMenuIds);
      String fullMenu = "<div class='msg S'><strong>Connected</strong> to " + String(lastSSID) + "</div>"
                        "<h3>Firmware Update</h3><hr>"
                        "<form method='POST' action='/github-update'>"
                        "<button type='submit'>Check for Online Update</button></form>"
                        "<hr>"
                        "<form method='POST' action='/file-update' enctype='multipart/form-data'>"
                        "<input type='file' name='update' accept='.bin'>"
                        "<button type='submit'>Upload &amp; Flash (File Update)</button></form>"
                        "<hr><a href='/change-password'>Change Password</a> &middot; <a href='/logout'>Logout</a>";
      wifiManager.setCustomMenuHTML(fullMenu.c_str());
    }

    unsigned long connectedAt = millis();
    while (millis() - connectedAt < (unsigned long)WIFI_STAY_CONNECTED_TIMEOUT * 1000UL) {
      // Matches dashboard.cpp:596-599's loopDashboard() exactly: both
      // calls, not just process() alone.
      wifiManager.process();
      if (wifiManager.server) wifiManager.server->handleClient();
      if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) break;
      delay(10);
    }
    wifiManager.stopWebPortal();
  }

  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  // enable lightsleep on busy
  display.epd2.setBusyCallback(PicoWatchDisplay::busyCallback);
  guiState = APP_STATE;
}

void PicoWatch::_configModeCallback(WiFiManager *myWiFiManager) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println(PW_WIFI_AP_CONNECT_TO);
  display.print(PW_WIFI_AP_SSID_LABEL);
  display.println(WIFI_AP_SSID);
  display.print(PW_WIFI_AP_PASS_LABEL);
  display.println(wifiApPassword);
  display.print(PW_WIFI_AP_IP_LABEL);
  display.println(WiFi.softAPIP());
  display.display(false); // full refresh
}

bool PicoWatch::connectWiFi() {
  if (WL_CONNECT_FAILED ==
      WiFi.begin()) { // WiFi not setup, you can also use hard coded credentials
                      // with WiFi.begin(SSID,PASS);
    WIFI_CONFIGURED = false;
  } else {
    if (WL_CONNECTED ==
        WiFi.waitForConnectResult()) { // attempt to connect for 10s
      lastIPAddress = WiFi.localIP();
      WiFi.SSID().toCharArray(lastSSID, 30);
      WIFI_CONFIGURED = true;
    } else { // connection failed, time out
      WIFI_CONFIGURED = false;
      // turn off radios
      WiFi.mode(WIFI_OFF);
      btStop();
    }
  }
  return WIFI_CONFIGURED;
}
/*
void PicoWatch::showUpdateFW() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Please visit");
  display.println("watchy.sqfmi.com");
  display.println("with a Bluetooth");
  display.println("enabled device");
  display.println(" ");
  display.println("Press menu button");
  display.println("again when ready");
  display.println(" ");
  display.println("Keep USB powered");
  display.display(false); // full refresh

  guiState = FW_UPDATE_STATE;
}

void PicoWatch::updateFWBegin() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Bluetooth Started");
  display.println(" ");
  display.println("PicoWatch BLE OTA");
  display.println(" ");
  display.println("Waiting for");
  display.println("connection...");
  display.display(false); // full refresh

  BLE BT;
  BT.begin("PicoWatch BLE OTA");
  int prevStatus = -1;
  int currentStatus;

  while (1) {
    currentStatus = BT.updateStatus();
    if (prevStatus != currentStatus || prevStatus == 1) {
      if (currentStatus == 0) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Connected!");
        display.println(" ");
        display.println("Waiting for");
        display.println("upload...");
        display.display(false); // full refresh
      }
      if (currentStatus == 1) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Downloading");
        display.println("firmware:");
        display.println(" ");
        display.print(BT.howManyBytes());
        display.println(" bytes");
        display.display(true); // partial refresh
      }
      if (currentStatus == 2) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Download");
        display.println("completed!");
        display.println(" ");
        display.println("Rebooting...");
        display.display(false); // full refresh

        delay(2000);
        esp_restart();
      }
      if (currentStatus == 4) {
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Disconnected!");
        display.println(" ");
        display.println("exiting...");
        display.display(false); // full refresh
        delay(1000);
        break;
      }
      prevStatus = currentStatus;
    }
    delay(100);
  }

  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  showMenu(menuIndex, false);
}
*/
void PicoWatch::showSyncNTP() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println(PW_NTP_SYNCING);
  display.print(PW_NTP_GMT_OFFSET);
  display.println(gmtOffset);
  display.display(false); // full refresh
  if (connectWiFi()) {
    if (syncNTP()) {
      display.println(PW_NTP_SUCCESS);
      display.println(PW_NTP_CURRENT_TIME);

      RTC.read(currentTime);

      display.print(tmYearToCalendar(currentTime.Year));
      display.print("/");
      display.print(currentTime.Month);
      display.print("/");
      display.print(currentTime.Day);
      display.print(" - ");

      if (currentTime.Hour < 10) {
        display.print("0");
      }
      display.print(currentTime.Hour);
      display.print(":");
      if (currentTime.Minute < 10) {
        display.print("0");
      }
      display.println(currentTime.Minute);
    } else {
      display.println(PW_NTP_FAILED);
    }
    WiFi.mode(WIFI_OFF);
    btStop();
  } else {
    display.println(PW_NTP_WIFI_NOT_CONFIGURED);
  }
  display.display(true); // full refresh
  delay(3000);
  showSettingsMenu(settingsMenuIndex, false);
}

bool PicoWatch::syncNTP() { // NTP sync - call after connecting to WiFi and
                         // remember to turn it back off
  return syncNTP(gmtOffset,
                 settings.ntpServer.c_str());
}

bool PicoWatch::syncNTP(long gmt) {
  return syncNTP(gmt, settings.ntpServer.c_str());
}

bool PicoWatch::syncNTP(long gmt, String ntpServer) {
  // NTP sync - call after connecting to
  // WiFi and remember to turn it back off
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, ntpServer.c_str(), gmt);
  timeClient.begin();
  if (!timeClient.forceUpdate()) {
    return false; // NTP sync failed
  }
  tmElements_t tm;
  breakTime((time_t)timeClient.getEpochTime(), tm);
  RTC.set(tm);
  return true;
}
