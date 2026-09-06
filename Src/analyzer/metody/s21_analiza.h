#ifndef _S21_ANALIZA_H_
#define _S21_ANALIZA_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    S21_TYP_NIEZNANY = 0,
    S21_TYP_LPF,
    S21_TYP_HPF,
    S21_TYP_BPF,
    S21_TYP_NOTCH
} S21_TYP_FILTRU_t;

typedef struct
{
    bool poprawny;
    bool ma_lewy_3db;
    bool ma_prawy_3db;
    uint16_t indeks_minimum;
    uint16_t indeks_maksimum;
    float f_min_hz;
    float f_max_hz;
    float strata_min_db;
    float strata_max_db;
    float prog_3db_db;
    float f1_3db_hz;
    float f2_3db_hz;
    float pasmo_3db_hz;
    float q_3db;

    /* Rozszerzona analiza charakterystyki skalarnej |S21|. */
    S21_TYP_FILTRU_t typ;
    float strata_lewego_brzegu_db;
    float strata_prawego_brzegu_db;
    float tlumienie_stop_db;
    float glebokosc_notch_db;
    float nachylenie_lewe_db_na_oktawe;
    float nachylenie_prawe_db_na_oktawe;
} S21_ANALIZA_t;

/*
 * Analiza skalarnej charakterystyki transmisji zapisanej jako dodatnia strata
 * w dB (0 dB = brak straty, wieksza liczba = wieksze tlumienie).
 *
 * Funkcja nie udaje pelnego VNA: nie wykorzystuje fazy S21, bo tor EU1KY jej
 * nie mierzy. Rozpoznaje podstawowa topologie charakterystyki LPF/HPF/BPF/
 * NOTCH, wyznacza progi 3 dB, pasmo, Q tam gdzie ma ono sens, tlumienie oraz
 * lokalne nachylenie zboczy w dB/okt przy progu 3 dB.
 */
bool S21_AnalizujFiltr(const float *strata_db, const uint8_t *poprawny,
                       uint16_t liczba, uint32_t f_start_hz, float krok_hz,
                       S21_ANALIZA_t *wynik);

const char *S21_NazwaTypu(S21_TYP_FILTRU_t typ);

#endif
