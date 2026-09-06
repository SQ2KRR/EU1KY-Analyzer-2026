#ifndef SYNTEZA_CAUER_FOSTER_H_
#define SYNTEZA_CAUER_FOSTER_H_

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Pierwszy etap syntezy Cauer/Foster obejmuje kanoniczną, bezstratną rodzinę
 * reaktancyjną zaczynającą się od indukcyjności szeregowej. Ograniczenie do
 * pięciu elementów jest celowe: daje maksymalnie dwa rezonatory równoległe
 * w postaci Fostera i pozwala walidować matematykę bez ciężkiego solvera.
 */
#define SYNTEZA_CF_MAX_ELEMENTOW_CAUER 5U
#define SYNTEZA_CF_MAX_GALEZI_FOSTER 2U

typedef enum
{
    SYNTEZA_CF_OK = 0,
    SYNTEZA_CF_BLAD_ARGUMENTU,
    SYNTEZA_CF_BLAD_TOPOLOGII,
    SYNTEZA_CF_BLAD_NIEPASYWNA,
    SYNTEZA_CF_BLAD_NUMERYCZNY
} SYNTEZA_CF_STATUS_t;

typedef struct
{
    /*
     * Pozycje parzyste 0,2,4: L szeregowe [H].
     * Pozycje nieparzyste 1,3: C równoległe do dalszej części drabinki [F].
     * Dozwolone długości: 1, 3 albo 5.
     */
    uint8_t liczba_elementow;
    double wartosc[SYNTEZA_CF_MAX_ELEMENTOW_CAUER];
} SYNTEZA_CF_CAUER_I_t;

typedef struct
{
    /* Równoległy obwód LC włączony szeregowo w postaci Foster I. */
    double l_h;
    double c_f;
} SYNTEZA_CF_GALAZ_FOSTER_t;

typedef struct
{
    double l_szeregowa_h;
    uint8_t liczba_galezi;
    SYNTEZA_CF_GALAZ_FOSTER_t galaz[SYNTEZA_CF_MAX_GALEZI_FOSTER];
} SYNTEZA_CF_FOSTER_I_t;

bool SYNTEZA_CF_CauerNaFoster(const SYNTEZA_CF_CAUER_I_t *cauer,
                              SYNTEZA_CF_FOSTER_I_t *foster,
                              SYNTEZA_CF_STATUS_t *status);

bool SYNTEZA_CF_FosterNaCauer(const SYNTEZA_CF_FOSTER_I_t *foster,
                              SYNTEZA_CF_CAUER_I_t *cauer,
                              SYNTEZA_CF_STATUS_t *status);

double complex SYNTEZA_CF_ImpedancjaCauer(const SYNTEZA_CF_CAUER_I_t *cauer,
                                          double czestotliwosc_hz);

double complex SYNTEZA_CF_ImpedancjaFoster(const SYNTEZA_CF_FOSTER_I_t *foster,
                                           double czestotliwosc_hz);

#endif
