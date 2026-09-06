#ifndef IKONY_MOTYWOW_H_
#define IKONY_MOTYWOW_H_

#include <stdint.h>

/*
 * Kompletny zestaw 53 ikon RETRO w BMP 4 bpp.
 * Klasyczny niebieski używa dokładnie tych samych bitmap i przekształca
 * ich paletę na granat/cyjan/biel podczas renderowania, więc nie dublujemy
 * zasobów Flash.
 */
typedef struct
{
    const uint8_t *dane;
    uint32_t rozmiar;
} UI_IKONA_BITMAP_t;

#define UI_IKONA_BITMAP_SZEROKOSC 70U
#define UI_IKONA_BITMAP_WYSOKOSC 30U
#define UI_IKONA_BITMAP_MOTYWY 1U
#define UI_IKONA_BITMAP_LICZBA 53U

extern const UI_IKONA_BITMAP_t ui_ikony_bitmap[UI_IKONA_BITMAP_MOTYWY][UI_IKONA_BITMAP_LICZBA];

#endif /* IKONY_MOTYWOW_H_ */
