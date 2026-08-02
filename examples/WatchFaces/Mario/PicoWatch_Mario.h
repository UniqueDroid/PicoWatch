#ifndef PICOWATCH_MARIO_H
#define PICOWATCH_MARIO_H

#include <PicoWatch.h>
#include "mario.h"

class PicoWatchMario: public PicoWatch{
    using PicoWatch::PicoWatch;
    public:
        void drawWatchFace();
};

#endif