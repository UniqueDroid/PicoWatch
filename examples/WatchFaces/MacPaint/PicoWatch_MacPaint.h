#ifndef PICOWATCH_MACPAINT_H
#define PICOWATCH_MACPAINT_H

#include <PicoWatch.h>
#include "macpaint.h"

class PicoWatchMacPaint : public PicoWatch{
    using PicoWatch::PicoWatch;
    public:
        void drawWatchFace();
};

#endif