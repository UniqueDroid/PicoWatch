#include <Arduino.h>
#include "localization.h"
#include "languages/localization_en.h"
#include "languages/localization_de.h"
#include "languages/localization_fr.h"
#include "languages/localization_es.h"

RTC_DATA_ATTR uint8_t picowatchLanguage = PICOWATCH_LANG;

namespace {
const char *const *const kStringTables[PW_LANG_COUNT] = {kLocalizedStrings_en, kLocalizedStrings_de,
                                                           kLocalizedStrings_fr, kLocalizedStrings_es};
const char *const *const kDayLabelTables[PW_LANG_COUNT] = {kLocalizedDayLabels_en, kLocalizedDayLabels_de,
                                                             kLocalizedDayLabels_fr, kLocalizedDayLabels_es};
// Always shown in the language's own name (never translated) - see
// PicoWatch::showLanguageSettings().
const char *const kLanguageNames[PW_LANG_COUNT] = {"English", "Deutsch", "Francais", "Espanol"};
}  // namespace

const char *pwStr(PwStringId id) {
  return kStringTables[picowatchLanguage][id];
}

const char *const *pwDayLabels() {
  return kDayLabelTables[picowatchLanguage];
}

const char *pwLanguageName(uint8_t language) {
  return kLanguageNames[language];
}
