#ifndef DOPASOWANIE_CAUER_FOSTER_H_
#define DOPASOWANIE_CAUER_FOSTER_H_

#include <stdbool.h>
#include <stdint.h>

#include "pomiar_s11.h"
#include "synteza_cauer_foster.h"

/*
 * Dopasowanie dotyczy bezstratnej części reaktancyjnej impedancji.
 * Rezystancja R nie jest "wchłaniana" do elementów LC: jest mierzona osobno
 * jako wskaźnik strat i decyduje o wiarygodności modelu bezstratnego.
 *
 * Etap test29 obsługuje tylko 1 i 3 elementy Cauer I. Model 5-elementowy ma
 * więcej stopni swobody i wymaga osobnej identyfikacji biegunów; nie dokładamy
 * jej, dopóki prostsza rodzina nie zostanie zweryfikowana na realnym S11.
 */
#define DOPASOWANIE_CF_MIN_PUNKTOW_L 3U
#define DOPASOWANIE_CF_MIN_PUNKTOW_LCL 7U

/* Progi są zabezpieczeniami inżynierskimi, a nie deklaracją niepewności. */
#define DOPASOWANIE_CF_MAX_BLAD_WZGLEDNY_X 0.10
#define DOPASOWANIE_CF_MAX_UDZIAL_STRAT 0.20

typedef enum
{
    DOPASOWANIE_CF_JAKOSC_OK = 0U,
    DOPASOWANIE_CF_JAKOSC_MALO_PUNKTOW = 1U << 0,
    DOPASOWANIE_CF_JAKOSC_NIEIDENTYFIKOWALNY = 1U << 1,
    DOPASOWANIE_CF_JAKOSC_SLABE_DOPASOWANIE = 1U << 2,
    DOPASOWANIE_CF_JAKOSC_STRATY_ISTOTNE = 1U << 3,
    DOPASOWANIE_CF_JAKOSC_BLAD_NUMERYCZNY = 1U << 4
} DOPASOWANIE_CF_JAKOSC_t;

typedef struct
{
    bool obliczony;
    bool zaakceptowany;
    uint32_t jakosc;
    uint16_t liczba_punktow;

    /* Model równoważny zapisany w obu postaciach. */
    SYNTEZA_CF_CAUER_I_t cauer;
    SYNTEZA_CF_FOSTER_I_t foster;

    /* Miary jakości w dziedzinie pomiarowej. */
    double blad_rms_x_ohm;
    double blad_wzgledny_x;
    double rms_r_ohm;
    double udzial_strat;
} DOPASOWANIE_CF_WYNIK_t;

bool DOPASOWANIE_CF_Dopasuj(const SERIA_S11_t *seria,
                            uint8_t liczba_elementow_cauer,
                            DOPASOWANIE_CF_WYNIK_t *wynik);

#endif
