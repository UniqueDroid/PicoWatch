#include "PicoWatch.h"
#include "localization.h"
#include <Preferences.h>
#include <cstring> // memset/memcpy - PicoWatch::playTetris()'s board
#include <mbedtls/sha256.h> // SHA256 verification for GitHub OTA - see PicoWatch::updateFromGithub()
#include <Fonts/FreeMonoBold12pt7b.h> // "Big" menu font size - see PicoWatch::showFontSizeSettings()
// Latin-1-range versions of the same two sizes, used ONLY by uiMenuFont()
// (the shared list-menu renderer, uiRenderList()) so umlauts/ss render
// correctly in menu text - every other screen still uses the ASCII-only
// FreeMonoBold9pt7b/12pt7b above unchanged. See FreeMonoBold9pt8b.h for
// how these were generated.
#include "FreeMonoBold9pt8b.h"
#include "FreeMonoBold12pt8b.h"
#include "FreeMonoBold15pt8b.h" // new top "Big" menu font tier - see PicoWatch::showFontSizeSettings()

// Cached copy of the persisted alarm (see setAlarm()/onReset()) - survives
// deep sleep like guiState/menuIndex; loaded from flash once on reset rather
// than re-reading NVS every single per-minute tick. Declared here (ahead of
// the anonymous namespace below) since loadAlarm()/saveAlarm() reference it.
RTC_DATA_ATTR uint8_t alarmHour;
RTC_DATA_ATTR uint8_t alarmMinute;
RTC_DATA_ATTR bool alarmEnabled;

// Hourly-vibrate active window (see PicoWatch::showVibrateWindowSettings())
// - replaces the old always-on-24/7 settings.vibrateOClock compile-time flag
// with a runtime-adjustable time-of-day range, same caching pattern as the
// alarm fields above.
RTC_DATA_ATTR uint8_t vibrateWindowFromHour;
RTC_DATA_ATTR uint8_t vibrateWindowToHour;
RTC_DATA_ATTR bool vibrateWindowEnabled;

// Weather city ID actually used at runtime (OpenWeatherMap numeric ID, see
// https://openweathermap.org/current#cityid) - defaults to settings.cityID
// but can be overridden on-device via PicoWatch::setWeatherCity() without a
// recompile. Can't just mutate settings.cityID directly: settings is a
// regular (non-RTC) object member reconstructed from the compile-time
// default on every wake from deep sleep, so an override stored there would
// vanish after the very next sleep cycle.
RTC_DATA_ATTR char weatherCityID[12];

// WiFi hostname (Settings -> Internet Access page, web menu only - see
// setupWifi()'s comment on why) - defaults to the router-visible
// "esp32-<chipid>" ESP32 core default, which is how Jan found the watch
// in his router's client list in the first place and asked for something
// recognizable instead (15.08.2026).
RTC_DATA_ATTR char picoWatchHostname[32];

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
// Swaps the menu color scheme (see PicoWatch::showInvertMenuSettings()) -
// false = white text on black (the original look), true = black text on
// white.
RTC_DATA_ATTR bool menuInverted;
// How often _checkBleNotifications() opens its BLE window, in minutes (see
// PicoWatch::showNotifyIntervalSettings()) - user-adjustable 1-10 min
// tradeoff between notification latency and battery use, was a fixed
// BLE_NOTIFY_CHECK_INTERVAL_MIN constant until Jan asked for control over
// it (14.08.2026).
RTC_DATA_ATTR uint8_t bleNotifyIntervalMin;
// WLAN vs BLE (Gadgetbridge phone proxy) for weather/time sync - see
// config.h's INTERNET_ACCESS_* and PicoWatch::showInternetAccessSettings().
RTC_DATA_ATTR uint8_t internetAccessMode;
// Notification popup/icon settings (see config.h's NOTIFICATION_POPUP_*
// comment and PicoWatch::showNotificationSettings()).
RTC_DATA_ATTR bool notificationPopupEnabled;
RTC_DATA_ATTR uint8_t notificationPopupDurationS;
RTC_DATA_ATTR bool notificationIconEnabled;
// true = white icon (default, matches every current dark-background
// watchface), false = black (for light-background faces, where a white
// icon is invisible - the exact problem Jan ran into, 15.08.2026).
RTC_DATA_ATTR bool notificationIconLight;
// Whether a new notification popup vibrates at all - separate from
// notificationPopupEnabled above, which gates the popup+vibration
// together; Jan wanted to be able to keep the popup but silence the buzz
// specifically (16.08.2026).
RTC_DATA_ATTR bool notificationVibrateEnabled;

// Global vibration intensity (see config.h's VIBRATION_STRENGTH_* and
// PicoWatch::showVibrationSettings()) - drives vibMotor()'s PWM duty
// cycle. Deliberately global rather than per-feature: applies to the
// o'clock vibrate window, the alarm, the notification popup, the reset
// boot buzz, and the manual Buzz test alike.
RTC_DATA_ATTR uint8_t vibrationStrength;

// Gadgetbridge notification ring buffer (see PicoWatch::_checkBleNotifications()/
// showNotifications()) - RTC_DATA_ATTR so it survives deep sleep without a
// flash write on every single notification, AND separately mirrored to
// NVS (see saveNotifications()/loadNotifications() below) so a true reset
// doesn't lose them either - Jan wanted notifications to survive a reset
// like any other setting (15.08.2026; originally these were deliberately
// left reset-transient, that decision is superseded now).
// notifications[0] is the most recent; a fixed-size ring, oldest entries
// are silently overwritten once full (see onBleNotificationReceived() below).
struct PwNotification {
  char time[6];  // "HH:MM" - when the watch received it, not when the phone did
  char src[NOTIFICATION_SRC_LEN];
  char title[NOTIFICATION_TITLE_LEN];
  char body[NOTIFICATION_BODY_LEN];
};
RTC_DATA_ATTR PwNotification notifications[NOTIFICATION_COUNT];
RTC_DATA_ATTR int notificationCount;    // how many of the slots above are actually populated (0..NOTIFICATION_COUNT)
RTC_DATA_ATTR bool hasUnreadNotification;

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

// Today's step total, split into stepsBaseline (RTC_DATA_ATTR, see below -
// steps already banked before the sensor's CURRENT counting period) plus
// the BMA423's own live hardware counter (PicoWatch::todaySteps()) - the
// sensor's counter alone isn't enough because PicoWatch::_bmaConfig()'s
// sensor.begin() does a soft-reset of the whole chip (including its
// feature engine, which the step counter lives in) on every true reset,
// silently zeroing "today so far" on every reflash/power cycle. Persisting
// just before that reset isn't possible either (begin() has to run before
// we can talk to the chip at all, by which point the count is already
// gone) - so instead this snapshots the running total to NVS periodically
// (once per minute, only when it actually changed - see
// PicoWatch::_persistStepsProgress()) and reloads that snapshot as the new
// baseline in onReset(), accepting at most ~1 minute of steps lost on an
// actual reset instead of the whole day.
constexpr const char *kPrefsStepsBaselineKey = "stepsBase";

uint32_t loadStepsBaseline() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  const uint32_t baseline = prefs.getUInt(kPrefsStepsBaselineKey, 0);
  prefs.end();
  return baseline;
}

void saveStepsBaseline(uint32_t total) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUInt(kPrefsStepsBaselineKey, total);
  prefs.end();
}

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

constexpr const char *kPrefsNotificationsKey = "notifs";
constexpr const char *kPrefsNotificationCountKey = "notifCnt";

void loadNotifications() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  const size_t want = sizeof(PwNotification) * NOTIFICATION_COUNT;
  const size_t got = prefs.getBytes(kPrefsNotificationsKey, notifications, want);
  notificationCount = got == want ? prefs.getInt(kPrefsNotificationCountKey, 0) : 0;
  prefs.end();
  if (notificationCount < 0 || notificationCount > NOTIFICATION_COUNT) notificationCount = 0;
}

// Called after every mutation (new notification arrives, one gets
// deleted) - each write is at most sizeof(PwNotification)*NOTIFICATION_COUNT
// (~1.4KB), infrequent enough (notifications, not a per-second value) that
// NVS wear isn't a concern here the way it would be for something written
// every minute.
void saveNotifications() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putBytes(kPrefsNotificationsKey, notifications, sizeof(PwNotification) * NOTIFICATION_COUNT);
  prefs.putInt(kPrefsNotificationCountKey, notificationCount);
  prefs.end();
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

constexpr const char *kPrefsVibWinFromKey = "vibWinFrom";
constexpr const char *kPrefsVibWinToKey = "vibWinTo";
constexpr const char *kPrefsVibWinEnabledKey = "vibWinOn";

// Defaults (7-22, enabled per defaultEnabled - the compile-time
// settings.vibrateOClock, passed in from init() since this free function
// can't reach the PicoWatch instance's settings member itself) keep the
// previous always-on-24/7 behavior's intent - vibrate on the hour - but
// confined to typical waking hours instead of also firing all night,
// which is the whole point of this setting existing. settings.vibrateOClock
// is no longer consulted once this has been loaded once.
void loadVibrateWindow(bool defaultEnabled) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  vibrateWindowFromHour = prefs.getUChar(kPrefsVibWinFromKey, 7);
  vibrateWindowToHour = prefs.getUChar(kPrefsVibWinToKey, 22);
  vibrateWindowEnabled = prefs.getBool(kPrefsVibWinEnabledKey, defaultEnabled);
  prefs.end();
}

void saveVibrateWindow() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsVibWinFromKey, vibrateWindowFromHour);
  prefs.putUChar(kPrefsVibWinToKey, vibrateWindowToHour);
  prefs.putBool(kPrefsVibWinEnabledKey, vibrateWindowEnabled);
  prefs.end();
}

// FromHour > ToHour means the window spans midnight (e.g. 22 -> 6 covers
// 22:00-05:59), same convention as config.h's NIGHT_SLEEP_AFTER_HOUR/
// NIGHT_SLEEP_BEFORE_HOUR.
bool hourInVibrateWindow(uint8_t hour) {
  if (vibrateWindowFromHour <= vibrateWindowToHour) {
    return hour >= vibrateWindowFromHour && hour < vibrateWindowToHour;
  }
  return hour >= vibrateWindowFromHour || hour < vibrateWindowToHour;
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

constexpr const char *kPrefsMenuInvertedKey = "menuInv";

void loadMenuInverted() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  menuInverted = prefs.getBool(kPrefsMenuInvertedKey, false);
  prefs.end();
}

void saveMenuInverted() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putBool(kPrefsMenuInvertedKey, menuInverted);
  prefs.end();
}

constexpr const char *kPrefsNotifyIntervalKey = "bleNotifyM";

void loadNotifyInterval() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  bleNotifyIntervalMin = prefs.getUChar(kPrefsNotifyIntervalKey, BLE_NOTIFY_CHECK_INTERVAL_MIN);
  prefs.end();
}

void saveNotifyInterval() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsNotifyIntervalKey, bleNotifyIntervalMin);
  prefs.end();
}

constexpr const char *kPrefsInternetAccessKey = "inetMode";

void loadInternetAccessMode() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  internetAccessMode = prefs.getUChar(kPrefsInternetAccessKey, INTERNET_ACCESS_WIFI);
  prefs.end();
}

void saveInternetAccessMode() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsInternetAccessKey, internetAccessMode);
  prefs.end();
}

constexpr const char *kPrefsNotifyPopupEnabledKey = "notifPopOn";
constexpr const char *kPrefsNotifyPopupDurationKey = "notifPopS";
constexpr const char *kPrefsNotifyIconEnabledKey = "notifIconOn";
constexpr const char *kPrefsNotifyIconLightKey = "notifIconLt";
constexpr const char *kPrefsNotifyVibrateEnabledKey = "notifVibOn";

void loadNotificationSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  notificationPopupEnabled   = prefs.getBool(kPrefsNotifyPopupEnabledKey, true);
  notificationPopupDurationS = prefs.getUChar(kPrefsNotifyPopupDurationKey, NOTIFICATION_POPUP_DURATION_DEFAULT_S);
  notificationIconEnabled    = prefs.getBool(kPrefsNotifyIconEnabledKey, true);
  notificationIconLight      = prefs.getBool(kPrefsNotifyIconLightKey, true);
  notificationVibrateEnabled = prefs.getBool(kPrefsNotifyVibrateEnabledKey, true);
  prefs.end();
}

void saveNotificationSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putBool(kPrefsNotifyPopupEnabledKey, notificationPopupEnabled);
  prefs.putUChar(kPrefsNotifyPopupDurationKey, notificationPopupDurationS);
  prefs.putBool(kPrefsNotifyIconEnabledKey, notificationIconEnabled);
  prefs.putBool(kPrefsNotifyIconLightKey, notificationIconLight);
  prefs.putBool(kPrefsNotifyVibrateEnabledKey, notificationVibrateEnabled);
  prefs.end();
}

constexpr const char *kPrefsVibrationStrengthKey = "vibStrength";

void loadVibrationSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  vibrationStrength = prefs.getUChar(kPrefsVibrationStrengthKey, VIBRATION_STRENGTH_HIGH);
  prefs.end();
}

void saveVibrationSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putUChar(kPrefsVibrationStrengthKey, vibrationStrength);
  prefs.end();
}

const char *vibrationStrengthName(uint8_t strength) {
  switch (strength) {
  case VIBRATION_STRENGTH_LOW: return PW_VIBRATION_STRENGTH_LOW;
  case VIBRATION_STRENGTH_MEDIUM: return PW_VIBRATION_STRENGTH_MEDIUM;
  default: return PW_VIBRATION_STRENGTH_HIGH;
  }
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

// Shifted up one notch on Jan's request (12.08.2026): the old built-in
// classic GFX font ("Small", nullptr - genuinely tiny on a 200px display,
// and has no Latin-1 glyphs for umlauts either) is gone entirely. What used
// to be "Default" (9pt) is now the bottom tier "Small", what used to be
// "Big" (12pt) is now the middle tier "Default", and a new, larger 15pt
// tier takes over as "Big" - see PicoWatch::showFontSizeSettings() and
// FreeMonoBold15pt8b.h. UI_FONT_SIZE_SMALL/DEFAULT/BIG (config.h) keep
// their old names/values (0/1/2, still just an NVS-persisted ordinal) even
// though what each now points to shifted - only the three functions below
// (plus the picker's own preview font, showFontSizeSettings()) needed to
// change.
const GFXfont *uiMenuFont() {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: return &FreeMonoBold9pt8b;
  case UI_FONT_SIZE_BIG: return &FreeMonoBold15pt8b;
  default: return &FreeMonoBold12pt8b;
  }
}

// Settings -> "Invert Menu" (menuInverted), factored out of uiRenderList()
// (which has used this exact swap since Invert Menu shipped) so every
// Settings submenu/detail screen (setAlarm(), showVibrateWindowSettings(),
// setTime(), etc.) can honor it too instead of hardcoding GxEPD_BLACK/
// WHITE - Jan wanted these consistent with the list menus (15.08.2026).
// Deliberately NOT used by showInvertMenuSettings()/showFontSizeSettings()
// themselves - those preview the CANDIDATE choice via their own local
// variables before it's committed, see their own comments.
uint16_t uiBgColor() { return menuInverted ? GxEPD_WHITE : GxEPD_BLACK; }
uint16_t uiFgColor() { return menuInverted ? GxEPD_BLACK : GxEPD_WHITE; }

// Rounded on/off toggle switch, drawn instead of the old plain "Yes"/"No"
// text for pure enabled/disabled settings fields - Jan wanted something
// rounder/nicer to look at than text for a simple on/off (16.08.2026).
// (x, y) is the pill's top-left corner. Colors are passed in explicitly
// (rather than reading uiFgColor()/uiBgColor() internally) since not
// every screen that has an on/off field honors Invert Menu yet - see
// setAlarm()'s call site, which still hardcodes GxEPD_WHITE/BLACK like
// the rest of that screen. Focus (the field currently selected,
// mid-blink) is shown as a thin outline box around the whole toggle
// rather than inverting its colors - inverting would make an "on"
// toggle look identical to an "off" one while blinking.
void uiDrawToggle(int16_t x, int16_t y, bool value, bool selected, bool blink, uint16_t fg, uint16_t bg) {
  constexpr int16_t w = 34, h = 16, r = h / 2;

  // display is a static PicoWatch member (see PicoWatch.h) - this is a
  // free function like uiFgColor()/uiBgColor() above, so it needs the
  // qualified name rather than the bare "display" a PicoWatch:: member
  // function could use.
  if (value) {
    PicoWatch::display.fillRoundRect(x, y, w, h, r, fg);
  } else {
    PicoWatch::display.drawRoundRect(x, y, w, h, r, fg);
  }

  const int16_t knobR = r - 3;
  const int16_t knobY = y + h / 2;
  const int16_t knobX = value ? (x + w - r) : (x + r);
  PicoWatch::display.fillCircle(knobX, knobY, knobR, value ? bg : fg);

  if (selected && blink) {
    PicoWatch::display.drawRoundRect(x - 3, y - 3, w + 6, h + 6, r, fg);
  }
}

// Row spacing and highlight-box padding tuned per size - the generic
// scroll/truncation machinery below (uiListVisibleRows() etc.) means rows
// no longer have to add up to fit DISPLAY_HEIGHT unscrolled, so these are
// just "looks right for this font" values, not a fit constraint anymore.
int uiMenuRowHeight() {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: return MENU_HEIGHT;
  case UI_FONT_SIZE_BIG: return 40;
  default: return 32;
  }
}

