#ifndef RUFONT_H_
#define RUFONT_H_

#include <stdint.h>
#include "font.h"

typedef struct
{
    const uint8_t *dane;
    uint8_t szerokosc;
    uint8_t wysokosc;
    uint8_t bajtow_na_wiersz;
} RU_FONT_GLIF_t;

/*
 * Zwraca osadzony glif cyrylicy dla FRAN/FRANBIG.
 * Mapowanie odbywa sie bezposrednio z Unicode, bez strony kodowej CP1251.
 */
int RU_FONT_Pobierz(uint32_t unicode, FONTS font, RU_FONT_GLIF_t *glif);

#endif
