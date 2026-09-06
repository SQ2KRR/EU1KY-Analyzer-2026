#ifndef _STROJENIE_ANTENA_H_
#define _STROJENIE_ANTENA_H_

#include <complex.h>
#include <stdint.h>

#define STROJENIE_ANTENA_MAX_REZONANSOW 8U

typedef enum
{
    STROJENIE_ANTENA_KIERUNEK_BRAK = 0,
    STROJENIE_ANTENA_KIERUNEK_W_CELU,
    STROJENIE_ANTENA_KIERUNEK_SKROC,
    STROJENIE_ANTENA_KIERUNEK_WYDLUZ
} STROJENIE_ANTENA_KIERUNEK_t;

typedef struct
{
    uint32_t czestotliwosc_hz;
    float r_ohm;
    float swr;
} STROJENIE_ANTENA_REZONANS_t;

typedef struct
{
    uint8_t poprawny;
    uint16_t liczba_punktow_poprawnych;
    uint16_t liczba_punktow_odrzuconych;

    uint32_t czestotliwosc_docelowa_hz;
    uint8_t punkt_docelowy_znaleziony;
    uint32_t f_punkt_docelowy_hz;
    float swr_docelowy;
    float r_docelowy_ohm;
    float x_docelowy_ohm;

    uint32_t f_min_swr_hz;
    float min_swr;
    float r_min_ohm;
    float x_min_ohm;

    uint8_t rezonans_znaleziony;
    uint32_t f_rezonans_hz;
    float r_rezonans_ohm;
    float swr_rezonans;
    int32_t odchylenie_rezonansu_hz;
    STROJENIE_ANTENA_KIERUNEK_t kierunek;

    /*
     * Przybliżona zmiana długości dla prostego elementu rezonansowego,
     * wynikająca z zależności f ~ 1/L. Wartość dodatnia = wydłużyć,
     * ujemna = skrócić. Nie jest to uniwersalny model każdej anteny.
     */
    float korekta_dlugosci_procent;

    uint8_t liczba_rezonansow;
    STROJENIE_ANTENA_REZONANS_t rezonanse[STROJENIE_ANTENA_MAX_REZONANSOW];

    uint8_t pasmo_swr_15_znalezione;
    uint32_t f_swr_15_dol_hz;
    uint32_t f_swr_15_gora_hz;

    uint8_t pasmo_swr_20_znalezione;
    uint32_t f_swr_20_dol_hz;
    uint32_t f_swr_20_gora_hz;
} STROJENIE_ANTENA_WYNIK_t;

/*
 * Analiza jest czystą matematyką: nie uruchamia generatora, ADC ani LCD.
 * Wszystkie metody pracują na tej samej, już skorygowanej serii impedancji.
 */
STROJENIE_ANTENA_WYNIK_t STROJENIE_ANTENA_Analizuj(
    const float complex *probki,
    const uint8_t *maska_poprawnosci,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm,
    uint32_t czestotliwosc_docelowa_hz);

#endif