// How many rows of a `total`-item list fit edge-to-edge in DISPLAY_HEIGHT
// at the current row height, capped at the list's actual length (no point
// reserving scroll space that will never be used). Shared by every list
// screen (main menu, Settings, Games, Time, Debug) via uiRenderList() below
// - previously only the Settings list scrolled, so any other list long
// enough (or a big enough font) to exceed DISPLAY_HEIGHT just silently ran
// its last row(s) off the bottom of the screen (e.g. the 7-item main menu
// at the "Big" font size, 7*32=224 > 200).
int uiListVisibleRows(int total) {
  const int rows = DISPLAY_HEIGHT / uiMenuRowHeight();
  return min(rows, total);
}

// Keeps the current selection centered where possible and clamped at the
// top/bottom of the full list, same idea as the old Settings-only
// settingsMenuScrollOffset(). Stateless - recomputed fresh from the
// selected index every render, no separate scroll-position variable to
// keep in sync.
int uiListScrollOffset(int index, int total, int visibleRows) {
  if (total <= visibleRows) return 0;
  const int maxOffset = total - visibleRows;
  int offset = index - visibleRows / 2;
  if (offset < 0) offset = 0;
  if (offset > maxOffset) offset = maxOffset;
  return offset;
}

void uiMenuHighlightPadding(int16_t &yOffset, uint16_t &heightPad) {
  switch (uiFontSize) {
  case UI_FONT_SIZE_SMALL: yOffset = -10; heightPad = 15; break;
  case UI_FONT_SIZE_BIG: yOffset = -15; heightPad = 22; break;
  default: yOffset = -12; heightPad = 18; break;
  }
}

// Decodes 2-byte UTF-8 sequences (U+0080-U+07FF, which in practice here
// only ever means Latin-1 Supplement - German umlauts/ss) into the single
// Latin-1 byte uiMenuFont()'s extended-range GFXfont (FreeMonoBold9pt8b/
// 12pt8b, see their header comment) actually indexes glyphs by. Lets the
// localization tables stay normal, directly-editable UTF-8 source (type
// "ü" like anywhere else) instead of needing hand-escaped \xFC bytes -
// Adafruit_GFX has no UTF-8 awareness itself, it treats every byte of a
// string as one glyph index. Anything outside that 2-byte-sequence shape
// (plain ASCII, or any other multi-byte UTF-8 this project's strings don't
// use) passes through unchanged.
String uiUtf8ToLatin1(const char *text) {
  String out;
  const uint8_t *p = (const uint8_t *)text;
  while (*p) {
    if ((p[0] & 0xE0) == 0xC0 && p[1] != 0 && (p[1] & 0xC0) == 0x80) {
      out += (char)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
      p += 2;
    } else {
      out += (char)p[0];
      p += 1;
    }
  }
  return out;
}

// Chops characters off the end of `text` until it fits within `maxWidth`
// at the current font, marking the cut with a trailing "." so it reads as
// deliberately abbreviated rather than silently clipped - a translation, a
// long device name (weekday + action names, etc.) or just the "Big" font
// size can otherwise run text off the right edge of the display.
// getTextBounds() measures the string's tight ink extent, which is
// consistently narrower than the cursor's actual advance while printing
// (each glyph's xAdvance includes spacing getTextBounds doesn't draw) - so
// a naive "does the ink fit?" check lets some strings through that still
// overflow once printed, at which point Adafruit_GFX's default autowrap
// wraps the tail onto (and overwrites) the row below. Re-measuring each
// candidate WITH the trailing "." (not just the bare truncated text)
// keeps the truncation itself honest against that same discrepancy.
String uiTruncateToWidth(const char *text, uint16_t maxWidth) {
  String s = uiUtf8ToLatin1(text);
  int16_t x1, y1;
  uint16_t width, h;
  PicoWatch::display.getTextBounds(s, 0, 0, &x1, &y1, &width, &h);
  if (width <= maxWidth) return s;
  while (s.length() > 1) {
    s.remove(s.length() - 1);
    const String candidate = s + ".";
    PicoWatch::display.getTextBounds(candidate, 0, 0, &x1, &y1, &width, &h);
    if (width <= maxWidth) return candidate;
  }
  return s;
}

// Word-wraps `text` to fit within maxWidth at the current font, breaking
// only at spaces - unlike Adafruit_GFX's own setTextWrap(true) autowrap,
// which just counts characters and breaks wherever the row runs out,
// splitting words in half mid-word (Jan, 15.08.2026: "Back to disconnect"
// wrapped as "Back to disconn" / "ect"). Returns the text with '\n'
// inserted at the chosen break points - print()/println() honor an
// embedded '\n' directly (advance to the next line) regardless of
// setTextWrap()'s own setting, so the caller doesn't need wrap on at all
// once text has been run through this. A single word wider than maxWidth
// on its own is left alone (nothing sensible to do without actually
// splitting the word) rather than looping forever trying to shrink it.
String uiWrapWords(const char *text, uint16_t maxWidth) {
  const String s = uiUtf8ToLatin1(text);
  String out, line;
  int16_t x1, y1;
  uint16_t width, h;
  int wordStart = 0;
  for (int i = 0; i <= (int)s.length(); i++) {
    if (i != (int)s.length() && s[i] != ' ') continue;
    const String word = s.substring(wordStart, i);
    wordStart = i + 1;
    if (word.length() == 0) continue;
    const String candidate = line.length() == 0 ? word : line + " " + word;
    PicoWatch::display.getTextBounds(candidate, 0, 0, &x1, &y1, &width, &h);
    if (width <= maxWidth || line.length() == 0) {
      line = candidate;
    } else {
      if (out.length() > 0) out += '\n';
      out += line;
      line = word;
    }
  }
  if (line.length() > 0) {
    if (out.length() > 0) out += '\n';
    out += line;
  }
  return out;
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

constexpr const char *kPrefsHostnameKey = "hostname";

void loadHostname() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);  // read-only
  const String saved = prefs.getString(kPrefsHostnameKey, "");
  prefs.end();
  strncpy(picoWatchHostname, saved.c_str(), sizeof(picoWatchHostname) - 1);
  picoWatchHostname[sizeof(picoWatchHostname) - 1] = '\0';
}

void saveHostname(const char *hostname) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);  // read-write
  prefs.putString(kPrefsHostnameKey, hostname);
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
RTC_DATA_ATTR int gamesMenuIndex;
RTC_DATA_ATTR int timeMenuIndex;
RTC_DATA_ATTR int debugMenuIndex;
// See kPrefsStepsBaselineKey's comment above (PicoWatch::todaySteps()) -
// stepsBaseline is loaded from NVS once per true reset (onReset()) and
// stays fixed for the rest of that boot session; lastSavedStepsTotal just
// tracks what's currently written to NVS so _persistStepsProgress() can
// skip the write when nothing changed since the last minute tick.
RTC_DATA_ATTR uint32_t stepsBaseline;
RTC_DATA_ATTR uint32_t lastSavedStepsTotal;
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
      if (vibrateWindowEnabled && currentTime.Minute == 0 &&
          hourInVibrateWindow(currentTime.Hour)) {
        // The RTC wakes us up once per minute
        vibMotor(75, 4);
      }
      _captureStepsAtMidnight(); // must run first - resets stepsBaseline/lastSavedStepsTotal to 0 at midnight
      _persistStepsProgress();
      if (alarmEnabled && currentTime.Hour == alarmHour && currentTime.Minute == alarmMinute) {
        vibMotor(150, 10); // longer/more pulses than the o'clock vibration, so it's distinguishable
      }
      _checkBleNotifications();
      // notificationPopupEnabled only gates the popup itself (vibration +
      // full-screen overlay) - hasUnreadNotification stays set either way,
      // so the watchface icon (if that's enabled) still shows.
      if (notificationPopupEnabled && hasUnreadNotification) {
        _showNotificationPopup(); // vibrates + redraws the watchface itself once dismissed
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
    loadVibrateWindow(settings.vibrateOClock);
    loadWeatherCityID(settings.cityID);
    loadHostname();
    loadButtonSettings();
    loadFontSize();
    loadMenuInverted();
    loadNotifyInterval();
    loadInternetAccessMode();
    loadNotificationSettings();
    loadVibrationSettings();
    loadNotifications();
    loadLanguage();
    // _bmaConfig() above just soft-reset the sensor's own step counter to
    // 0 (see kPrefsStepsBaselineKey's comment) - restore whatever was last
    // persisted as "today's steps so far" as the new baseline, so
    // todaySteps() picks up close to where it left off instead of
    // silently dropping back to 0 on every reset/reflash.
    stepsBaseline = loadStepsBaseline();
    lastSavedStepsTotal = stepsBaseline;
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
    w->showNotifications();
    break;
  case 6:
    w->showGamesMenu(gamesMenuIndex, false);
    break;
  case 7:
    w->showSettingsMenu(settingsMenuIndex, false);
    break;
  default:
    break;
  }
}

void dispatchGamesMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->playSnake();
    break;
  case 1:
    w->playPong();
    break;
  case 2:
    w->playTetris();
    break;
  case 3:
    w->playFlappy();
    break;
  default:
    break;
  }
}

// Wraps `idx` by one step within [0, length) - the Up(-1)/Down(+1)
// index-cycling arithmetic repeated for every guiState in BOTH
// handleButtonPress()'s single-press path and its fast-menu polling loop
// (MAIN_MENU_STATE, SETTINGS_MENU_STATE, GAMES_MENU_STATE, TIME_MENU_STATE,
// DEBUG_MENU_STATE x Up/Down x 2 call sites = 20 places). Deliberately
// factors out ONLY this pure arithmetic, not the surrounding control flow
// (which guiState maps to which action, the WATCHFACE_STATE special
// cases, loop-break placement) - that control flow differs on purpose
// between the two paths and is exactly what caused the 31.07.2026
// fast-menu regression (commit 157914b) when it was last touched; see
// project memory, 14.08.2026 for why a full merge of the two paths was
// deliberately scoped down to just this safe piece.
int menuIndexCycle(int idx, bool up, int length) {
  if (up) {
    idx--;
    if (idx < 0) idx = length - 1;
  } else {
    idx++;
    if (idx > length - 1) idx = 0;
  }
  return idx;
}

void dispatchSettingsMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->showAbout();
    break;
  case 1:
    w->showTimeMenu(timeMenuIndex, false);
    break;
  case 2:
    w->setupWifi();
    break;
  case 3:
    w->showInternetAccessSettings();
    break;
  case 4:
    w->showNotificationSettings();
    break;
  case 5:
    w->showVibrationSettings();
    break;
  case 6:
    w->setWeatherCity();
    break;
  case 7:
    w->updateFromGithub();
    break;
  case 8:
    w->showButtonSettings();
    break;
  case 9:
    w->showFontSizeSettings();
    break;
  case 10:
    w->showInvertMenuSettings();
    break;
  case 11:
    w->showLanguageSettings();
    break;
  case 12:
    w->showDebugMenu(debugMenuIndex, false);
    break;
  default:
    break;
  }
}

void dispatchTimeMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->setTime();
    break;
  case 1:
    w->showSyncNTP();
    break;
  case 2:
    w->setTimezone();
    break;
  case 3:
    w->showVibrateWindowSettings();
    break;
  case 4:
    w->showNotifyIntervalSettings();
    break;
  default:
    break;
  }
}

void dispatchDebugMenu(PicoWatch *w, int index) {
  switch (index) {
  case 0:
    w->showBuzz();
    break;
  case 1:
    w->showAccelerometer();
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
      // handles MAIN_MENU_STATE/APP_STATE/SETTINGS_MENU_STATE, so falling
      // through into it while already on WATCHFACE_STATE would leave the
      // watch appearing frozen (ignoring all button input) for up to 5
      // seconds. Return immediately in that case, same as the "Back while
      // already on WATCHFACE_STATE" case below.
      if (guiState == WATCHFACE_STATE) return;
    } else if (guiState == SETTINGS_MENU_STATE) { // select settings submenu item
      dispatchSettingsMenu(this, settingsMenuIndex);
    } else if (guiState == GAMES_MENU_STATE) { // select games submenu item
      dispatchGamesMenu(this, gamesMenuIndex);
    } else if (guiState == TIME_MENU_STATE) { // select time submenu item
      dispatchTimeMenu(this, timeMenuIndex);
    } else if (guiState == DEBUG_MENU_STATE) { // select debug submenu item
      dispatchDebugMenu(this, debugMenuIndex);
    }
  }
  // Back Button
  else if (wakeupBit & logicalBackMask) {
    if (guiState == MAIN_MENU_STATE) { // exit to watch face if already in menu
      RTC.read(currentTime);
      showWatchFace(false);
    } else if (guiState == SETTINGS_MENU_STATE) { // exit to top menu if already in settings
      showMenu(menuIndex, false);
    } else if (guiState == GAMES_MENU_STATE) { // exit to top menu if already in games
      showMenu(menuIndex, false);
    } else if (guiState == TIME_MENU_STATE) { // exit to settings menu if already in time
      showSettingsMenu(settingsMenuIndex, false);
    } else if (guiState == DEBUG_MENU_STATE) { // exit to settings menu if already in debug
      showSettingsMenu(settingsMenuIndex, false);
    } else if (guiState == APP_STATE) {
      showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in a settings app
    } else if (guiState == WATCHFACE_STATE) {
      return;
    }
  }
  // Up Button
  else if (wakeupBit & UP_BTN_MASK) {
    if (guiState == MAIN_MENU_STATE) { // increment menu index
      menuIndex = menuIndexCycle(menuIndex, true, MENU_LENGTH);
      showMenu(menuIndex, true);
    } else if (guiState == SETTINGS_MENU_STATE) { // increment settings menu index
      settingsMenuIndex = menuIndexCycle(settingsMenuIndex, true, SETTINGS_MENU_LENGTH);
      showSettingsMenu(settingsMenuIndex, true);
    } else if (guiState == GAMES_MENU_STATE) { // increment games menu index
      gamesMenuIndex = menuIndexCycle(gamesMenuIndex, true, GAMES_MENU_LENGTH);
      showGamesMenu(gamesMenuIndex, true);
    } else if (guiState == TIME_MENU_STATE) { // increment time menu index
      timeMenuIndex = menuIndexCycle(timeMenuIndex, true, TIME_MENU_LENGTH);
      showTimeMenu(timeMenuIndex, true);
    } else if (guiState == DEBUG_MENU_STATE) { // increment debug menu index
      debugMenuIndex = menuIndexCycle(debugMenuIndex, true, DEBUG_MENU_LENGTH);
      showDebugMenu(debugMenuIndex, true);
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
      menuIndex = menuIndexCycle(menuIndex, false, MENU_LENGTH);
      showMenu(menuIndex, true);
    } else if (guiState == SETTINGS_MENU_STATE) { // decrement settings menu index
      settingsMenuIndex = menuIndexCycle(settingsMenuIndex, false, SETTINGS_MENU_LENGTH);
      showSettingsMenu(settingsMenuIndex, true);
    } else if (guiState == GAMES_MENU_STATE) { // decrement games menu index
      gamesMenuIndex = menuIndexCycle(gamesMenuIndex, false, GAMES_MENU_LENGTH);
      showGamesMenu(gamesMenuIndex, true);
    } else if (guiState == TIME_MENU_STATE) { // decrement time menu index
      timeMenuIndex = menuIndexCycle(timeMenuIndex, false, TIME_MENU_LENGTH);
      showTimeMenu(timeMenuIndex, true);
    } else if (guiState == DEBUG_MENU_STATE) { // decrement debug menu index
      debugMenuIndex = menuIndexCycle(debugMenuIndex, false, DEBUG_MENU_LENGTH);
      showDebugMenu(debugMenuIndex, true);
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
        } else if (guiState == GAMES_MENU_STATE) {
          dispatchGamesMenu(this, gamesMenuIndex);
        } else if (guiState == TIME_MENU_STATE) {
          dispatchTimeMenu(this, timeMenuIndex);
        } else if (guiState == DEBUG_MENU_STATE) {
          dispatchDebugMenu(this, debugMenuIndex);
        }
      } else if (digitalRead(logicalBackPin) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState ==
            MAIN_MENU_STATE) { // exit to watch face if already in menu
          RTC.read(currentTime);
          showWatchFace(false);
          break; // leave loop
        } else if (guiState == SETTINGS_MENU_STATE) {
          showMenu(menuIndex, false); // exit to top menu if already in settings
        } else if (guiState == GAMES_MENU_STATE) {
          showMenu(menuIndex, false); // exit to top menu if already in games
        } else if (guiState == TIME_MENU_STATE) {
          showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in time
        } else if (guiState == DEBUG_MENU_STATE) {
          showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in debug
        } else if (guiState == APP_STATE) {
          showSettingsMenu(settingsMenuIndex, false); // exit to settings menu if already in a settings app
        }
      } else if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // increment menu index
          menuIndex = menuIndexCycle(menuIndex, true, MENU_LENGTH);
          showFastMenu(menuIndex);
        } else if (guiState == SETTINGS_MENU_STATE) {
          settingsMenuIndex = menuIndexCycle(settingsMenuIndex, true, SETTINGS_MENU_LENGTH);
          showFastSettingsMenu(settingsMenuIndex);
        } else if (guiState == GAMES_MENU_STATE) {
          gamesMenuIndex = menuIndexCycle(gamesMenuIndex, true, GAMES_MENU_LENGTH);
          showFastGamesMenu(gamesMenuIndex);
        } else if (guiState == TIME_MENU_STATE) {
          timeMenuIndex = menuIndexCycle(timeMenuIndex, true, TIME_MENU_LENGTH);
          showFastTimeMenu(timeMenuIndex);
        } else if (guiState == DEBUG_MENU_STATE) {
          debugMenuIndex = menuIndexCycle(debugMenuIndex, true, DEBUG_MENU_LENGTH);
          showFastDebugMenu(debugMenuIndex);
        }
      } else if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
        lastTimeout = millis();
        if (guiState == MAIN_MENU_STATE) { // decrement menu index
          menuIndex = menuIndexCycle(menuIndex, false, MENU_LENGTH);
          showFastMenu(menuIndex);
        } else if (guiState == SETTINGS_MENU_STATE) {
          settingsMenuIndex = menuIndexCycle(settingsMenuIndex, false, SETTINGS_MENU_LENGTH);
          showFastSettingsMenu(settingsMenuIndex);
        } else if (guiState == GAMES_MENU_STATE) {
          gamesMenuIndex = menuIndexCycle(gamesMenuIndex, false, GAMES_MENU_LENGTH);
          showFastGamesMenu(gamesMenuIndex);
        } else if (guiState == TIME_MENU_STATE) {
          timeMenuIndex = menuIndexCycle(timeMenuIndex, false, TIME_MENU_LENGTH);
          showFastTimeMenu(timeMenuIndex);
        } else if (guiState == DEBUG_MENU_STATE) {
          debugMenuIndex = menuIndexCycle(debugMenuIndex, false, DEBUG_MENU_LENGTH);
          showFastDebugMenu(debugMenuIndex);
        }
      }
    }
  }
}

