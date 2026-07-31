#include "MultiFaceWatchy.h"

#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

#include "DSEG7_Classic_Bold_25.h"
#include "DSEG7_Classic_Regular_15.h"
#include "DSEG7_Classic_Regular_39.h"
#include "Face7SegIcons.h"
#include "FaceMacPaintAssets.h"
#include "FaceMarioAssets.h"
#include "FacePokemonAssets.h"
#include "FaceStarryStars.h"
#include "FaceTetrisAssets.h"
#include "MadeSunflower39pt7b.h"
#include "Px437_IBM_BIOS5pt7b.h"
#include "Seven_Segment10pt7b.h"

// Watchy.cpp defines this the same way, but only for its own translation
// unit - AllFaces builds exclusively for V3 (ARDUINO_ESP32S3_DEV), so this
// always matches.
#define ACTIVE_LOW 0

// Persists across deep sleep like guiState/menuIndex (declared in Watchy.cpp).
RTC_DATA_ATTR int selectedFace = 0;

namespace {
constexpr const char *kFaceNames[MultiFaceWatchy::FACE_COUNT] = {
    "Basic", "7 Segment", "DOS", "MacPaint", "Mario", "Pokemon", "Starry Horizon", "Tetris"};
}  // namespace

void MultiFaceWatchy::changeWatchface() {
  guiState = APP_STATE;

  int pick = selectedFace;

  pinMode(DOWN_BTN_PIN, INPUT);
  pinMode(UP_BTN_PIN, INPUT);
  pinMode(MENU_BTN_PIN, INPUT);
  pinMode(BACK_BTN_PIN, INPUT);

  // Same button-bounce hazard as Watchy::setTimezone(): Menu here confirms
  // immediately, and it's the very button that was just pressed to select
  // "Change Watchface" from the main menu.
  while (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
    delay(10);
  }

  display.setFullWindow();

  bool confirmed = true;
  while (1) {
    if (digitalRead(MENU_BTN_PIN) == ACTIVE_LOW) {
      confirmed = true;
      break;
    }
    if (digitalRead(BACK_BTN_PIN) == ACTIVE_LOW) {
      confirmed = false;
      break;
    }
    if (digitalRead(DOWN_BTN_PIN) == ACTIVE_LOW) {
      pick = (pick + 1) % FACE_COUNT;
    }
    if (digitalRead(UP_BTN_PIN) == ACTIVE_LOW) {
      pick = (pick - 1 + FACE_COUNT) % FACE_COUNT;
    }

    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    for (int i = 0; i < FACE_COUNT; i++) {
      const int16_t yPos = MENU_HEIGHT + (MENU_HEIGHT * i);
      display.setCursor(0, yPos);
      if (i == pick) {
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(kFaceNames[i], 0, yPos, &x1, &y1, &w, &h);
        display.fillRect(x1 - 1, y1 - 10, 200, h + 15, GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
      } else {
        display.setTextColor(GxEPD_WHITE);
      }
      display.println(kFaceNames[i]);
    }
    display.display(true);  // partial refresh
  }

  if (confirmed) {
    selectedFace = pick;
    showWatchFace(false);
  } else {
    showMenu(menuIndex, false);
  }
}

void MultiFaceWatchy::drawWatchFace() {
  switch (selectedFace) {
    case 0:
      drawBasic();
      break;
    case 1:
      draw7Seg();
      break;
    case 2:
      drawDos();
      break;
    case 3:
      drawMacPaint();
      break;
    case 4:
      drawMario();
      break;
    case 5:
      drawPokemon();
      break;
    case 6:
      drawStarryHorizon();
      break;
    case 7:
      drawTetris();
      break;
    default:
      selectedFace = 0;
      drawBasic();
      break;
  }
}

// ---- Basic (library default look, ported from Watchy::drawWatchFace()) ----

void MultiFaceWatchy::drawBasic() {
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

// ---- 7_SEG (ported from Watchy_7_SEG.cpp) ----

namespace {
constexpr bool k7SegDarkMode = true;
constexpr uint8_t k7SegBatterySegmentWidth = 7;
constexpr uint8_t k7SegBatterySegmentHeight = 11;
constexpr uint8_t k7SegBatterySegmentSpacing = 9;
constexpr uint8_t k7SegWeatherIconWidth = 48;
constexpr uint8_t k7SegWeatherIconHeight = 32;
}  // namespace

void MultiFaceWatchy::draw7Seg() {
  using namespace face7seg;
  display.fillScreen(k7SegDarkMode ? GxEPD_BLACK : GxEPD_WHITE);
  display.setTextColor(k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  draw7SegTime();
  draw7SegDate();
  draw7SegSteps();
  draw7SegWeather();
  draw7SegBattery();
  display.drawBitmap(116, 75, WIFI_CONFIGURED ? wifi : wifioff, 26, 18, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  if (BLE_CONFIGURED) {
    display.drawBitmap(100, 73, bluetooth, 13, 21, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  }
#ifdef ARDUINO_ESP32S3_DEV
  if (USB_PLUGGED_IN) {
    display.drawBitmap(140, 75, charge, 16, 18, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  }
#endif
}

void MultiFaceWatchy::draw7SegTime() {
  display.setFont(&DSEG7_Classic_Bold_53);
  display.setCursor(5, 53 + 5);
  int displayHour;
  if (HOUR_12_24 == 12) {
    displayHour = ((currentTime.Hour + 11) % 12) + 1;
  } else {
    displayHour = currentTime.Hour;
  }
  if (displayHour < 10) {
    display.print("0");
  }
  display.print(displayHour);
  display.print(":");
  if (currentTime.Minute < 10) {
    display.print("0");
  }
  display.println(currentTime.Minute);
}

void MultiFaceWatchy::draw7SegDate() {
  display.setFont(&Seven_Segment10pt7b);

  int16_t x1, y1;
  uint16_t w, h;

  String dayOfWeek = dayStr(currentTime.Wday);
  display.getTextBounds(dayOfWeek, 5, 85, &x1, &y1, &w, &h);
  if (currentTime.Wday == 4) {
    w = w - 5;
  }
  display.setCursor(85 - w, 85);
  display.println(dayOfWeek);

  String month = monthShortStr(currentTime.Month);
  display.getTextBounds(month, 60, 110, &x1, &y1, &w, &h);
  display.setCursor(85 - w, 110);
  display.println(month);

  display.setFont(&DSEG7_Classic_Bold_25);
  display.setCursor(5, 120);
  if (currentTime.Day < 10) {
    display.print("0");
  }
  display.println(currentTime.Day);
  display.setCursor(5, 150);
  display.println(tmYearToCalendar(currentTime.Year));  // offset from 1970, since year is stored in uint8_t
}

void MultiFaceWatchy::draw7SegSteps() {
  using namespace face7seg;
  // reset step counter at midnight
  if (currentTime.Hour == 0 && currentTime.Minute == 0) {
    sensor.resetStepCounter();
  }
  uint32_t stepCount = sensor.getCounter();
  display.drawBitmap(10, 165, steps, 19, 23, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  display.setCursor(35, 190);
  display.println(stepCount);
}

void MultiFaceWatchy::draw7SegBattery() {
  using namespace face7seg;
  display.drawBitmap(158, 73, battery, 37, 21, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  display.fillRect(163, 78, 27, k7SegBatterySegmentHeight,
                    k7SegDarkMode ? GxEPD_BLACK : GxEPD_WHITE);  // clear battery segments
  int8_t batteryLevel = 0;
  float VBAT = getBatteryVoltage();
  if (VBAT > 4.0) {
    batteryLevel = 3;
  } else if (VBAT > 3.6 && VBAT <= 4.0) {
    batteryLevel = 2;
  } else if (VBAT > 3.20 && VBAT <= 3.6) {
    batteryLevel = 1;
  } else if (VBAT <= 3.20) {
    batteryLevel = 0;
  }

  for (int8_t batterySegments = 0; batterySegments < batteryLevel; batterySegments++) {
    display.fillRect(163 + (batterySegments * k7SegBatterySegmentSpacing), 78, k7SegBatterySegmentWidth,
                      k7SegBatterySegmentHeight, k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  }
}

void MultiFaceWatchy::draw7SegWeather() {
  using namespace face7seg;
  weatherData currentWeather = getWeatherData();

  int8_t temperature = currentWeather.temperature;
  int16_t weatherConditionCode = currentWeather.weatherConditionCode;

  display.setFont(&DSEG7_Classic_Regular_39);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(String(temperature), 0, 0, &x1, &y1, &w, &h);
  if (159 - w - x1 > 87) {
    display.setCursor(159 - w - x1, 150);
  } else {
    display.setFont(&DSEG7_Classic_Bold_25);
    display.getTextBounds(String(temperature), 0, 0, &x1, &y1, &w, &h);
    display.setCursor(159 - w - x1, 136);
  }
  display.println(temperature);
  display.drawBitmap(165, 110, currentWeather.isMetric ? celsius : fahrenheit, 26, 20,
                      k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
  const unsigned char *weatherIcon;

  if (WIFI_CONFIGURED) {
    // https://openweathermap.org/weather-conditions
    if (weatherConditionCode > 801) {  // Cloudy
      weatherIcon = cloudy;
    } else if (weatherConditionCode == 801) {  // Few Clouds
      weatherIcon = cloudsun;
    } else if (weatherConditionCode == 800) {  // Clear
      weatherIcon = sunny;
    } else if (weatherConditionCode >= 700) {  // Atmosphere
      weatherIcon = atmosphere;
    } else if (weatherConditionCode >= 600) {  // Snow
      weatherIcon = snow;
    } else if (weatherConditionCode >= 500) {  // Rain
      weatherIcon = rain;
    } else if (weatherConditionCode >= 300) {  // Drizzle
      weatherIcon = drizzle;
    } else if (weatherConditionCode >= 200) {  // Thunderstorm
      weatherIcon = thunderstorm;
    } else
      return;
  } else {
    weatherIcon = chip;
  }

  display.drawBitmap(145, 158, weatherIcon, k7SegWeatherIconWidth, k7SegWeatherIconHeight,
                      k7SegDarkMode ? GxEPD_WHITE : GxEPD_BLACK);
}

// ---- DOS (ported from Watchy_DOS.cpp) ----

void MultiFaceWatchy::drawDos() {
  char time[6];
  time[0] = '0' + ((currentTime.Hour / 10) % 10);
  time[1] = '0' + (currentTime.Hour % 10);
  time[2] = ':';
  time[3] = '0' + ((currentTime.Minute / 10) % 10);
  time[4] = '0' + (currentTime.Minute % 10);
  time[5] = 0;
  display.fillScreen(GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setFont(&Px437_IBM_BIOS5pt7b);
  display.setCursor(0, 24);
  display.println("WATCHY-DOS 1.1.8");
  display.println("Copyright (c) 2020");
  display.println(" ");
  display.print("AUTOEXEC BAT ");
  display.println(time);
  display.print("COMMAND  COM ");
  display.println(time);
  display.print("CONFIG   SYS ");
  display.println(time);
  display.print("ESPTOOL  PY  ");
  display.println(time);
  display.println(" ");
  display.println("  4 files 563 bytes");
  display.println("  2048 bytes free");
  display.println(" ");
  display.println("<C:\\>esptool");
}

// ---- MacPaint (ported from Watchy_MacPaint.cpp) ----

void MultiFaceWatchy::drawMacPaint() {
  using namespace facemacpaint;
  const unsigned char *numbers[10] = {numbers0, numbers1, numbers2, numbers3, numbers4,
                                       numbers5, numbers6, numbers7, numbers8, numbers9};

  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(0, 0, window, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);

  // Hour
  display.drawBitmap(35, 70, numbers[currentTime.Hour / 10], 38, 50, GxEPD_BLACK);
  display.drawBitmap(70, 70, numbers[currentTime.Hour % 10], 38, 50, GxEPD_BLACK);

  // Colon
  display.drawBitmap(100, 80, colon, 11, 31, GxEPD_BLACK);

  // Minute
  display.drawBitmap(115, 70, numbers[currentTime.Minute / 10], 38, 50, GxEPD_BLACK);
  display.drawBitmap(153, 70, numbers[currentTime.Minute % 10], 38, 50, GxEPD_BLACK);
}

// ---- Mario (ported from Watchy_Mario.cpp) ----

void MultiFaceWatchy::drawMario() {
  using namespace facemario;
  static constexpr int kNumW = 44;
  static constexpr int kNumH = 44;
  static constexpr int kCoinW = 24;
  static constexpr int kCoinH = 30;
  static constexpr int kPipeW = 42;
  static constexpr int kPipeH = 47;
  static constexpr int kMarioW = 56;
  static constexpr int kMarioH = 54;
  static constexpr int kNumSpacing = 4;
  static constexpr int kCoinSpacing = 4;
  static constexpr int kFloorH = 19;
  static constexpr int kPipePadding = DISPLAY_HEIGHT - kFloorH - kPipeH;
  static constexpr int kXPadding = (DISPLAY_WIDTH - (4 * kNumW) - (3 * kNumSpacing)) / 2;
  static constexpr int kYPadding = 2 * kCoinSpacing + kCoinH;

  const unsigned char *numbers[10] = {mario0, mario1, mario2, mario3, mario4,
                                       mario5, mario6, mario7, mario8, mario9};

  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(0, 0, mariobg, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);

  int hour10 = currentTime.Hour / 10;
  int hour01 = currentTime.Hour % 10;
  int minute10 = currentTime.Minute / 10;
  int minute01 = currentTime.Minute % 10;

  int pos = 0;

  if (hour01 == 0 && minute10 == 0 && minute01 == 0) {
    pos = 0;
  } else if (minute10 == 0 && minute01 == 0) {
    pos = 1;
  } else if (minute01 == 0) {
    pos = 2;
  } else {
    pos = 3;
  }

  display.drawBitmap(kXPadding + pos * (kNumSpacing + kNumW) + (kNumW / 2 - kMarioW / 2) + (pos < 2 ? 8 : -8),
                      kYPadding + kNumH + 4, pos < 2 ? mariomariol : mariomarior, kMarioW, kMarioH,
                      GxEPD_BLACK);  // mario
  display.drawBitmap(kXPadding + pos * (kNumSpacing + kNumW) + (kNumW / 2 - kCoinW / 2), kCoinSpacing, mariocoin,
                      kCoinW, kCoinH, GxEPD_BLACK);  // coin

  if (pos == 0) {
    display.drawBitmap(DISPLAY_WIDTH - 2 * kPipeW, kPipePadding, mariopipe, kPipeW, kPipeH, GxEPD_BLACK);
  } else if (pos == 1 || pos == 2) {
    display.drawBitmap(kXPadding, kPipePadding, mariopipe, kPipeW, kPipeH, GxEPD_BLACK);
    display.drawBitmap(DISPLAY_WIDTH - kPipeW - kXPadding, kPipePadding, mariopipe, kPipeW, kPipeH, GxEPD_BLACK);
  } else {
    display.drawBitmap(2 * kPipeW, kPipePadding, mariopipe, kPipeW, kPipeH, GxEPD_BLACK);
  }

  // Hour
  display.drawBitmap(kXPadding, pos == 0 ? kYPadding : kYPadding + 20, numbers[hour10], kNumW, kNumH, GxEPD_BLACK);
  display.drawBitmap(kXPadding + kNumSpacing + kNumW, pos == 1 ? kYPadding : kYPadding + 20, numbers[hour01], kNumW,
                      kNumH, GxEPD_BLACK);

  // Minute
  display.drawBitmap(kXPadding + 2 * (kNumSpacing + kNumW), pos == 2 ? kYPadding : kYPadding + 20, numbers[minute10],
                      kNumW, kNumH, GxEPD_BLACK);
  display.drawBitmap(kXPadding + 3 * (kNumSpacing + kNumW), pos == 3 ? kYPadding : kYPadding + 20, numbers[minute01],
                      kNumW, kNumH, GxEPD_BLACK);
}

// ---- Pokemon (ported from Watchy_Pokemon.cpp) ----

void MultiFaceWatchy::drawPokemon() {
  using namespace facepokemon;
  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(0, 0, pokemon, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(10, 170);
  if (currentTime.Hour < 10) {
    display.print('0');
  }
  display.print(currentTime.Hour);
  display.print(':');
  if (currentTime.Minute < 10) {
    display.print('0');
  }
  display.print(currentTime.Minute);
}

// ---- Tetris (ported from Watchy_Tetris.cpp) ----

void MultiFaceWatchy::drawTetris() {
  using namespace facetetris;
  const unsigned char *tetris_nums[10] = {tetris0, tetris1, tetris2, tetris3, tetris4,
                                           tetris5, tetris6, tetris7, tetris8, tetris9};

  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(0, 0, tetrisbg, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);

  // Hour
  display.drawBitmap(25, 20, tetris_nums[currentTime.Hour / 10], 40, 60, GxEPD_BLACK);
  display.drawBitmap(75, 20, tetris_nums[currentTime.Hour % 10], 40, 60, GxEPD_BLACK);

  // Minute
  display.drawBitmap(25, 110, tetris_nums[currentTime.Minute / 10], 40, 60, GxEPD_BLACK);
  display.drawBitmap(75, 110, tetris_nums[currentTime.Minute % 10], 40, 60, GxEPD_BLACK);
}

// ---- StarryHorizon (ported from StarryHorizon.ino) ----

namespace {
constexpr int kStarryHorizonY = 150;
constexpr int kStarryPlanetR = 650;
constexpr int kStarryStarCount = 900;

struct StarryXyPoint {
  int x;
  int y;
};

StarryXyPoint starryRotatePointAround(int x, int y, int ox, int oy, double angle) {
  double qx = (double)ox + (cos(angle) * (double)(x - ox)) + (sin(angle) * (double)(y - oy));
  double qy = (double)oy + (-sin(angle) * (double)(x - ox)) + (cos(angle) * (double)(y - oy));
  StarryXyPoint newPoint;
  newPoint.x = (int)qx;
  newPoint.y = (int)qy;
  return newPoint;
}
}  // namespace

void MultiFaceWatchy::drawStarryHorizon() {
  display.fillScreen(GxEPD_BLACK);
  display.fillCircle(100, kStarryHorizonY + kStarryPlanetR, kStarryPlanetR, GxEPD_WHITE);
  drawStarryGrid();
  drawStarryStarsField();
  drawStarryTime();
  drawStarryDate();
}

void MultiFaceWatchy::drawStarryGrid() {
  int prevY = kStarryHorizonY;
  for (int i = 0; i < 40; i += 1) {
    int y = prevY + int(abs(sin(double(i) / 10) * 10));
    if (y <= 200) {
      display.drawFastHLine(0, y, 200, GxEPD_BLACK);
    }
    prevY = y;
  }
  int vanishY = kStarryHorizonY - 25;
  for (int x = -230; x < 430; x += 20) {
    display.drawLine(x, 200, 100, vanishY, GxEPD_BLACK);
  }
}

void MultiFaceWatchy::drawStarryStarsField() {
  using namespace facestarry;
  // rotate stars so that they make an entire revolution once per hour
  int minute = (int)currentTime.Minute;
  double minuteAngle = ((2.0 * M_PI) / 60.0) * (double)minute;

  for (int starI = 0; starI < kStarryStarCount; starI++) {
    int starX = STARS[starI].x;
    int starY = STARS[starI].y;
    int starR = STARS[starI].r;

    StarryXyPoint rotated = starryRotatePointAround(starX, starY, 100, 100, minuteAngle);
    if (rotated.x < 0 || rotated.y < 0 || rotated.x > 200 || rotated.y > kStarryHorizonY) {
      continue;
    }
    if (starR == 0) {
      display.drawPixel(rotated.x, rotated.y, GxEPD_WHITE);
    } else {
      display.fillCircle(rotated.x, rotated.y, starR, GxEPD_WHITE);
    }
  }
}

void MultiFaceWatchy::drawStarryTime() {
  display.setFont(&MADE_Sunflower_PERSONAL_USE39pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setTextWrap(false);
  char *timeStr;
  asprintf(&timeStr, "%d:%02d", currentTime.Hour, currentTime.Minute);
  drawStarryCenteredString(timeStr, 100, 115, false);
  free(timeStr);
}

void MultiFaceWatchy::drawStarryDate() {
  String monthStr = monthShortStr(currentTime.Month);
  String dayOfWeek = dayShortStr(currentTime.Wday);
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_WHITE);
  display.setTextWrap(false);
  char *dateStr;
  asprintf(&dateStr, "%s %s %d", dayOfWeek.c_str(), monthStr.c_str(), currentTime.Day);
  drawStarryCenteredString(dateStr, 100, 140, true);
  free(dateStr);
}

void MultiFaceWatchy::drawStarryCenteredString(const String &str, int x, int y, bool drawBg) {
  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);
  display.setCursor(x - w / 2, y);
  if (drawBg) {
    int padY = 3;
    int padX = 10;
    display.fillRect(x - (w / 2 + padX), y - (h + padY), w + padX * 2, h + padY * 2, GxEPD_BLACK);
  }
  display.print(str);
}
