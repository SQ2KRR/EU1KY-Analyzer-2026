#ifndef _ANALIZA_PRZEBIEGU_H_
#define _ANALIZA_PRZEBIEGU_H_

#include <complex.h>
#include <stdint.h>

typedef struct
{
    uint8_t poprawna;
    uint16_t liczba_punktow_poprawnych;
    uint16_t liczba_punktow_odrzuconych;
    uint32_t f_min_swr_hz;
    float min_swr;
    float r_min_ohm;
    float x_min_ohm;

    uint8_t rezonans_znaleziony;
    uint32_t f_rezonans_hz;
    float r_rezonans_ohm;

    uint8_t pasmo_swr_15_znalezione;
    uint32_t f_swr_15_dol_hz;
    uint32_t f_swr_15_gora_hz;

    uint8_t pasmo_swr_20_znalezione;
    uint32_t f_swr_20_dol_hz;
    uint32_t f_swr_20_gora_hz;
} ANALIZA_PRZEBIEGU_WYNIK_t;

/*
 * Analizuje jeden gotowy przebieg impedancji. Funkcja nie wykonuje pomiarów
 * i nie korzysta z konfiguracji globalnej, dzięki czemu można ją testować
 * niezależnie od sprzętu.
 */
ANALIZA_PRZEBIEGU_WYNIK_t ANALIZA_PRZEBIEGU_Oblicz(
    const float complex *probki,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm);

/*
 * Wariant z maską jakości. Maska ma wartość 1 dla punktu rzeczywiście
 * zmierzonego/wiarygodnego i 0 dla punktu odrzuconego. Dzięki temu analiza
 * nie udaje pełnego pasma na podstawie wartości zastępczych używanych tylko
 * do zachowania ciągłości rysowania wykresu.
 */
ANALIZA_PRZEBIEGU_WYNIK_t ANALIZA_PRZEBIEGU_ObliczZMaska(
    const float complex *probki,
    const uint8_t *maska_poprawnosci,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm);

#endif
