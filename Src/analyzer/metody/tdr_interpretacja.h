#ifndef TDR_INTERPRETACJA_H_
#define TDR_INTERPRETACJA_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Lekka interpretacja gotowej odpowiedzi czasowej TDR.
 *
 * Modul nie wykonuje pomiaru, FFT ani przeliczenia czasu na odleglosc. Dostaje
 * juz policzony przebieg i wskazuje tylko charakterystyczne odbicia. Dzięki temu
 * można rozwijać podpowiedzi dla użytkownika bez ryzyka zmiany rdzenia TDR.
 */

typedef enum
{
    TDRI_CHARAKTER_BRAK = 0,
    TDRI_CHARAKTER_WZROST_IMPEDANCJI,
    TDRI_CHARAKTER_SPADEK_IMPEDANCJI,
    TDRI_CHARAKTER_NIEJEDNOZNACZNY
} TDRI_CHARAKTER_t;

typedef enum
{
    TDRI_FLAGA_BRAK = 0U,
    TDRI_FLAGA_SLABY_SYGNAL = 1U << 0,
    TDRI_FLAGA_WIELE_ODBIC = 1U << 1,
    TDRI_FLAGA_BLISKO_POCZATKU = 1U << 2,
    TDRI_FLAGA_BLISKO_KONCA = 1U << 3
} TDRI_FLAGA_t;

typedef struct
{
    bool poprawny;
    uint16_t indeks_pierwszego;
    uint16_t indeks_najsilniejszego;
    /*
     * Dokładne położenie szczytu po interpolacji parabolicznej. Wartość
     * całkowita nadal wskazuje realną próbkę i pozostaje zgodna ze starszym UI.
     */
    float indeks_pierwszego_dokladny;
    float indeks_najsilniejszego_dokladny;
    float amplituda_pierwszego;
    float amplituda_najsilniejszego;
    float poziom_tla_rms;
    float stosunek_szczyt_tlo;
    TDRI_CHARAKTER_t charakter;
    uint32_t flagi;
} TDRI_WYNIK_t;

/*
 * Szuka pierwszego istotnego lokalnego maksimum oraz najsilniejszego odbicia.
 * Funkcja nie interpretuje amplitudy jako bezpośredniego Gamma i nie wylicza
 * impedancji. Znak odbicia służy wyłącznie do określenia kierunku zmiany Z.
 */
bool TDRI_Analizuj(const float *odpowiedz, uint16_t liczba, TDRI_WYNIK_t *wynik);

#endif /* TDR_INTERPRETACJA_H_ */
