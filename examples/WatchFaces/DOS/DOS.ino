#include "PicoWatch_DOS.h"
#include "settings.h"

PicoWatchDOS picowatch(settings);

void setup(){
  picowatch.init();
}

void loop(){}
