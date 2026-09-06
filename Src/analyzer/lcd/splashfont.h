#ifndef SPLASHFONT_H_
#define SPLASHFONT_H_

#include <stdint.h>

typedef struct
{
    uint16_t szerokosc;
    uint16_t wysokosc;
    const uint8_t *alfa;
} SPLASH_MASKA_t;

extern const SPLASH_MASKA_t splash_maska_prefix;
extern const SPLASH_MASKA_t splash_maska_rok;
extern const SPLASH_MASKA_t splash_maska_kod_pl;
extern const SPLASH_MASKA_t splash_maska_kod_en;
extern const SPLASH_MASKA_t splash_maska_kod_de;
extern const SPLASH_MASKA_t splash_maska_kod_ru;
extern const SPLASH_MASKA_t splash_maska_opis_pl;
extern const SPLASH_MASKA_t splash_maska_opis_en;
extern const SPLASH_MASKA_t splash_maska_opis_de;
extern const SPLASH_MASKA_t splash_maska_opis_ru;

#endif
