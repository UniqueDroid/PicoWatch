#ifndef PICOWATCH_LOCALIZATION_EN_H
#define PICOWATCH_LOCALIZATION_EN_H

// ENGLISH TRANSLATIONS - see ../localization.h

// Top-level menu (PicoWatch::showMenu()/showFastMenu())
#define PW_MENU_CHANGE_WATCHFACE "Change Watchface"
#define PW_MENU_STOPWATCH "Stopwatch"
#define PW_MENU_STEPS "Steps (7 Days)"
#define PW_MENU_ALARM "Alarm"
#define PW_MENU_WEATHER "Weather (5 Days)"
#define PW_MENU_SETTINGS "Settings"

// Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
#define PW_SETTINGS_ABOUT "About PicoWatch"
#define PW_SETTINGS_VIBRATE "Vibrate Motor"
#define PW_SETTINGS_ACCELEROMETER "Show Accelerometer"
#define PW_SETTINGS_SET_TIME "Set Time"
#define PW_SETTINGS_SETUP_WIFI "Setup WiFi"
#define PW_SETTINGS_SYNC_NTP "Sync NTP"
#define PW_SETTINGS_SET_TIMEZONE "Set Timezone"
#define PW_SETTINGS_SET_CITY "Set City"
#define PW_SETTINGS_UPDATE_GITHUB "Update via GitHub"
#define PW_SETTINGS_BUTTON_SETTINGS "Button Settings"
#define PW_SETTINGS_FONT_SIZE "Font Size"

// Stopwatch (PicoWatch::showStopwatch())
#define PW_STOPWATCH_TITLE "Stopwatch"
#define PW_STOPWATCH_UP_RESET "Up: Reset"
#define PW_STOPWATCH_MENU_STOP "Menu: Stop"
#define PW_STOPWATCH_MENU_START "Menu: Start"

// Steps (PicoWatch::showStepsHistory())
#define PW_STEPS_TITLE "Steps - 7 Days"
#define PW_STEPS_DAY_LABELS {"Yesterday", "2 days ago", "3 days ago", "4 days ago", "5 days ago", "6 days ago", "7 days ago"}

// Alarm (PicoWatch::setAlarm())
#define PW_ALARM_TITLE "Alarm"
#define PW_ALARM_ENABLED_LABEL "Enabled: "
#define PW_YES "Yes"
#define PW_NO "No"

// Button Settings (PicoWatch::showButtonSettings())
#define PW_BUTTON_SETTINGS_TITLE "Button Settings"
#define PW_BUTTON_SETTINGS_SWAP "Swap Menu/Back:"
#define PW_BUTTON_SETTINGS_UP_SHORT "Up (short):"
#define PW_BUTTON_SETTINGS_UP_LONG "Up (long):"
#define PW_BUTTON_SETTINGS_DOWN_SHORT "Down (short):"
#define PW_BUTTON_SETTINGS_DOWN_LONG "Down (long):"
// watchfaceActionName()
#define PW_ACTION_NONE "None"
#define PW_ACTION_SETTINGS "Settings"
#define PW_ACTION_CHANGE_WATCHFACE "Change Watchface"
#define PW_ACTION_WEATHER "Weather"
#define PW_ACTION_STOPWATCH "Stopwatch"
#define PW_ACTION_ALARM "Alarm"

// Font Size (PicoWatch::showFontSizeSettings())
#define PW_FONT_SIZE_TITLE "Font Size"
#define PW_FONT_SIZE_SUBTITLE "(menu + settings)"
#define PW_FONT_SIZE_SMALL "Small"
#define PW_FONT_SIZE_DEFAULT "Default"
#define PW_FONT_SIZE_BIG "Big"

// Weather forecast (PicoWatch::showWeatherForecast())
#define PW_WEATHER_LOADING "Loading..."
#define PW_WEATHER_CHECK_WIFI "Check WiFi and the"
#define PW_WEATHER_CHECK_API_KEY "weather API key."
// weatherConditionLabel()
#define PW_WEATHER_COND_CLOUDY "Cloudy"
#define PW_WEATHER_COND_FEW_CLOUDS "Few Clouds"
#define PW_WEATHER_COND_CLEAR "Clear"
#define PW_WEATHER_COND_HAZE "Haze"
#define PW_WEATHER_COND_SNOW "Snow"
#define PW_WEATHER_COND_RAIN "Rain"
#define PW_WEATHER_COND_DRIZZLE "Drizzle"
#define PW_WEATHER_COND_STORM "Storm"
#define PW_WEATHER_COND_UNKNOWN "?"

