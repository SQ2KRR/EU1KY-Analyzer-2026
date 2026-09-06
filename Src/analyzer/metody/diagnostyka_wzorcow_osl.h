#ifndef DIAGNOSTYKA_WZORCOW_OSL_H
#define DIAGNOSTYKA_WZORCOW_OSL_H

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

#define OSL_DIAG_MAX_PUNKTOW 41U

typedef enum
{
    OSL_DIAG_SHORT = 0,
    OSL_DIAG_LOAD = 1,
    OSL_DIAG_OPEN = 2
} OSL_DIAG_TYP_t;

typedef struct
{
    uint32_t czestotliwosc_hz;
    float complex gamma_przed_osl;
    uint8_t poprawny;
} OSL_DIAG_PUNKT_t;

typedef struct
{
    uint16_t liczba_punktow;
    uint16_t liczba_poprawnych;
    uint32_t f_min_hz;
    uint32_t f_max_hz;
    OSL_DIAG_TYP_t typ;
    OSL_DIAG_PUNKT_t punkty[OSL_DIAG_MAX_PUNKTOW];
} OSL_DIAG_PROFIL_t;

typedef struct
{
    uint16_t liczba_porownanych;
    float rms_delta_gamma;
    float max_delta_gamma;
    float rms_delta_amplitudy_db;
    float rms_delta_fazy_stopnie;
} OSL_DIAG_POROWNANIE_t;

bool OSL_DIAG_Utworz(OSL_DIAG_TYP_t typ,
                     const uint32_t *czestotliwosci_hz,
                     const float complex *gamma_przed_osl,
                     const uint8_t *poprawny,
                     uint16_t liczba,
                     OSL_DIAG_PROFIL_t *profil);

bool OSL_DIAG_Porownaj(const OSL_DIAG_PROFIL_t *baza,
                       const OSL_DIAG_PROFIL_t *aktualny,
                       OSL_DIAG_POROWNANIE_t *wynik);

bool OSL_DIAG_Zapisz(const OSL_DIAG_PROFIL_t *profil);
bool OSL_DIAG_Wczytaj(OSL_DIAG_TYP_t typ, OSL_DIAG_PROFIL_t *profil);
bool OSL_DIAG_CzyIstnieje(OSL_DIAG_TYP_t typ);
const char *OSL_DIAG_NazwaTypu(OSL_DIAG_TYP_t typ);

#endif
