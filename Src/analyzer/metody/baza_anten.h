#ifndef _BAZA_ANTEN_H_
#define _BAZA_ANTEN_H_

#include <stdint.h>

#include "strojenie_antena.h"

#define ANTENA_BAZA_WERSJA_PROFILU 1U
#define ANTENA_BAZA_MAX_WYMIAROW   5U

typedef enum
{
    ANTENA_TYP_DIPOL_POLFALOWY = 0,
    ANTENA_TYP_W3DZZ,
    ANTENA_TYP_QUAD_1EL,
    ANTENA_TYP_QUAD_2EL,
    ANTENA_TYP_QUAD_3EL,
    ANTENA_TYP_TWINYAGI_SP2XDQ,
    ANTENA_TYP_YAGI3_DK7ZB_2M,
    ANTENA_TYP_EFHW_40_10,
    ANTENA_TYP_LICZBA
} ANTENA_TYP_t;

typedef enum
{
    ANTENA_POLE_BRAK = 0,
    ANTENA_POLE_DIPOL_DLUGOSC_CALKOWITA,
    ANTENA_POLE_W3DZZ_ODCINEK_WEWNETRZNY,
    ANTENA_POLE_W3DZZ_ODCINEK_ZEWNETRZNY,
    ANTENA_POLE_QUAD_OBWOD_ZASILANY,
    ANTENA_POLE_QUAD_OBWOD_REFLEKTORA,
    ANTENA_POLE_QUAD_OBWOD_DIREKTORA,
    ANTENA_POLE_QUAD_ODSTEP_R_Z,
    ANTENA_POLE_QUAD_ODSTEP_Z_D,
    ANTENA_POLE_TWIN_REFLEKTOR,
    ANTENA_POLE_TWIN_WIBRATOR,
    ANTENA_POLE_TWIN_D1,
    ANTENA_POLE_TWIN_D2,
    ANTENA_POLE_TWIN_D3,
    ANTENA_POLE_YAGI_REFLEKTOR,
    ANTENA_POLE_YAGI_WIBRATOR,
    ANTENA_POLE_YAGI_DIREKTOR,
    ANTENA_POLE_YAGI_ODSTEP_R_W,
    ANTENA_POLE_YAGI_ODSTEP_W_D,
    ANTENA_POLE_EFHW_DRUT
} ANTENA_POLE_t;

typedef struct
{
    uint32_t wersja;
    ANTENA_TYP_t typ;
    uint32_t czestotliwosc_docelowa_hz;

    /*
     * Wymiary są przechowywane w milimetrach. Znaczenie pozycji wynika z typu
     * anteny i jest opisane przez ANTENA_BAZA_PobierzPole(). Dzięki temu format
     * można rozszerzać bez wiązania części obliczeniowej z interfejsem LCD.
     */
    uint32_t wymiary_mm[ANTENA_BAZA_MAX_WYMIAROW];

    /* Parametry klasycznej W3DZZ z receptury użytej w pierwszej bazie. */
    uint32_t trap_czestotliwosc_hz;
    uint32_t trap_indukcyjnosc_nh;
    uint32_t trap_pojemnosc_pf_x10;

    /* Dwa kolejne pomiary tej samej anteny pozwalają zastąpić książkowe
       f~1/L lokalną czułością rzeczywistej konstrukcji. */
    uint32_t poprzedni_wymiar_strojony_mm;
    uint32_t poprzedni_rezonans_hz;
} ANTENA_PROFIL_t;

typedef struct
{
    ANTENA_POLE_t pole;
    uint32_t zalecane_mm;
    uint32_t minimum_mm;
    uint32_t maksimum_mm;
    uint8_t ma_zakres;
    uint8_t regulowane_z_s11;
} ANTENA_WYMIAR_ZALECANY_t;

typedef struct
{
    uint8_t liczba;
    ANTENA_WYMIAR_ZALECANY_t wymiary[ANTENA_BAZA_MAX_WYMIAROW];
} ANTENA_ZALECENIA_t;

typedef enum
{
    ANTENA_PEWNOSC_BRAK = 0,
    ANTENA_PEWNOSC_NISKA,
    ANTENA_PEWNOSC_SREDNIA,
    ANTENA_PEWNOSC_WYSOKA
} ANTENA_PEWNOSC_t;

typedef enum
{
    ANTENA_REGULACJA_BRAK = 0,
    ANTENA_REGULACJA_DIPOL_RAMIONA,
    ANTENA_REGULACJA_W3DZZ_ZEWNETRZNE,
    ANTENA_REGULACJA_W3DZZ_WEWNETRZNE,
    ANTENA_REGULACJA_QUAD_OBWOD_ZASILANY,
    ANTENA_REGULACJA_EFHW_DRUT
} ANTENA_REGULACJA_t;

typedef struct
{
    uint8_t dostepna;
    ANTENA_PEWNOSC_t pewnosc;
    ANTENA_REGULACJA_t regulacja;
    STROJENIE_ANTENA_KIERUNEK_t kierunek;

    uint32_t wymiar_biezacy_mm;
    float wymiar_nowy_mm;
    float zmiana_calkowita_mm;
    float zmiana_na_miejsce_mm;
    uint8_t liczba_miejsc_regulacji;

    /* Dla konstrukcji złożonych informuje UI, że sama proporcja f~1/L nie
       wystarcza do jednoznacznej regulacji i potrzebne jest ostrzeżenie. */
    uint8_t wymaga_ostroznosci;
    uint8_t uzyto_historii;
    float czulosc_hz_na_mm;
} ANTENA_KOREKTA_t;

void ANTENA_BAZA_InicjalizujProfil(ANTENA_PROFIL_t *profil, ANTENA_TYP_t typ,
                                   uint32_t czestotliwosc_docelowa_hz);
void ANTENA_BAZA_ObliczZalecenia(const ANTENA_PROFIL_t *profil,
                                 ANTENA_ZALECENIA_t *zalecenia);
ANTENA_POLE_t ANTENA_BAZA_PobierzPole(const ANTENA_PROFIL_t *profil, uint8_t indeks);
uint8_t ANTENA_BAZA_LiczbaWymiarow(const ANTENA_PROFIL_t *profil);

float ANTENA_BAZA_RoznicaProcent(uint32_t biezacy_mm, uint32_t zalecany_mm);
ANTENA_KOREKTA_t ANTENA_BAZA_WyznaczKorekte(const ANTENA_PROFIL_t *profil,
                                             const STROJENIE_ANTENA_WYNIK_t *pomiar);
uint32_t ANTENA_BAZA_WymiarStrojony(const ANTENA_PROFIL_t *profil);

#endif
