#ifndef PICOWATCH_POKEMON_H
#define PICOWATCH_POKEMON_H

#include <PicoWatch.h>
#include "pokemon.h"

class PicoWatchPokemon : public PicoWatch{
    using PicoWatch::PicoWatch;
    public:
        void drawWatchFace();
};

#endif