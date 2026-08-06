#ifndef PICOWATCH_LOCALIZATION_H
#define PICOWATCH_LOCALIZATION_H

// Compile-time language selection, same mechanism as InkWatchy
// (github.com/Szybet/InkWatchy, src/defines/localization.h) - ported
// directly per Jan's request, not just "inspired by": a language macro
// picks one of several per-language header files full of string
// #defines, all used throughout PicoWatch.cpp instead of hardcoded
// literals. This is compile-time only (like InkWatchy's), not an
// on-device runtime toggle - switching language means setting
// PICOWATCH_LANG below (or in config.h before this include) and
// reflashing.
#define PW_LANG_EN 1
#define PW_LANG_DE 2

#ifndef PICOWATCH_LANG
#define PICOWATCH_LANG PW_LANG_DE // Jan's own build defaults to German
#endif

#if PICOWATCH_LANG == PW_LANG_DE
#include "languages/localization_de.h"
#elif PICOWATCH_LANG == PW_LANG_EN
#include "languages/localization_en.h"
#else
#include "languages/localization_en.h"
#warning "Unsupported PICOWATCH_LANG - defaulting to English. Define PW_LANG_EN or PW_LANG_DE."
#endif

#endif
