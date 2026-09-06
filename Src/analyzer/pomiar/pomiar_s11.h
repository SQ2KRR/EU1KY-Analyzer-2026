#ifndef _POMIAR_S11_H_
#define _POMIAR_S11_H_

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

typedef enum
{
    POMIAR_S11_TOR_STANDARD = 0,
    POMIAR_S11_TOR_LC = 1
} POMIAR_S11_TOR_t;

typedef enum
{
    POMIAR_S11_FLAGA_POPRAWNY       = 1U << 0,
    POMIAR_S11_FLAGA_KOREKCJA_HW    = 1U << 1,
    POMIAR_S11_FLAGA_KOREKCJA_OSL   = 1U << 2,
    POMIAR_S11_FLAGA_PORT_EXTENSION = 1U << 3,
    POMIAR_S11_FLAGA_TOR_LC         = 1U << 4,
    POMIAR_S11_FLAGA_KABEL_DEEMBED    = 1U << 6,
    POMIAR_S11_FLAGA_OSL_70CM          = 1U << 7
} POMIAR_S11_FLAGA_t;

typedef enum
{
    POMIAR_S11_KOREKCJA_BRAK = 0,
    POMIAR_S11_KOREKCJA_OSL_KLASYCZNY = 1,
    POMIAR_S11_KOREKCJA_OSL_LC = 3,
    POMIAR_S11_KOREKCJA_OSL_70CM = 4
} POMIAR_S11_KOREKCJA_t;

typedef struct
{
    uint32_t czestotliwosc_hz;

    /* Dane na granicy akwizycji - po korekcji HW, ale przed OSL. */
    float complex gamma_przed_osl;
    float complex impedancja_przed_osl_ohm;

    /* Dane przekazywane metodom i UI po zadanych korekcjach. */
    float complex gamma;
    float complex impedancja_ohm;

    float z0_ohm;
    float napiecie_v_mv;
    float napiecie_i_mv;
    float stosunek_amplitud;
    float stosunek_db;
    float faza_stopnie;
    float spojnosc_fazy;
    float rozrzut_v_proc;
    float rozrzut_i_proc;
    uint32_t port_extension_ps;
    uint32_t flagi;
    POMIAR_S11_KOREKCJA_t model_korekcji;
} POMIAR_S11_t;

typedef struct
{
    POMIAR_S11_t *punkty;
    uint16_t liczba;
} SERIA_S11_t;

typedef struct
{
    POMIAR_S11_TOR_t tor;
    uint8_t liczba_usrednien;
    bool korekcja_hw;
    bool korekcja_osl;
    bool kompensacja_portu;
    bool kompensacja_kabla;

    /*
     * Lokalna OSL jest opt-in na poziomie konkretnego ekranu pomiarowego.
     * Zapobiega to przypadkowemu uzyciu 70 cm w TDR lub innych szerokich
     * procedurach, ktore zbieraja punkty pojedynczo.
     */
    bool automatyczna_osl_pasmowa;
    uint32_t zakres_pomiaru_od_hz;
    uint32_t zakres_pomiaru_do_hz;
} POMIAR_S11_USTAWIENIA_t;

/*
 * Publiczna granica pomiedzy fizycznym torem pomiarowym a metodami analizy.
 * Metody obliczeniowe maja dostawac POMIAR_S11/SERIA_S11 i nie powinny
 * wywolywac DSP_Measure(), OSL_CorrectZ() ani generatora bezposrednio.
 */
bool POMIAR_S11_PobierzPunkt(uint32_t czestotliwosc_hz,
                             const POMIAR_S11_USTAWIENIA_t *ustawienia,
                             POMIAR_S11_t *wynik);

bool POMIAR_S11_PobierzSerie(const uint32_t *czestotliwosci_hz, uint16_t liczba,
                             const POMIAR_S11_USTAWIENIA_t *ustawienia,
                             SERIA_S11_t *seria);

float complex POMIAR_S11_GammaZImpedancji(float complex impedancja_ohm, float z0_ohm);
float complex POMIAR_S11_ImpedancjaZGamma(float complex gamma, float z0_ohm);

/* Aliasy zgodnosci z v2.03-test2. Nazwy byly semantycznie odwrocone. */
float complex POMIAR_S11_GammaNaImpedancje(float complex gamma, float z0_ohm);
float complex POMIAR_S11_ImpedancjaNaGamma(float complex impedancja_ohm, float z0_ohm);

#endif
