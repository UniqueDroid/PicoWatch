#ifndef PICOWATCH_LOCALIZATION_DE_H
#define PICOWATCH_LOCALIZATION_DE_H

// GERMAN TRANSLATIONS - see ../localization.h
//
// Deliberately ASCII-only (ae/oe/ue/ss instead of ä/ö/ü/ß) - same
// convention InkWatchy's own localization_de.h uses, because the bundled
// Adafruit GFX fonts (FreeMonoBold9pt7b etc.) only cover the printable
// ASCII range (0x20-0x7E), not Latin-1 umlauts. A literal ä/ö/ü/ß in a
// #define here would just render as garbage/missing glyphs on-device.

// Top-level menu (PicoWatch::showMenu()/showFastMenu())
#define PW_MENU_CHANGE_WATCHFACE "Zifferblatt"
#define PW_MENU_STOPWATCH "Stoppuhr"
#define PW_MENU_STEPS "Schritte (7 Tage)"
#define PW_MENU_ALARM "Wecker"
#define PW_MENU_WEATHER "Wetter (5 Tage)"
#define PW_MENU_SETTINGS "Einstellungen"

// Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
#define PW_SETTINGS_ABOUT "Ueber PicoWatch"
#define PW_SETTINGS_VIBRATE "Vibrationsmotor"
#define PW_SETTINGS_ACCELEROMETER "Beschleunigungssensor"
#define PW_SETTINGS_SET_TIME "Uhrzeit einstellen"
#define PW_SETTINGS_SETUP_WIFI "WLAN einrichten"
#define PW_SETTINGS_SYNC_NTP "NTP synchron."
#define PW_SETTINGS_SET_TIMEZONE "Zeitzone"
#define PW_SETTINGS_SET_CITY "Stadt einstellen"
#define PW_SETTINGS_UPDATE_GITHUB "Update via GitHub"
#define PW_SETTINGS_BUTTON_SETTINGS "Tasten-Einst."
#define PW_SETTINGS_FONT_SIZE "Schriftgroesse"

// Stopwatch (PicoWatch::showStopwatch())
#define PW_STOPWATCH_TITLE "Stoppuhr"
#define PW_STOPWATCH_UP_RESET "Hoch: Zuruecksetzen"
#define PW_STOPWATCH_MENU_STOP "Menu: Stopp"
#define PW_STOPWATCH_MENU_START "Menu: Start"

// Steps (PicoWatch::showStepsHistory())
#define PW_STEPS_TITLE "Schritte - 7 Tage"
#define PW_STEPS_DAY_LABELS {"Gestern", "Vor 2 Tagen", "Vor 3 Tagen", "Vor 4 Tagen", "Vor 5 Tagen", "Vor 6 Tagen", "Vor 7 Tagen"}

// Alarm (PicoWatch::setAlarm())
#define PW_ALARM_TITLE "Wecker"
#define PW_ALARM_ENABLED_LABEL "Aktiv: "
#define PW_YES "Ja"
#define PW_NO "Nein"

// Button Settings (PicoWatch::showButtonSettings())
#define PW_BUTTON_SETTINGS_TITLE "Tasten-Einstellungen"
#define PW_BUTTON_SETTINGS_SWAP "Menu/Zurueck tauschen:"
#define PW_BUTTON_SETTINGS_UP_SHORT "Hoch (kurz):"
#define PW_BUTTON_SETTINGS_UP_LONG "Hoch (lang):"
#define PW_BUTTON_SETTINGS_DOWN_SHORT "Runter (kurz):"
#define PW_BUTTON_SETTINGS_DOWN_LONG "Runter (lang):"
// watchfaceActionName()
#define PW_ACTION_NONE "Keine"
#define PW_ACTION_SETTINGS "Einstellungen"
#define PW_ACTION_CHANGE_WATCHFACE "Zifferblatt"
#define PW_ACTION_WEATHER "Wetter"
#define PW_ACTION_STOPWATCH "Stoppuhr"
#define PW_ACTION_ALARM "Wecker"

// Font Size (PicoWatch::showFontSizeSettings())
#define PW_FONT_SIZE_TITLE "Schriftgroesse"
#define PW_FONT_SIZE_SUBTITLE "(Menue + Einstellungen)"
#define PW_FONT_SIZE_SMALL "Klein"
#define PW_FONT_SIZE_DEFAULT "Standard"
#define PW_FONT_SIZE_BIG "Gross"

// Weather forecast (PicoWatch::showWeatherForecast())
#define PW_WEATHER_LOADING "Laedt..."
#define PW_WEATHER_CHECK_WIFI "WLAN und Wetter-API-"
#define PW_WEATHER_CHECK_API_KEY "Schluessel pruefen."
// weatherConditionLabel()
#define PW_WEATHER_COND_CLOUDY "Bewoelkt"
#define PW_WEATHER_COND_FEW_CLOUDS "Leicht bewoelkt"
#define PW_WEATHER_COND_CLEAR "Klar"
#define PW_WEATHER_COND_HAZE "Dunst"
#define PW_WEATHER_COND_SNOW "Schnee"
#define PW_WEATHER_COND_RAIN "Regen"
#define PW_WEATHER_COND_DRIZZLE "Nieselregen"
#define PW_WEATHER_COND_STORM "Sturm"
#define PW_WEATHER_COND_UNKNOWN "?"