// Shared body behind every show*Menu()/showFast*Menu() pair (main menu,
// Settings, Games, Time, Debug) - draws a scrollable, width-truncated list
// of `total` items with `selectedIndex` highlighted, in the current
// Settings -> Font Size choice. Doesn't touch guiState/alreadyInMenu;
// callers set those themselves same as before, since the Fast variants
// intentionally don't reset alreadyInMenu. Protected (not a free function)
// so subclasses with their own list-style pickers can use it too, and
// automatically stay in sync with the font-size setting - see
// MultiFacePicoWatch::changeWatchface(), which used to hardcode its own
// font/spacing and silently ignore Font Size entirely.
void PicoWatch::uiRenderList(const char *const *items, int total, int selectedIndex,
                              bool partialRefresh) {
  // Settings -> "Invert Menu" (menuInverted) swaps this whole scheme:
  // normally white-on-black with a white highlight bar/black selected text,
  // inverted flips every one of those four colors. uiBgColor()/uiFgColor()
  // are this exact swap, factored out so other Settings screens can use it.
  const uint16_t bgColor = uiBgColor();
  const uint16_t fgColor = uiFgColor();
  const uint16_t highlightColor = fgColor;
  const uint16_t selectedTextColor = bgColor;

  display.setFullWindow();
  display.fillScreen(bgColor);
  display.setFont(uiMenuFont());
  // Belt-and-suspenders against uiTruncateToWidth() ever being wrong by a
  // few pixels: with wrap left at Adafruit_GFX's default (on), an
  // overflowing string wraps its tail onto the row below and overwrites
  // it - with wrap off, worst case is a few clipped pixels at the very
  // right edge, never a corrupted neighboring row.
  display.setTextWrap(false);

  int16_t x1, y1;
  uint16_t width, h;
  int16_t yPos;
  const int rowHeight = uiMenuRowHeight();
  int16_t highlightYOffset;
  uint16_t highlightHeightPad;
  uiMenuHighlightPadding(highlightYOffset, highlightHeightPad);

  // A few px of slack below the physical display width - see
  // uiTruncateToWidth()'s comment on why the ink-extent measurement it
  // uses can still under-count the real printed advance width.
  constexpr uint16_t kMaxLabelWidth = DISPLAY_WIDTH - 8;

  const int visibleRows = uiListVisibleRows(total);
  const int scrollOffset = uiListScrollOffset(selectedIndex, total, visibleRows);
  const int visibleCount = min(visibleRows, total - scrollOffset);
  for (int row = 0; row < visibleCount; row++) {
    const int i = scrollOffset + row;
    yPos = rowHeight + (rowHeight * row);
    const String label = uiTruncateToWidth(items[i], kMaxLabelWidth);
    display.setCursor(0, yPos);
    display.getTextBounds(label, 0, yPos, &x1, &y1, &width, &h);
    uint16_t color;
    if (i == selectedIndex) {
      display.fillRect(0, y1 + highlightYOffset, DISPLAY_WIDTH, h + highlightHeightPad, highlightColor);
      color = selectedTextColor;
    } else {
      color = fgColor;
    }
    display.setTextColor(color);
    display.println(label);
  }

  display.display(partialRefresh);
}

void PicoWatch::showMenu(byte menuIndex, bool partialRefresh) {
  const char *const kItems[] = {PW_MENU_CHANGE_WATCHFACE, PW_MENU_STOPWATCH,       PW_MENU_STEPS,
                                 PW_MENU_ALARM,            PW_MENU_WEATHER,        PW_MENU_NOTIFICATIONS,
                                 PW_MENU_GAMES,            PW_MENU_SETTINGS};
  uiRenderList(kItems, MENU_LENGTH, menuIndex, partialRefresh);
  guiState = MAIN_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastMenu(byte menuIndex) {
  const char *const kItems[] = {PW_MENU_CHANGE_WATCHFACE, PW_MENU_STOPWATCH,       PW_MENU_STEPS,
                                 PW_MENU_ALARM,            PW_MENU_WEATHER,        PW_MENU_NOTIFICATIONS,
                                 PW_MENU_GAMES,            PW_MENU_SETTINGS};
  uiRenderList(kItems, MENU_LENGTH, menuIndex, true);
  guiState = MAIN_MENU_STATE;
}

void PicoWatch::showSettingsMenu(byte settingsMenuIndex, bool partialRefresh) {
  // Vibrate Motor Test/Show Accelerometer used to be separate entries here -
  // now grouped under the single "Debug" entry (see kDebugMenuItems in
  // showDebugMenu()) so this top-level list stays shorter. Set Time/Sync
  // NTP/Set Timezone got the same treatment earlier (see kTimeMenuItems).
  const char *const kItems[] = {PW_SETTINGS_ABOUT,           PW_SETTINGS_TIME,
                                 PW_SETTINGS_SETUP_WIFI,      PW_INTERNET_ACCESS_TITLE,
                                 PW_NOTIF_SETTINGS_TITLE,     PW_SETTINGS_VIBRATION,
                                 PW_SETTINGS_SET_CITY,        PW_SETTINGS_UPDATE_GITHUB,
                                 PW_SETTINGS_BUTTON_SETTINGS, PW_SETTINGS_FONT_SIZE,
                                 PW_SETTINGS_INVERT_MENU,     PW_SETTINGS_LANGUAGE,
                                 PW_SETTINGS_DEBUG};
  uiRenderList(kItems, SETTINGS_MENU_LENGTH, settingsMenuIndex, partialRefresh);
  guiState = SETTINGS_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastSettingsMenu(byte settingsMenuIndex) {
  const char *const kItems[] = {PW_SETTINGS_ABOUT,           PW_SETTINGS_TIME,
                                 PW_SETTINGS_SETUP_WIFI,      PW_INTERNET_ACCESS_TITLE,
                                 PW_NOTIF_SETTINGS_TITLE,     PW_SETTINGS_VIBRATION,
                                 PW_SETTINGS_SET_CITY,        PW_SETTINGS_UPDATE_GITHUB,
                                 PW_SETTINGS_BUTTON_SETTINGS, PW_SETTINGS_FONT_SIZE,
                                 PW_SETTINGS_INVERT_MENU,     PW_SETTINGS_LANGUAGE,
                                 PW_SETTINGS_DEBUG};
  uiRenderList(kItems, SETTINGS_MENU_LENGTH, settingsMenuIndex, true);
  guiState = SETTINGS_MENU_STATE;
}

void PicoWatch::showGamesMenu(byte gamesMenuIndex, bool partialRefresh) {
  const char *const kItems[] = {PW_GAME_SNAKE, PW_GAME_PONG, PW_GAME_TETRIS, PW_GAME_FLAPPY};
  uiRenderList(kItems, GAMES_MENU_LENGTH, gamesMenuIndex, partialRefresh);
  guiState = GAMES_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastGamesMenu(byte gamesMenuIndex) {
  const char *const kItems[] = {PW_GAME_SNAKE, PW_GAME_PONG, PW_GAME_TETRIS, PW_GAME_FLAPPY};
  uiRenderList(kItems, GAMES_MENU_LENGTH, gamesMenuIndex, true);
  guiState = GAMES_MENU_STATE;
}

void PicoWatch::showTimeMenu(byte timeMenuIndex, bool partialRefresh) {
  const char *const kItems[] = {PW_SETTINGS_SET_TIME, PW_SETTINGS_SYNC_NTP,
                                 PW_SETTINGS_SET_TIMEZONE, PW_TIME_VIBRATE_WINDOW,
                                 PW_TIME_NOTIFY_INTERVAL};
  uiRenderList(kItems, TIME_MENU_LENGTH, timeMenuIndex, partialRefresh);
  guiState = TIME_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastTimeMenu(byte timeMenuIndex) {
  const char *const kItems[] = {PW_SETTINGS_SET_TIME, PW_SETTINGS_SYNC_NTP,
                                 PW_SETTINGS_SET_TIMEZONE, PW_TIME_VIBRATE_WINDOW,
                                 PW_TIME_NOTIFY_INTERVAL};
  uiRenderList(kItems, TIME_MENU_LENGTH, timeMenuIndex, true);
  guiState = TIME_MENU_STATE;
}

// Debug submenu (Vibrate Motor Test/Show Accelerometer) - reached via
// Settings' "Debug" entry, same list-rendering pattern as the other
// submenus but for DEBUG_MENU_LENGTH items and DEBUG_MENU_STATE.
void PicoWatch::showDebugMenu(byte debugMenuIndex, bool partialRefresh) {
  const char *const kItems[] = {PW_SETTINGS_VIBRATE, PW_SETTINGS_ACCELEROMETER};
  uiRenderList(kItems, DEBUG_MENU_LENGTH, debugMenuIndex, partialRefresh);
  guiState = DEBUG_MENU_STATE;
  alreadyInMenu = false;
}

void PicoWatch::showFastDebugMenu(byte debugMenuIndex) {
  const char *const kItems[] = {PW_SETTINGS_VIBRATE, PW_SETTINGS_ACCELEROMETER};
  uiRenderList(kItems, DEBUG_MENU_LENGTH, debugMenuIndex, true);
  guiState = DEBUG_MENU_STATE;
}

void PicoWatch::showVibrateWindowSettings() {
  guiState = APP_STATE;

  uint8_t fromHour = vibrateWindowFromHour;
  uint8_t toHour = vibrateWindowToHour;
  bool enabled = vibrateWindowEnabled;

  int8_t setIndex = SET_VIBWIN_FROM;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as setAlarm() - Menu was just used to select
  // "Vibrate Window" from the Time menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_VIBWIN_ENABLED) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_VIBWIN_FROM) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_VIBWIN_FROM:
        fromHour == 23 ? (fromHour = 0) : fromHour++;
        break;
      case SET_VIBWIN_TO:
        toHour == 23 ? (toHour = 0) : toHour++;
        break;
      case SET_VIBWIN_ENABLED:
        enabled = !enabled;
        break;
      default:
        break;
      }
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_VIBWIN_FROM:
        fromHour == 0 ? (fromHour = 23) : fromHour--;
        break;
      case SET_VIBWIN_TO:
        toHour == 0 ? (toHour = 23) : toHour--;
        break;
      case SET_VIBWIN_ENABLED:
        enabled = !enabled;
        break;
      default:
        break;
      }
    }

    display.fillScreen(uiBgColor());
    display.setTextColor(uiFgColor());
    display.setFont(uiMenuFont());
    display.setCursor(20, 25);
    display.println(PW_VIBWIN_TITLE);

    display.setCursor(5, 70);
    display.print(PW_VIBWIN_FROM_LABEL);
    if (setIndex == SET_VIBWIN_FROM) {
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    if (fromHour < 10) display.print("0");
    display.println(fromHour);

    display.setTextColor(uiFgColor());
    display.setCursor(5, 105);
    display.print(PW_VIBWIN_TO_LABEL);
    if (setIndex == SET_VIBWIN_TO) {
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    if (toHour < 10) display.print("0");
    display.println(toHour);

    display.setTextColor(uiFgColor());
    display.setCursor(5, 140);
    display.print(PW_ALARM_ENABLED_LABEL);
    uiDrawToggle(150, 128, enabled, setIndex == SET_VIBWIN_ENABLED, blink, uiFgColor(), uiBgColor());

    display.display(true); // partial refresh
  }

  vibrateWindowFromHour = fromHour;
  vibrateWindowToHour = toHour;
  vibrateWindowEnabled = enabled;
  saveVibrateWindow();

  showTimeMenu(timeMenuIndex, false);
}

// Global vibration strength picker (Settings -> "Vibration") - 3-value
// cycle, same shape as showFontSizeSettings()/showInternetAccessSettings()
// (fillRect highlight around the chosen name, Up/Down cycle in opposite
// directions like FontSize's 3-way pick, uiFgColor()/uiBgColor() theming
// like InternetAccess since this is a new screen, not one of the older
// hardcoded-black/white ones). Confirming previews the newly chosen
// strength with an actual buzz, so Jan can feel what he just picked
// instead of guessing (16.08.2026).
void PicoWatch::showVibrationSettings() {
  guiState = APP_STATE;

  uint8_t strength = vibrationStrength;
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
      strength = (strength + 1) % 3;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      strength = (strength + 2) % 3;
    }

    display.fillScreen(uiBgColor());
    display.setFont(uiMenuFont());
    display.setTextWrap(false); // heading only, clip don't wrap - see showInternetAccessSettings()'s comment
    display.setTextColor(uiFgColor());
    display.setCursor(10, 20);
    display.println(PW_VIBRATION_TITLE);

    display.setCursor(10, 55);
    display.println(PW_VIBRATION_STRENGTH_LABEL);

    display.setTextColor(blink ? uiFgColor() : uiBgColor());
    const char *name = vibrationStrengthName(strength);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(name, 10, 90, &x1, &y1, &w, &h); // X must match setCursor() below - see showFontSizeSettings()'s comment
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, uiFgColor());
    display.setCursor(10, 90);
    display.println(name);

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    vibrationStrength = strength;
    saveVibrationSettings();
    vibMotor(); // preview the newly chosen strength
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showNotificationSettings() {
  guiState = APP_STATE;

  bool popupEnabled      = notificationPopupEnabled;
  uint8_t popupDuration  = notificationPopupDurationS;
  bool iconEnabled       = notificationIconEnabled;
  bool iconLight         = notificationIconLight;
  bool vibrateEnabled    = notificationVibrateEnabled;

  int8_t setIndex = SET_NOTIF_POPUP_ENABLED;
  int8_t blink = 0;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as setAlarm()/showVibrateWindowSettings() -
  // Menu was just used to select this screen from the Settings list.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      setIndex++;
      if (setIndex > SET_NOTIF_VIBRATE_ENABLED) {
        break;
      }
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      if (setIndex != SET_NOTIF_POPUP_ENABLED) {
        setIndex--;
      }
    }

    blink = 1 - blink;

    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_NOTIF_POPUP_ENABLED:
        popupEnabled = !popupEnabled;
        break;
      case SET_NOTIF_POPUP_DURATION:
        popupDuration = (popupDuration <= NOTIFICATION_POPUP_DURATION_MIN_S)
                             ? NOTIFICATION_POPUP_DURATION_MAX_S
                             : popupDuration - NOTIFICATION_POPUP_DURATION_STEP_S;
        break;
      case SET_NOTIF_ICON_ENABLED:
        iconEnabled = !iconEnabled;
        break;
      case SET_NOTIF_ICON_LIGHT:
        iconLight = !iconLight;
        break;
      case SET_NOTIF_VIBRATE_ENABLED:
        vibrateEnabled = !vibrateEnabled;
        break;
      default:
        break;
      }
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      switch (setIndex) {
      case SET_NOTIF_POPUP_ENABLED:
        popupEnabled = !popupEnabled;
        break;
      case SET_NOTIF_POPUP_DURATION:
        popupDuration = (popupDuration >= NOTIFICATION_POPUP_DURATION_MAX_S)
                             ? NOTIFICATION_POPUP_DURATION_MIN_S
                             : popupDuration + NOTIFICATION_POPUP_DURATION_STEP_S;
        break;
      case SET_NOTIF_ICON_ENABLED:
        iconEnabled = !iconEnabled;
        break;
      case SET_NOTIF_ICON_LIGHT:
        iconLight = !iconLight;
        break;
      case SET_NOTIF_VIBRATE_ENABLED:
        vibrateEnabled = !vibrateEnabled;
        break;
      default:
        break;
      }
    }

    display.fillScreen(uiBgColor());
    display.setTextColor(uiFgColor());
    display.setFont(uiMenuFont());
    display.setCursor(10, 20);
    display.println(PW_NOTIF_SETTINGS_TITLE);

    display.setCursor(5, 55);
    display.print(PW_NOTIF_SETTINGS_POPUP_LABEL);
    uiDrawToggle(150, 43, popupEnabled, setIndex == SET_NOTIF_POPUP_ENABLED, blink, uiFgColor(), uiBgColor());

    display.setTextColor(uiFgColor());
    display.setCursor(5, 85);
    display.print(PW_NOTIF_SETTINGS_DURATION_LABEL);
    if (setIndex == SET_NOTIF_POPUP_DURATION) {
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    display.print(popupDuration);
    display.println("s");

    display.setTextColor(uiFgColor());
    display.setCursor(5, 115);
    display.print(PW_NOTIF_SETTINGS_ICON_LABEL);
    uiDrawToggle(150, 103, iconEnabled, setIndex == SET_NOTIF_ICON_ENABLED, blink, uiFgColor(), uiBgColor());

    display.setTextColor(uiFgColor());
    display.setCursor(5, 145);
    display.print(PW_NOTIF_SETTINGS_ICON_COLOR_LABEL);
    if (setIndex == SET_NOTIF_ICON_LIGHT) {
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    display.println(iconLight ? PW_NOTIF_SETTINGS_ICON_LIGHT : PW_NOTIF_SETTINGS_ICON_DARK);

    display.setTextColor(uiFgColor());
    display.setCursor(5, 175);
    display.print(PW_NOTIF_SETTINGS_VIBRATE_LABEL);
    uiDrawToggle(150, 163, vibrateEnabled, setIndex == SET_NOTIF_VIBRATE_ENABLED, blink, uiFgColor(), uiBgColor());

    display.display(true); // partial refresh
  }

  notificationPopupEnabled   = popupEnabled;
  notificationPopupDurationS = popupDuration;
  notificationIconEnabled    = iconEnabled;
  notificationIconLight      = iconLight;
  notificationVibrateEnabled = vibrateEnabled;
  saveNotificationSettings();

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::playSnake() {
  guiState = APP_STATE;

  constexpr int kCell = 10;
  constexpr int kCols = DISPLAY_WIDTH / kCell;   // 20
  constexpr int kRows = DISPLAY_HEIGHT / kCell;  // 20
  constexpr int kMaxLen = kCols * kRows;
  constexpr unsigned long kTickMs = 500;
  // Clockwise direction cycle so "turn right" is always (dir+1)%4 and "turn
  // left" is (dir+3)%4, regardless of current heading - avoids a 4-way
  // lookup table for what only needs modular arithmetic.
  enum Direction { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };
  constexpr int8_t kDx[4] = {0, 1, 0, -1};
  constexpr int8_t kDy[4] = {-1, 0, 1, 0};

  static int8_t snakeX[kMaxLen];
  static int8_t snakeY[kMaxLen];
  int length = 3;
  int headIdx = 0; // snake[0] is the head; body shifts down as it grows/moves
  snakeX[0] = kCols / 2;
  snakeY[0] = kRows / 2;
  snakeX[1] = snakeX[0] - 1;
  snakeY[1] = snakeY[0];
  snakeX[2] = snakeX[0] - 2;
  snakeY[2] = snakeY[0];
  int direction = DIR_RIGHT;
  int score = 0;

  int foodX, foodY;
  auto placeFood = [&]() {
    bool onSnake;
    do {
      foodX = random(kCols);
      foodY = random(kRows);
      onSnake = false;
      for (int i = 0; i < length; i++) {
        if (snakeX[i] == foodX && snakeY[i] == foodY) {
          onSnake = true;
          break;
        }
      }
    } while (onSnake);
  };
  placeFood();

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  // Same button-bounce hazard as showStopwatch() - Menu was just used to
  // select "Snake" from the games menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool gameOver = false;
  unsigned long lastTick = millis();
  bool paused = false;
  while (!gameOver) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10);
      showGamesMenu(gamesMenuIndex, false);
      return;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      paused = !paused;
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    // Up/Down turn relative to current heading (see Direction enum above) -
    // this watch only has 2 directional buttons, not a 4-way d-pad, so
    // Snake steers like a classic 2-button "always moving forward" game
    // instead of picking an absolute direction.
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      direction = (direction + 3) % 4; // turn left
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      direction = (direction + 1) % 4; // turn right
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) delay(10);
    }

    if (!paused && millis() - lastTick >= kTickMs) {
      lastTick = millis();

      int newX = snakeX[0] + kDx[direction];
      int newY = snakeY[0] + kDy[direction];

      if (newX < 0 || newX >= kCols || newY < 0 || newY >= kRows) {
        gameOver = true;
      } else {
        for (int i = 0; i < length; i++) {
          if (snakeX[i] == newX && snakeY[i] == newY) {
            gameOver = true;
            break;
          }
        }
      }

      if (!gameOver) {
        const bool ate = (newX == foodX && newY == foodY);
        const int newLength = ate ? length + 1 : length;
        if (newLength <= kMaxLen) {
          for (int i = newLength - 1; i > 0; i--) {
            snakeX[i] = snakeX[i - 1];
            snakeY[i] = snakeY[i - 1];
          }
          snakeX[0] = newX;
          snakeY[0] = newY;
          length = newLength;
          if (ate) {
            score++;
            placeFood();
          }
        } else {
          gameOver = true; // filled the entire board - effectively a win
        }
      }

      if (!gameOver) {
        display.fillScreen(GxEPD_BLACK);
        for (int i = 0; i < length; i++) {
          display.fillRect(snakeX[i] * kCell, snakeY[i] * kCell, kCell - 1, kCell - 1, GxEPD_WHITE);
        }
        display.fillRect(foodX * kCell + 2, foodY * kCell + 2, kCell - 5, kCell - 5, GxEPD_WHITE);
        display.display(true);
      }
    }
  }

  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 90);
  display.println(PW_GAME_OVER);
  display.setCursor(20, 115);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%d", PW_GAME_SCORE_LABEL, score);
  display.println(buf);
  display.display(false);
  delay(1500); // let the score register before returning to the menu

  showGamesMenu(gamesMenuIndex, false);
}

