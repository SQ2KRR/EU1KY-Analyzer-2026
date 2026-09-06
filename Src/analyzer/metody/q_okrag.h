#ifndef _Q_OKRAG_H_
#define _Q_OKRAG_H_

#include <stdbool.h>
#include <stdint.h>
#include <complex.h>

#include "pomiar_s11.h"

/*
 * Dopasowanie okręgu rezonansowego do zespolonego S11.
 *
 * Ten moduł jest pierwszym, świadomie ograniczonym etapem metody Q-circle
 * stosowanej m.in. w pracach Darko Kajfeza. Wyznacza wyłącznie:
 * - okrąg na płaszczyźnie zespolonej,
 * - częstotliwość rezonansu,
 * - dobroć obciążoną Q_L z przebiegu fazy wokół okręgu.
 *
 * Nie wyznacza jeszcze dobroci nieobciążonej Q0 ani współczynnika sprzężenia.
 * Do tych wielkości potrzebna jest dodatkowa, zweryfikowana na sprzęcie
 * interpretacja geometrii sprzężenia. Nie wolno utożsamiać Q_L z Q0.
 */

#define Q_OKRAG_MIN_PUNKTOW 9U
#define Q_OKRAG_MAX_PUNKTOW 320U
#define Q_OKRAG_MIN_LUK_RAD 1.5707963267948966f
#define Q_OKRAG_MAX_BLAD_OKREGU_WZGL 0.05f
#define Q_OKRAG_MAX_BLAD_FAZY_RMS_RAD 0.10f
#define Q_OKRAG_MARGINES_REZONANSU 0.03f

typedef enum
{
    Q_OKRAG_JAKOSC_OK = 0U,
    Q_OKRAG_JAKOSC_MALO_PUNKTOW = 1U << 0,
    Q_OKRAG_JAKOSC_ZLA_OS_CZESTOTLIWOSCI = 1U << 1,
    Q_OKRAG_JAKOSC_ZDEGENEROWANY_OKRAG = 1U << 2,
    Q_OKRAG_JAKOSC_MALY_LUK = 1U << 3,
    Q_OKRAG_JAKOSC_BLAD_OKREGU = 1U << 4,
    Q_OKRAG_JAKOSC_BLAD_FAZY = 1U << 5,
    Q_OKRAG_JAKOSC_REZONANS_PRZY_BRZEGU = 1U << 6
} Q_OKRAG_JAKOSC_t;

typedef struct
{
    bool obliczony;
    bool wiarygodny;
    uint32_t jakosc;
    uint16_t liczba_punktow;

    float f0_hz;
    float q_obciazone;

    float complex srodek;
    float promien;
    float luk_rad;
    float blad_okregu_rms;
    float blad_okregu_wzgledny;
    float blad_fazy_rms_rad;
    float faza_rezonansu_rad;
    int32_t opoznienie_ps;
} Q_OKRAG_WYNIK_t;

/* Jak Q_OKRAG_Dopasuj(), ale przed dopasowaniem usuwa zadane opoznienie
 * elektryczne w jedna strone. Znak i konwencja sa zgodne z Port Extension:
 * dodatnia wartosc kompensuje opoznienie pomiedzy plaszczyzna OSL a DUT.
 * Funkcja nie czyta konfiguracji i nie zgaduje opoznienia automatycznie. */
bool Q_OKRAG_DopasujZOpoznieniem(const SERIA_S11_t *seria, int32_t opoznienie_ps,
                                  Q_OKRAG_WYNIK_t *wynik);

bool Q_OKRAG_Dopasuj(const SERIA_S11_t *seria, Q_OKRAG_WYNIK_t *wynik);

#endif