// About (PicoWatch::showAbout())
#define PW_ABOUT_LIBVER "LibVer: "
#define PW_ABOUT_REV "Rev: v"
#define PW_ABOUT_BATT "Batt: "
#define PW_ABOUT_VOLT_UNIT "V"
#define PW_ABOUT_UPTIME "Uptime: "
#define PW_ABOUT_DAYS "d"
#define PW_ABOUT_HOURS "h"
#define PW_ABOUT_MINUTES "m"
#define PW_ABOUT_SSID "SSID: "
#define PW_ABOUT_IP "IP: "
#define PW_ABOUT_WIFI_NOT_CONNECTED "WiFi Not Connected"

// GitHub Update (PicoWatch::updateFromGithub())
#define PW_GITHUB_CHECKING "Checking GitHub..."
#define PW_GITHUB_WIFI_NOT_CONNECTED "WiFi not connected."
#define PW_GITHUB_NO_RELEASE "No release found"
#define PW_GITHUB_NETWORK_ERROR "or network error."
#define PW_GITHUB_BAD_DATA "Bad release data."
#define PW_GITHUB_ALREADY_LATEST_1 "Already on the"
#define PW_GITHUB_ALREADY_LATEST_2 "latest version:"
#define PW_GITHUB_ASSET_NOT_FOUND "Asset not found"
#define PW_GITHUB_IN_LATEST_RELEASE "in latest release."
#define PW_GITHUB_DOWNLOADING "Downloading:"
#define PW_GITHUB_DOWNLOAD_FAILED "Download failed."
#define PW_GITHUB_NOT_ENOUGH_SPACE "Not enough space"
#define PW_GITHUB_FOR_UPDATE "for update."
#define PW_GITHUB_VERIFY_1 "Update verify"
#define PW_GITHUB_VERIFY_2 "failed - aborted."
#define PW_GITHUB_FAILED_1 "Update failed"
#define PW_GITHUB_FAILED_2 "while finalizing."
#define PW_GITHUB_VERIFIED "Update verified."
#define PW_GITHUB_REBOOTING "Rebooting..."

// Buzz (PicoWatch::showBuzz())
#define PW_BUZZ "Buzz!"

// Set City (PicoWatch::setWeatherCity())
#define PW_SET_CITY_TITLE "Set City ID"
#define PW_SET_CITY_FIND_1 "Find your city ID at"

// Accelerometer debug (PicoWatch::showAccelerometer())
#define PW_ACCEL_FAIL "getAccel FAIL"
#define PW_ACCEL_FACE_DOWN "FACE DOWN"
#define PW_ACCEL_FACE_UP "FACE UP"
#define PW_ACCEL_BOTTOM_EDGE "BOTTOM EDGE"
#define PW_ACCEL_TOP_EDGE "TOP EDGE"
#define PW_ACCEL_RIGHT_EDGE "RIGHT EDGE"
#define PW_ACCEL_LEFT_EDGE "LEFT EDGE"
#define PW_ACCEL_ERROR "ERROR!!!"

// WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
#define PW_WIFI_CONNECTING "Connecting..."
#define PW_WIFI_CONNECT_PHONE_TO "Connect phone to:"
#define PW_WIFI_PASSWORD_NEXT_1 "(password on next"
#define PW_WIFI_PASSWORD_NEXT_2 "screen)"
#define PW_WIFI_SETUP_FAILED "Setup failed &"
#define PW_WIFI_TIMED_OUT "timed out!"
#define PW_WIFI_CONNECTED_TO "Connected to:"
#define PW_WIFI_OPEN_IN_BROWSER "Open in browser:"
#define PW_WIFI_BACK_TO_DISCONNECT "Back to disconnect"
#define PW_WIFI_RECEIVING_UPDATE_1 "Receiving update"
#define PW_WIFI_RECEIVING_UPDATE_2 "via File Update..."
#define PW_WIFI_AP_CONNECT_TO "Connect to"
#define PW_WIFI_AP_SSID_LABEL "SSID: "
#define PW_WIFI_AP_PASS_LABEL "Pass: "
#define PW_WIFI_AP_IP_LABEL "IP: "

// Sync NTP (PicoWatch::showSyncNTP())
#define PW_NTP_SYNCING "Syncing NTP... "
#define PW_NTP_GMT_OFFSET "GMT offset: "
#define PW_NTP_SUCCESS "NTP Sync Success\n"
#define PW_NTP_CURRENT_TIME "Current Time Is:"
#define PW_NTP_FAILED "NTP Sync Failed"
#define PW_NTP_WIFI_NOT_CONFIGURED "WiFi Not Configured"

#endif
