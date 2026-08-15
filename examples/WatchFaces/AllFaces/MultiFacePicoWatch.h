#ifndef MULTI_FACE_PICOWATCH_H
#define MULTI_FACE_PICOWATCH_H

#include <PicoWatch.h>

// Combines all 8 example watchfaces into one firmware. Selecting "Change
// Watchface" in the main menu (see changeWatchface()) opens a scrollable
// list of all 8 designs; confirming applies selectedFace and redraws, Back
// cancels back to the menu. Each face's original drawWatchFace() body was
// moved here unchanged as its own method
// (drawBasic/draw7Seg/drawDos/drawMacPaint/drawMario/drawPokemon/
// drawStarryHorizon/drawTetris); their bitmap/font assets were copied from
// the individual examples into per-face namespaces (face7seg, facemacpaint,
// facemario, facepokemon, facetetris, facestarry) since several of them
// reused identical generic names (e.g. both MacPaint and Mario declared
// "numbers", 7_SEG's icons.h used bare names like "battery"/"wifi").
class MultiFacePicoWatch : public PicoWatch {
 public:
  static constexpr int FACE_COUNT = 9;

 private:
  void drawBasic();
  void draw7Seg();
  void drawDos();
  void drawMacPaint();
  void drawMario();
  void drawPokemon();
  void drawStarryHorizon();
  void drawTetris();

  // 7_SEG helpers (ported from PicoWatch_7_SEG.cpp)
  void draw7SegTime();
  void draw7SegDate();
  void draw7SegSteps();
  void draw7SegBattery();
  void draw7SegWeather();

  // StarryHorizon helpers (ported from StarryHorizon.ino)
  void drawStarryGrid();
  void drawStarryStarsField();
  void drawStarryTime();
  void drawStarryDate();
  void drawStarryCenteredString(const String &str, int x, int y, bool drawBg);

 public:
  using PicoWatch::PicoWatch;
  void drawWatchFace() override;
  void changeWatchface() override;
  void onReset() override;

  int webFaceCount() override { return FACE_COUNT; }
  const char *webFaceName(int index) override;
  int webSelectedFace() override;
  void webSetFace(int index) override;
};

#endif
