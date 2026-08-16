#ifndef PICOWATCH_LOCALIZATION_ES_H
#define PICOWATCH_LOCALIZATION_ES_H

#include "../localization_ids.h"

// SPANISH TRANSLATIONS - see ../localization.h
//
// Deliberately ASCII-only (no accents/enie) per languages/localization_template.h's
// convention - most of these strings are printed directly via println()
// without going through uiTruncateToWidth()/uiWrapWords() (the only two
// places that convert UTF-8 source bytes to the Latin-1 the fonts actually
// expect), so an accented character here would render as two wrong glyphs
// at those call sites. See localization_fr.h's comment for the full
// reasoning (same applies here).
//
// One string per PW_STR_* id in ../localization_ids.h, IN THE SAME ORDER -
// pwStr() indexes into this array positionally, not by name.
const char *const kLocalizedStrings_es[PW_STR_COUNT] = {
  // Top-level menu (PicoWatch::showMenu()/showFastMenu())
  "Cambiar Esfera",  // PW_MENU_CHANGE_WATCHFACE
  "Cronometro",  // PW_MENU_STOPWATCH
  "Pasos",  // PW_MENU_STEPS
  "Alarma",  // PW_MENU_ALARM
  "Clima",  // PW_MENU_WEATHER
  "Ajustes",  // PW_MENU_SETTINGS
  // Settings menu (PicoWatch::showSettingsMenu()/showFastSettingsMenu())
  "Acerca de PicoWatch",  // PW_SETTINGS_ABOUT
  "Motor Vibrador",  // PW_SETTINGS_VIBRATE
  "Ver Acelerometro",  // PW_SETTINGS_ACCELEROMETER
  "Ajustar Hora",  // PW_SETTINGS_SET_TIME
  "WiFi",  // PW_SETTINGS_SETUP_WIFI
  "Sincronizar NTP",  // PW_SETTINGS_SYNC_NTP
  "Zona Horaria",  // PW_SETTINGS_SET_TIMEZONE
  "Elegir Ciudad",  // PW_SETTINGS_SET_CITY
  "Actualizacion",  // PW_SETTINGS_UPDATE_GITHUB
  "Ajustes de Botones",  // PW_SETTINGS_BUTTON_SETTINGS
  "Tamano de Letra",  // PW_SETTINGS_FONT_SIZE
  "Idioma",  // PW_SETTINGS_LANGUAGE
  // Stopwatch (PicoWatch::showStopwatch())
  "Cronometro",  // PW_STOPWATCH_TITLE
  "Arriba: Reiniciar",  // PW_STOPWATCH_UP_RESET
  "Menu: Detener",  // PW_STOPWATCH_MENU_STOP
  "Menu: Iniciar",  // PW_STOPWATCH_MENU_START
  // Steps (PicoWatch::showStepsHistory())
  "Pasos - 7 Dias",  // PW_STEPS_TITLE
  // Alarm (PicoWatch::setAlarm())
  "Alarma",  // PW_ALARM_TITLE
  "Activada: ",  // PW_ALARM_ENABLED_LABEL
  "Si",  // PW_YES
  "No",  // PW_NO
  // Button Settings (PicoWatch::showButtonSettings())
  "Ajustes de Botones",  // PW_BUTTON_SETTINGS_TITLE
  "Cambiar Menu/Atras:",  // PW_BUTTON_SETTINGS_SWAP
  "Arriba (corto):",  // PW_BUTTON_SETTINGS_UP_SHORT
  "Arriba (largo):",  // PW_BUTTON_SETTINGS_UP_LONG
  "Abajo (corto):",  // PW_BUTTON_SETTINGS_DOWN_SHORT
  "Abajo (largo):",  // PW_BUTTON_SETTINGS_DOWN_LONG
  // watchfaceActionName()
  "Ninguna",  // PW_ACTION_NONE
  "Ajustes",  // PW_ACTION_SETTINGS
  "Cambiar Esfera",  // PW_ACTION_CHANGE_WATCHFACE
  "Clima",  // PW_ACTION_WEATHER
  "Cronometro",  // PW_ACTION_STOPWATCH
  "Alarma",  // PW_ACTION_ALARM
  // Font Size (PicoWatch::showFontSizeSettings())
  "Tamano de Letra",  // PW_FONT_SIZE_TITLE
  "(menu + ajustes)",  // PW_FONT_SIZE_SUBTITLE
  "Pequena",  // PW_FONT_SIZE_SMALL
  "Normal",  // PW_FONT_SIZE_DEFAULT
  "Grande",  // PW_FONT_SIZE_BIG
  // Weather forecast (PicoWatch::showWeatherForecast())
  "Cargando...",  // PW_WEATHER_LOADING
  "Verifique el WiFi y",  // PW_WEATHER_CHECK_WIFI
  "la clave API del clima.",  // PW_WEATHER_CHECK_API_KEY
  // weatherConditionLabel()
  "Nublado",  // PW_WEATHER_COND_CLOUDY
  "Pocas Nubes",  // PW_WEATHER_COND_FEW_CLOUDS
  "Despejado",  // PW_WEATHER_COND_CLEAR
  "Neblina",  // PW_WEATHER_COND_HAZE
  "Nieve",  // PW_WEATHER_COND_SNOW
  "Lluvia",  // PW_WEATHER_COND_RAIN
  "Llovizna",  // PW_WEATHER_COND_DRIZZLE
  "Tormenta",  // PW_WEATHER_COND_STORM
  "?",  // PW_WEATHER_COND_UNKNOWN
  // About (PicoWatch::showAbout())
  "LibVer: ",  // PW_ABOUT_LIBVER
  "Rev: v",  // PW_ABOUT_REV
  "Bat.: ",  // PW_ABOUT_BATT
  "V",  // PW_ABOUT_VOLT_UNIT
  "Activo: ",  // PW_ABOUT_UPTIME
  "d",  // PW_ABOUT_DAYS
  "h",  // PW_ABOUT_HOURS
  "m",  // PW_ABOUT_MINUTES
  "SSID: ",  // PW_ABOUT_SSID
  "IP: ",  // PW_ABOUT_IP
  "WiFi No Conectado",  // PW_ABOUT_WIFI_NOT_CONNECTED
  // GitHub Update (PicoWatch::updateFromGithub())
  "Verificando GitHub...",  // PW_GITHUB_CHECKING
  "WiFi no conectado.",  // PW_GITHUB_WIFI_NOT_CONNECTED
  "No se encontro version",  // PW_GITHUB_NO_RELEASE
  "o error de red.",  // PW_GITHUB_NETWORK_ERROR
  "Datos invalidos.",  // PW_GITHUB_BAD_DATA
  "Ya en la",  // PW_GITHUB_ALREADY_LATEST_1
  "ultima version:",  // PW_GITHUB_ALREADY_LATEST_2
  "Archivo no encontrado",  // PW_GITHUB_ASSET_NOT_FOUND
  "en la ultima version.",  // PW_GITHUB_IN_LATEST_RELEASE
  "Descargando:",  // PW_GITHUB_DOWNLOADING
  "Descarga fallida.",  // PW_GITHUB_DOWNLOAD_FAILED
  "Espacio insuficiente",  // PW_GITHUB_NOT_ENOUGH_SPACE
  "para la actualizacion.",  // PW_GITHUB_FOR_UPDATE
  "Verificacion",  // PW_GITHUB_VERIFY_1
  "fallida - cancelada.",  // PW_GITHUB_VERIFY_2
  "Fallo en la",  // PW_GITHUB_FAILED_1
  "actualizacion final.",  // PW_GITHUB_FAILED_2
  "Actualizacion verificada.",  // PW_GITHUB_VERIFIED
  "Reiniciando...",  // PW_GITHUB_REBOOTING
  // Buzz (PicoWatch::showBuzz())
  "Vibracion!",  // PW_BUZZ
  // Set City (PicoWatch::setWeatherCity())
  "ID de Ciudad",  // PW_SET_CITY_TITLE
  "Busque su ID en",  // PW_SET_CITY_FIND_1
  // Accelerometer debug (PicoWatch::showAccelerometer())
  "getAccel FAIL",  // PW_ACCEL_FAIL
  "BOCA ABAJO",  // PW_ACCEL_FACE_DOWN
  "BOCA ARRIBA",  // PW_ACCEL_FACE_UP
  "BORDE INFERIOR",  // PW_ACCEL_BOTTOM_EDGE
  "BORDE SUPERIOR",  // PW_ACCEL_TOP_EDGE
  "BORDE DERECHO",  // PW_ACCEL_RIGHT_EDGE
  "BORDE IZQUIERDO",  // PW_ACCEL_LEFT_EDGE
  "ERROR!!!",  // PW_ACCEL_ERROR
  // WiFi setup (PicoWatch::setupWifi()/_configModeCallback())
  "Conectando...",  // PW_WIFI_CONNECTING
  "Conecte el telefono a:",  // PW_WIFI_CONNECT_PHONE_TO
  "(contrasena en la",  // PW_WIFI_PASSWORD_NEXT_1
  "siguiente pantalla)",  // PW_WIFI_PASSWORD_NEXT_2
  "Config. fallida y",  // PW_WIFI_SETUP_FAILED
  "tiempo agotado!",  // PW_WIFI_TIMED_OUT
  "Conectado a:",  // PW_WIFI_CONNECTED_TO
  "Abrir en el navegador:",  // PW_WIFI_OPEN_IN_BROWSER
  "Atras para desconectar",  // PW_WIFI_BACK_TO_DISCONNECT
  "Recibiendo",  // PW_WIFI_RECEIVING_UPDATE_1
  "actualizacion...",  // PW_WIFI_RECEIVING_UPDATE_2
  "Conectese a",  // PW_WIFI_AP_CONNECT_TO
  "SSID: ",  // PW_WIFI_AP_SSID_LABEL
  "Clave: ",  // PW_WIFI_AP_PASS_LABEL
  "IP: ",  // PW_WIFI_AP_IP_LABEL
  // Sync NTP (PicoWatch::showSyncNTP())
  "Sincronizando NTP... ",  // PW_NTP_SYNCING
  "Desfase GMT: ",  // PW_NTP_GMT_OFFSET
  "Sincronizacion Exitosa\n",  // PW_NTP_SUCCESS
  "Hora actual:",  // PW_NTP_CURRENT_TIME
  "Fallo de Sincronizacion",  // PW_NTP_FAILED
  "WiFi No Configurado",  // PW_NTP_WIFI_NOT_CONFIGURED
  // Games menu (PicoWatch::showGamesMenu()/showFastGamesMenu())
  "Juegos",  // PW_STR_MENU_GAMES
  "Serpiente",  // PW_STR_GAME_SNAKE
  "Pong",  // PW_STR_GAME_PONG
  "Tetris",  // PW_STR_GAME_TETRIS
  "Flappy",  // PW_STR_GAME_FLAPPY
  "Pausado",  // PW_STR_GAME_PAUSED
  "Fin del Juego",  // PW_STR_GAME_OVER
  "Puntos: ",  // PW_STR_GAME_SCORE_LABEL
  "Proximamente",  // PW_STR_GAME_COMING_SOON
  // Time submenu (PicoWatch::showTimeMenu()/showFastTimeMenu())
  "Hora",  // PW_STR_SETTINGS_TIME
  "Rango de Vibracion",  // PW_STR_TIME_VIBRATE_WINDOW
  "Vibracion Horaria",  // PW_STR_VIBWIN_TITLE
  "Desde: ",  // PW_STR_VIBWIN_FROM_LABEL
  "Hasta: ",  // PW_STR_VIBWIN_TO_LABEL
  "Debug",  // PW_STR_SETTINGS_DEBUG
  "Invertir Menu",  // PW_STR_SETTINGS_INVERT_MENU
  "Notificaciones",  // PW_STR_MENU_NOTIFICATIONS
  "Sin notificaciones",  // PW_STR_NOTIFICATIONS_EMPTY
  "Menu: leer  Atras: descartar",  // PW_STR_NOTIFICATION_HINT
  "Menu: emparejar",  // PW_STR_NOTIFICATIONS_PAIR_HINT
  "Emparejamiento Bluetooth",  // PW_STR_NOTIFICATIONS_PAIRING
  "Agregue el dispositivo en Gadgetbridge (Atras para cancelar)",  // PW_STR_NOTIFICATIONS_PAIRING_HINT
  "Intervalo de Verif.",  // PW_STR_TIME_NOTIFY_INTERVAL
  "Frecuencia de verificacion de mensajes",  // PW_STR_NOTIFY_INTERVAL_SUBTITLE
  "Menu: borrar  Atras: descartar",  // PW_STR_NOTIFICATION_DELETE_HINT
  "Acceso a Internet",  // PW_STR_INTERNET_ACCESS_TITLE
  "WiFi",  // PW_STR_INTERNET_ACCESS_WIFI
  "Bluetooth",  // PW_STR_INTERNET_ACCESS_BLE
  "BLE usa su telefono via Gadgetbridge",  // PW_STR_INTERNET_ACCESS_HINT
  "Ajustes de Notificaciones",  // PW_STR_NOTIF_SETTINGS_TITLE
  "Ventana: ",  // PW_STR_NOTIF_SETTINGS_POPUP_LABEL
  "Duracion: ",  // PW_STR_NOTIF_SETTINGS_DURATION_LABEL
  "Icono: ",  // PW_STR_NOTIF_SETTINGS_ICON_LABEL
  "Color Icono: ",  // PW_STR_NOTIF_SETTINGS_ICON_COLOR_LABEL
  "Claro",  // PW_STR_NOTIF_SETTINGS_ICON_LIGHT
  "Oscuro",  // PW_STR_NOTIF_SETTINGS_ICON_DARK
  "Vibrar: ",  // PW_STR_NOTIF_SETTINGS_VIBRATE_LABEL
  "Vibracion",  // PW_STR_SETTINGS_VIBRATION
  "Vibracion",  // PW_STR_VIBRATION_TITLE
  "Intensidad: ",  // PW_STR_VIBRATION_STRENGTH_LABEL
  "Baja",  // PW_STR_VIBRATION_STRENGTH_LOW
  "Media",  // PW_STR_VIBRATION_STRENGTH_MEDIUM
  "Alta",  // PW_STR_VIBRATION_STRENGTH_HIGH
  "Apagar",  // PW_STR_SETTINGS_POWER
  "Apagar",  // PW_STR_POWER_TITLE
  "Reiniciar",  // PW_STR_POWER_RESTART
  "Apagar",  // PW_STR_POWER_SHUTDOWN
  "Apagado - pulsa cualquier boton",  // PW_STR_POWER_OFF_MSG
};

// 7-day steps history labels (separate from the table above since it's an
// array-of-strings, not a single string) - see PicoWatch::showStepsHistory().
const char *const kLocalizedDayLabels_es[7] = {"Ayer", "Hace 2 dias", "Hace 3 dias", "Hace 4 dias", "Hace 5 dias", "Hace 6 dias", "Hace 7 dias"};

#endif
