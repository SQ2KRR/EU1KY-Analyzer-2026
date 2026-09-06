#ifndef POROWNANIE_MODELI_ELEMENTU_H_
#define POROWNANIE_MODELI_ELEMENTU_H_

#include <stdbool.h>
#include <stdint.h>

#include "pomiar_s11.h"
#include "rejestr_metod.h"

/*
 * Wspólna ocena modeli elementu na tych samych punktach R+jX.
 *
 * test30 nie zmienia sposobu dopasowania istniejących metod. Jego zadaniem jest
 * dopiero porównanie gotowych modeli wspólną miarą błędu zespolonej impedancji
 * i karą za liczbę parametrów. Dzięki temu bardziej rozbudowany model nie
 * wygrywa automatycznie tylko dlatego, że ma więcej stopni swobody.
 */
#define POR_MODELI_MAX_BLAD_WZGLEDNY 0.15

/*
 * Dwa modele z różnicą AICc nie większą niż 2 traktujemy jako porównywalnie
 * dobrze podparte danymi. W takiej grupie rekomendujemy model prostszy.
 * To reguła wyboru modelu, nie próg niepewności metrologicznej.
 */
#define POR_MODELI_DELTA_AICC_PODOBNE 2.0

/*
 * Dolna granica reszty używanej tylko w AICc. Chroni ranking przed sytuacją,
 * w której różnica wynikająca wyłącznie z reprezentacji float (np. 0 kontra
 * kilka ULP) daje sztuczną przewagę modelowi bardziej złożonemu. Nie zmienia
 * raportowanego RMS i nie jest deklaracją dokładności pomiaru.
 */
#define POR_MODELI_MIN_BLAD_WZGLEDNY_AICC 1.0e-6

typedef enum
{
    POR_MODELI_IDEALNY = 0,
    POR_MODELI_RLC,
    POR_MODELI_RF_PASOZYTNICZY,
    POR_MODELI_CAUER_1,
    POR_MODELI_CAUER_3,
    POR_MODELI_LICZBA
} POR_MODELI_ID_t;

typedef enum
{
    POR_MODELI_JAKOSC_OK = 0U,
    POR_MODELI_JAKOSC_MALO_PUNKTOW = 1U << 0,
    POR_MODELI_JAKOSC_NIEOBSLUGIWANY = 1U << 1,
    POR_MODELI_JAKOSC_SLABE_DOPASOWANIE = 1U << 2,
    POR_MODELI_JAKOSC_ZRODLO_OSTRZEGA = 1U << 3,
    POR_MODELI_JAKOSC_BLAD_NUMERYCZNY = 1U << 4
} POR_MODELI_JAKOSC_t;

typedef struct
{
    POR_MODELI_ID_t id;
    bool obliczony;
    bool zaakceptowany;
    uint8_t liczba_parametrow;
    uint16_t liczba_punktow;
    uint32_t jakosc;

    /* RMS modułu błędu zespolonej impedancji, w omach. */
    double blad_rms_ohm;

    /* sqrt(sum(|Zpom-Zmod|^2) / sum(|Zpom|^2)). */
    double blad_wzgledny;

    /*
     * Skorygowane kryterium informacyjne Akaike (AICc).
     * Służy wyłącznie do porównania modeli na tym samym zestawie danych.
     * Nie jest niepewnością pomiaru ani deklaracją dokładności przyrządu.
     */
    double aicc;
    double delta_aicc;
} POR_MODELI_WYNIK_t;

typedef struct
{
    bool poprawny;
    bool ma_najlepszy;
    POR_MODELI_ID_t najlepszy;
    POR_MODELI_WYNIK_t model[POR_MODELI_LICZBA];
} POR_MODELI_POROWNANIE_t;

#define POR_MODELI_MASKA(id) (1UL << (uint32_t)(id))
#define POR_MODELI_MASKA_WSZYSTKIE ((1UL << (uint32_t)POR_MODELI_LICZBA) - 1UL)

bool POR_MODELI_PorownajMaska(const SERIA_S11_t *seria,
                              METODA_ELEMENT_TYP_t typ,
                              uint16_t indeks_reprezentatywny,
                              uint32_t maska_modeli,
                              POR_MODELI_POROWNANIE_t *wynik);

/* Zgodność z dotychczasowym API: porównaj wszystkie modele. */
bool POR_MODELI_Porownaj(const SERIA_S11_t *seria,
                         METODA_ELEMENT_TYP_t typ,
                         uint16_t indeks_reprezentatywny,
                         POR_MODELI_POROWNANIE_t *wynik);

const char *POR_MODELI_Nazwa(POR_MODELI_ID_t id);

#endif
