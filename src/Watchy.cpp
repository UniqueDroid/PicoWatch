#include "Watchy.h"
#include <Preferences.h>
#include <mbedtls/sha256.h> // step 2/5 - linked but unused so far, see Watchy.h

// Cached copy of the persisted alarm (see setAlarm()/onReset()) - survives
// deep sleep like guiState/menuIndex; loaded from flash once on reset rather
// than re-reading NVS every single per-minute tick. Declared here (ahead of
// the anonymous namespace below) since loadAlarm()/saveAlarm() reference it.
RTC_DATA_ATTR uint8_t alarmHour;
RTC_DATA_ATTR uint8_t alarmMinute;
RTC_DATA_ATTR bool alarmEnabled;

// Weather city ID actually used at runtime (OpenWeatherMap numeric ID, see
// https://openweathermap.org/current#cityid) - defaults to settings.cityID
// but can be overridden on-device via Watchy::setWeatherCity() without a
// recompile. Can't just mutate settings.cityID directly: settings is a
// regular (non-RTC) object member reconstructed from the compile-time
// default on every wake from deep sleep, so an override stored there would
// vanish after the very next sleep cycle.
RTC_DATA_ATTR char weatherCityID[12];

namespace {
// Manual timezone override, persisted in flash (NVS) so it survives power
// loss, not just deep sleep (unlike the RTC_DATA_ATTR globals below). Once
// set via Watchy::setTimezone(), the weather-fetch code stops overwriting
// gmtOffset with the queried city's timezone (see the weather API handling
// further down) - that auto-overwrite was clobbering manually-corrected
// times whenever weather/NTP synced.
constexpr const char *kPrefsNamespace = "watchy";
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

// Step-count history for the last 7 complete days, persisted in flash (NVS)
// so it survives a reset. history[0] is the most recently completed day,
// history[6] the oldest. Captured once per real day at 00:00 regardless of
// which watchface is active (see Watchy::init()'s WATCHFACE_STATE tick
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
  if (code > 801) return "Cloudy";
  if (code == 801) return "Few Clouds";
  if (code == 800) return "Clear";
  if (code >= 700) return "Haze";
  if (code >= 600) return "Snow";
  if (code >= 500) return "Rain";
  if (code >= 300) return "Drizzle";
  if (code >= 200) return "Storm";
  return "?";
}

struct DayForecastEntry {
  char date[11] = ""; // "YYYY-MM-DD"
  int temp = 0;
  int conditionCode = 0;
};
}  // namespace

#ifdef ARDUINO_ESP32S3_DEV
  Watchy32KRTC Watchy::RTC;
  #define ACTIVE_LOW 0
#else
  WatchyRTC Watchy::RTC;
  #define ACTIVE_LOW 1
#endif
GxEPD2_BW<WatchyDisplay, WatchyDisplay::HEIGHT> Watchy::display(
    WatchyDisplay{});

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

