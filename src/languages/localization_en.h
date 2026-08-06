#ifndef PICOWATCH_LOCALIZATION_EN_H
#define PICOWATCH_LOCALIZATION_EN_H

#include "../localization_ids.h"

// ENGLISH TRANSLATIONS - see ../localization.h
//
// One string per PW_STR_* id in ../localization_ids.h, IN THE SAME ORDER -
// pwStr() indexes into this array positionally, not by name. If you add a
// new string, add it to ../localization_ids.h's enum AND to every one of
// these per-language table files at the matching position.
const char *const kLocalizedStrings_en[PW_STR_COUNT] = {
  // Top-level menu (PicoWatch::showMenu()/showFastMenu())
  "Change Watchface",  // PW_MENU_CHANGE_WATCHFACE
  "Stopwatch",  // PW_MENU_STOPWATCH
  "Steps (7 Days)",  // PW_MENU_STEPS
  "Alarm",  // PW_MENU_ALARM
  "Weather (5 Days)",  // PW_MENU_WEATHER
  "Settings",  // PW_MENU_SETTINGS
  // Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
  "About PicoWatch",  // PW_SETTINGS_ABOUT
  "Vibrate Motor",  // PW_SETTINGS_VIBRATE
  "Show Accelerometer",  // PW_SETTINGS_ACCELEROMETER
  "Set Time",  // PW_SETTINGS_SET_TIME
  "WiFi",  // PW_SETTINGS_SETUP_WIFI
  "Sync NTP",  // PW_SETTINGS_SYNC_NTP
  "Set Timezone",  // PW_SETTINGS_SET_TIMEZONE
  "Set City",  // PW_SETTINGS_SET_CITY
  "Online Update",  // PW_SETTINGS_UPDATE_GITHUB
  "Button Settings",  // PW_SETTINGS_BUTTON_SETTINGS
  "Font Size",  // PW_SETTINGS_FONT_SIZE
  "Language",  // PW_SETTINGS_LANGUAGE
  // Stopwatch (PicoWatch::showStopwatch())
  "Stopwatch",  // PW_STOPWATCH_TITLE
  "Up: Reset",  // PW_STOPWATCH_UP_RESET
  "Menu: Stop",  // PW_STOPWATCH_MENU_STOP
  "Menu: Start",  // PW_STOPWATCH_MENU_START
  // Steps (PicoWatch::showStepsHistory())
  "Steps - 7 Days",  // PW_STEPS_TITLE
  // Alarm (PicoWatch::setAlarm())
  "Alarm",  // PW_ALARM_TITLE
  "Enabled: ",  // PW_ALARM_ENABLED_LABEL
  "Yes",  // PW_YES
  "No",  // PW_NO
  // Button Settings (PicoWatch::showButtonSettings())
  "Button Settings",  // PW_BUTTON_SETTINGS_TITLE
  "Swap Menu/Back:",  // PW_BUTTON_SETTINGS_SWAP
  "Up (short):",  // PW_BUTTON_SETTINGS_UP_SHORT
  "Up (long):",  // PW_BUTTON_SETTINGS_UP_LONG
  "Down (short):",  // PW_BUTTON_SETTINGS_DOWN_SHORT
  "Down (long):",  // PW_BUTTON_SETTINGS_DOWN_LONG
  // watchfaceActionName()
  "None",  // PW_ACTION_NONE
  "Settings",  // PW_ACTION_SETTINGS
  "Change Watchface",  // PW_ACTION_CHANGE_WATCHFACE
  "Weather",  // PW_ACTION_WEATHER
  "Stopwatch",  // PW_ACTION_STOPWATCH
  "Alarm",  // PW_ACTION_ALARM
  // Font Size (PicoWatch::showFontSizeSettings())
  "Font Size",  // PW_FONT_SIZE_TITLE
  "(menu + settings)",  // PW_FONT_SIZE_SUBTITLE
  "Small",  // PW_FONT_SIZE_SMALL
  "Default",  // PW_FONT_SIZE_DEFAULT
  "Big",  // PW_FONT_SIZE_BIG
  // Weather forecast (PicoWatch::showWeatherForecast())
  "Loading...",  // PW_WEATHER_LOADING
  "Check WiFi and the",  // PW_WEATHER_CHECK_WIFI
  "weather API key.",  // PW_WEATHER_CHECK_API_KEY
  // weatherConditionLabel()
  "Cloudy",  // PW_WEATHER_COND_CLOUDY
  "Few Clouds",  // PW_WEATHER_COND_FEW_CLOUDS
  "Clear",  // PW_WEATHER_COND_CLEAR
  "Haze",  // PW_WEATHER_COND_HAZE
  "Snow",  // PW_WEATHER_COND_SNOW
  "Rain",  // PW_WEATHER_COND_RAIN
  "Drizzle",  // PW_WEATHER_COND_DRIZZLE
  "Storm",  // PW_WEATHER_COND_STORM
  "?",  // PW_WEATHER_COND_UNKNOWN
  // About (PicoWatch::showAbout())
  "LibVer: ",  // PW_ABOUT_LIBVER
  "Rev: v",  // PW_ABOUT_REV
  "Batt: ",  // PW_ABOUT_BATT
  "V",  // PW_ABOUT_VOLT_UNIT
  "Uptime: ",  // PW_ABOUT_UPTIME
  "d",  // PW_ABOUT_DAYS
  "h",  // PW_ABOUT_HOURS
  "m",  // PW_ABOUT_MINUTES
  "SSID: ",  // PW_ABOUT_SSID
  "IP: ",  // PW_ABOUT_IP
  "WiFi Not Connected",  // PW_ABOUT_WIFI_NOT_CONNECTED
  // GitHub Update (PicoWatch::updateFromGithub())
  "Checking GitHub...",  // PW_GITHUB_CHECKING
  "WiFi not connected.",  // PW_GITHUB_WIFI_NOT_CONNECTED
  "No release found",  // PW_GITHUB_NO_RELEASE
  "or network error.",  // PW_GITHUB_NETWORK_ERROR
  "Bad release data.",  // PW_GITHUB_BAD_DATA
  "Already on the",  // PW_GITHUB_ALREADY_LATEST_1
  "latest version:",  // PW_GITHUB_ALREADY_LATEST_2
  "Asset not found",  // PW_GITHUB_ASSET_NOT_FOUND
  "in latest release.",  // PW_GITHUB_IN_LATEST_RELEASE
  "Downloading:",  // PW_GITHUB_DOWNLOADING
  "Download failed.",  // PW_GITHUB_DOWNLOAD_FAILED
  "Not enough space",  // PW_GITHUB_NOT_ENOUGH_SPACE
  "for update.",  // PW_GITHUB_FOR_UPDATE
  "Update verify",  // PW_GITHUB_VERIFY_1
  "failed - aborted.",  // PW_GITHUB_VERIFY_2
  "Update failed",  // PW_GITHUB_FAILED_1
  "while finalizing.",  // PW_GITHUB_FAILED_2
  "Update verified.",  // PW_GITHUB_VERIFIED
  "Rebooting...",  // PW_GITHUB_REBOOTING
  // Buzz (PicoWatch::showBuzz())
  "Buzz!",  // PW_BUZZ
  // Set City (PicoWatch::setWeatherCity())
  "Set City ID",  // PW_SET_CITY_TITLE
  "Find your city ID at",  // PW_SET_CITY_FIND_1
  // Accelerometer debug (PicoWatch::showAccelerometer())
  "getAccel FAIL",  // PW_ACCEL_FAIL
  "FACE DOWN",  // PW_ACCEL_FACE_DOWN
  "FACE UP",  // PW_ACCEL_FACE_UP
  "BOTTOM EDGE",  // PW_ACCEL_BOTTOM_EDGE
  "TOP EDGE",  // PW_ACCEL_TOP_EDGE
  "RIGHT EDGE",  // PW_ACCEL_RIGHT_EDGE
  "LEFT EDGE",  // PW_ACCEL_LEFT_EDGE
  "ERROR!!!",  // PW_ACCEL_ERROR
  // WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
  "Connecting...",  // PW_WIFI_CONNECTING
  "Connect phone to:",  // PW_WIFI_CONNECT_PHONE_TO
  "(password on next",  // PW_WIFI_PASSWORD_NEXT_1
  "screen)",  // PW_WIFI_PASSWORD_NEXT_2
  "Setup failed &",  // PW_WIFI_SETUP_FAILED
  "timed out!",  // PW_WIFI_TIMED_OUT
  "Connected to:",  // PW_WIFI_CONNECTED_TO
  "Open in browser:",  // PW_WIFI_OPEN_IN_BROWSER
  "Back to disconnect",  // PW_WIFI_BACK_TO_DISCONNECT
  "Receiving update",  // PW_WIFI_RECEIVING_UPDATE_1
  "via File Update...",  // PW_WIFI_RECEIVING_UPDATE_2
  "Connect to",  // PW_WIFI_AP_CONNECT_TO
  "SSID: ",  // PW_WIFI_AP_SSID_LABEL
  "Pass: ",  // PW_WIFI_AP_PASS_LABEL
  "IP: ",  // PW_WIFI_AP_IP_LABEL
  // Sync NTP (PicoWatch::showSyncNTP())
  "Syncing NTP... ",  // PW_NTP_SYNCING
  "GMT offset: ",  // PW_NTP_GMT_OFFSET
  "NTP Sync Success\n",  // PW_NTP_SUCCESS
  "Current Time Is:",  // PW_NTP_CURRENT_TIME
  "NTP Sync Failed",  // PW_NTP_FAILED
  "WiFi Not Configured",  // PW_NTP_WIFI_NOT_CONFIGURED
  // Games menu (PicoWatch::showGamesMenu()/showFastGamesMenu())
  "Games",  // PW_STR_MENU_GAMES
  "Snake",  // PW_STR_GAME_SNAKE
  "Pong",  // PW_STR_GAME_PONG
  "Tetris",  // PW_STR_GAME_TETRIS
  "Flappy",  // PW_STR_GAME_FLAPPY
  "Paused",  // PW_STR_GAME_PAUSED
  "Game Over",  // PW_STR_GAME_OVER
  "Score: ",  // PW_STR_GAME_SCORE_LABEL
  "Coming soon",  // PW_STR_GAME_COMING_SOON
};

// 7-day steps history labels (separate from the table above since it's an
// array-of-strings, not a single string) - see PicoWatch::showStepsHistory().
const char *const kLocalizedDayLabels_en[7] = {"Yesterday", "2 days ago", "3 days ago", "4 days ago", "5 days ago", "6 days ago", "7 days ago"};

#endif
