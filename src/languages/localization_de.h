#ifndef PICOWATCH_LOCALIZATION_DE_H
#define PICOWATCH_LOCALIZATION_DE_H

#include "../localization_ids.h"

// GERMAN TRANSLATIONS - see ../localization.h
//
// Deliberately ASCII-only (ae/oe/ue/ss instead of ae/oe/ue/ss) - same
// convention InkWatchy's own localization_de.h uses, because the bundled
// Adafruit GFX fonts (FreeMonoBold9pt7b etc.) only cover the printable
// ASCII range (0x20-0x7E), not Latin-1 umlauts. A literal ae/oe/ue/ss
// here would just render as garbage/missing glyphs on-device.
//
// One string per PW_STR_* id in ../localization_ids.h, IN THE SAME ORDER -
// pwStr() indexes into this array positionally, not by name. If you add a
// new string, add it to ../localization_ids.h's enum AND to every one of
// these per-language table files at the matching position.
const char *const kLocalizedStrings_de[PW_STR_COUNT] = {
  // Top-level menu (PicoWatch::showMenu()/showFastMenu())
  "Zifferblatt",  // PW_MENU_CHANGE_WATCHFACE
  "Stoppuhr",  // PW_MENU_STOPWATCH
  "Schritte (7 Tage)",  // PW_MENU_STEPS
  "Wecker",  // PW_MENU_ALARM
  "Wetter (5 Tage)",  // PW_MENU_WEATHER
  "Einstellungen",  // PW_MENU_SETTINGS
  // Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
  "Ueber PicoWatch",  // PW_SETTINGS_ABOUT
  "Vibrationsmotor",  // PW_SETTINGS_VIBRATE
  "Beschleunigungssensor",  // PW_SETTINGS_ACCELEROMETER
  "Uhrzeit einstellen",  // PW_SETTINGS_SET_TIME
  "WLAN",  // PW_SETTINGS_SETUP_WIFI
  "NTP synchron.",  // PW_SETTINGS_SYNC_NTP
  "Zeitzone",  // PW_SETTINGS_SET_TIMEZONE
  "Stadt einstellen",  // PW_SETTINGS_SET_CITY
  "Online-Update",  // PW_SETTINGS_UPDATE_GITHUB
  "Tasten-Einst.",  // PW_SETTINGS_BUTTON_SETTINGS
  "Schriftgroesse",  // PW_SETTINGS_FONT_SIZE
  "Sprache",  // PW_SETTINGS_LANGUAGE
  // Stopwatch (PicoWatch::showStopwatch())
  "Stoppuhr",  // PW_STOPWATCH_TITLE
  "Hoch: Zuruecksetzen",  // PW_STOPWATCH_UP_RESET
  "Menu: Stopp",  // PW_STOPWATCH_MENU_STOP
  "Menu: Start",  // PW_STOPWATCH_MENU_START
  // Steps (PicoWatch::showStepsHistory())
  "Schritte - 7 Tage",  // PW_STEPS_TITLE
  // Alarm (PicoWatch::setAlarm())
  "Wecker",  // PW_ALARM_TITLE
  "Aktiv: ",  // PW_ALARM_ENABLED_LABEL
  "Ja",  // PW_YES
  "Nein",  // PW_NO
  // Button Settings (PicoWatch::showButtonSettings())
  "Tasten-Einstellungen",  // PW_BUTTON_SETTINGS_TITLE
  "Menu/Zurueck tauschen:",  // PW_BUTTON_SETTINGS_SWAP
  "Hoch (kurz):",  // PW_BUTTON_SETTINGS_UP_SHORT
  "Hoch (lang):",  // PW_BUTTON_SETTINGS_UP_LONG
  "Runter (kurz):",  // PW_BUTTON_SETTINGS_DOWN_SHORT
  "Runter (lang):",  // PW_BUTTON_SETTINGS_DOWN_LONG
  // watchfaceActionName()
  "Keine",  // PW_ACTION_NONE
  "Einstellungen",  // PW_ACTION_SETTINGS
  "Zifferblatt",  // PW_ACTION_CHANGE_WATCHFACE
  "Wetter",  // PW_ACTION_WEATHER
  "Stoppuhr",  // PW_ACTION_STOPWATCH
  "Wecker",  // PW_ACTION_ALARM
  // Font Size (PicoWatch::showFontSizeSettings())
  "Schriftgroesse",  // PW_FONT_SIZE_TITLE
  "(Menue + Einstellungen)",  // PW_FONT_SIZE_SUBTITLE
  "Klein",  // PW_FONT_SIZE_SMALL
  "Standard",  // PW_FONT_SIZE_DEFAULT
  "Gross",  // PW_FONT_SIZE_BIG
  // Weather forecast (PicoWatch::showWeatherForecast())
  "Laedt...",  // PW_WEATHER_LOADING
  "WLAN und Wetter-API-",  // PW_WEATHER_CHECK_WIFI
  "Schluessel pruefen.",  // PW_WEATHER_CHECK_API_KEY
  // weatherConditionLabel()
  "Bewoelkt",  // PW_WEATHER_COND_CLOUDY
  "Leicht bewoelkt",  // PW_WEATHER_COND_FEW_CLOUDS
  "Klar",  // PW_WEATHER_COND_CLEAR
  "Dunst",  // PW_WEATHER_COND_HAZE
  "Schnee",  // PW_WEATHER_COND_SNOW
  "Regen",  // PW_WEATHER_COND_RAIN
  "Nieselregen",  // PW_WEATHER_COND_DRIZZLE
  "Sturm",  // PW_WEATHER_COND_STORM
  "?",  // PW_WEATHER_COND_UNKNOWN
  // About (PicoWatch::showAbout())
  "LibVer: ",  // PW_ABOUT_LIBVER
  "Rev: v",  // PW_ABOUT_REV
  "Akku: ",  // PW_ABOUT_BATT
  "V",  // PW_ABOUT_VOLT_UNIT
  "Laufzeit: ",  // PW_ABOUT_UPTIME
  "T",  // PW_ABOUT_DAYS
  "Std",  // PW_ABOUT_HOURS
  "Min",  // PW_ABOUT_MINUTES
  "SSID: ",  // PW_ABOUT_SSID
  "IP: ",  // PW_ABOUT_IP
  "WLAN nicht verbunden",  // PW_ABOUT_WIFI_NOT_CONNECTED
  // GitHub Update (PicoWatch::updateFromGithub())
  "Pruefe GitHub...",  // PW_GITHUB_CHECKING
  "WLAN nicht verbunden.",  // PW_GITHUB_WIFI_NOT_CONNECTED
  "Kein Release gefunden",  // PW_GITHUB_NO_RELEASE
  "oder Netzwerkfehler.",  // PW_GITHUB_NETWORK_ERROR
  "Ungueltige Release-Daten.",  // PW_GITHUB_BAD_DATA
  "Bereits auf der",  // PW_GITHUB_ALREADY_LATEST_1
  "neusten Version:",  // PW_GITHUB_ALREADY_LATEST_2
  "Datei nicht gefunden",  // PW_GITHUB_ASSET_NOT_FOUND
  "im neusten Release.",  // PW_GITHUB_IN_LATEST_RELEASE
  "Lade herunter:",  // PW_GITHUB_DOWNLOADING
  "Download fehlgeschlagen.",  // PW_GITHUB_DOWNLOAD_FAILED
  "Nicht genug Speicher",  // PW_GITHUB_NOT_ENOUGH_SPACE
  "fuer Update.",  // PW_GITHUB_FOR_UPDATE
  "Pruefung",  // PW_GITHUB_VERIFY_1
  "fehlgeschlagen - abgebrochen.",  // PW_GITHUB_VERIFY_2
  "Update fehlgeschlagen",  // PW_GITHUB_FAILED_1
  "beim Abschliessen.",  // PW_GITHUB_FAILED_2
  "Update geprueft.",  // PW_GITHUB_VERIFIED
  "Neustart...",  // PW_GITHUB_REBOOTING
  // Buzz (PicoWatch::showBuzz())
  "Brumm!",  // PW_BUZZ
  // Set City (PicoWatch::setWeatherCity())
  "Stadt-ID einstellen",  // PW_SET_CITY_TITLE
  "Stadt-ID finden auf",  // PW_SET_CITY_FIND_1
  // Accelerometer debug (PicoWatch::showAccelerometer())
  "getAccel FEHLER",  // PW_ACCEL_FAIL
  "UNTEN",  // PW_ACCEL_FACE_DOWN
  "OBEN",  // PW_ACCEL_FACE_UP
  "UNTERE KANTE",  // PW_ACCEL_BOTTOM_EDGE
  "OBERE KANTE",  // PW_ACCEL_TOP_EDGE
  "RECHTE KANTE",  // PW_ACCEL_RIGHT_EDGE
  "LINKE KANTE",  // PW_ACCEL_LEFT_EDGE
  "FEHLER!!!",  // PW_ACCEL_ERROR
  // WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
  "Verbinde...",  // PW_WIFI_CONNECTING
  "Handy verbinden mit:",  // PW_WIFI_CONNECT_PHONE_TO
  "(Passwort auf dem",  // PW_WIFI_PASSWORD_NEXT_1
  "naechsten Bildschirm)",  // PW_WIFI_PASSWORD_NEXT_2
  "Einrichtung",  // PW_WIFI_SETUP_FAILED
  "fehlgeschlagen/Zeit abgelaufen!",  // PW_WIFI_TIMED_OUT
  "Verbunden mit:",  // PW_WIFI_CONNECTED_TO
  "Im Browser oeffnen:",  // PW_WIFI_OPEN_IN_BROWSER
  "Zurueck zum Trennen",  // PW_WIFI_BACK_TO_DISCONNECT
  "Empfange Update",  // PW_WIFI_RECEIVING_UPDATE_1
  "via File Update...",  // PW_WIFI_RECEIVING_UPDATE_2
  "Verbinden mit",  // PW_WIFI_AP_CONNECT_TO
  "SSID: ",  // PW_WIFI_AP_SSID_LABEL
  "Passwort: ",  // PW_WIFI_AP_PASS_LABEL
  "IP: ",  // PW_WIFI_AP_IP_LABEL
  // Sync NTP (PicoWatch::showSyncNTP())
  "Synchronisiere NTP... ",  // PW_NTP_SYNCING
  "GMT-Offset: ",  // PW_NTP_GMT_OFFSET
  "NTP-Sync erfolgreich\n",  // PW_NTP_SUCCESS
  "Aktuelle Zeit:",  // PW_NTP_CURRENT_TIME
  "NTP-Sync fehlgeschlagen",  // PW_NTP_FAILED
  "WLAN nicht eingerichtet",  // PW_NTP_WIFI_NOT_CONFIGURED
  // Games menu (PicoWatch::showGamesMenu()/showFastGamesMenu())
  "Spiele",  // PW_STR_MENU_GAMES
  "Snake",  // PW_STR_GAME_SNAKE
  "Pong",  // PW_STR_GAME_PONG
  "Tetris",  // PW_STR_GAME_TETRIS
  "Flappy",  // PW_STR_GAME_FLAPPY
  "Pausiert",  // PW_STR_GAME_PAUSED
  "Game Over",  // PW_STR_GAME_OVER
  "Punkte: ",  // PW_STR_GAME_SCORE_LABEL
  "Demnaechst",  // PW_STR_GAME_COMING_SOON
  // Time submenu (PicoWatch::showTimeMenu()/showFastTimeMenu())
  "Zeit",  // PW_STR_SETTINGS_TIME
  "Vibrations-Fenster",  // PW_STR_TIME_VIBRATE_WINDOW
  "Stuendl. Vibration",  // PW_STR_VIBWIN_TITLE
  "Von: ",  // PW_STR_VIBWIN_FROM_LABEL
  "Bis: ",  // PW_STR_VIBWIN_TO_LABEL
};

// 7-day steps history labels (separate from the table above since it's an
// array-of-strings, not a single string) - see PicoWatch::showStepsHistory().
const char *const kLocalizedDayLabels_de[7] = {"Gestern", "Vor 2 Tagen", "Vor 3 Tagen", "Vor 4 Tagen", "Vor 5 Tagen", "Vor 6 Tagen", "Vor 7 Tagen"};

#endif