void Watchy::init(String datetime) {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause(); // get wake up reason
  #ifdef ARDUINO_ESP32S3_DEV
    Wire.begin(WATCHY_V3_SDA, WATCHY_V3_SCL);     // init i2c
  #else
    Wire.begin(SDA, SCL);                         // init i2c
  #endif
  RTC.init();
  // Init the display since is almost sure we will use it
  display.epd2.initWatchy();

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
void Watchy::deepSleep() {
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
  int secToNextMin = 60 - timeinfo.tm_sec;
  esp_sleep_enable_timer_wakeup(secToNextMin * uS_TO_S_FACTOR);
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
void dispatchTopMenu(Watchy *w, int index) {
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

void dispatchSettingsMenu(Watchy *w, int index) {
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
    w->showUpdateViaGithubPlaceholder();
    break;
  default:
    break;
  }
}
}  // namespace

void Watchy::handleButtonPress() {
  uint64_t wakeupBit = esp_sleep_get_ext1_wakeup_status();
  // Menu Button
  if (wakeupBit & MENU_BTN_MASK) {
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
  else if (wakeupBit & BACK_BTN_MASK) {
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
      if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
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
      } else if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
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

void Watchy::showMenu(byte menuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {"Change Watchface", "Stopwatch", "Steps (7 Days)", "Alarm", "Weather (5 Days)", "Settings"};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(menuItems[i]);
    }
  }

  display.display(partialRefresh);

  guiState = MAIN_MENU_STATE;
  alreadyInMenu = false;
}

void Watchy::showFastMenu(byte menuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *menuItems[] = {"Change Watchface", "Stopwatch", "Steps (7 Days)", "Alarm", "Weather (5 Days)", "Settings"};
  for (int i = 0; i < MENU_LENGTH; i++) {
    yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == menuIndex) {
      display.getTextBounds(menuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(menuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(menuItems[i]);
    }
  }

  display.display(true);

  guiState = MAIN_MENU_STATE;
}

void Watchy::showSettingsMenu(byte settingsMenuIndex, bool partialRefresh) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *settingsMenuItems[] = {"About Watchy", "Vibrate Motor", "Show Accelerometer",
                                      "Set Time",     "Setup WiFi",    /*"Update Firmware",*/
                                      "Sync NTP",     "Set Timezone", "Set City",
                                      "Update via GitHub"};
  for (int i = 0; i < SETTINGS_MENU_LENGTH; i++) {
    yPos = SETTINGS_MENU_ITEM_HEIGHT + (SETTINGS_MENU_ITEM_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == settingsMenuIndex) {
      display.getTextBounds(settingsMenuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 12, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(settingsMenuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(settingsMenuItems[i]);
    }
  }

  display.display(partialRefresh);

  guiState = SETTINGS_MENU_STATE;
  alreadyInMenu = false;
}

void Watchy::showFastSettingsMenu(byte settingsMenuIndex) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  int16_t yPos;

  const char *settingsMenuItems[] = {"About Watchy", "Vibrate Motor", "Show Accelerometer",
                                      "Set Time",     "Setup WiFi",    /*"Update Firmware",*/
                                      "Sync NTP",     "Set Timezone", "Set City",
                                      "Update via GitHub"};
  for (int i = 0; i < SETTINGS_MENU_LENGTH; i++) {
    yPos = SETTINGS_MENU_ITEM_HEIGHT + (SETTINGS_MENU_ITEM_HEIGHT * i);
    display.setCursor(0, yPos);
    if (i == settingsMenuIndex) {
      display.getTextBounds(settingsMenuItems[i], 0, yPos, &x1, &y1, &w, &h);
      display.fillRect(x1 - 1, y1 - 10, 200, h + 12, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);
      display.println(settingsMenuItems[i]);
    } else {
      display.setTextColor(GxEPD_WHITE);
      display.println(settingsMenuItems[i]);
    }
  }

  display.display(true);

  guiState = SETTINGS_MENU_STATE;
}

void Watchy::_captureStepsAtMidnight() {
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

void Watchy::showStopwatch() {
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
      display.println("Stopwatch");

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
      display.println(running ? "Menu: Stop" : "Menu: Start");
      if (!running) {
        display.setCursor(20, 185);
        display.println("Up: Reset");
      }
      display.display(true); // partial refresh
    }
  }

  showMenu(menuIndex, false);
}

void Watchy::showStepsHistory() {
  guiState = APP_STATE;

  int32_t history[kStepsHistoryDays];
  loadStepsHistory(history);

  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(20, 30);
  display.println("Steps - 7 Days");

  static constexpr const char *kLabels[kStepsHistoryDays] = {"Yesterday", "2 days ago", "3 days ago",
                                                              "4 days ago", "5 days ago", "6 days ago",
                                                              "7 days ago"};
  for (int i = 0; i < kStepsHistoryDays; i++) {
    display.setCursor(5, 55 + i * 20);
    char buf[32];
    snprintf(buf, sizeof(buf), "%-11s %6ld", kLabels[i], (long)history[i]);
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

void Watchy::setAlarm() {
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
    display.println("Alarm");

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
    display.print("Enabled: ");
    if (setIndex == SET_ALARM_ENABLED) {
      display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    display.println(enabled ? "Yes" : "No");

    display.display(true); // partial refresh
  }

  alarmHour = hour;
  alarmMinute = minute;
  alarmEnabled = enabled;
  saveAlarm();

  showMenu(menuIndex, false);
}

void Watchy::showWeatherForecast() {
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
  display.println("Loading...");
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
    display.println("Check WiFi and the");
    display.println("weather API key.");
  }
  display.display(false); // full refresh

  pinMode(BACK_BTN_PIN, INPUT);
  while (digitalRead(BACK_BTN_PIN) != ACTIVE_LOW) {
    delay(50);
  }
  while (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) delay(10); // wait for release

  showMenu(menuIndex, false);
}

void Watchy::showAbout() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 20);

  display.print("LibVer: ");
  display.println(WATCHY_LIB_VER);

  display.print("Rev: v");
  display.println(getBoardRevision());

  display.print("Batt: ");
  float voltage = getBatteryVoltage();
  display.print(voltage);
  display.println("V");

  #ifndef ARDUINO_ESP32S3_DEV
  display.print("Uptime: ");
  RTC.read(currentTime);
  time_t b = makeTime(bootTime);
  time_t c = makeTime(currentTime);
  int totalSeconds = c-b;
  //int seconds = (totalSeconds % 60);
  int minutes = (totalSeconds % 3600) / 60;
  int hours = (totalSeconds % 86400) / 3600;
  int days = (totalSeconds % (86400 * 30)) / 86400; 
  display.print(days);
  display.print("d");
  display.print(hours);
  display.print("h");
  display.print(minutes);
  display.println("m");  
  #endif
  
  if(WIFI_CONFIGURED){
    display.print("SSID: ");
    display.println(lastSSID);
    display.print("IP: ");
    display.println(IPAddress(lastIPAddress).toString());
  }else{
    display.println("WiFi Not Connected");
  }
  display.display(false); // full refresh

  guiState = APP_STATE;
}

