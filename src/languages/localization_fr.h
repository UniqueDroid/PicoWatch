#ifndef PICOWATCH_LOCALIZATION_FR_H
#define PICOWATCH_LOCALIZATION_FR_H

#include "../localization_ids.h"

// FRENCH TRANSLATIONS - see ../localization.h
//
// Deliberately ASCII-only (no accents) per languages/localization_template.h's
// convention - most of these strings are printed directly via println()
// without going through uiTruncateToWidth()/uiWrapWords() (the only two
// places that convert UTF-8 source bytes to the Latin-1 the fonts actually
// expect), so an accented character here would render as two wrong glyphs
// at those call sites. localization_de.h gets away with a couple of literal
// umlauts because those specific strings happen to only be used as
// uiRenderList() menu-list items (which DOES convert) - not a safe general
// pattern to copy, so this file plays it safe everywhere instead of
// auditing every call site.
//
// One string per PW_STR_* id in ../localization_ids.h, IN THE SAME ORDER -
// pwStr() indexes into this array positionally, not by name.
const char *const kLocalizedStrings_fr[PW_STR_COUNT] = {
  // Top-level menu (PicoWatch::showMenu()/showFastMenu())
  "Changer Cadran",  // PW_MENU_CHANGE_WATCHFACE
  "Chronometre",  // PW_MENU_STOPWATCH
  "Pas",  // PW_MENU_STEPS
  "Alarme",  // PW_MENU_ALARM
  "Meteo",  // PW_MENU_WEATHER
  "Reglages",  // PW_MENU_SETTINGS
  // Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
  "A propos",  // PW_SETTINGS_ABOUT
  "Vibreur",  // PW_SETTINGS_VIBRATE
  "Accelerometre",  // PW_SETTINGS_ACCELEROMETER
  "Regler Heure",  // PW_SETTINGS_SET_TIME
  "WiFi",  // PW_SETTINGS_SETUP_WIFI
  "Sync NTP",  // PW_SETTINGS_SYNC_NTP
  "Fuseau Horaire",  // PW_SETTINGS_SET_TIMEZONE
  "Choisir Ville",  // PW_SETTINGS_SET_CITY
  "Mise a Jour",  // PW_SETTINGS_UPDATE_GITHUB
  "Reglages Boutons",  // PW_SETTINGS_BUTTON_SETTINGS
  "Taille Police",  // PW_SETTINGS_FONT_SIZE
  "Langue",  // PW_SETTINGS_LANGUAGE
  // Stopwatch (PicoWatch::showStopwatch())
  "Chronometre",  // PW_STOPWATCH_TITLE
  "Haut: Reinit.",  // PW_STOPWATCH_UP_RESET
  "Menu: Stop",  // PW_STOPWATCH_MENU_STOP
  "Menu: Depart",  // PW_STOPWATCH_MENU_START
  // Steps (PicoWatch::showStepsHistory())
  "Pas - 7 Jours",  // PW_STEPS_TITLE
  // Alarm (PicoWatch::setAlarm())
  "Alarme",  // PW_ALARM_TITLE
  "Active: ",  // PW_ALARM_ENABLED_LABEL
  "Oui",  // PW_YES
  "Non",  // PW_NO
  // Button Settings (PicoWatch::showButtonSettings())
  "Reglages Boutons",  // PW_BUTTON_SETTINGS_TITLE
  "Permuter Menu/Retour:",  // PW_BUTTON_SETTINGS_SWAP
  "Haut (court):",  // PW_BUTTON_SETTINGS_UP_SHORT
  "Haut (long):",  // PW_BUTTON_SETTINGS_UP_LONG
  "Bas (court):",  // PW_BUTTON_SETTINGS_DOWN_SHORT
  "Bas (long):",  // PW_BUTTON_SETTINGS_DOWN_LONG
  // watchfaceActionName()
  "Aucune",  // PW_ACTION_NONE
  "Reglages",  // PW_ACTION_SETTINGS
  "Changer Cadran",  // PW_ACTION_CHANGE_WATCHFACE
  "Meteo",  // PW_ACTION_WEATHER
  "Chronometre",  // PW_ACTION_STOPWATCH
  "Alarme",  // PW_ACTION_ALARM
  // Font Size (PicoWatch::showFontSizeSettings())
  "Taille Police",  // PW_FONT_SIZE_TITLE
  "(menu + reglages)",  // PW_FONT_SIZE_SUBTITLE
  "Petite",  // PW_FONT_SIZE_SMALL
  "Normale",  // PW_FONT_SIZE_DEFAULT
  "Grande",  // PW_FONT_SIZE_BIG
  // Weather forecast (PicoWatch::showWeatherForecast())
  "Chargement...",  // PW_WEATHER_LOADING
  "Verifiez le WiFi et",  // PW_WEATHER_CHECK_WIFI
  "la cle API meteo.",  // PW_WEATHER_CHECK_API_KEY
  // weatherConditionLabel()
  "Nuageux",  // PW_WEATHER_COND_CLOUDY
  "Peu Nuageux",  // PW_WEATHER_COND_FEW_CLOUDS
  "Degage",  // PW_WEATHER_COND_CLEAR
  "Brume",  // PW_WEATHER_COND_HAZE
  "Neige",  // PW_WEATHER_COND_SNOW
  "Pluie",  // PW_WEATHER_COND_RAIN
  "Bruine",  // PW_WEATHER_COND_DRIZZLE
  "Orage",  // PW_WEATHER_COND_STORM
  "?",  // PW_WEATHER_COND_UNKNOWN
  // About (PicoWatch::showAbout())
  "LibVer: ",  // PW_ABOUT_LIBVER
  "Rev: v",  // PW_ABOUT_REV
  "Batt: ",  // PW_ABOUT_BATT
  "V",  // PW_ABOUT_VOLT_UNIT
  "Actif: ",  // PW_ABOUT_UPTIME
  "j",  // PW_ABOUT_DAYS
  "h",  // PW_ABOUT_HOURS
  "m",  // PW_ABOUT_MINUTES
  "SSID: ",  // PW_ABOUT_SSID
  "IP: ",  // PW_ABOUT_IP
  "WiFi Non Connecte",  // PW_ABOUT_WIFI_NOT_CONNECTED
  // GitHub Update (PicoWatch::updateFromGithub())
  "Verif. GitHub...",  // PW_GITHUB_CHECKING
  "WiFi non connecte.",  // PW_GITHUB_WIFI_NOT_CONNECTED
  "Aucune version",  // PW_GITHUB_NO_RELEASE
  "ou erreur reseau.",  // PW_GITHUB_NETWORK_ERROR
  "Donnees invalides.",  // PW_GITHUB_BAD_DATA
  "Deja a la",  // PW_GITHUB_ALREADY_LATEST_1
  "derniere version:",  // PW_GITHUB_ALREADY_LATEST_2
  "Fichier introuvable",  // PW_GITHUB_ASSET_NOT_FOUND
  "dans la derniere version.",  // PW_GITHUB_IN_LATEST_RELEASE
  "Telechargement:",  // PW_GITHUB_DOWNLOADING
  "Echec telechargement.",  // PW_GITHUB_DOWNLOAD_FAILED
  "Espace insuffisant",  // PW_GITHUB_NOT_ENOUGH_SPACE
  "pour la mise a jour.",  // PW_GITHUB_FOR_UPDATE
  "Verification",  // PW_GITHUB_VERIFY_1
  "echouee - annulee.",  // PW_GITHUB_VERIFY_2
  "Echec de la mise",  // PW_GITHUB_FAILED_1
  "a jour finale.",  // PW_GITHUB_FAILED_2
  "Mise a jour verifiee.",  // PW_GITHUB_VERIFIED
  "Redemarrage...",  // PW_GITHUB_REBOOTING
  // Buzz (PicoWatch::showBuzz())
  "Vibration!",  // PW_BUZZ
  // Set City (PicoWatch::setWeatherCity())
  "ID de Ville",  // PW_SET_CITY_TITLE
  "Trouvez votre ID sur",  // PW_SET_CITY_FIND_1
  // Accelerometer debug (PicoWatch::showAccelerometer())
  "getAccel FAIL",  // PW_ACCEL_FAIL
  "FACE VERS BAS",  // PW_ACCEL_FACE_DOWN
  "FACE VERS HAUT",  // PW_ACCEL_FACE_UP
  "BORD BAS",  // PW_ACCEL_BOTTOM_EDGE
  "BORD HAUT",  // PW_ACCEL_TOP_EDGE
  "BORD DROIT",  // PW_ACCEL_RIGHT_EDGE
  "BORD GAUCHE",  // PW_ACCEL_LEFT_EDGE
  "ERREUR!!!",  // PW_ACCEL_ERROR
  // WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
  "Connexion...",  // PW_WIFI_CONNECTING
  "Connectez le tel. a:",  // PW_WIFI_CONNECT_PHONE_TO
  "(mot de passe sur",  // PW_WIFI_PASSWORD_NEXT_1
  "l'ecran suivant)",  // PW_WIFI_PASSWORD_NEXT_2
  "Config. echouee &",  // PW_WIFI_SETUP_FAILED
  "delai depasse!",  // PW_WIFI_TIMED_OUT
  "Connecte a:",  // PW_WIFI_CONNECTED_TO
  "Ouvrir dans le navigateur:",  // PW_WIFI_OPEN_IN_BROWSER
  "Retour pour deconnecter",  // PW_WIFI_BACK_TO_DISCONNECT
  "Reception de la",  // PW_WIFI_RECEIVING_UPDATE_1
  "mise a jour...",  // PW_WIFI_RECEIVING_UPDATE_2
  "Connectez-vous a",  // PW_WIFI_AP_CONNECT_TO
  "SSID: ",  // PW_WIFI_AP_SSID_LABEL
  "Mdp: ",  // PW_WIFI_AP_PASS_LABEL
  "IP: ",  // PW_WIFI_AP_IP_LABEL
  // Sync NTP (PicoWatch::showSyncNTP())
  "Sync NTP... ",  // PW_NTP_SYNCING
  "Decalage GMT: ",  // PW_NTP_GMT_OFFSET
  "Sync NTP Reussie\n",  // PW_NTP_SUCCESS
  "Heure actuelle:",  // PW_NTP_CURRENT_TIME
  "Echec Sync NTP",  // PW_NTP_FAILED
  "WiFi Non Configure",  // PW_NTP_WIFI_NOT_CONFIGURED
  // Games menu (PicoWatch::showGamesMenu()/showFastGamesMenu())
  "Jeux",  // PW_STR_MENU_GAMES
  "Serpent",  // PW_STR_GAME_SNAKE
  "Pong",  // PW_STR_GAME_PONG
  "Tetris",  // PW_STR_GAME_TETRIS
  "Flappy",  // PW_STR_GAME_FLAPPY
  "Pause",  // PW_STR_GAME_PAUSED
  "Partie Terminee",  // PW_STR_GAME_OVER
  "Score: ",  // PW_STR_GAME_SCORE_LABEL
  "Bientot disponible",  // PW_STR_GAME_COMING_SOON
  // Time submenu (PicoWatch::showTimeMenu()/showFastTimeMenu())
  "Heure",  // PW_STR_SETTINGS_TIME
  "Plage Vibration",  // PW_STR_TIME_VIBRATE_WINDOW
  "Vibration Horaire",  // PW_STR_VIBWIN_TITLE
  "De: ",  // PW_STR_VIBWIN_FROM_LABEL
  "A: ",  // PW_STR_VIBWIN_TO_LABEL
  "Debug",  // PW_STR_SETTINGS_DEBUG
  "Inverser Menu",  // PW_STR_SETTINGS_INVERT_MENU
  "Notifications",  // PW_STR_MENU_NOTIFICATIONS
  "Aucune notification",  // PW_STR_NOTIFICATIONS_EMPTY
  "Menu: lire  Retour: ignorer",  // PW_STR_NOTIFICATION_HINT
  "Menu: jumeler",  // PW_STR_NOTIFICATIONS_PAIR_HINT
  "Jumelage Bluetooth",  // PW_STR_NOTIFICATIONS_PAIRING
  "Ajoutez l'appareil dans Gadgetbridge (Retour pour annuler)",  // PW_STR_NOTIFICATIONS_PAIRING_HINT
  "Intervalle Verif.",  // PW_STR_TIME_NOTIFY_INTERVAL
  "Frequence de verification des messages",  // PW_STR_NOTIFY_INTERVAL_SUBTITLE
  "Menu: supprimer  Retour: ignorer",  // PW_STR_NOTIFICATION_DELETE_HINT
  "Acces Internet",  // PW_STR_INTERNET_ACCESS_TITLE
  "WiFi",  // PW_STR_INTERNET_ACCESS_WIFI
  "Bluetooth",  // PW_STR_INTERNET_ACCESS_BLE
  "Le BLE utilise votre telephone via Gadgetbridge",  // PW_STR_INTERNET_ACCESS_HINT
  "Reglages Notifications",  // PW_STR_NOTIF_SETTINGS_TITLE
  "Popup: ",  // PW_STR_NOTIF_SETTINGS_POPUP_LABEL
  "Duree: ",  // PW_STR_NOTIF_SETTINGS_DURATION_LABEL
  "Icone: ",  // PW_STR_NOTIF_SETTINGS_ICON_LABEL
  "Couleur Icone: ",  // PW_STR_NOTIF_SETTINGS_ICON_COLOR_LABEL
  "Clair",  // PW_STR_NOTIF_SETTINGS_ICON_LIGHT
  "Sombre",  // PW_STR_NOTIF_SETTINGS_ICON_DARK
};

// 7-day steps history labels (separate from the table above since it's an
// array-of-strings, not a single string) - see PicoWatch::showStepsHistory().
const char *const kLocalizedDayLabels_fr[7] = {"Hier", "Il y a 2 jours", "Il y a 3 jours", "Il y a 4 jours", "Il y a 5 jours", "Il y a 6 jours", "Il y a 7 jours"};

#endif
