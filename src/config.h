#ifndef CONFIG_H
#define CONFIG_H

// Versioning
#define PICOWATCH_LIB_VER "1.4.14"

//pins

#ifdef ARDUINO_ESP32S3_DEV //V3

#define PICOWATCH_V3_SDA 12
#define PICOWATCH_V3_SCL 11

#define PICOWATCH_V3_SS    33
#define PICOWATCH_V3_MOSI  48
#define PICOWATCH_V3_MISO  46
#define PICOWATCH_V3_SCK   47

#define MENU_BTN_PIN  7
#define BACK_BTN_PIN  6
#define UP_BTN_PIN    0
#define DOWN_BTN_PIN  8

#define DISPLAY_CS    33
#define DISPLAY_DC    34
#define DISPLAY_RES   35
#define DISPLAY_BUSY  36
#define ACC_INT_1_PIN 14
#define ACC_INT_2_PIN 13
#define VIB_MOTOR_PIN 17
#define BATT_ADC_PIN 9
#define CHRG_STATUS_PIN 10
#define USB_DET_PIN 21
#define RTC_INT_PIN -1 //not used

#define MENU_BTN_MASK (BIT64(7))
#define BACK_BTN_MASK (BIT64(6))
#define UP_BTN_MASK   (BIT64(0))
#define DOWN_BTN_MASK (BIT64(8))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK

#else //V1,V1.5,V2

#if !defined(ARDUINO_WATCHY_V10) && !defined(ARDUINO_WATCHY_V15) && !defined(ARDUINO_WATCHY_V20)

#pragma message "Please install the latest ESP32 Arduino Core (2.0.5+) and choose PicoWatch as the target board"
#pragma message "Hardware revision is not defined at the project level, please define in config.h. Defaulting to ARDUINO_WATCHY_V20"

#define ARDUINO_WATCHY_V20

#define MENU_BTN_PIN 26
#define BACK_BTN_PIN 25
#define DOWN_BTN_PIN 4
#define DISPLAY_CS 5
#define DISPLAY_RES 9
#define DISPLAY_DC 10
#define DISPLAY_BUSY 19
#define ACC_INT_1_PIN 14
#define ACC_INT_2_PIN 12
#define VIB_MOTOR_PIN 13
#define RTC_INT_PIN 27

#if defined (ARDUINO_WATCHY_V10)
    #define UP_BTN_PIN 32
    #define BATT_ADC_PIN 33
    #define UP_BTN_MASK  (BIT64(32))
    #define RTC_TYPE 1 //DS3231
#elif defined (ARDUINO_WATCHY_V15)
    #define UP_BTN_PIN 32
    #define BATT_ADC_PIN 35
    #define UP_BTN_MASK  (BIT64(32))
    #define RTC_TYPE 2 //PCF8563
#elif defined (ARDUINO_WATCHY_V20)
    #define UP_BTN_PIN 35
    #define BATT_ADC_PIN 34
    #define UP_BTN_MASK  (BIT64(35))
    #define RTC_TYPE 2 //PCF8563
#endif

#define MENU_BTN_MASK (BIT64(26))
#define BACK_BTN_MASK (BIT64(25))
#define DOWN_BTN_MASK (BIT64(4))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK

#endif

#endif

//display
#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
// wifi
#define WIFI_AP_TIMEOUT 60          // seconds with zero AP clients connected before the setup portal auto-shuts-down
#define WIFI_AP_SSID    "PicoWatch AP"
#define WIFI_STAY_CONNECTED_TIMEOUT 300  // seconds the connection stays up/reachable after a successful WiFi Setup, before auto-disconnecting to save battery
// Web UI login (status/File-Update/GitHub-Update page), same idea as
// pfsense-status-esp32's menu password - IP+timeout session, not Basic Auth.
#define WEB_MENU_PASSWORD_DEFAULT "clockmenu123"
#define WEB_MENU_SESSION_MS 3600000UL  // 1 hour, matches pfsense-status-esp32
// GitHub OTA update (Settings -> "Update via GitHub", and the WiFi web UI's
// "GitHub Update" button both call PicoWatch::updateFromGithub(), which checks
// this repo's latest release and downloads this asset by exact name).
#define GITHUB_OTA_OWNER "UniqueDroid"
#define GITHUB_OTA_REPO  "PicoWatch"
#ifndef GITHUB_OTA_ASSET_NAME
#define GITHUB_OTA_ASSET_NAME "AllFaces.ino-v30.bin"
#endif
// menu
#define WATCHFACE_STATE     -1
#define MAIN_MENU_STATE     0
#define APP_STATE           1
#define FW_UPDATE_STATE     2
#define SETTINGS_MENU_STATE 3
#define MENU_HEIGHT     25
#define MENU_LENGTH     6           // top-level: Change Watchface, Stopwatch, Steps, Alarm, Weather, Settings
#define SETTINGS_MENU_LENGTH 11     // About, Vibrate, Accelerometer, Set Time, WiFi, Sync NTP, Set Timezone, Set City, Update via GitHub, Button Settings, Font Size
#define SETTINGS_MENU_VISIBLE_ROWS 6  // scrolling window size - showing all rows at once (via a tighter row height) was too cramped/hard to read
// menu list font size (Settings -> "Font Size"), scoped to just the top-
// level menu and Settings list (see project memory - a true system-wide
// size would need every single screen's hand-tuned pixel positions
// re-derived per size, a much bigger undertaking than this).
#define UI_FONT_SIZE_SMALL   0
#define UI_FONT_SIZE_DEFAULT 1
#define UI_FONT_SIZE_BIG     2
#define UI_FONT_SIZE_COUNT   3
// button remapping (PicoWatch::showButtonSettings()) - Menu/Back swap plus
// short/long-press action pickers for Up/Down while on the watchface
// (previously unused there). Long-press threshold in ms.
#define LONG_PRESS_MS 600
#define WATCHFACE_ACTION_NONE            0
#define WATCHFACE_ACTION_SETTINGS        1
#define WATCHFACE_ACTION_CHANGE_WATCHFACE 2
#define WATCHFACE_ACTION_WEATHER         3
#define WATCHFACE_ACTION_STOPWATCH       4
#define WATCHFACE_ACTION_ALARM           5
#define WATCHFACE_ACTION_COUNT           6
#define BUTTON_SETTINGS_FIELD_COUNT      5  // swap, up-short, up-long, down-short, down-long
// alarm (PicoWatch::setAlarm())
#define SET_ALARM_HOUR    0
#define SET_ALARM_MINUTE  1
#define SET_ALARM_ENABLED 2
// set time
#define SET_HOUR   0
#define SET_MINUTE 1
#define SET_YEAR   2
#define SET_MONTH  3
#define SET_DAY    4
#define HOUR_12_24 24
// BLE OTA
#define BLE_DEVICE_NAME        "PicoWatch BLE OTA"
#define WATCHFACE_NAME         "PicoWatch 7 Segment"
#define SOFTWARE_VERSION_MAJOR 1
#define SOFTWARE_VERSION_MINOR 0
#define SOFTWARE_VERSION_PATCH 0
#define HARDWARE_VERSION_MAJOR 1
#define HARDWARE_VERSION_MINOR 0

#endif