// About (PicoWatch::showAbout())
#define PW_ABOUT_LIBVER "LibVer: "
#define PW_ABOUT_REV "Rev: v"
#define PW_ABOUT_BATT "Akku: "
#define PW_ABOUT_VOLT_UNIT "V"
#define PW_ABOUT_UPTIME "Laufzeit: "
#define PW_ABOUT_DAYS "T"
#define PW_ABOUT_HOURS "Std"
#define PW_ABOUT_MINUTES "Min"
#define PW_ABOUT_SSID "SSID: "
#define PW_ABOUT_IP "IP: "
#define PW_ABOUT_WIFI_NOT_CONNECTED "WLAN nicht verbunden"

// GitHub Update (PicoWatch::updateFromGithub())
#define PW_GITHUB_CHECKING "Pruefe GitHub..."
#define PW_GITHUB_WIFI_NOT_CONNECTED "WLAN nicht verbunden."
#define PW_GITHUB_NO_RELEASE "Kein Release gefunden"
#define PW_GITHUB_NETWORK_ERROR "oder Netzwerkfehler."
#define PW_GITHUB_BAD_DATA "Ungueltige Release-Daten."
#define PW_GITHUB_ALREADY_LATEST_1 "Bereits auf der"
#define PW_GITHUB_ALREADY_LATEST_2 "neusten Version:"
#define PW_GITHUB_ASSET_NOT_FOUND "Datei nicht gefunden"
#define PW_GITHUB_IN_LATEST_RELEASE "im neusten Release."
#define PW_GITHUB_DOWNLOADING "Lade herunter:"
#define PW_GITHUB_DOWNLOAD_FAILED "Download fehlgeschlagen."
#define PW_GITHUB_NOT_ENOUGH_SPACE "Nicht genug Speicher"
#define PW_GITHUB_FOR_UPDATE "fuer Update."
#define PW_GITHUB_VERIFY_1 "Pruefung"
#define PW_GITHUB_VERIFY_2 "fehlgeschlagen - abgebrochen."
#define PW_GITHUB_FAILED_1 "Update fehlgeschlagen"
#define PW_GITHUB_FAILED_2 "beim Abschliessen."
#define PW_GITHUB_VERIFIED "Update geprueft."
#define PW_GITHUB_REBOOTING "Neustart..."

// Buzz (PicoWatch::showBuzz())
#define PW_BUZZ "Brumm!"

// Set City (PicoWatch::setWeatherCity())
#define PW_SET_CITY_TITLE "Stadt-ID einstellen"
#define PW_SET_CITY_FIND_1 "Stadt-ID finden auf"

// Accelerometer debug (PicoWatch::showAccelerometer())
#define PW_ACCEL_FAIL "getAccel FEHLER"
#define PW_ACCEL_FACE_DOWN "UNTEN"
#define PW_ACCEL_FACE_UP "OBEN"
#define PW_ACCEL_BOTTOM_EDGE "UNTERE KANTE"
#define PW_ACCEL_TOP_EDGE "OBERE KANTE"
#define PW_ACCEL_RIGHT_EDGE "RECHTE KANTE"
#define PW_ACCEL_LEFT_EDGE "LINKE KANTE"
#define PW_ACCEL_ERROR "FEHLER!!!"

// WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
#define PW_WIFI_CONNECTING "Verbinde..."
#define PW_WIFI_CONNECT_PHONE_TO "Handy verbinden mit:"
#define PW_WIFI_PASSWORD_NEXT_1 "(Passwort auf dem"
#define PW_WIFI_PASSWORD_NEXT_2 "naechsten Bildschirm)"
#define PW_WIFI_SETUP_FAILED "Einrichtung"
#define PW_WIFI_TIMED_OUT "fehlgeschlagen/Zeit abgelaufen!"
#define PW_WIFI_CONNECTED_TO "Verbunden mit:"
#define PW_WIFI_OPEN_IN_BROWSER "Im Browser oeffnen:"
#define PW_WIFI_BACK_TO_DISCONNECT "Zurueck zum Trennen"
#define PW_WIFI_RECEIVING_UPDATE_1 "Empfange Update"
#define PW_WIFI_RECEIVING_UPDATE_2 "via File Update..."
#define PW_WIFI_AP_CONNECT_TO "Verbinden mit"
#define PW_WIFI_AP_SSID_LABEL "SSID: "
#define PW_WIFI_AP_PASS_LABEL "Passwort: "
#define PW_WIFI_AP_IP_LABEL "IP: "

// Sync NTP (PicoWatch::showSyncNTP())
#define PW_NTP_SYNCING "Synchronisiere NTP... "
#define PW_NTP_GMT_OFFSET "GMT-Offset: "
#define PW_NTP_SUCCESS "NTP-Sync erfolgreich\n"
#define PW_NTP_CURRENT_TIME "Aktuelle Zeit:"
#define PW_NTP_FAILED "NTP-Sync fehlgeschlagen"
#define PW_NTP_WIFI_NOT_CONFIGURED "WLAN nicht eingerichtet"

#endif
