#include <PicoWatch.h>
#include "settings.h"

PicoWatch picowatch(settings);

void setup(){
  picowatch.init();
}

void loop(){}