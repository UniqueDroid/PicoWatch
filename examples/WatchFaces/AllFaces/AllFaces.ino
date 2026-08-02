#include "MultiFacePicoWatch.h"
#include "settings.h"

MultiFacePicoWatch picowatch(settings);

void setup() {
  picowatch.init();
}

void loop() {}