// No PicoRead reference for this one (unlike Snake/Flappy/Tetris) - built
// from scratch as solo "wall pong": a single right-side paddle the player
// moves with Up/Down (held, not a single toggle like Snake's turns - this
// wants continuous movement while held), ball bounces off the top/bottom/
// left walls, game over when it gets past the paddle on the right.
void PicoWatch::playPong() {
  guiState = APP_STATE;

  constexpr int kPaddleThickness = 8;
  constexpr int kPaddleHeight = 40;
  constexpr int kPaddleX = DISPLAY_WIDTH - kPaddleThickness;
  constexpr int kPaddleStep = 12;
  constexpr int kBallSize = 8;
  constexpr int kBallStep = 10;
  constexpr unsigned long kTickMs = 300;

  int paddleY = (DISPLAY_HEIGHT - kPaddleHeight) / 2;
  int ballX = DISPLAY_WIDTH / 3;
  int ballY = DISPLAY_HEIGHT / 2;
  int ballVX = -kBallStep;
  int ballVY = kBallStep;
  int score = 0;
  bool gameOver = false;

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  // Same button-bounce hazard as playSnake()/playFlappy() - Menu was just
  // used to select "Pong" from the games menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool paused = false;
  unsigned long lastTick = millis();
  while (!gameOver) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10);
      showGamesMenu(gamesMenuIndex, false);
      return;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      paused = !paused;
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    // Deliberately no wait-for-release here, unlike Snake's turn/Flappy's
    // flap - holding Up/Down should move the paddle continuously.
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      paddleY -= kPaddleStep;
      if (paddleY < 0) paddleY = 0;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      paddleY += kPaddleStep;
      if (paddleY > DISPLAY_HEIGHT - kPaddleHeight) paddleY = DISPLAY_HEIGHT - kPaddleHeight;
    }

    if (!paused && millis() - lastTick >= kTickMs) {
      lastTick = millis();

      ballX += ballVX;
      ballY += ballVY;

      if (ballY <= 0) {
        ballY = 0;
        ballVY = -ballVY;
      } else if (ballY + kBallSize >= DISPLAY_HEIGHT) {
        ballY = DISPLAY_HEIGHT - kBallSize;
        ballVY = -ballVY;
      }
      if (ballX <= 0) {
        ballX = 0;
        ballVX = -ballVX;
      }

      if (ballX + kBallSize >= kPaddleX) {
        const bool paddleHit = ballY + kBallSize > paddleY && ballY < paddleY + kPaddleHeight;
        if (paddleHit) {
          ballX = kPaddleX - kBallSize;
          ballVX = -ballVX;
          score++;
        } else if (ballX > DISPLAY_WIDTH) {
          gameOver = true;
        }
      }

      if (!gameOver) {
        display.fillScreen(GxEPD_BLACK);
        display.fillRect(kPaddleX, paddleY, kPaddleThickness, kPaddleHeight, GxEPD_WHITE);
        display.fillRect(ballX, ballY, kBallSize, kBallSize, GxEPD_WHITE);
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(2, 12);
        char scoreBuf[16];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);
        display.print(scoreBuf);
        display.display(true);
      }
    }
  }

  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 90);
  display.println(PW_GAME_OVER);
  display.setCursor(20, 115);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%d", PW_GAME_SCORE_LABEL, score);
  display.println(buf);
  display.display(false);
  delay(1500);

  showGamesMenu(gamesMenuIndex, false);
}

namespace {
// Piece tables ported verbatim from Jan's PicoRead project
// (~/Projekte/PicoRead/src/activities/home/TetrisGameActivity.cpp) - same
// plain in-place rotation with no SRS wall-kicks (a rotation that doesn't
// fit is just rejected), same board dimensions (standard 10x20).
struct TetrisCell {
  int8_t dx, dy;
};
using TetrisRotation = TetrisCell[4];
constexpr int kTetrisPieceCount = 7;
constexpr TetrisRotation kTetrisPieces[kTetrisPieceCount][4] = {
    // I
    {{{0, 1}, {1, 1}, {2, 1}, {3, 1}},
     {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
     {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
     {{2, 0}, {2, 1}, {2, 2}, {2, 3}}},
    // O
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
    // T
    {{{0, 1}, {1, 1}, {2, 1}, {1, 0}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 1}},
     {{0, 1}, {1, 1}, {2, 1}, {1, 2}},
     {{1, 0}, {1, 1}, {1, 2}, {0, 1}}},
    // S
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {2, 2}}},
    // Z
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}},
     {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
     {{2, 0}, {1, 1}, {2, 1}, {1, 2}}},
    // J
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 0}, {1, 1}, {1, 2}, {0, 2}}},
    // L
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {0, 2}},
     {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},
};
constexpr int kTetrisLineScore[5] = {0, 100, 300, 500, 800};
}  // namespace

// Controls (confirmed with Jan given no left/right buttons on this
// hardware): Up = shift left, Down = shift right, Menu = rotate, Back =
// exit. No manual soft/hard drop - the piece just falls at the normal
// tick rate, which is plenty since e-ink can't show a "fast drop" as
// anything meaningfully different anyway. Board is the standard 10x20;
// at 10px cells that's a 100x200 field, flush against this display's full
// 200px height with 100px left over on the right for the score.
void PicoWatch::playTetris() {
  guiState = APP_STATE;

  constexpr int kBoardCols = 10;
  constexpr int kBoardRows = 20;
  constexpr int kCellSize = 10;
  constexpr unsigned long kTickMs = 700;

  static uint8_t board[kBoardRows][kBoardCols];
  static uint8_t scratchBoard[kBoardRows][kBoardCols];
  memset(board, 0, sizeof(board));

  int pieceType = random(kTetrisPieceCount);
  int nextPieceType = random(kTetrisPieceCount);
  int rotation = 0;
  int pieceX = 0, pieceY = 0;
  int score = 0;
  bool gameOver = false;

  auto pieceFits = [&](int type, int rot, int x, int y) {
    for (const auto &cell : kTetrisPieces[type][rot]) {
      const int col = x + cell.dx;
      const int row = y + cell.dy;
      if (col < 0 || col >= kBoardCols || row >= kBoardRows) return false;
      if (row >= 0 && board[row][col]) return false;
    }
    return true;
  };
  auto spawnPiece = [&]() {
    pieceType = nextPieceType;
    nextPieceType = random(kTetrisPieceCount);
    rotation = 0;
    pieceX = kBoardCols / 2 - 2;
    pieceY = -1;
  };
  spawnPiece();

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  // Same button-bounce hazard as the other games - Menu was just used to
  // select "Tetris" from the games menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  unsigned long lastTick = millis();
  while (!gameOver) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10);
      showGamesMenu(gamesMenuIndex, false);
      return;
    }
    bool moved = false;
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      const int newRotation = (rotation + 1) % 4;
      if (pieceFits(pieceType, newRotation, pieceX, pieceY)) {
        rotation = newRotation;
        moved = true;
      }
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      if (pieceFits(pieceType, rotation, pieceX - 1, pieceY)) {
        pieceX--;
        moved = true;
      }
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      if (pieceFits(pieceType, rotation, pieceX + 1, pieceY)) {
        pieceX++;
        moved = true;
      }
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) delay(10);
    }

    const unsigned long now = millis();
    const bool tickDue = now - lastTick >= kTickMs;
    if (tickDue || moved) {
      if (tickDue) {
        lastTick = now;
        if (pieceFits(pieceType, rotation, pieceX, pieceY + 1)) {
          pieceY++;
        } else {
          for (const auto &cell : kTetrisPieces[pieceType][rotation]) {
            const int col = pieceX + cell.dx;
            const int row = pieceY + cell.dy;
            if (row >= 0 && row < kBoardRows && col >= 0 && col < kBoardCols) {
              board[row][col] = 1;
            }
          }
          memset(scratchBoard, 0, sizeof(scratchBoard));
          int writeRow = kBoardRows - 1;
          int cleared = 0;
          for (int row = kBoardRows - 1; row >= 0; row--) {
            bool full = true;
            for (int col = 0; col < kBoardCols; col++) {
              if (!board[row][col]) {
                full = false;
                break;
              }
            }
            if (full) {
              cleared++;
              continue;
            }
            for (int col = 0; col < kBoardCols; col++) scratchBoard[writeRow][col] = board[row][col];
            writeRow--;
          }
          memcpy(board, scratchBoard, sizeof(board));
          if (cleared > 0) {
            score += kTetrisLineScore[min(cleared, 4)];
          }
          spawnPiece();
          if (!pieceFits(pieceType, rotation, pieceX, pieceY)) {
            gameOver = true;
          }
        }
      }

      if (!gameOver) {
        display.fillScreen(GxEPD_BLACK);
        for (int row = 0; row < kBoardRows; row++) {
          for (int col = 0; col < kBoardCols; col++) {
            if (board[row][col]) {
              display.fillRect(col * kCellSize + 1, row * kCellSize + 1, kCellSize - 2, kCellSize - 2,
                                GxEPD_WHITE);
            }
          }
        }
        for (const auto &cell : kTetrisPieces[pieceType][rotation]) {
          const int col = pieceX + cell.dx;
          const int row = pieceY + cell.dy;
          if (row >= 0 && row < kBoardRows && col >= 0 && col < kBoardCols) {
            display.fillRect(col * kCellSize + 1, row * kCellSize + 1, kCellSize - 2, kCellSize - 2,
                              GxEPD_WHITE);
          }
        }
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(kBoardCols * kCellSize + 5, 20);
        char scoreBuf[16];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);
        display.print(scoreBuf);
        display.display(true);
      }
    }
  }

  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 90);
  display.println(PW_GAME_OVER);
  display.setCursor(20, 115);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%d", PW_GAME_SCORE_LABEL, score);
  display.println(buf);
  display.display(false);
  delay(1500);

  showGamesMenu(gamesMenuIndex, false);
}

// Core rules ported from Jan's PicoRead project
// (~/Projekte/PicoRead/src/activities/home/FlappyGameActivity.cpp) - same
// tick-and-redraw shape (that codebase hits the same "e-ink is too slow for
// a real animation loop" wall we do), scaled down from its 800x480 screen
// to this watch's 200x200 one. Single button (Up = flap) instead of
// PicoRead's dedicated Confirm button - this hardware doesn't have one to
// spare once Back/Menu are reserved for exit/pause.
void PicoWatch::playFlappy() {
  guiState = APP_STATE;

  constexpr int kFieldTop = 20;                        // leaves room for the score line above
  constexpr int kFieldHeight = DISPLAY_HEIGHT - kFieldTop;
  constexpr int kBirdSize = 12;
  constexpr int kBirdX = 30;
  constexpr int kPipeWidth = 20;
  constexpr int kPipeGapHeight = 70;
  constexpr int kFlapStep = 20;
  constexpr int kGravityStep = 8;
  constexpr int kPipeSpeedStep = 12;
  constexpr unsigned long kTickMs = 400;

  bool gameOver = false;
  bool started = false; // pipe stays put until the first flap, like the reference
  bool flapLatched = false;
  int score = 0;
  int birdY = kFieldHeight / 2;
  int pipeX = 0;
  int pipeGapY = 0;

  auto spawnPipe = [&]() {
    pipeX = DISPLAY_WIDTH;
    constexpr int margin = 15;
    constexpr int range = kFieldHeight - kPipeGapHeight - 2 * margin;
    pipeGapY = margin + (range > 0 ? random(range) : 0);
  };
  spawnPipe();

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  // Same button-bounce hazard as playSnake() - Menu was just used to select
  // "Flappy" from the games menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool paused = false;
  unsigned long lastTick = millis();
  while (!gameOver) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10);
      showGamesMenu(gamesMenuIndex, false);
      return;
    }
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      paused = !paused;
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      flapLatched = true;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
    }

    if (!paused && millis() - lastTick >= kTickMs) {
      lastTick = millis();

      if (flapLatched) {
        started = true;
        birdY -= kFlapStep;
        if (birdY < 0) birdY = 0;
        flapLatched = false;
      } else if (started) {
        birdY += kGravityStep;
      }

      if (birdY + kBirdSize >= kFieldHeight) {
        birdY = kFieldHeight - kBirdSize;
        if (started) gameOver = true;
      }

      if (started && !gameOver) {
        pipeX -= kPipeSpeedStep;
        if (pipeX + kPipeWidth < 0) {
          score++;
          spawnPipe();
        } else {
          const bool birdInPipeX = kBirdX + kBirdSize > pipeX && kBirdX < pipeX + kPipeWidth;
          if (birdInPipeX) {
            const bool inGap = birdY > pipeGapY && birdY + kBirdSize < pipeGapY + kPipeGapHeight;
            if (!inGap) gameOver = true;
          }
        }
      }

      if (!gameOver) {
        display.fillScreen(GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(2, 12);
        char scoreBuf[16];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", score);
        display.print(scoreBuf);

        if (pipeX + kPipeWidth > 0 && pipeX < DISPLAY_WIDTH) {
          display.fillRect(pipeX, kFieldTop, kPipeWidth, pipeGapY, GxEPD_WHITE);
          display.fillRect(pipeX, kFieldTop + pipeGapY + kPipeGapHeight, kPipeWidth,
                            kFieldHeight - pipeGapY - kPipeGapHeight, GxEPD_WHITE);
        }
        display.fillRect(kBirdX, kFieldTop + birdY, kBirdSize, kBirdSize, GxEPD_WHITE);

        display.display(true);
      }
    }
  }

  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 90);
  display.println(PW_GAME_OVER);
  display.setCursor(20, 115);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%d", PW_GAME_SCORE_LABEL, score);
  display.println(buf);
  display.display(false);
  delay(1500);

  showGamesMenu(gamesMenuIndex, false);
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
  history[0] = (int32_t)todaySteps();

  Preferences writePrefs;
  writePrefs.begin(kPrefsNamespace, false);  // read-write
  writePrefs.putBytes(kPrefsStepsHistKey, history, sizeof(history));
  writePrefs.putLong(kPrefsStepsDayKey, today);
  writePrefs.end();

  sensor.resetStepCounter();
  stepsBaseline = 0;
  lastSavedStepsTotal = 0;
  saveStepsBaseline(0);
}

