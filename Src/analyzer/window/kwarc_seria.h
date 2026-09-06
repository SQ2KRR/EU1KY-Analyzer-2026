#ifndef _KWARC_SERIA_H_
#define _KWARC_SERIA_H_

#include <stdbool.h>
#include <stdint.h>

#include "kwarc_metrologia.h"

#define KWARC_SERIA_MAKS 16U
#define KWARC_ZESTAW_MAKS 8U

typedef struct
{
    uint8_t numer;
    KWARC_WYNIK_t wynik;
} KWARC_REKORD_t;

typedef struct
{
    KWARC_REKORD_t rekordy[KWARC_SERIA_MAKS];
    uint8_t liczba;
} KWARC_SERIA_t;

typedef struct
{
    bool poprawny;
    uint8_t liczba;
    uint8_t indeksy[KWARC_ZESTAW_MAKS];
    float srednia_fs_hz;
    float rozrzut_fs_hz;
    float sredni_rm_ohm;
    float rozrzut_rm_proc;
    float q_min;
    float q_srednie;
} KWARC_DOPASOWANIE_t;

/*
 * Sesja jest celowo mala i statyczna. Nie uzywa sterty ani plikow SD.
 * Numer rekordu odpowiada kolejnosci rzeczywistego pomiaru kwarcow.
 */
void KWARC_SERIA_Inicjalizuj(KWARC_SERIA_t *seria);
bool KWARC_SERIA_Dodaj(KWARC_SERIA_t *seria, const KWARC_WYNIK_t *wynik);

/*
 * Szuka najlepiej dopasowanej grupy 2..8 kwarcow z calej serii.
 * Kryterium podstawowe: najmniejszy rozrzut Fs (max-min).
 * Przy remisie wybieramy mniejszy względny rozrzut Rm, a na koncu
 * zestaw o wyzszej najgorszej dobroci Q. Fs jest kryterium podstawowym,
 * bo to ono najsilniej decyduje o dopasowaniu kwarcow filtra drabinkowego.
 */
bool KWARC_SERIA_Dobierz(const KWARC_SERIA_t *seria, uint8_t liczba_w_zestawie,
                         KWARC_DOPASOWANIE_t *dopasowanie);

bool KWARC_SERIA_CzyWybrany(const KWARC_DOPASOWANIE_t *dopasowanie, uint8_t indeks);

#endif
