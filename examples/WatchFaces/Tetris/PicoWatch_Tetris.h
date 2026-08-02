#ifndef PICOWATCH_TETRIS_H
#define PICOWATCH_TETRIS_H

#include <PicoWatch.h>
#include "tetris.h"

class PicoWatchTetris : public PicoWatch{
    public:
        using PicoWatch::PicoWatch;
        void drawWatchFace();
};

#endif