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

// Vibration motor PWM (PicoWatch::vibMotor(), see config.h's
// VIBRATION_STRENGTH_* and PicoWatch::showVibrationSettings()) - channel
// is arbitrary (0), nothing else in this codebase uses LEDC. 8-bit
// resolution (0-255 duty) is plenty for 3 discrete strength levels;
// 5kHz is a typical ERM vibration-motor PWM frequency, well above
// audible/felt-buzz range from the PWM signal itself.
#define VIB_MOTOR_PWM_CHANNEL 0
#define VIB_MOTOR_PWM_FREQ 5000
#define VIB_MOTOR_PWM_RESOLUTION_BITS 8
#define VIB_MOTOR_DUTY_LOW    90
#define VIB_MOTOR_DUTY_MEDIUM 170
#define VIB_MOTOR_DUTY_HIGH   255

//display
#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
// Night wake-interval reduction (PicoWatch::deepSleep(), V3/ESP32-S3 timer
// wakeup path only) - ported from InkWatchy's NIGHT_SLEEP_* defines. Instead
// of waking every single minute to redraw the watchface, wake only every
// NIGHT_SLEEP_FOR_M minutes between NIGHT_SLEEP_AFTER_HOUR and
// NIGHT_SLEEP_BEFORE_HOUR (24h, AFTER > BEFORE spans midnight) - saves
// battery while asleep, button/USB wake still works normally throughout.
#define NIGHT_SLEEP_AFTER_HOUR 23
#define NIGHT_SLEEP_BEFORE_HOUR 5
#define NIGHT_SLEEP_FOR_M 45
// Idle CPU clock, ported from InkWatchy's CPU_SPEED idea (setCpuFrequencyMhz
// instead of the default 240MHz). Almost the entire awake cycle is either
// delay(10) button-polling loops or waiting on I2C/SPI/WiFi I/O, none of
// which is CPU-bound, so a slower clock draws less current for the same
// wall-clock work. 80 is Espressif's documented floor for stable WiFi/BT -
// do NOT lower this further, WiFi Setup/Sync NTP/Weather/GitHub Update all
// depend on it.
#define CPU_FREQ_MHZ 80
// wifi
// How long setupWifi() waits for a direct WiFi.begin() reconnect (known
// network, already configured) before giving up and falling back to the
// AP config portal - was a hardcoded 8000ms, too short in practice (Jan,
// 15.08.2026): a real WPA2 handshake + DHCP lease can take longer than
// that, especially at the edge of range, and hitting this timeout means
// a full reconfigure detour for what's usually just a slow reconnect.
#define WIFI_CONNECT_TIMEOUT_MS 20000
// Was previously defined but never actually wired to WiFiManager (dead
// code) - startConfigPortal() had NO timeout at all as a result and
// blocked forever until the form was completed (see setupWifi()'s
// comment, 15.08.2026, for why this is now wired via
// setConfigPortalTimeout()+setAPClientCheck() instead of a Back-button
// interrupt).
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
// Must exactly match the filename the release workflows actually upload
// (.github/workflows/v3.actions.yml / main.actions.yml rename each build's
// "AllFaces.ino.bin" to "AllFaces-<board-revision>.ino.bin" before
// attaching it to the GitHub Release) - verified against that rename
// script directly (15.08.2026), not just assumed; the previous hardcoded
// "AllFaces.ino-v30.bin" never matched any real asset name (never caught
// before because no release existed yet to test against), which would
// have made GitHub Update silently fail with "asset not found" on every
// board, V2 and V3 alike.
#ifndef GITHUB_OTA_ASSET_NAME
#ifdef ARDUINO_ESP32S3_DEV
#define GITHUB_OTA_ASSET_NAME "AllFaces-v30.ino.bin"
#else
#define GITHUB_OTA_ASSET_NAME "AllFaces-v20.ino.bin"
#endif
#endif
// menu
#define WATCHFACE_STATE     -1
#define MAIN_MENU_STATE     0
#define APP_STATE           1
#define SETTINGS_MENU_STATE 3
#define GAMES_MENU_STATE    4
#define TIME_MENU_STATE     5
#define DEBUG_MENU_STATE    6
#define MENU_HEIGHT     25
#define MENU_LENGTH     8           // top-level: Change Watchface, Stopwatch, Steps, Alarm, Weather, Notifications, Games, Settings
#define SETTINGS_MENU_LENGTH 14     // About, Time, WiFi, Internet Access, Notification Settings, Vibration, Set City, Online Update, Button Settings, Font Size, Invert Menu, Language, Debug, Power
#define GAMES_MENU_LENGTH 4         // Snake, Pong, Tetris, Flappy
#define TIME_MENU_LENGTH 5          // Set Time, Sync NTP, Set Timezone, Vibrate Window, Notify Interval
#define DEBUG_MENU_LENGTH 2         // Vibrate Motor, Show Accelerometer
// Scrolling window size is computed at runtime as DISPLAY_HEIGHT / row
// height (see uiListVisibleRows() in PicoWatch.cpp) instead of a fixed
// constant, so it fills the screen edge-to-edge (no empty black bar at the
// bottom) at every font size - a fixed row count sized for Default would
// either leave dead space (Small) or overflow the display (Big).
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
// hourly vibrate window (PicoWatch::showVibrateWindowSettings())
#define SET_VIBWIN_FROM    0
#define SET_VIBWIN_TO      1
#define SET_VIBWIN_ENABLED 2
// notification popup/icon settings (PicoWatch::showNotificationSettings())
#define SET_NOTIF_POPUP_ENABLED  0
#define SET_NOTIF_POPUP_DURATION 1
#define SET_NOTIF_ICON_ENABLED   2
#define SET_NOTIF_ICON_LIGHT     3
#define SET_NOTIF_VIBRATE_ENABLED 4
// global vibration strength (PicoWatch::showVibrationSettings()) - drives
// vibMotor()'s PWM duty cycle, see its definition for the actual duty
// values per level. Applies to every vibration in the app (o'clock window,
// alarm, notification popup, reset boot buzz, manual Buzz test), not just
// notifications - Jan wanted one global "how hard does it buzz" setting
// (16.08.2026).
#define SET_VIBRATION_STRENGTH 0
#define VIBRATION_STRENGTH_LOW    0
#define VIBRATION_STRENGTH_MEDIUM 1
#define VIBRATION_STRENGTH_HIGH   2
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
// Gadgetbridge notification forwarding (PicoWatch::_checkBleNotifications(),
// src/Hardware/BLE.cpp's Nordic-UART-based notify service). The advertised
// name MUST match Gadgetbridge's Bangle.js/Espruino device-detection regex
// ("Bangle\.js.*|Pixl\.js.*|Puck\.js.*|MDBT42Q.*|Espruino.*") for it to be
// found and paired as that device type - this is Gadgetbridge's own
// generic DIY protocol, no vendor auth/bonding required. See project
// memory for the researched protocol details.
#define BLE_NOTIFY_DEVICE_NAME "Espruino PicoWatch"
// How often (every Nth minute tick) to open a short BLE advertising window
// to receive any Gadgetbridge-queued notifications, and how long that
// window stays open before giving up - balances "notifications arrive
// reasonably promptly" against "don't keep the radio on more than
// necessary" (deep sleep between ticks is still the main battery saver).
#define BLE_NOTIFY_CHECK_INTERVAL_MIN 5
#define BLE_NOTIFY_WINDOW_MS 12000
// Once a phone actually connects during a check, how much longer to keep
// the radio up (safety cap, not a target) so Gadgetbridge's multi-step
// connect/subscribe/init handshake has room to finish instead of getting
// cut off mid-negotiation - see _checkBleNotifications()'s comment.
#define BLE_NOTIFY_MAX_CONNECTED_MS 60000
// How long a CONNECTED link may sit idle (no write since the connect or
// the last received data) before _checkBleNotifications() closes it early
// instead of waiting out the full MAX_CONNECTED_MS - added 14.08.2026
// after Jan reported the watch draining its battery fast; a connection
// that's already delivered everything and gone quiet has no reason to
// keep the radio up for up to a full minute.
#define BLE_NOTIFY_CONNECTED_IDLE_MS 5000
// Ring buffer of the most recent notifications (PicoWatch::showNotifications()),
// RTC_DATA_ATTR - survives deep sleep, not a true reset (notifications are
// inherently transient, unlike settings).
#define NOTIFICATION_COUNT      8
#define NOTIFICATION_SRC_LEN    24
#define NOTIFICATION_TITLE_LEN  48
#define NOTIFICATION_BODY_LEN   96
// Notification popup/icon behavior (Settings -> "Notification Settings",
// PicoWatch::showNotificationSettings()) - Jan wanted control over
// whether the popup shows at all, how long it stays up, whether the
// top-center watchface icon (_drawNotificationIndicator()) shows, and in
// which color (a white icon is invisible on light-background watchfaces -
// exactly what he ran into, 15.08.2026).
#define NOTIFICATION_POPUP_DURATION_MIN_S 5
#define NOTIFICATION_POPUP_DURATION_MAX_S 30
#define NOTIFICATION_POPUP_DURATION_STEP_S 5
#define NOTIFICATION_POPUP_DURATION_DEFAULT_S 15 // matches the previous hardcoded 15000ms
// Internet Access setting (Settings -> "Internet Access") - lets weather/
// time sync go over Gadgetbridge's phone-proxied HTTP-over-BLE instead of
// the watch's own WiFi (see PicoWatch::_httpViaBle(), project memory
// 15.08.2026 for the researched {"t":"http"} protocol). GitHub OTA update
// deliberately stays WiFi-only - no chunking/flow-control in Gadgetbridge's
// proxy, a ~1.5MB firmware download over it would be thousands of
// unthrottled BLE writes, not practical.
#define INTERNET_ACCESS_WIFI 0
#define INTERNET_ACCESS_BLE  1
// How long to wait for a phone to connect at all before giving up on this
// sync attempt - longer than BLE_NOTIFY_WINDOW_MS since this isn't a
// background poll, it's blocking an actual weather/time update the user
// is waiting on (or that's due right now), and Gadgetbridge might not
// have been in range/foreground at that exact moment.
#define BLE_HTTP_CONNECT_TIMEOUT_MS 20000
// Grace period after connecting before sending the HTTP request - the raw
// GATT connect event fires before Gadgetbridge's own subscribe/MTU-
// negotiate/"initialized" handshake completes (same multi-round-trip
// process described in _checkBleNotifications()'s comment); firing the
// request too early would arrive before Gadgetbridge is ready to route it.
#define BLE_HTTP_INIT_GRACE_MS 2500
// How long to wait for Gadgetbridge's response after sending the request -
// real round trip (BLE relay + the phone's own HTTP request over
// WiFi/cellular), needs more room than a same-device operation would.
#define BLE_HTTP_RESPONSE_TIMEOUT_MS 15000
// Public time API queried via the BLE HTTP proxy when Internet Access is
// set to BLE, in place of true NTP (which needs a UDP socket - not
// something Gadgetbridge's HTTP-only proxy can carry). Auto-detects
// timezone from the requester's (the phone's) IP, matching this project's
// existing "auto-detect timezone from the weather lookup" behavior
// (_getWeatherData()) unless the user set one manually (setTimezone()).
#define BLE_TIME_API_URL "http://worldtimeapi.org/api/ip"
#define SOFTWARE_VERSION_MAJOR 1
#define SOFTWARE_VERSION_MINOR 3
#define SOFTWARE_VERSION_PATCH 1
#define HARDWARE_VERSION_MAJOR 1
#define HARDWARE_VERSION_MINOR 0

#endif