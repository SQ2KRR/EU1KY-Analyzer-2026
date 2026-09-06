#ifndef _RF_AKWIZYCJA_H_
#define _RF_AKWIZYCJA_H_

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

typedef enum
{
    RF_AKW_TOR_STANDARD = 0,
    RF_AKW_TOR_LC = 1
} RF_AKW_TOR_t;

typedef enum
{
    RF_AKW_FLAGA_POPRAWNY        = 1U << 0,
    RF_AKW_FLAGA_KOREKCJA_HW     = 1U << 1,
    RF_AKW_FLAGA_TOR_LC          = 1U << 3
} RF_AKW_FLAGA_t;

typedef struct
{
    uint32_t czestotliwosc_hz;
    float complex impedancja_przed_osl_ohm;
    float complex gamma_przed_osl;
    float z0_ohm;
    float napiecie_v_mv;
    float napiecie_i_mv;
    float stosunek_amplitud;
    float faza_stopnie;
    float spojnosc_fazy;
    float rozrzut_v_proc;
    float rozrzut_i_proc;
    uint8_t liczba_usrednien;
    uint32_t flagi;
} RF_AKW_PUNKT_t;

/*
 * Warstwa akwizycji konczy sie PRZED korekcja OSL.
 * Moze wykonac korekcje sprzetowa toru V/I, bo jest ona czescia sposobu
 * pozyskania wiarygodnej impedancji z mostka. Nie wykonuje OSL ani Port Extension.
 */
bool RF_AKW_PobierzPunkt(uint32_t czestotliwosc_hz,
                         RF_AKW_TOR_t tor,
                         bool korekcja_hw,
                         uint8_t liczba_usrednien,
                         RF_AKW_PUNKT_t *wynik);

float complex RF_AKW_ImpedancjaNaGamma(float complex impedancja_ohm, float z0_ohm);

#endif