// Step 1 of re-adding the WiFi/Update feature in isolated, individually
// testable pieces (see project memory - the full feature caused an
// unexplained menu/display regression and was fully reverted). This is a
// placeholder for the real GitHub-update flow, deliberately using ONLY
// existing display APIs (no new #includes, no WebServer/WiFiClientSecure/
// Update/mbedtls) so this step tests just the settings-menu item bump in
// isolation.
void Watchy::showUpdateViaGithubPlaceholder() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Update via GitHub");
  display.println(" ");
  display.println("Coming soon.");
  display.display(false); // full refresh

  guiState = APP_STATE;
}

void Watchy::showBuzz() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(70, 80);
  display.println("Buzz!");
  display.display(false); // full refresh
  vibMotor();
  showSettingsMenu(settingsMenuIndex, false);
}

void Watchy::vibMotor(uint8_t intervalMs, uint8_t length) {
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  bool motorOn = false;
  for (int i = 0; i < length; i++) {
    motorOn = !motorOn;
    digitalWrite(VIB_MOTOR_PIN, motorOn);
    delay(intervalMs);
  }
}

void Watchy::setTime() {

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

void Watchy::setTimezone() {
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

void Watchy::setWeatherCity() {
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
    display.println("Set City ID");

    for (int i = 0; i < kDigitCount; i++) {
      display.setCursor(15 + i * 24, 90);
      if (i == setIndex) {
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
      } else {
        display.setTextColor(GxEPD_WHITE);
      }
      display.print(digits[i]);
    }

    display.setTextColor(GxEPD_WHITE);
    display.setCursor(5, 150);
    display.println("Find your city ID at");
    display.setCursor(5, 170);
    display.println("openweathermap.org");
    display.setCursor(5, 190);
    display.println("/current#cityid");

    display.display(true); // partial refresh
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

void Watchy::showAccelerometer() {
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
        display.println("getAccel FAIL");
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
          display.println("FACE DOWN");
          break;
        case DIRECTION_DISP_UP:
          display.println("FACE UP");
          break;
        case DIRECTION_BOTTOM_EDGE:
          display.println("BOTTOM EDGE");
          break;
        case DIRECTION_TOP_EDGE:
          display.println("TOP EDGE");
          break;
        case DIRECTION_RIGHT_EDGE:
          display.println("RIGHT EDGE");
          break;
        case DIRECTION_LEFT_EDGE:
          display.println("LEFT EDGE");
          break;
        default:
          display.println("ERROR!!!");
          break;
        }
      }
      display.display(true); // full refresh
    }
  }

  showSettingsMenu(settingsMenuIndex, false);
}

void Watchy::showWatchFace(bool partialRefresh) {
  display.setFullWindow();
  // At this point it is sure we are going to update
  display.epd2.asyncPowerOn();
  drawWatchFace();
  display.display(partialRefresh); // partial refresh
  guiState = WATCHFACE_STATE;
}

