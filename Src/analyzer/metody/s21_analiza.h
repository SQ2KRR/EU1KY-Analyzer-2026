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

    /* Parametry przydatne przy badaniu filtrów pasmowych. */
    bool ma_6db;
    bool ma_10db;
    bool ma_20db;
    bool ma_40db;
    bool ma_60db;
    float f1_6db_hz;
    float f2_6db_hz;
    float f1_10db_hz;
    float f2_10db_hz;
    float f1_20db_hz;
    float f2_20db_hz;
    float f1_40db_hz;
    float f2_40db_hz;
    float f1_60db_hz;
    float f2_60db_hz;
    float pasmo_6db_hz;
    float pasmo_10db_hz;
    float pasmo_20db_hz;
    float pasmo_40db_hz;
    float pasmo_60db_hz;
    float shape_20_3;
    float shape_40_3;
    float shape_60_3;
    /*
     * W kartach filtrów kwarcowych selektywność jest często podawana
     * jako stosunek szerokości przy dużym tłumieniu do szerokości -6 dB.
     * Trzymamy oba warianty, żeby nie mieszać definicji shape factor.
     */
    float shape_40_6;
    float shape_60_6;
    float f_srodek_3db_hz;
    float przesuniecie_piku_od_srodka_hz;
    float zafalowanie_srodka_db;
    float asymetria_3db_proc;

    /* Informacja o rozdzielczości i jakości próbkowania. */
    float krok_hz;
    uint16_t punkty_poprawne;
    uint16_t punkty_zmierzone;
    uint16_t punkty_bw3;
    uint16_t punkty_zmierzone_bw3;
    uint8_t jakosc_probkowania_proc;
    /*
     * Konserwatywna rozdzielczość wyznaczenia BW3 wynikająca wyłącznie z
     * siatki częstotliwości. To nie jest pełna niepewność metrologiczna.
     */
    float rozdzielczosc_bw3_hz;
} S21_ANALIZA_t;

/*
 * Analiza skalarnej charakterystyki transmisji zapisanej jako dodatnia strata
 * w dB (0 dB = brak straty, większa liczba = większe tłumienie).
 *
 * poprawny[] oznacza punkty, które można wykorzystać w analizie. zmierzony[]
 * rozróżnia rzeczywiste próbki od punktów uzupełnionych/interpolowanych przez
 * szybki skan. Może być NULL - wtedy każdy poprawny punkt jest traktowany jako
 * rzeczywiście zmierzony.
 *
 * Funkcja nie udaje pełnego VNA: nie wykorzystuje fazy S21, bo tor EU1KY jej
 * nie mierzy. Rozpoznaje podstawową topologię charakterystyki LPF/HPF/BPF/
 * NOTCH, wyznacza progi 3/6/10/20/40/60 dB, pasmo, Q, shape factor, zafalowanie
 * środkowej części pasma oraz ocenę gęstości próbkowania.
 */
bool S21_AnalizujFiltr(const float *strata_db, const uint8_t *poprawny,
                       const uint8_t *zmierzony, uint16_t liczba,
                       uint32_t f_start_hz, float krok_hz,
                       S21_ANALIZA_t *wynik);

const char *S21_NazwaTypu(S21_TYP_FILTRU_t typ);

#endif
