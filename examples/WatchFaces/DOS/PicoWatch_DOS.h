#ifndef PICOWATCH_DOS_H
#define PICOWATCH_DOS_H

#include <PicoWatch.h>
#include "Px437_IBM_BIOS5pt7b.h"

class PicoWatchDOS : public PicoWatch{
    using PicoWatch::PicoWatch;
    public:
        void drawWatchFace();
};

#endif