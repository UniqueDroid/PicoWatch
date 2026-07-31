#ifndef MULTI_FACE_WATCHY_H
#define MULTI_FACE_WATCHY_H

#include <Watchy.h>

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
class MultiFaceWatchy : public Watchy {
 public:
  static constexpr int FACE_COUNT = 8;

 private:
  void drawBasic();
  void draw7Seg();
  void drawDos();
  void drawMacPaint();
  void drawMario();
  void drawPokemon();
  void drawStarryHorizon();
  void drawTetris();

  // 7_SEG helpers (ported from Watchy_7_SEG.cpp)
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
  using Watchy::Watchy;
  void drawWatchFace() override;
  void changeWatchface() override;
};

#endif
