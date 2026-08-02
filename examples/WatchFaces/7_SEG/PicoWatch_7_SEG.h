#ifndef PICOWATCH_7_SEG_H
#define PICOWATCH_7_SEG_H

#include <PicoWatch.h>
#include "Seven_Segment10pt7b.h"
#include "DSEG7_Classic_Regular_15.h"
#include "DSEG7_Classic_Bold_25.h"
#include "DSEG7_Classic_Regular_39.h"
#include "icons.h"

class PicoWatch7SEG : public PicoWatch{
    using PicoWatch::PicoWatch;
    public:
        void drawWatchFace();
        void drawTime();
        void drawDate();
        void drawSteps();
        void drawWeather();
        void drawBattery();
};

#endif