uint32_t PicoWatch::todaySteps() { return stepsBaseline + sensor.getCounter(); }

void PicoWatch::_persistStepsProgress() {
  const uint32_t total = todaySteps();
  if (total == lastSavedStepsTotal) return; // nothing new since the last tick - skip the NVS write
  saveStepsBaseline(total);
  lastSavedStepsTotal = total;
}

namespace {
// Matches BleNotificationCallback's signature (see BLE.h) - invoked
// directly from the BLE write callback while _checkBleNotifications()'s
// window is open. Pushes onto the front of the ring buffer, oldest entry
// falls off once full.
void onBleNotificationReceived(const char *src, const char *title, const char *body) {
  const int last = min(notificationCount, NOTIFICATION_COUNT - 1);
  for (int i = last; i > 0; i--) notifications[i] = notifications[i - 1];
  // Free function (fixed C-function-pointer callback signature, see BLE.h's
  // BleNotificationCallback), not a PicoWatch member - can't read the
  // instance's `currentTime`, so read the RTC directly via the public
  // static PicoWatch::RTC instead (see project memory, 14.08.2026: Jan
  // wanted a way to tell same-looking notifications apart).
  tmElements_t now;
  PicoWatch::RTC.read(now);
  snprintf(notifications[0].time, sizeof(notifications[0].time), "%02d:%02d", now.Hour, now.Minute);
  strncpy(notifications[0].src, src, NOTIFICATION_SRC_LEN - 1);
  notifications[0].src[NOTIFICATION_SRC_LEN - 1] = '\0';
  strncpy(notifications[0].title, title, NOTIFICATION_TITLE_LEN - 1);
  notifications[0].title[NOTIFICATION_TITLE_LEN - 1] = '\0';
  strncpy(notifications[0].body, body, NOTIFICATION_BODY_LEN - 1);
  notifications[0].body[NOTIFICATION_BODY_LEN - 1] = '\0';
  if (notificationCount < NOTIFICATION_COUNT) notificationCount++;
  hasUnreadNotification = true;
  saveNotifications();
}
}  // namespace

void PicoWatch::_checkBleNotifications() {
  if (currentTime.Minute % bleNotifyIntervalMin != 0) return;

  BLE ble;
  ble.beginNotify(BLE_NOTIFY_DEVICE_NAME, onBleNotificationReceived);

  // Only the "nobody's connected yet" wait is capped at BLE_NOTIFY_WINDOW_MS
  // - once a phone actually connects, keep the radio up until it
  // disconnects on its own OR goes quiet for BLE_NOTIFY_CONNECTED_IDLE_MS
  // (up to BLE_NOTIFY_MAX_CONNECTED_MS as an absolute safety cap).
  // Gadgetbridge's connect -> subscribe -> MTU-negotiate -> "initialized"
  // handshake is several sequential round trips and can easily outlast a
  // short fixed window - tearing the BLE stack down via stopNotify()'s
  // NimBLEDevice::deinit() mid-handshake left the watch permanently stuck
  // "connecting but never initialized" on the phone's side (see project
  // memory, 14.08.2026). The idle-exit (added later the same day, after
  // Jan reported the watch draining its battery fast) matters because
  // without it, EVERY successful connection burned the full
  // BLE_NOTIFY_MAX_CONNECTED_MS (60s) of active-radio time regardless of
  // whether Gadgetbridge had already finished minutes earlier - ending as
  // soon as the link goes quiet cuts the typical connected-and-done case
  // from 60s down to ~BLE_NOTIFY_CONNECTED_IDLE_MS, without weakening the
  // original fix (a genuinely slow/stalled handshake still gets the full
  // MAX_CONNECTED_MS, since msSinceLastActivity() resets on the connect
  // event itself and every subsequent write).
  const unsigned long start = millis();
  while (true) {
    const unsigned long elapsed = millis() - start;
    if (ble.notifyClientConnected()) {
      if (elapsed >= BLE_NOTIFY_MAX_CONNECTED_MS) break;
      if (ble.msSinceLastActivity() >= BLE_NOTIFY_CONNECTED_IDLE_MS) break;
    } else if (elapsed >= BLE_NOTIFY_WINDOW_MS) {
      break;
    }
    delay(50);
  }

  ble.stopNotify();
}

// On-demand, open-ended version of _checkBleNotifications() for INITIAL
// pairing - the periodic check only advertises for BLE_NOTIFY_WINDOW_MS
// (12s) once every BLE_NOTIFY_CHECK_INTERVAL_MIN (5min), which is far too
// short/rare a window to reliably catch while manually finding+connecting
// in Gadgetbridge (or a generic tool like nRF Connect) - realistically
// takes well over a minute to scan, select, connect and subscribe as a
// first-time user. No timeout here at all; only Back closes it, so
// there's no race between the firmware tearing the radio down
// (stopNotify()'s NimBLEDevice::deinit()) and the phone still being
// mid-connection/mid-handshake.
void PicoWatch::_pairBluetooth() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(0, 30);
  display.println(PW_NOTIFICATIONS_PAIRING);
  display.println(" ");
  display.println(BLE_NOTIFY_DEVICE_NAME);
  display.println(" ");
  display.println(PW_NOTIFICATIONS_PAIRING_HINT);
  display.display(false);

  BLE ble;
  ble.beginNotify(BLE_NOTIFY_DEVICE_NAME, onBleNotificationReceived);

  pinMode(BACK_BTN_PIN, INPUT);
  while (true) {
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) break;
    delay(50);
  }

  ble.stopNotify();
}

bool PicoWatch::_httpViaBle(const String &url, String &body) {
  BLE ble;
  ble.beginNotify(BLE_NOTIFY_DEVICE_NAME, onBleNotificationReceived);

  const unsigned long connectStart = millis();
  while (!ble.notifyClientConnected()) {
    if (millis() - connectStart >= BLE_HTTP_CONNECT_TIMEOUT_MS) {
      ble.stopNotify();
      return false;
    }
    delay(50);
  }

  // The raw GATT connect fires before Gadgetbridge's own subscribe/MTU-
  // negotiate/"initialized" handshake completes (same multi-round-trip
  // process as _checkBleNotifications()'s comment describes) - sending
  // the request immediately would arrive before Gadgetbridge is ready to
  // route it.
  delay(BLE_HTTP_INIT_GRACE_MS);

  if (!ble.httpGet(url.c_str())) {
    ble.stopNotify();
    return false;
  }

  const unsigned long requestStart = millis();
  while (!ble.httpResponseReady()) {
    if (millis() - requestStart >= BLE_HTTP_RESPONSE_TIMEOUT_MS) {
      ble.stopNotify();
      return false;
    }
    delay(50);
  }

  const bool ok = ble.httpResponseSuccess();
  if (ok) body = ble.httpResponseBody();
  ble.stopNotify();
  return ok;
}

// Returns true if the user deleted this notification (Menu button) -
// callers that hold their own copy of the notification list/count (e.g.
// showNotifications()'s label array) must treat that as stale and rebuild
// rather than continuing to use it (see showNotifications() below).
bool PicoWatch::_showNotificationDetail(int index) {
  if (index < 0 || index >= notificationCount) return false;
  const PwNotification &n = notifications[index];

  display.setFullWindow();
  display.setTextColor(GxEPD_WHITE);
  // 8b, not the ASCII-only 7b: src/title/body come straight from BLE.cpp's
  // Gadgetbridge parser as raw Latin-1 bytes (real umlauts etc.) - 7b's
  // glyph table stops at 0x7E, so Adafruit_GFX::write() silently skips
  // any byte above that (no glyph AND no cursor advance) instead of
  // rendering it, e.g. "Testgerät" -> "Testgert" (14.08.2026).
  display.setFont(&FreeMonoBold9pt8b);
  // uiRenderList() (the shared list-menu renderer, used to get here via
  // showNotifications()) turns word-wrap OFF and never turns it back on -
  // it's a per-display-object flag that just stays however the last
  // screen left it, not something Adafruit_GFX resets per draw. Adafruit's
  // own autowrap is character-count-based though, splitting words in half
  // mid-word (Jan, 15.08.2026: "Back to disconnect" -> "Back to disconn"/
  // "ect") - uiWrapWords() below breaks only at spaces instead, so wrap
  // stays off here and the '\n's it inserts do the line-breaking.
  display.setTextWrap(false);

  // Flatten header/title/body into one scrollable line list - long bodies
  // used to just run off the bottom edge and overlap the delete hint (Jan's
  // screenshot, 15.08.2026: a 6-line status update overwrote the hint text
  // instead of scrolling). Same idea as uiRenderList()'s scrollOffset/
  // visibleRows windowing, but over free-flowing text rows instead of menu
  // items.
  String full = uiWrapWords((String(n.time) + " " + n.src).c_str(), DISPLAY_WIDTH - 10);
  full += "\n\n";
  full += uiWrapWords(n.title, DISPLAY_WIDTH - 10);
  full += "\n\n";
  full += uiWrapWords(n.body, DISPLAY_WIDTH - 10);

  const int kMaxLines = 48; // generous cap for one notification's flattened text
  String lines[kMaxLines];
  int lineCount = 0;
  int start = 0;
  while (lineCount < kMaxLines) {
    const int nl = full.indexOf('\n', start);
    if (nl < 0) {
      lines[lineCount++] = full.substring(start);
      break;
    }
    lines[lineCount++] = full.substring(start, nl);
    start = nl + 1;
  }

  // 18px = FreeMonoBold9pt8b's own yAdvance. Content viewport stops well
  // short of the 200px bottom edge to leave room for the fixed delete hint
  // below it, instead of the two overlapping.
  const int kLineHeight = 18;
  const int kContentTopY = 20;
  const int kContentBottomY = 150;
  const int visibleLines = max(1, (kContentBottomY - kContentTopY) / kLineHeight + 1);
  const int maxScroll = max(0, lineCount - visibleLines);
  int scrollOffset = 0;

  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);
  // Menu was just used to OPEN this screen from showNotifications()'s list
  // - without waiting for it to be released first, a still-held press
  // would immediately register as "delete" below the instant this screen
  // starts polling, deleting an entry the user never meant to touch (same
  // button-reuse hazard as setTimezone()/changeWatchface() etc. elsewhere
  // in this file).
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);

  bool deletePressed = false;
  bool redraw = true;
  while (true) {
    if (redraw) {
      display.fillScreen(GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
      int y = kContentTopY;
      for (int row = 0; row < visibleLines && scrollOffset + row < lineCount; row++) {
        display.setCursor(5, y);
        display.println(lines[scrollOffset + row]);
        y += kLineHeight;
      }
      // y=190 (too close to the 200px bottom edge) was cut off - this hint
      // text is wider than the display at 9pt, so word-wrap pushes it onto
      // a second line; that second line's baseline needs room too (Jan's
      // screenshot, 15.08.2026).
      display.setCursor(5, 165);
      display.println(uiWrapWords(PW_NOTIFICATION_DELETE_HINT, DISPLAY_WIDTH - 10));
      display.display(true); // partial refresh - this screen redraws on every scroll step
      redraw = false;
    }

    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      deletePressed = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      deletePressed = false;
      break;
    }
    if (maxScroll > 0 && digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) delay(10); // wait for release, same debounce idiom as elsewhere in this file
      if (scrollOffset < maxScroll) {
        scrollOffset++;
        redraw = true;
      }
    }
    if (maxScroll > 0 && digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
      if (scrollOffset > 0) {
        scrollOffset--;
        redraw = true;
      }
    }
    delay(10);
  }
  while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW || digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10); // wait for release

  if (!deletePressed) return false;
  for (int i = index; i < notificationCount - 1; i++) notifications[i] = notifications[i + 1];
  notificationCount--;
  saveNotifications();
  return true;
}

void PicoWatch::_showNotificationPopup() {
  if (notificationCount == 0) {
    hasUnreadNotification = false;
    return;
  }
  const PwNotification &n = notifications[0];

  if (notificationVibrateEnabled) {
    vibMotor(100, 6);
  }

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt8b); // see _showNotificationDetail() comment - same Latin-1-vs-ASCII-glyph-table reason
  display.setTextWrap(false); // see _showNotificationDetail()'s comment - uiWrapWords() does the line-breaking instead
  display.setCursor(5, 25);
  display.print(n.time);
  display.print(" ");
  display.println(uiWrapWords(n.src, DISPLAY_WIDTH - 10));
  display.println();
  display.println(uiWrapWords(n.title, DISPLAY_WIDTH - 10));
  // Hint stays at a fixed Y (unlike the detail screen's flowing layout) -
  // src/title together only run to a handful of lines even fully wrapped.
  // y=180, not lower: this text is wider than the display at 9pt, wraps
  // to a second line, and that second line's baseline needs room before
  // the 200px bottom edge too (see _showNotificationDetail()'s identical
  // fix, Jan's screenshot 15.08.2026).
  display.setCursor(5, 160);
  display.println(uiWrapWords(PW_NOTIFICATION_HINT, DISPLAY_WIDTH - 10));
  display.display(true);

  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(DOWN_BTN_PIN, INPUT);

  bool openDetail = false;
  const unsigned long start = millis();
  // Was a hardcoded 15000 - now Settings -> "Notification Settings" ->
  // popup duration (5-30s, see config.h's NOTIFICATION_POPUP_DURATION_*).
  while (millis() - start < (unsigned long)notificationPopupDurationS * 1000UL) { // auto-dismiss if ignored, same as any other unattended screen
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      openDetail = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW || digitalRead(UP_BTN_PIN) == ACTIVE_LOW ||
        digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      break;
    }
    delay(20);
  }

  // Only clear on an actual read (Menu -> detail view), NOT just because
  // the popup timed out/got dismissed - this used to run unconditionally,
  // so hasUnreadNotification (and therefore _drawNotificationIndicator()'s
  // watchface icon) got cleared the instant the popup closed either way,
  // meaning the icon could never actually show once the popup was done -
  // exactly backwards from the point of having a persistent icon at all
  // (Jan, 15.08.2026: "hab das Icon noch nie gesehen").
  if (openDetail) {
    hasUnreadNotification = false;
    _showNotificationDetail(0);
  }

  RTC.read(currentTime);
  showWatchFace(false);
}