void Watchy::drawWatchFace() {
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

weatherData Watchy::getWeatherData() {
  return _getWeatherData(weatherCityID, settings.lat, settings.lon,
    settings.weatherUnit, settings.weatherLang, settings.weatherURL,
    settings.weatherAPIKey, settings.weatherUpdateInterval);
}

weatherData Watchy::_getWeatherData(String cityID, String lat, String lon, String units, String lang,
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

float Watchy::getBatteryVoltage() {
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

uint8_t Watchy::getBoardRevision() {
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

uint16_t Watchy::_readRegister(uint8_t address, uint8_t reg, uint8_t *data,
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

uint16_t Watchy::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data,
                                uint16_t len) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(data, len);
  return (0 != Wire.endTransmission());
}

void Watchy::_bmaConfig() {

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
}  // namespace

void Watchy::setupWifi() {
  display.epd2.setBusyCallback(0); // temporarily disable lightsleep on busy
  WiFiManager wifiManager;
  // NOTE: deliberately no resetSettings() here (that call used to force a
  // full reconfigure on every single "Setup WiFi" visit, wiping saved
  // credentials each time). autoConnect() already does the right thing on
  // its own: try the saved network first, only fall back to the AP portal
  // if that fails - so once configured, revisiting this screen just
  // reconnects and shows the current IP instead of asking to set up again.
  wifiManager.setTimeout(WIFI_AP_TIMEOUT);
  wifiManager.setAPCallback(_configModeCallback);
  wifiManager.setCustomHeadElement(kWifiPortalTheme);
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  // Missing in the stock code: without this, text prints wherever the
  // cursor was left by whatever screen ran right before this one (e.g. the
  // Settings menu, whose last row can leave the cursor near/past the
  // bottom edge) - looks like a blank/black screen since nothing visible
  // gets drawn. Every other screen in this file sets this explicitly.
  display.setCursor(0, 30);
  bool connected = wifiManager.autoConnect(WIFI_AP_SSID);
  if (!connected) { // WiFi setup failed
    display.println("Setup failed &");
    display.println("timed out!");
  } else {
    display.println("Connected to:");
    display.println(WiFi.SSID());
		display.println("Local IP:");
		display.println(WiFi.localIP());
    display.println(" ");
    display.println("Back to disconnect");
    weatherIntervalCounter = -1; // Reset to force weather to be read again
    lastIPAddress = WiFi.localIP();
    WiFi.SSID().toCharArray(lastSSID, 30);
  }
  display.display(false); // full refresh

  if (connected) {
    // Keep the connection (and IP) actually reachable for a while instead
    // of tearing it straight back down - otherwise the display still says
    // "Connected to..." but the radio is already off underneath it, so
    // pinging the shown IP only works for the couple of seconds before
    // this point. Exits early on Back, or after WIFI_STAY_CONNECTED_TIMEOUT
    // seconds of nobody touching it.
    pinMode(BACK_BTN_PIN, INPUT);
    unsigned long connectedAt = millis();
    while (millis() - connectedAt < (unsigned long)WIFI_STAY_CONNECTED_TIMEOUT * 1000UL) {
      if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) break;
      delay(50);
    }
  }

  // turn off radios
  WiFi.mode(WIFI_OFF);
  btStop();
  // enable lightsleep on busy
  display.epd2.setBusyCallback(WatchyDisplay::busyCallback);
  guiState = APP_STATE;
}

void Watchy::_configModeCallback(WiFiManager *myWiFiManager) {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Connect to");
  display.print("SSID: ");
  display.println(WIFI_AP_SSID);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
	display.println("MAC address:");
	display.println(WiFi.softAPmacAddress().c_str());
  display.display(false); // full refresh
}

bool Watchy::connectWiFi() {
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
void Watchy::showUpdateFW() {
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

void Watchy::updateFWBegin() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Bluetooth Started");
  display.println(" ");
  display.println("Watchy BLE OTA");
  display.println(" ");
  display.println("Waiting for");
  display.println("connection...");
  display.display(false); // full refresh

  BLE BT;
  BT.begin("Watchy BLE OTA");
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
void Watchy::showSyncNTP() {
  display.setFullWindow();
  display.fillScreen(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(0, 30);
  display.println("Syncing NTP... ");
  display.print("GMT offset: ");
  display.println(gmtOffset);
  display.display(false); // full refresh
  if (connectWiFi()) {
    if (syncNTP()) {
      display.println("NTP Sync Success\n");
      display.println("Current Time Is:");

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
      display.println("NTP Sync Failed");
    }
    WiFi.mode(WIFI_OFF);
    btStop();
  } else {
    display.println("WiFi Not Configured");
  }
  display.display(true); // full refresh
  delay(3000);
  showSettingsMenu(settingsMenuIndex, false);
}

bool Watchy::syncNTP() { // NTP sync - call after connecting to WiFi and
                         // remember to turn it back off
  return syncNTP(gmtOffset,
                 settings.ntpServer.c_str());
}

bool Watchy::syncNTP(long gmt) {
  return syncNTP(gmt, settings.ntpServer.c_str());
}

bool Watchy::syncNTP(long gmt, String ntpServer) {
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
