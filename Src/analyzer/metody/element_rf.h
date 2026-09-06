#ifndef _ELEMENT_RF_H_
#define _ELEMENT_RF_H_

#include <stdbool.h>
#include <stdint.h>

#include "pomiar_s11.h"

typedef enum
{
    ELEMENT_RF_CEWKA = 0,
    ELEMENT_RF_KONDENSATOR = 1
} ELEMENT_RF_TYP_t;

typedef enum
{
    ELEMENT_RF_UWAGA_BRAK = 0,
    ELEMENT_RF_UWAGA_MALO_PUNKTOW = 1U << 0,
    ELEMENT_RF_UWAGA_SLABE_DOPASOWANIE = 1U << 1,
    ELEMENT_RF_UWAGA_SRF_POZA_SKANEM = 1U << 2,
    ELEMENT_RF_UWAGA_SRF_NA_BRZEGU = 1U << 3,
    ELEMENT_RF_UWAGA_REZYSTANCJA_NIEPEWNA = 1U << 4
} ELEMENT_RF_UWAGA_t;

typedef struct
{
    bool poprawny;
    ELEMENT_RF_TYP_t typ;

    /* Parametry modelu fizycznego. */
    float l_h;
    float c_f;
    float r_strat_ohm;
    float pasozyt_f_lub_h; /* Cp dla cewki, ESL dla kondensatora. */

    /* Wyniki pochodne. */
    float srf_hz;
    float srf_obserwowane_hz;
    float q_przy_fref;
    float fref_hz;
    float blad_rms_ohm;
    uint16_t liczba_punktow;
    uint32_t uwagi;
} ELEMENT_RF_WYNIK_t;

/*
 * Analiza rzeczywistego elementu na podstawie tej samej, skorygowanej serii S11.
 *
 * Cewka:       (Rs + j*w*L) || Cp
 * Kondensator: ESR + j*w*ESL + 1/(j*w*C)
 *
 * fref_hz służy wyłącznie do podania Q w częstotliwości użytecznej dla
 * użytkownika. Parametry modelu są dopasowywane do całej serii poprawnych
 * punktów, a nie tylko do punktu reprezentatywnego.
 */
bool ELEMENT_RF_Analizuj(const SERIA_S11_t *seria, ELEMENT_RF_TYP_t typ,
                         float fref_hz, ELEMENT_RF_WYNIK_t *wynik);

/* Model pomocniczy używany również przez testy numeryczne. */
bool ELEMENT_RF_ModelImpedancji(const ELEMENT_RF_WYNIK_t *model, float f_hz,
                                float *r_ohm, float *x_ohm);

#endif