void PicoWatch::showNotifications() {
  guiState = APP_STATE;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  // Reaching the list at all - whether via the popup or straight from the
  // main menu - counts as "checked"; clears the top-center watchface
  // indicator (_drawNotificationIndicator()) same as the popup path
  // already did. Without this, browsing here directly (never triggering
  // the popup) left the flag set, so the icon never went away and the
  // next minute tick would still pop up the same already-read notification.
  hasUnreadNotification = false;

  if (notificationCount == 0) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 90);
    display.println(PW_NOTIFICATIONS_EMPTY);
    display.setCursor(10, 180);
    display.println(PW_NOTIFICATIONS_PAIR_HINT);
    display.display(false);
    while (1) {
      if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
        while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
        _pairBluetooth();
        break;
      }
      if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
        while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10);
        break;
      }
      delay(20);
    }
    showMenu(menuIndex, false);
    return;
  }

  // "HH:MM src" one-liner per stored notification - built fresh into local
  // Strings since (unlike the other list menus) these labels are runtime
  // data, not compile-time PW_XXX macros; uiRenderList() only needs the
  // char* to stay valid for the duration of each call, which these are.
  // Time-first (not "src: title" as before) so repeated same-src
  // notifications (e.g. several WhatsApp messages, or Gadgetbridge's own
  // test button) are still distinguishable at a glance in the list.
  String labels[NOTIFICATION_COUNT];
  const char *items[NOTIFICATION_COUNT];
  for (int i = 0; i < notificationCount; i++) {
    labels[i] = String(notifications[i].time) + " " + String(notifications[i].src);
    items[i] = labels[i].c_str();
  }

  int pick = 0;
  bool needsRedraw = true;
  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      // A deletion invalidates `items`/`labels`/notificationCount (built
      // once, above, before this loop started) - rather than patching
      // them in place, just re-enter fresh (also correctly falls back to
      // the empty-state screen if that was the last notification).
      if (_showNotificationDetail(pick)) {
        showNotifications();
        return;
      }
      needsRedraw = true;
      while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      break;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      pick = (pick + 1) % notificationCount;
      needsRedraw = true;
      while (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) delay(10);
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      pick = (pick - 1 + notificationCount) % notificationCount;
      needsRedraw = true;
      while (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) delay(10);
    }

    if (!needsRedraw) continue;
    needsRedraw = false;
    uiRenderList(items, notificationCount, pick, true);
  }

  showMenu(menuIndex, false);
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
    uiDrawToggle(150, 128, enabled, setIndex == SET_ALARM_ENABLED, blink, GxEPD_WHITE, GxEPD_BLACK);

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

    display.fillScreen(uiBgColor());
    display.setTextColor(uiFgColor());
    display.setFont(uiMenuFont());
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
      display.setTextColor(uiFgColor());
      display.setCursor(0, yPos);
      display.println(labels[i]);
      display.setCursor(10, yPos + 14);
      if (i == setIndex) {
        display.setTextColor(blink ? uiFgColor() : uiBgColor());
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

    display.setFont(size == UI_FONT_SIZE_SMALL     ? &FreeMonoBold9pt8b
                     : size == UI_FONT_SIZE_BIG    ? &FreeMonoBold15pt8b
                                                    : &FreeMonoBold12pt8b);
    display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    int16_t x1, y1;
    uint16_t w, h;
    const char *name = fontSizeName(size);
    // X must match the setCursor() below (10, not 0) - getTextBounds()
    // measures the string as if printed starting at the given X, and a
    // custom font's per-glyph left bearing means the returned box isn't a
    // simple x-independent width; measuring at the wrong X left the
    // highlight box 10px offset from the actually-drawn text, clipping
    // the last character's right edge (Jan's screenshot, 15.08.2026 -
    // "the h in Bluetooth is only half marked", same bug in every
    // highlight-box picker screen, not just Internet Access).
    display.getTextBounds(name, 10, 90, &x1, &y1, &w, &h);
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

void PicoWatch::showInvertMenuSettings() {
  guiState = APP_STATE;

  bool inverted = menuInverted;
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
      inverted = !inverted;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      inverted = !inverted;
    }

    // Preview the actual invert effect this choice will apply to every
    // list menu, not just its name - same "preview the real thing"
    // reasoning as showFontSizeSettings() above.
    const uint16_t bg = inverted ? GxEPD_WHITE : GxEPD_BLACK;
    const uint16_t fg = inverted ? GxEPD_BLACK : GxEPD_WHITE;
    display.fillScreen(bg);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(fg);
    display.setCursor(10, 20);
    display.println(PW_SETTINGS_INVERT_MENU);

    display.setTextColor(blink ? fg : bg);
    int16_t x1, y1;
    uint16_t w, h;
    const char *name = inverted ? PW_YES : PW_NO;
    display.getTextBounds(name, 10, 90, &x1, &y1, &w, &h); // X must match setCursor() below - see showFontSizeSettings()'s comment
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, fg);
    display.setCursor(10, 90);
    display.println(name);

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    menuInverted = inverted;
    saveMenuInverted();
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void PicoWatch::showNotifyIntervalSettings() {
  guiState = APP_STATE;

  uint8_t minutes = bleNotifyIntervalMin;
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

    // Clamped, not wrapped (unlike showFontSizeSettings()'s modulo cycle) -
    // 1..10 min is a real battery/latency tradeoff range, wrapping from 10
    // back to 1 (or 1 down to 10) would silently jump past the middle of
    // the range a user is trying to fine-tune.
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      if (minutes > 1) minutes--;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      if (minutes < 10) minutes++;
    }

    display.fillScreen(uiBgColor());
    display.setFont(uiMenuFont());
    display.setTextColor(uiFgColor());
    display.setCursor(10, 20);
    display.println(PW_TIME_NOTIFY_INTERVAL);
    display.setCursor(10, 40);
    display.println(PW_NOTIFY_INTERVAL_SUBTITLE);

    display.setTextColor(blink ? uiFgColor() : uiBgColor());
    char buf[16];
    snprintf(buf, sizeof(buf), "%d min", minutes);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(buf, 10, 90, &x1, &y1, &w, &h); // X must match setCursor() below - see showFontSizeSettings()'s comment
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, uiFgColor());
    display.setCursor(10, 90);
    display.println(buf);

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    bleNotifyIntervalMin = minutes;
    saveNotifyInterval();
  }

  showTimeMenu(timeMenuIndex, false);
}

