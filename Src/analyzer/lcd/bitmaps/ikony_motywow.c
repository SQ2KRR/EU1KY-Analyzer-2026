#include "ikony_motywow.h"

/*
 * V2.1 Retro: wszystkie używane ikony 0..52 mają wersję szczegółową.
 * Pusta tablica jest świadomym bezpiecznikiem zgodności: renderer może nadal
 * wykonać ścieżkę awaryjną, ale nie przechowujemy drugiego, słabszego zestawu
 * 70 x 30 i nie marnujemy na niego pamięci Flash.
 */
const UI_IKONA_BITMAP_t ui_ikony_bitmap[UI_IKONA_BITMAP_MOTYWY][UI_IKONA_BITMAP_LICZBA] =
{
    { { 0, 0 } }
};
