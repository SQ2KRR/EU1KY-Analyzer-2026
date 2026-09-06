#ifndef IKONY_SZCZEGOLOWE_NOWE_H
#define IKONY_SZCZEGOLOWE_NOWE_H

#include <stdint.h>
#include "ikony_szczegolowe_makiety.h"

/*
 * Szczegółowe ikony Retro. Większość ma 136 x 50 px i 4 bpp.
 * Ikony anten są zapisane jako 112 x 42 px, ponieważ ich prosta geometria
 * pozostaje czytelna po niewielkim skalowaniu, a oszczędzamy pamięć Flash.
 *
 * Tablica jest indeksowana bezpośrednio wartością UI_IKONA_MENU_t.
 * Puste wpisy 10, 11, 15, 16, 17 i 19 świadomie korzystają z pięciu
 * zatwierdzonych bitmap głównej makiety.
 */
#define UI_IKONY_SZCZEGOLOWE_NOWE_LICZBA 56U

extern const UI_IKONA_SZCZEGOLOWA_t
    ui_ikony_szczegolowe_nowe[UI_IKONY_SZCZEGOLOWE_NOWE_LICZBA];

#endif
