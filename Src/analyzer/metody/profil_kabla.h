#ifndef PROFIL_KABLA_H
#define PROFIL_KABLA_H

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

#include "kabel_linia.h"

#define PROFIL_KABLA_MAX_PUNKTOW 101U
#define PROFIL_KABLA_SCIEZKA "/aa/cable/profile.bin"
#define PROFIL_KABLA_MAX_BLAD_LOAD_PROC 10.0f

typedef struct
{
    uint32_t czestotliwosc_hz;
    KABEL_LINIA_PUNKT_t linia;
    uint8_t poprawny;
} PROFIL_KABLA_PUNKT_t;

typedef struct
{
    uint16_t liczba_punktow;
    uint16_t liczba_poprawnych;
    uint32_t f_min_hz;
    uint32_t f_max_hz;
    float z0_ohm;
    float load_rms_ohm;
    float load_max_proc;
    uint8_t load_sprawdzony;
    PROFIL_KABLA_PUNKT_t punkty[PROFIL_KABLA_MAX_PUNKTOW];
} PROFIL_KABLA_t;

bool PROFIL_KABLA_Utworz(const uint32_t *czestotliwosci_hz,
                         const float complex *z_open_ohm,
                         const float complex *z_short_ohm,
                         uint16_t liczba,
                         float z0_ohm,
                         PROFIL_KABLA_t *profil);

bool PROFIL_KABLA_Interpoluj(const PROFIL_KABLA_t *profil, uint32_t czestotliwosc_hz,
                             KABEL_LINIA_PUNKT_t *linia);

bool PROFIL_KABLA_Deembeduj(const PROFIL_KABLA_t *profil, uint32_t czestotliwosc_hz,
                            float complex z_wejscia_ohm, float complex *z_obciazenia_ohm);

bool PROFIL_KABLA_CzyZweryfikowany(const PROFIL_KABLA_t *profil);

bool PROFIL_KABLA_OcenLoad(PROFIL_KABLA_t *profil,
                           const uint32_t *czestotliwosci_hz,
                           const float complex *z_load_zmierzony_ohm,
                           uint16_t liczba, float complex z_load_wzorcowy_ohm);

bool PROFIL_KABLA_Zapisz(const PROFIL_KABLA_t *profil);
bool PROFIL_KABLA_Wczytaj(PROFIL_KABLA_t *profil);
bool PROFIL_KABLA_WczytajAktywny(void);
bool PROFIL_KABLA_CzyAktywnyWczytany(void);
bool PROFIL_KABLA_PobierzAktywny(PROFIL_KABLA_t *profil);
bool PROFIL_KABLA_DeembedujAktywny(uint32_t czestotliwosc_hz,
                                   float complex z_wejscia_ohm,
                                   float complex *z_obciazenia_ohm);
void PROFIL_KABLA_UniewaznijCache(void);

#endif