void PicoWatch::showInternetAccessSettings() {
  guiState = APP_STATE;

  uint8_t mode = internetAccessMode;
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

    // Only two choices - Up/Down both just toggle, matching
    // showInvertMenuSettings()'s boolean-choice pattern rather than
    // showNotifyIntervalSettings()'s clamped range.
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW || digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      blink = 1;
      mode = (mode == INTERNET_ACCESS_WIFI) ? INTERNET_ACCESS_BLE : INTERNET_ACCESS_WIFI;
    }

    display.fillScreen(uiBgColor());
    display.setFont(uiMenuFont());
    // Wrap OFF for the title - it's a one-line heading, not paragraph
    // text; at "Big" font size PW_INTERNET_ACCESS_TITLE is wide enough to
    // wrap onto a second line, which then landed under the fixed-position
    // "Bluetooth"/"WiFi" highlight box below and got overwritten by it
    // (Jan's screenshot, 15.08.2026) - clipping at the edge is the correct
    // failure mode for a heading, not wrapping into content underneath.
    display.setTextWrap(false);
    display.setTextColor(uiFgColor());
    display.setCursor(10, 20);
    display.println(PW_INTERNET_ACCESS_TITLE);

    display.setTextColor(blink ? uiFgColor() : uiBgColor());
    const char *name = mode == INTERNET_ACCESS_BLE ? PW_INTERNET_ACCESS_BLE : PW_INTERNET_ACCESS_WIFI;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(name, 10, 55, &x1, &y1, &w, &h); // X must match setCursor() below - see showFontSizeSettings()'s comment
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, uiFgColor());
    display.setCursor(10, 55);
    display.println(name);

    // Jan explicitly wanted this called out - BLE mode only works via
    // Gadgetbridge's phone-proxied HTTP, same connection weather/
    // notifications already use, not a second independent link. It's
    // genuinely meant to flow onto multiple lines, but via uiWrapWords()
    // (word boundaries, not Adafruit's own character-count autowrap) - see
    // _showNotificationDetail()'s comment on why wrap stays off entirely.
    display.setTextColor(uiFgColor());
    display.setCursor(10, 100);
    display.println(uiWrapWords(PW_INTERNET_ACCESS_HINT, DISPLAY_WIDTH - 15));

    display.display(true); // partial refresh
  }

  if (confirmed && !cancelled) {
    internetAccessMode = mode;
    saveInternetAccessMode();
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

    display.fillScreen(uiBgColor());
    display.setFont(uiMenuFont());
    display.setTextColor(uiFgColor());
    display.setCursor(10, 20);
    display.println(PW_SETTINGS_LANGUAGE);

    // Language names are deliberately never translated (see
    // pwLanguageName()) - a German speaker still needs to recognize
    // "Deutsch" while the UI is currently showing English strings.
    display.setTextColor(blink ? uiFgColor() : uiBgColor());
    int16_t x1, y1;
    uint16_t w, h;
    const char *name = pwLanguageName(language);
    display.getTextBounds(name, 10, 60, &x1, &y1, &w, &h); // X must match setCursor() below - see showFontSizeSettings()'s comment
    display.fillRect(x1 - 4, y1 - 6, w + 8, h + 12, uiFgColor());
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
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
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
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
  // uiWrapWords() does the line-breaking instead of Adafruit_GFX's own
  // character-count autowrap (which splits mid-word) - see
  // _showNotificationDetail()'s comment for why. This screen uses
  // uiMenuFont(), which scales with the user's Font Size setting
  // (Settings -> Font Size), so a short-looking string like "Bad release
  // data." can still overflow DISPLAY_WIDTH at the "Big" size - it did,
  // clipped clean off the edge with no wrap at all since textWrap was
  // left at whatever the previous screen last set it to (Jan's
  // screenshot, 16.08.2026). Explicit setTextWrap(false) here removes
  // that dependency on ambient state entirely.
  display.setTextWrap(false);
  display.setCursor(0, 30);
  display.println(uiWrapWords(PW_GITHUB_CHECKING, DISPLAY_WIDTH - 5));
  display.display(false);

  auto showResultAndReturn = [&](const char *line1, const char *line2 = nullptr) {
    display.fillScreen(uiBgColor());
    display.setCursor(0, 30);
    display.println(uiWrapWords(line1, DISPLAY_WIDTH - 5));
    if (line2) display.println(uiWrapWords(line2, DISPLAY_WIDTH - 5));
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
    display.fillScreen(uiBgColor());
    display.setCursor(0, 30);
    display.println(uiWrapWords(PW_GITHUB_ALREADY_LATEST_1, DISPLAY_WIDTH - 5));
    display.println(uiWrapWords(PW_GITHUB_ALREADY_LATEST_2, DISPLAY_WIDTH - 5));
    display.println(tag); // a git tag ("vX.Y.Z") is always short, no wrap needed
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

  display.fillScreen(uiBgColor());
  display.setCursor(0, 30);
  display.println(uiWrapWords(PW_GITHUB_DOWNLOADING, DISPLAY_WIDTH - 5));
  display.println(tag); // a git tag ("vX.Y.Z") is always short, no wrap needed
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
          display.fillRect(0, 60, 200, 20, uiBgColor());
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

  display.fillScreen(uiBgColor());
  display.setCursor(0, 30);
  display.println(uiWrapWords(PW_GITHUB_VERIFIED, DISPLAY_WIDTH - 5));
  display.println(uiWrapWords(PW_GITHUB_REBOOTING, DISPLAY_WIDTH - 5));
  display.display(false);
  delay(1000);
  ESP.restart();
}

void PicoWatch::showBuzz() {
  display.setFullWindow();
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
  display.setCursor(70, 80);
  display.println(PW_BUZZ);
  display.display(false); // full refresh
  vibMotor();
  showDebugMenu(debugMenuIndex, false);
}

// Switched from a plain digitalWrite() on/off pulse to LEDC PWM (16.08.2026)
// so vibrationStrength (see config.h's VIBRATION_STRENGTH_* and
// PicoWatch::showVibrationSettings()) can actually control how hard the
// motor buzzes, not just whether it does - a vibration motor's felt
// intensity scales with drive voltage/duty, which digitalWrite() can't
// vary. ledcSetup()/ledcAttachPin() (arduino-esp32 2.0.17's channel-based
// LEDC API - NOT the pin-based ledcAttach() from core 3.x, see project
// memory on why this repo is pinned to 2.0.17) are cheap enough to redo on
// every call rather than needing an "already attached" flag, given
// vibrations only fire a handful of times per minute at most.
void PicoWatch::vibMotor(uint8_t intervalMs, uint8_t length) {
  ledcSetup(VIB_MOTOR_PWM_CHANNEL, VIB_MOTOR_PWM_FREQ, VIB_MOTOR_PWM_RESOLUTION_BITS);
  ledcAttachPin(VIB_MOTOR_PIN, VIB_MOTOR_PWM_CHANNEL);

  uint8_t duty;
  switch (vibrationStrength) {
  case VIBRATION_STRENGTH_LOW:    duty = VIB_MOTOR_DUTY_LOW;    break;
  case VIBRATION_STRENGTH_MEDIUM: duty = VIB_MOTOR_DUTY_MEDIUM; break;
  default:                        duty = VIB_MOTOR_DUTY_HIGH;   break; // VIBRATION_STRENGTH_HIGH
  }

  bool motorOn = false;
  for (int i = 0; i < length; i++) {
    motorOn = !motorOn;
    ledcWrite(VIB_MOTOR_PWM_CHANNEL, motorOn ? duty : 0);
    delay(intervalMs);
  }
  ledcWrite(VIB_MOTOR_PWM_CHANNEL, 0); // always end off, even if a future caller passes an odd length
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

    display.fillScreen(uiBgColor());
    display.setTextColor(uiFgColor());
    // DSEG7 (the big 7-segment clock font) stays fixed here regardless of
    // Settings -> Font Size - it's the same decorative digit style the
    // main watchface itself uses, not menu text, so it's not what "match
    // the Settings font size" means (unlike the FreeMonoBold date row
    // below, which is exactly that).
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if (setIndex == SET_HOUR) { // blink hour digits
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    if (hour < 10) {
      display.print("0");
    }
    display.print(hour);

    display.setTextColor(uiFgColor());
    display.print(":");

    display.setCursor(108, 80);
    if (setIndex == SET_MINUTE) { // blink minute digits
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    if (minute < 10) {
      display.print("0");
    }
    display.print(minute);

    display.setTextColor(uiFgColor());

    display.setFont(uiMenuFont());
    display.setCursor(45, 150);
    if (setIndex == SET_YEAR) { // blink minute digits
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    display.print(2000 + year);

    display.setTextColor(uiFgColor());
    display.print("/");

    if (setIndex == SET_MONTH) { // blink minute digits
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
    }
    if (month < 10) {
      display.print("0");
    }
    display.print(month);

    display.setTextColor(uiFgColor());
    display.print("/");

    if (setIndex == SET_DAY) { // blink minute digits
      display.setTextColor(blink ? uiFgColor() : uiBgColor());
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

  showTimeMenu(timeMenuIndex, false);
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

    display.fillScreen(uiBgColor());
    display.setTextColor(blink ? uiFgColor() : uiBgColor());
    display.setFont(uiMenuFont());
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

  showTimeMenu(timeMenuIndex, false);
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

    display.fillScreen(uiBgColor());
    display.setTextColor(uiFgColor());
    display.setFont(uiMenuFont());
    display.setCursor(10, 25);
    display.println(PW_SET_CITY_TITLE);

    for (int i = 0; i < kDigitCount; i++) {
      display.setCursor(15 + i * 24, 90);
      if (i == setIndex) {
        display.setTextColor(blink ? uiFgColor() : uiBgColor());
      } else {
        display.setTextColor(uiFgColor());
      }
      display.print(digits[i]);
    }

    // Shifted up 15px from the original 150/170/190 - the last line was
    // sitting right at the 200px display edge and getting clipped/hard to
    // read.
    display.setTextColor(uiFgColor());
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
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());

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
      display.fillScreen(uiBgColor());
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

  showDebugMenu(debugMenuIndex, false);
}

void PicoWatch::showWatchFace(bool partialRefresh) {
  display.setFullWindow();
  // At this point it is sure we are going to update
  display.epd2.asyncPowerOn();
  drawWatchFace();
  _drawNotificationIndicator(); // over the watchface, not per-face - see .h
  display.display(partialRefresh); // partial refresh
  guiState = WATCHFACE_STATE;
}

// A drawn shape (rect + two lines forming an envelope flap), not a
// PROGMEM bitmap - trivial to size/reposition, and unlike a bitmap has no
// fixed foreground color baked in, so honoring notificationIconLight is
// just picking which color to pass below instead of needing a second
// bitmap. Settings -> "Notification Settings" (showNotificationSettings())
// controls both whether this draws at all and which color - a fixed
// white icon was invisible on light-background watchfaces (Jan,
// 15.08.2026).
void PicoWatch::_drawNotificationIndicator() {
  if (!notificationIconEnabled || !hasUnreadNotification) return;
  const uint16_t color = notificationIconLight ? GxEPD_WHITE : GxEPD_BLACK;
  const int w = 18, h = 13;
  const int x = (DISPLAY_WIDTH - w) / 2;
  const int y = 3;
  display.drawRect(x, y, w, h, color);
  display.drawLine(x, y, x + w / 2, y + h / 2, color);
  display.drawLine(x + w, y, x + w / 2, y + h / 2, color);
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

    // Settings -> "Internet Access": same request either way, only the
    // transport (and therefore who actually performs it) differs - see
    // config.h's INTERNET_ACCESS_*/BLE_HTTP_* comment and _httpViaBle().
    bool gotPayload = false;
    String payload;
    if (internetAccessMode == INTERNET_ACCESS_BLE) {
      gotPayload = _httpViaBle(weatherQueryURL, payload);
    } else if (connectWiFi()) {
      HTTPClient http; // Use Weather API for live data if WiFi is connected
      http.setConnectTimeout(3000); // 3 second max timeout
      http.begin(weatherQueryURL.c_str());
      const int httpResponseCode = http.GET();
      if (httpResponseCode == 200) {
        payload = http.getString();
        gotPayload = true;
      }
      http.end();
      // turn off radios
      WiFi.mode(WIFI_OFF);
      btStop();
    }

    if (gotPayload) {
      JSONVar responseObject     = JSON.parse(payload);
      currentWeather.temperature = int(responseObject["main"]["temp"]);
      currentWeather.weatherConditionCode =
          int(responseObject["weather"][0]["id"]);
      currentWeather.weatherDescription =
          JSONVar::stringify(responseObject["weather"][0]["main"]);
      currentWeather.external = true;
      breakTime((time_t)(int)responseObject["sys"]["sunrise"], currentWeather.sunrise);
      breakTime((time_t)(int)responseObject["sys"]["sunset"], currentWeather.sunset);
      // sync time during weather fetch and use timezone of lat & lon,
      // unless the user has set a timezone manually (see setTimezone()) -
      // otherwise this would silently overwrite their choice on every sync.
      {
        long manualOffset;
        if (!loadManualGmtOffset(manualOffset)) {
          gmtOffset = int(responseObject["timezone"]);
        }
      }
      // True NTP needs a UDP socket, which Gadgetbridge's HTTP-only proxy
      // can't carry - syncNTP() stays WiFi-only, _syncTimeViaBle() is the
      // BLE-mode equivalent (see config.h's BLE_TIME_API_URL comment).
      if (internetAccessMode == INTERNET_ACCESS_BLE) {
        _syncTimeViaBle(gmtOffset);
      } else {
        syncNTP(gmtOffset);
      }
    } else { // No connection (either transport), use internal temperature sensor
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
    "*{box-sizing:border-box}"
    "body{background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;"
    "color:#e7f3fb;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif}"
    "h1,h2,h3{color:#e7f3fb}"
    "a{color:#4fc3f7}a:hover{color:#7dd8fb}"
    // width:100%/line-height/font-size/border/cursor here match WiFiManager's
    // own stock button styling (wm_strings_en.h's HTTP_STYLE) - the pages
    // rendered by themedPage() below are standalone documents that never
    // load that stock stylesheet, so without repeating these rules here
    // their buttons/inputs render at shrink-to-fit width instead of the
    // full-width stacked look every WiFiManager-native page already has.
    "input,select{background:#101823;border:1px solid #22303f;color:#e7f3fb;border-radius:10px;"
    "margin:8px 0;width:100%;padding:8px;font-size:1rem}"
    "button,input[type='button'],input[type='submit'],a.btnlink{background:linear-gradient(135deg,#4fc3f7,#0f6fa8);"
    "color:#0d1015;font-weight:600;border-radius:999px;margin:6px 0;border:0;cursor:pointer;"
    "width:100%;line-height:2.4rem;font-size:1.1rem}"
    "button.D{background:#dc3630;color:#fff}"
    "a.btnlink{display:block;text-align:center;text-decoration:none}"
    ".msg{background:#101823;border:1px solid #22303f;border-left-width:5px;border-radius:10px;"
    "color:#a9bcca;padding:10px}"
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

// The "<div class='msg S'>...</div><hr><a class='btnlink' href=...>Back</a>"
// success-response body was repeated verbatim (only the message text/back
// link changed) after nearly every settings POST handler below - factored
// out once savings were requested (see project memory, 14.08.2026).
String savedPage(const String &title, const String &backHref, const String &msg = "Saved.") {
  return themedPage(title, "<div class='msg S'>" + msg + "</div>"
                            "<hr><a class='btnlink' href='" + backHref + "'>Back</a>");
}

// WiFiManager stores pointers, not copies (setCustomMenuHTML() just does
// `_customMenuHTML = html;`) - matches pfsense-status-esp32's own
// config_portal.cpp comment on gCustomMenuHtml verbatim: "WiFiManager
// stores pointers, so keep backing strings in module-level storage." A
// local String's backing buffer isn't guaranteed to outlive the whole
// WIFI_STAY_CONNECTED_TIMEOUT window the portal stays up for; this does.
String gCustomMenuHtml;

// handleRequest() is `protected` in the stock WiFiManager (pfsense-status-
// esp32 gets around this with a source-patched fork, third_party/
// wifimanager-patched/ here, but that's only ever applied to the LOCAL
// dev install - the release GitHub Actions workflows install a fresh,
// unpatched WiFiManager from the registry every run and never copy the
// patch over it). A protected member is reachable from a subclass though,
// so re-exposing it here works against the plain stock library everywhere
// - no workflow/CI changes, no "did the patch actually get applied"
// class of bug. (Found this the hard way: the very first tagged release
// this repo ever built - v1.1.0, 15.08.2026 - failed in CI with exactly
// this "handleRequest is protected" compile error.)
class PicoWatchWiFiManager : public WiFiManager {
 public:
  using WiFiManager::handleRequest;
};
}  // namespace

void PicoWatch::setupWifi() {
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy
  PicoWatchWiFiManager wifiManager;
  wifiManager.setAPCallback(_configModeCallback);
  wifiManager.setCustomHeadElement(kWifiPortalTheme);

  display.setFullWindow();
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
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
  // Must come after mode(WIFI_STA) but before begin() - the hostname is
  // sent as part of the DHCP request, too late to change once connected.
  // Empty means "never configured yet" - falls back to the ESP32 core's
  // own default ("esp32-<chipid>"), which is genuinely how Jan noticed
  // the watch in his router's client list and asked for this setting
  // (15.08.2026, web menu only - see the Internet Access page comment).
  if (picoWatchHostname[0] != '\0') WiFi.setHostname(picoWatchHostname);
  WiFi.begin();
  unsigned long waitStart = millis();
  // Was 8000ms - too short in practice (Jan, 15.08.2026): a real WPA2
  // handshake + DHCP lease can take longer than that on some routers/
  // distances, and hitting this timeout falls all the way through to the
  // AP config portal below, forcing a reconfigure for what's usually just
  // a slow reconnect, not an actual credentials problem.
  while (WiFi.status() != WL_CONNECTED && millis() - waitStart < WIFI_CONNECT_TIMEOUT_MS) {
    delay(50);
  }

  bool connected = (WiFi.status() == WL_CONNECTED);
  if (!connected) {
    display.fillScreen(uiBgColor());
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
    //
    // setConfigPortalTimeout() matters more than it looks: WIFI_AP_TIMEOUT
    // was defined in config.h but never actually wired to anything (dead
    // code) - startConfigPortal() therefore had NO timeout at all and
    // blocked forever until someone actually completed the config form.
    // Jan hit this directly (15.08.2026): once it fell into AP mode there
    // was no way back to the watchface short of configuring WiFi. This
    // firmware deliberately does NOT poll the Back button inside this
    // call - that would mean patching into WiFiManager's own blocking
    // portal loop, which caused a real "always lands in AP mode"
    // regression the last time this area was touched (see project memory,
    // 04.08.2026) - so a bounded auto-timeout via WiFiManager's own public
    // API is the safe fix here, not a button-interrupt.
    // setAPClientCheck(true) makes the timeout above only count down while
    // NOBODY is connected to the AP (matches WIFI_AP_TIMEOUT's original
    // "seconds with zero AP clients" comment) - without it, a flat
    // unconditional 60s would risk cutting Jan off mid-setup if it takes
    // him a bit to actually fill in the form after connecting his phone.
    wifiManager.setAPClientCheck(true);
    wifiManager.setConfigPortalTimeout(WIFI_AP_TIMEOUT);
    connected = wifiManager.startConfigPortal(WIFI_AP_SSID, wifiApPassword);
  }

  display.fillScreen(uiBgColor());
  display.setCursor(0, 30);
  // uiWrapWords(), not raw println() - these are some of the longer
  // static strings in the whole UI (translations can run longer still),
  // and at "Big" font size easily overflow one line; GxEPD2's own
  // autowrap breaks wherever the character count runs out (mid-word),
  // uiWrapWords() only breaks at spaces (Jan, 15.08.2026 - "Back to
  // disconnect" split as "Back to disconn"/"ect").
  if (!connected) { // WiFi setup failed
    display.println(uiWrapWords(PW_WIFI_SETUP_FAILED, DISPLAY_WIDTH - 5));
    display.println(uiWrapWords(PW_WIFI_TIMED_OUT, DISPLAY_WIDTH - 5));
  } else {
    display.println(uiWrapWords(PW_WIFI_CONNECTED_TO, DISPLAY_WIDTH - 5));
    display.println(WiFi.SSID());
    display.println(uiWrapWords(PW_WIFI_OPEN_IN_BROWSER, DISPLAY_WIDTH - 5));
    display.println(WiFi.localIP());
    display.println(" ");
    display.println(uiWrapWords(PW_WIFI_BACK_TO_DISCONNECT, DISPLAY_WIDTH - 5));
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

    String webMenuPassword = loadWebMenuPassword();
    // Real WiFiManager-native login gate, ported 1:1 from pfsense-status-
    // esp32 (found 06.08.2026: that project doesn't use stock WiFiManager
    // at all - it vendors a patched fork, see third_party/
    // wifimanager-patched/, which adds setHttpAuth()/setCustomStatusHTML()/
    // a public handleRequest() and a real /wm-login page baked directly
    // into WiFiManager.cpp's own request handling, gating even its own
    // built-in pages - not something achievable with the stock library's
    // public API). "admin" is passed as the username the same way
    // pfsense's config_portal.cpp:890 does, even though the patched
    // setHttpAuth() ignores it entirely (password-only login) - kept for
    // literal parity with the reference. Same >=8 char minimum as our own
    // saveWebMenuPassword() flow already enforces.
    wifiManager.setHttpAuth("admin", webMenuPassword.c_str());

    if (wifiManager.server) {
      WebServer &server = *wifiManager.server;
      server.on("/change-password", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        server.send(200, "text/html",
                     themedPage("Change Password",
                                "<form method='POST' action='/change-password'>"
                                "<input type='password' name='p' placeholder='New password (min 8 chars)'>"
                                "<button type='submit'>Save</button></form>"
                                "<hr><a class='btnlink' href='/'>Back</a>"));
      });
      server.on("/change-password", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        const String newPass = server.arg("p");
        if (newPass.length() < 8) {
          server.send(200, "text/html",
                       themedPage("Change Password",
                                  "<div class='msg D'>Password must be at least 8 characters.</div>"
                                  "<form method='POST' action='/change-password'>"
                                  "<input type='password' name='p' placeholder='New password (min 8 chars)'>"
                                  "<button type='submit'>Save</button></form>"
                                  "<hr><a class='btnlink' href='/'>Back</a>"));
          return;
        }
        webMenuPassword = newPass;
        saveWebMenuPassword(newPass);
        wifiManager.setHttpAuth("admin", webMenuPassword.c_str()); // re-arm the gate with the new password immediately
        server.send(200, "text/html",
                     themedPage("Change Password",
                                "<div class='msg S'>Password changed.</div><hr><a class='btnlink' href='/'>Back</a>"));
      });
      // Matches pfsense-status-esp32's eraseAllAndReboot() (config_portal.cpp:651)
      // - wipes everything (our own NVS settings namespace + WiFiManager's
      // saved STA credentials) and reboots. PicoWatch has no Telegram
      // config to preserve like pfsense's version does, so there's nothing
      // to exclude - genuinely erases all of it.
      server.on("/config-erase", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        server.send(200, "text/html",
                     themedPage("Config Erase", "<div class='msg S'><strong>Config erased.</strong><br/>Rebooting&hellip;</div>"));
        server.client().stop();
        delay(500);
        Preferences prefs;
        prefs.begin(kPrefsNamespace, false);
        prefs.clear();
        prefs.end();
        wifiManager.resetSettings();
        delay(500);
        ESP.restart();
      });
      server.on("/github-update", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        server.send(200, "text/html",
                     themedPage("Online Update",
                                "<div class='msg P'>Starting GitHub update check&hellip;<br/>"
                                "Watch the device screen for progress.</div>"));
        updateFromGithub(); // blocking; reboots on success, falls through here on failure
      });
      // Dedicated page for the file-upload form, matching pfsense-status-
      // esp32's pattern of one button per menu item, each navigating to
      // its own page - not everything crammed into the menu block itself.
      server.on("/file-update-page", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        server.send(200, "text/html",
                     themedPage("File Update",
                                "<form method='POST' action='/file-update' enctype='multipart/form-data'>"
                                "<input type='file' name='update' accept='.bin'>"
                                "<button type='submit'>Upload &amp; Flash</button></form>"
                                "<hr><a class='btnlink' href='/'>Back</a>"));
      });
      // Shared between the two /file-update lambdas below (the upload
      // callback always runs first for a multipart POST, before the main
      // handler) - set once per request so the main handler doesn't need
      // to call handleRequest() a second time (it already sent the
      // redirect response itself if unauthenticated; a second call would
      // try to send another response on top of that).
      static bool uploadAuthorized = false;
      server.on(
          "/file-update", HTTP_POST,
          [&]() {
            if (!uploadAuthorized) return; // already redirected in the upload callback
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
            // the main handler above gets a chance to run - checking auth
            // only there would let an unauthenticated request's file
            // bytes reach Update.write() regardless of the final
            // response. Gate it here too, matching the original code's
            // reasoning (see git history) even though the auth mechanism
            // underneath changed.
            HTTPUpload &upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
              uploadAuthorized = wifiManager.handleRequest();
              if (!uploadAuthorized) return; // handleRequest() already sent the redirect
              display.fillScreen(uiBgColor());
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

      // ---- Watch Settings (web mirror of the on-device Settings menu) ----
      // Jan asked for every on-device setting - not just WiFi/Firmware
      // Update - to also be reachable from the web UI, one page per
      // Settings/Time-submenu entry, same "each button navigates to its own
      // page" pattern as the rest of this file. Each handler reads/writes
      // the exact same RTC_DATA_ATTR globals + load*/save*() NVS helpers
      // the on-device interactive pickers (showButtonSettings() etc.) use,
      // so the two stay in sync automatically.
      server.on("/watch-settings", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body;
        body += "<form method='GET' action='/settings/time'><button>Time</button></form>";
        body += "<form method='GET' action='/settings/alarm'><button>Alarm</button></form>";
        body += "<form method='GET' action='/settings/internet-access'><button>Internet Access</button></form>";
        body += "<form method='GET' action='/settings/hostname'><button>WiFi Hostname</button></form>";
        body += "<form method='GET' action='/settings/notifications'><button>Notification Settings</button></form>";
        body += "<form method='GET' action='/settings/vibration'><button>Vibration</button></form>";
        body += "<form method='GET' action='/settings/city'><button>Weather City</button></form>";
        body += "<form method='GET' action='/settings/buttons'><button>Button Settings</button></form>";
        body += "<form method='GET' action='/settings/fontsize'><button>Font Size</button></form>";
        body += "<form method='GET' action='/settings/invertmenu'><button>Invert Menu</button></form>";
        body += "<form method='GET' action='/settings/language'><button>Language</button></form>";
        body += "<form method='GET' action='/settings/debug'><button>Debug</button></form>";
        if (webFaceCount() > 0) {
          body += "<form method='GET' action='/settings/watchface'><button>Change Watchface</button></form>";
        }
        body += "<hr><a class='btnlink' href='/'>Back</a>";
        server.send(200, "text/html", themedPage("Watch Settings", body));
      });

      server.on("/settings/debug", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body;
        body += "<form method='POST' action='/settings/buzz'><button>Vibrate Motor Test</button></form>";
        body += "<form method='GET' action='/settings/accelerometer'><button>Show Accelerometer</button></form>";
        body += "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Debug", body));
      });

      server.on("/settings/time", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body;
        body += "<form method='GET' action='/settings/set-time'><button>Set Time</button></form>";
        body += "<form method='POST' action='/settings/sync-ntp'><button>Sync NTP</button></form>";
        body += "<form method='GET' action='/settings/timezone'><button>Set Timezone</button></form>";
        body += "<form method='GET' action='/settings/vibrate-window'><button>Vibrate Window</button></form>";
        body += "<form method='GET' action='/settings/notify-interval'><button>Check Interval</button></form>";
        body += "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Time", body));
      });

      server.on("/settings/set-time", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        RTC.read(currentTime);
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/set-time'>"
                 "Year<br><input type='number' name='y' min='2000' max='2099' value='%d'><br>"
                 "Month<br><input type='number' name='mo' min='1' max='12' value='%d'><br>"
                 "Day<br><input type='number' name='d' min='1' max='31' value='%d'><br>"
                 "Hour<br><input type='number' name='h' min='0' max='23' value='%d'><br>"
                 "Minute<br><input type='number' name='mi' min='0' max='59' value='%d'><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/settings/time'>Back</a>",
                 tmYearToCalendar(currentTime.Year), currentTime.Month, currentTime.Day,
                 currentTime.Hour, currentTime.Minute);
        server.send(200, "text/html", themedPage("Set Time", buf));
      });
      server.on("/settings/set-time", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        tmElements_t tm;
        tm.Year = CalendarYrToTm(server.arg("y").toInt());
        tm.Month = constrain(server.arg("mo").toInt(), 1, 12);
        tm.Day = constrain(server.arg("d").toInt(), 1, 31);
        tm.Hour = constrain(server.arg("h").toInt(), 0, 23);
        tm.Minute = constrain(server.arg("mi").toInt(), 0, 59);
        tm.Second = 0;
        RTC.set(tm);
        server.send(200, "text/html", savedPage("Set Time", "/settings/time", "Time saved."));
      });

      server.on("/settings/sync-ntp", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        const bool ok = syncNTP(); // already connected - no connectWiFi()/teardown needed here
        server.send(200, "text/html",
                     themedPage("Sync NTP",
                                String(ok ? "<div class='msg S'>NTP sync OK.</div>"
                                          : "<div class='msg D'>NTP sync failed.</div>") +
                                    "<hr><a class='btnlink' href='/settings/time'>Back</a>"));
      });

      server.on("/settings/timezone", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/timezone'><select name='tz'>";
        for (long off = kGmtOffsetMinSec; off <= kGmtOffsetMaxSec; off += kGmtOffsetStepSec) {
          char label[16];
          const long absOff = off < 0 ? -off : off;
          snprintf(label, sizeof(label), "GMT%c%02ld:%02ld", off < 0 ? '-' : '+', absOff / 3600,
                    (absOff % 3600) / 60);
          body += "<option value='" + String(off) + "'" + (off == gmtOffset ? " selected" : "") + ">" +
                  label + "</option>";
        }
        body += "</select><br><button type='submit'>Save</button></form>"
                "<hr><a class='btnlink' href='/settings/time'>Back</a>";
        server.send(200, "text/html", themedPage("Set Timezone", body));
      });
      server.on("/settings/timezone", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        gmtOffset = constrain(server.arg("tz").toInt(), kGmtOffsetMinSec, kGmtOffsetMaxSec);
        saveManualGmtOffset(gmtOffset);
        server.send(200, "text/html", savedPage("Set Timezone", "/settings/time", "Timezone saved."));
      });

      server.on("/settings/vibrate-window", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/vibrate-window'>"
                 "From hour<br><input type='number' name='f' min='0' max='23' value='%d'><br>"
                 "To hour<br><input type='number' name='t' min='0' max='23' value='%d'><br>"
                 "<label><input type='checkbox' name='on' value='1'%s> Enabled</label><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/settings/time'>Back</a>",
                 vibrateWindowFromHour, vibrateWindowToHour, vibrateWindowEnabled ? " checked" : "");
        server.send(200, "text/html", themedPage("Vibrate Window", buf));
      });
      server.on("/settings/vibrate-window", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        vibrateWindowFromHour = constrain(server.arg("f").toInt(), 0, 23);
        vibrateWindowToHour = constrain(server.arg("t").toInt(), 0, 23);
        vibrateWindowEnabled = server.hasArg("on");
        saveVibrateWindow();
        server.send(200, "text/html", savedPage("Vibrate Window", "/settings/time"));
      });

      server.on("/settings/notify-interval", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[384];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/notify-interval'>"
                 "Minutes (1-10)<br><input type='number' name='m' min='1' max='10' value='%d'><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/settings/time'>Back</a>",
                 bleNotifyIntervalMin);
        server.send(200, "text/html", themedPage("Check Interval", buf));
      });
      server.on("/settings/notify-interval", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        bleNotifyIntervalMin = constrain(server.arg("m").toInt(), 1, 10);
        saveNotifyInterval();
        server.send(200, "text/html", savedPage("Check Interval", "/settings/time"));
      });

      server.on("/settings/alarm", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/alarm'>"
                 "Hour<br><input type='number' name='h' min='0' max='23' value='%d'><br>"
                 "Minute<br><input type='number' name='m' min='0' max='59' value='%d'><br>"
                 "<label><input type='checkbox' name='on' value='1'%s> Enabled</label><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/watch-settings'>Back</a>",
                 alarmHour, alarmMinute, alarmEnabled ? " checked" : "");
        server.send(200, "text/html", themedPage("Alarm", buf));
      });
      server.on("/settings/alarm", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        alarmHour = constrain(server.arg("h").toInt(), 0, 23);
        alarmMinute = constrain(server.arg("m").toInt(), 0, 59);
        alarmEnabled = server.hasArg("on");
        saveAlarm();
        server.send(200, "text/html", savedPage("Alarm", "/watch-settings"));
      });

      server.on("/settings/city", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/city'>"
                      "OpenWeatherMap City ID<br>"
                      "<input type='text' name='c' maxlength='7' pattern='[0-9]{1,7}' value='" +
                      String(weatherCityID) + "'><br><button type='submit'>Save</button></form>"
                      "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Weather City", body));
      });
      server.on("/settings/city", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        String cid = server.arg("c");
        cid.trim();
        if (cid.length() > 0) {
          strncpy(weatherCityID, cid.c_str(), sizeof(weatherCityID) - 1);
          weatherCityID[sizeof(weatherCityID) - 1] = '\0';
          saveWeatherCityID(weatherCityID);
          weatherIntervalCounter = -1; // force a fresh weather fetch for the new city
        }
        server.send(200, "text/html", savedPage("Weather City", "/watch-settings"));
      });

      server.on("/settings/buttons", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        auto actionSelect = [](const char *name, uint8_t current) {
          String s = "<select name='" + String(name) + "'>";
          for (int i = 0; i < WATCHFACE_ACTION_COUNT; i++) {
            s += "<option value='" + String(i) + "'" + (i == current ? " selected" : "") + ">" +
                 watchfaceActionName(i) + "</option>";
          }
          s += "</select><br>";
          return s;
        };
        String body = "<form method='POST' action='/settings/buttons'>";
        body += "<label><input type='checkbox' name='swap' value='1'" +
                String(menuBackSwapped ? " checked" : "") + "> Swap Menu/Back</label><br>";
        body += "Up short press<br>" + actionSelect("us", watchfaceUpShortAction);
        body += "Up long press<br>" + actionSelect("ul", watchfaceUpLongAction);
        body += "Down short press<br>" + actionSelect("ds", watchfaceDownShortAction);
        body += "Down long press<br>" + actionSelect("dl", watchfaceDownLongAction);
        body += "<button type='submit'>Save</button></form>"
                "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Button Settings", body));
      });
      server.on("/settings/buttons", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        menuBackSwapped = server.hasArg("swap");
        watchfaceUpShortAction = constrain(server.arg("us").toInt(), 0, WATCHFACE_ACTION_COUNT - 1);
        watchfaceUpLongAction = constrain(server.arg("ul").toInt(), 0, WATCHFACE_ACTION_COUNT - 1);
        watchfaceDownShortAction = constrain(server.arg("ds").toInt(), 0, WATCHFACE_ACTION_COUNT - 1);
        watchfaceDownLongAction = constrain(server.arg("dl").toInt(), 0, WATCHFACE_ACTION_COUNT - 1);
        saveButtonSettings();
        server.send(200, "text/html", savedPage("Button Settings", "/watch-settings"));
      });

      server.on("/settings/fontsize", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/fontsize'><select name='s'>";
        for (int i = 0; i < UI_FONT_SIZE_COUNT; i++) {
          body += "<option value='" + String(i) + "'" + (i == uiFontSize ? " selected" : "") + ">" +
                  fontSizeName(i) + "</option>";
        }
        body += "</select><br><button type='submit'>Save</button></form>"
                "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Font Size", body));
      });
      server.on("/settings/fontsize", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        uiFontSize = constrain(server.arg("s").toInt(), 0, UI_FONT_SIZE_COUNT - 1);
        saveFontSize();
        server.send(200, "text/html", savedPage("Font Size", "/watch-settings"));
      });

      server.on("/settings/invertmenu", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/invertmenu'>"
                      "<label><input type='checkbox' name='inv' value='1'" +
                      String(menuInverted ? " checked" : "") + "> Inverted (black on white)</label><br>"
                      "<button type='submit'>Save</button></form>"
                      "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Invert Menu", body));
      });
      server.on("/settings/invertmenu", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        menuInverted = server.hasArg("inv");
        saveMenuInverted();
        server.send(200, "text/html", savedPage("Invert Menu", "/watch-settings"));
      });

      server.on("/settings/internet-access", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/internet-access'>"
                      "<label><input type='checkbox' name='ble' value='1'" +
                      String(internetAccessMode == INTERNET_ACCESS_BLE ? " checked" : "") +
                      "> Use Bluetooth (via Gadgetbridge) instead of WiFi</label><br>"
                      "<button type='submit'>Save</button></form>"
                      "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Internet Access", body));
      });
      server.on("/settings/internet-access", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        internetAccessMode = server.hasArg("ble") ? INTERNET_ACCESS_BLE : INTERNET_ACCESS_WIFI;
        saveInternetAccessMode();
        server.send(200, "text/html", savedPage("Internet Access", "/watch-settings"));
      });

      server.on("/settings/hostname", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/hostname'>"
                 "Hostname (letters/digits/hyphen, empty = device default)<br>"
                 "<input type='text' name='h' maxlength='%d' value='%s'><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/watch-settings'>Back</a>"
                 "<p><small>Takes effect on the next WiFi connect, not the current session.</small></p>",
                 (int)sizeof(picoWatchHostname) - 1, picoWatchHostname);
        server.send(200, "text/html", themedPage("WiFi Hostname", buf));
      });
      server.on("/settings/hostname", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        String h = server.arg("h");
        h.trim();
        // DNS/mDNS-safe subset only - anything else (spaces, punctuation)
        // silently becomes a hyphen rather than producing a hostname that
        // half of home routers would mangle or reject outright.
        for (size_t i = 0; i < h.length(); i++) {
          const char c = h[i];
          if (!isalnum((unsigned char)c) && c != '-') h.setCharAt(i, '-');
        }
        strncpy(picoWatchHostname, h.c_str(), sizeof(picoWatchHostname) - 1);
        picoWatchHostname[sizeof(picoWatchHostname) - 1] = '\0';
        saveHostname(picoWatchHostname);
        server.send(200, "text/html", savedPage("WiFi Hostname", "/watch-settings"));
      });

      server.on("/settings/notifications", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[768];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/notifications'>"
                 "<label><input type='checkbox' name='popup' value='1'%s> Show popup</label><br>"
                 "Popup duration (%d-%ds)<br><input type='number' name='dur' min='%d' max='%d' step='%d' value='%d'><br>"
                 "<label><input type='checkbox' name='icon' value='1'%s> Show watchface icon</label><br>"
                 "<label><input type='checkbox' name='light' value='1'%s> Icon color: light (unchecked = dark)</label><br>"
                 "<label><input type='checkbox' name='vibrate' value='1'%s> Vibrate on new notification</label><br>"
                 "<button type='submit'>Save</button></form>"
                 "<hr><a class='btnlink' href='/watch-settings'>Back</a>",
                 notificationPopupEnabled ? " checked" : "",
                 NOTIFICATION_POPUP_DURATION_MIN_S, NOTIFICATION_POPUP_DURATION_MAX_S,
                 NOTIFICATION_POPUP_DURATION_MIN_S, NOTIFICATION_POPUP_DURATION_MAX_S,
                 NOTIFICATION_POPUP_DURATION_STEP_S, notificationPopupDurationS,
                 notificationIconEnabled ? " checked" : "",
                 notificationIconLight ? " checked" : "",
                 notificationVibrateEnabled ? " checked" : "");
        server.send(200, "text/html", themedPage("Notification Settings", buf));
      });
      server.on("/settings/notifications", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        notificationPopupEnabled = server.hasArg("popup");
        notificationPopupDurationS = constrain(server.arg("dur").toInt(),
                                                NOTIFICATION_POPUP_DURATION_MIN_S,
                                                NOTIFICATION_POPUP_DURATION_MAX_S);
        notificationIconEnabled = server.hasArg("icon");
        notificationIconLight = server.hasArg("light");
        notificationVibrateEnabled = server.hasArg("vibrate");
        saveNotificationSettings();
        server.send(200, "text/html", savedPage("Notification Settings", "/watch-settings"));
      });

      server.on("/settings/vibration", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        char buf[640];
        snprintf(buf, sizeof(buf),
                 "<form method='POST' action='/settings/vibration'>"
                 "Strength (applies to all vibrations - o'clock, alarm, notifications):<br>"
                 "<label><input type='radio' name='strength' value='0'%s> Low</label><br>"
                 "<label><input type='radio' name='strength' value='1'%s> Medium</label><br>"
                 "<label><input type='radio' name='strength' value='2'%s> High</label><br>"
                 "<button type='submit'>Save</button></form>"
                 "<form method='POST' action='/settings/buzz'><button>Test Buzz</button></form>"
                 "<hr><a class='btnlink' href='/watch-settings'>Back</a>",
                 vibrationStrength == VIBRATION_STRENGTH_LOW ? " checked" : "",
                 vibrationStrength == VIBRATION_STRENGTH_MEDIUM ? " checked" : "",
                 vibrationStrength == VIBRATION_STRENGTH_HIGH ? " checked" : "");
        server.send(200, "text/html", themedPage("Vibration", buf));
      });
      server.on("/settings/vibration", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        vibrationStrength = constrain(server.arg("strength").toInt(), 0, 2);
        saveVibrationSettings();
        server.send(200, "text/html", savedPage("Vibration", "/watch-settings"));
      });

      server.on("/settings/language", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        String body = "<form method='POST' action='/settings/language'><select name='l'>";
        for (int i = 0; i < PW_LANG_COUNT; i++) {
          body += "<option value='" + String(i) + "'" + (i == picowatchLanguage ? " selected" : "") + ">" +
                  pwLanguageName(i) + "</option>";
        }
        body += "</select><br><button type='submit'>Save</button></form>"
                "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
        server.send(200, "text/html", themedPage("Language", body));
      });
      server.on("/settings/language", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        picowatchLanguage = constrain(server.arg("l").toInt(), 0, PW_LANG_COUNT - 1);
        saveLanguage();
        server.send(200, "text/html", savedPage("Language", "/watch-settings"));
      });

      server.on("/settings/buzz", HTTP_POST, [&]() {
        if (!wifiManager.handleRequest()) return;
        vibMotor();
        server.send(200, "text/html", savedPage("Vibrate Motor Test", "/settings/debug", "Buzzed."));
      });

      server.on("/settings/accelerometer", HTTP_GET, [&]() {
        if (!wifiManager.handleRequest()) return;
        Accel acc;
        const bool ok = sensor.getAccel(acc);
        String body;
        if (!ok) {
          body = "<div class='msg D'>Read failed.</div>";
        } else {
          const char *dirName;
          switch (sensor.getDirection()) {
          case DIRECTION_DISP_DOWN: dirName = "Face Down"; break;
          case DIRECTION_DISP_UP: dirName = "Face Up"; break;
          case DIRECTION_BOTTOM_EDGE: dirName = "Bottom Edge"; break;
          case DIRECTION_TOP_EDGE: dirName = "Top Edge"; break;
          case DIRECTION_RIGHT_EDGE: dirName = "Right Edge"; break;
          case DIRECTION_LEFT_EDGE: dirName = "Left Edge"; break;
          default: dirName = "Unknown"; break;
          }
          body = "<div class='msg'>X: " + String(acc.x) + "<br>Y: " + String(acc.y) + "<br>Z: " +
                 String(acc.z) + "<br>Direction: " + dirName + "</div>";
        }
        // Snapshot, not the on-device live-updating loop (showAccelerometer())
        // - a static page can't redraw itself as the watch tilts, so this
        // reads once per request instead; "Read again" just re-requests it.
        body += "<form method='GET' action='/settings/accelerometer'><button>Read again</button></form>"
                "<hr><a class='btnlink' href='/settings/debug'>Back</a>";
        server.send(200, "text/html", themedPage("Accelerometer", body));
      });

      if (webFaceCount() > 0) {
        server.on("/settings/watchface", HTTP_GET, [&]() {
          if (!wifiManager.handleRequest()) return;
          String body = "<form method='POST' action='/settings/watchface'><select name='f'>";
          for (int i = 0; i < webFaceCount(); i++) {
            body += "<option value='" + String(i) + "'" + (i == webSelectedFace() ? " selected" : "") + ">" +
                    webFaceName(i) + "</option>";
          }
          body += "</select><br><button type='submit'>Save</button></form>"
                  "<hr><a class='btnlink' href='/watch-settings'>Back</a>";
          server.send(200, "text/html", themedPage("Watchface", body));
        });
        server.on("/settings/watchface", HTTP_POST, [&]() {
          if (!wifiManager.handleRequest()) return;
          webSetFace(server.arg("f").toInt());
          server.send(200, "text/html", savedPage("Watchface", "/watch-settings"));
        });
      }

      // Once connected, WiFiManager's own root page ("/") becomes our
      // app's home screen: stock menu items (WiFi/Info/Restart) plus our
      // own buttons injected via setCustomMenuHTML() - ported structurally
      // 1:1 from pfsense-status-esp32's buildCustomMenuHtml()/
      // applyPortalCustomHtml() (config_portal.cpp): one <form><button>
      // per action, each navigating to its own page, instead of
      // everything crammed into one block.
      //
      // The actual bug (found 06.08.2026): WiFiManager's handleRoot() only
      // appends _customMenuHTML when the menu list passed to setMenu()
      // contains the literal token "custom" (WiFiManager.cpp ~line 1443) -
      // pfsense-status-esp32's own config_portal.cpp:784 fullMenu[] array
      // includes it explicitly, but our first port only had "wifi", so our
      // whole Firmware Update section/buttons silently never rendered.
      static std::vector<const char *> kConnectedMenuIds = {"wifi", "info", "custom", "restart"};
      wifiManager.setMenu(kConnectedMenuIds);
      // Info page cleanup (Jan, 15.08.2026): he wanted to keep the actual
      // diagnostic content but not WiFiManager's own bundled "Erase"/
      // "Update" (OTA firmware upload - unrelated to our GitHub Update
      // flow, would let anyone on the network flash arbitrary firmware)
      // buttons, and wanted a way back to the menu, which WiFiManager's
      // Info/WiFi/etc. pages don't show by default.
      wifiManager.setShowInfoErase(false);
      wifiManager.setShowInfoUpdate(false);
      wifiManager.setShowBack(true);
      // NOT setCustomStatusHTML() - WiFiManager already renders its own
      // native "Connected to X, with IP Y" status (wm_strings_en.h's
      // HTTP_STATUS_ON) on every page automatically; setCustomStatusHTML()
      // appends "below" that, not replacing it, so this was ALWAYS a
      // redundant second, less-informative (no IP) status box stacked
      // directly under the real one - Jan noticed it duplicated on every
      // page (15.08.2026, screenshot showed both "Connected to t800 with
      // IP..." and a second bare "Connected to t800" underneath).
      gCustomMenuHtml = "<form method='GET' action='/watch-settings'><button>Watch Settings</button></form>";
      gCustomMenuHtml += "<form method='POST' action='/github-update'><button>Check for Online Update</button></form>";
      gCustomMenuHtml += "<form method='GET' action='/file-update-page'><button>File Update</button></form>";
      gCustomMenuHtml += "<form method='GET' action='/change-password'><button>Change Password</button></form>";
      gCustomMenuHtml += "<form method='POST' action='/config-erase' onsubmit=\"return confirm('Erase all settings and reboot?');\">"
                         "<button style='background:#dc3630;color:#fff'>Config Erase</button></form>";
      wifiManager.setCustomMenuHTML(gCustomMenuHtml.c_str());
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
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
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

void PicoWatch::showSyncNTP() {
  display.setFullWindow();
  display.fillScreen(uiBgColor());
  display.setFont(uiMenuFont());
  display.setTextColor(uiFgColor());
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
  showTimeMenu(timeMenuIndex, false);
}

bool PicoWatch::_syncTimeViaBle(long gmt) {
  String payload;
  if (!_httpViaBle(BLE_TIME_API_URL, payload)) return false;
  JSONVar responseObject = JSON.parse(payload);
  if (JSON.typeof(responseObject) != "object" || !responseObject.hasOwnProperty("unixtime")) {
    return false;
  }
  tmElements_t tm;
  breakTime((time_t)((long)responseObject["unixtime"] + gmt), tm);
  RTC.set(tm);
  return true;
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
