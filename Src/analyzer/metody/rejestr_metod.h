#ifndef _REJESTR_METOD_H_
#define _REJESTR_METOD_H_

#include <stdbool.h>
#include <stdint.h>

#include "metody_eksperymentalne.h"
#include "pomiar_s11.h"

typedef enum
{
    METODA_FLAGA_ZALECANA       = 1U << 0,
    METODA_FLAGA_EKSPERYMENTALNA = 1U << 1,
    METODA_FLAGA_TYLKO_ZAAWANSOWANY = 1U << 2
} METODA_FLAGA_t;

typedef enum
{
    METODA_JAKOSC_OK = 0,
    METODA_JAKOSC_MALO_PUNKTOW = 1U << 0,
    METODA_JAKOSC_SLABE_DOPASOWANIE = 1U << 1,
    METODA_JAKOSC_POZA_ZAKRESEM = 1U << 2,
    METODA_JAKOSC_BRAK_DANYCH = 1U << 3,
    METODA_JAKOSC_REZONANS_NA_BRZEGU = 1U << 4,
    METODA_JAKOSC_WYNIK_NIEZALECANY = 1U << 5,
    METODA_JAKOSC_ROZBIEZNOSC_METOD = 1U << 6
} METODA_JAKOSC_t;

typedef struct
{
    const char *identyfikator;
    const char *(*nazwa)(void);
    const char *(*opis)(void);
    uint32_t flagi;
} METODA_OPIS_t;

typedef enum
{
    METODA_ELEMENT_IDEALNA = 0,
    METODA_ELEMENT_RLC,
    METODA_ELEMENT_RF_PASOZYTNICZY,
    METODA_ELEMENT_LICZBA
} METODA_ELEMENT_ID_t;

typedef enum
{
    METODA_Q_3DB = 0,
    METODA_Q_LORENTZ,
    METODA_Q_RLC,
    METODA_Q_OKRAG,
    METODA_Q_LICZBA
} METODA_Q_ID_t;

typedef enum
{
    METODA_TDR_KBD = 0,
    METODA_TDR_PROSTOKATNE,
    METODA_TDR_HANN,
    METODA_TDR_HAMMING,
    METODA_TDR_BLACKMAN_HARRIS,
    METODA_TDR_KAISER,
    METODA_TDR_LICZBA
} METODA_TDR_ID_t;

typedef enum
{
    METODA_ELEMENT_CEWKA = 0,
    METODA_ELEMENT_KONDENSATOR = 1
} METODA_ELEMENT_TYP_t;

typedef struct
{
    const SERIA_S11_t *seria;
    uint16_t indeks_reprezentatywny;
    METODA_ELEMENT_TYP_t typ;
} METODA_ELEMENT_DANE_t;

typedef struct
{
    bool poprawny;
    float wartosc_h_lub_f;
    float l_h;
    float c_f;
    float r_ohm;
    float q;
    float f0_hz;
    float blad_rms_ohm;
    float pasozyt_f_lub_h;
    float srf_obserwowane_hz;
    MET_MODEL_RLC_t model_rlc;
    uint32_t jakosc;
} METODA_ELEMENT_WYNIK_t;

typedef struct
{
    const float *f_hz;
    const float *poziom_db;
    const float *amplituda;
    uint16_t liczba;
    bool rezonans_jako_maksimum;
    const SERIA_S11_t *seria_s11;
} METODA_Q_DANE_t;

typedef struct
{
    bool poprawny;
    float f0_hz;
    float q;
    float szerokosc_hz;
    float blad;
    uint32_t jakosc;
} METODA_Q_WYNIK_t;

typedef bool (*METODA_ELEMENT_FUNKCJA_t)(const METODA_ELEMENT_DANE_t *dane,
                                         METODA_ELEMENT_WYNIK_t *wynik);
typedef bool (*METODA_Q_FUNKCJA_t)(const METODA_Q_DANE_t *dane,
                                   METODA_Q_WYNIK_t *wynik);

typedef struct
{
    METODA_OPIS_t opis;
    METODA_ELEMENT_FUNKCJA_t oblicz;
} METODA_ELEMENT_REJESTR_t;

typedef struct
{
    METODA_OPIS_t opis;
    METODA_Q_FUNKCJA_t oblicz;
    float q_min_zalecane;
    float q_max_zalecane;
} METODA_Q_REJESTR_t;

typedef struct
{
    METODA_OPIS_t opis;
    METODA_TDR_ID_t id;
    MET_OKNO_t okno;
    bool uzywa_historycznego_kbd;
    bool ma_parametr_beta;
} METODA_TDR_REJESTR_t;

const METODA_ELEMENT_REJESTR_t *METODA_RejestrElementow(uint8_t *liczba);
const METODA_Q_REJESTR_t *METODA_RejestrQ(uint8_t *liczba);
const METODA_TDR_REJESTR_t *METODA_RejestrTDR(uint8_t *liczba);

bool METODA_CzyWidoczna(const METODA_OPIS_t *opis, bool tryb_zaawansowany);
uint8_t METODA_ZnajdzPoIdentyfikatorzeElementu(const char *identyfikator);
uint8_t METODA_ZnajdzPoIdentyfikatorzeQ(const char *identyfikator);
uint8_t METODA_ZnajdzPoIdentyfikatorzeTDR(const char *identyfikator);

/*
 * Publiczne wejscie do algorytmow. Ekrany nie powinny wywolywac wskaznika
 * oblicz z tablicy bezposrednio, bo wtedy ominelyby wspolna ocene jakosci.
 */
bool METODA_ObliczElement(uint8_t metoda, const METODA_ELEMENT_DANE_t *dane,
                          METODA_ELEMENT_WYNIK_t *wynik);
bool METODA_ObliczQ(uint8_t metoda, const METODA_Q_DANE_t *dane,
                    METODA_Q_WYNIK_t *wynik);
const char *METODA_TekstJakosci(uint32_t jakosc);

#endif
