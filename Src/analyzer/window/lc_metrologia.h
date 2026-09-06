#ifndef LC_METROLOGIA_H
#define LC_METROLOGIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LCM_TRYB_INDUKCYJNOSC = 0,
    LCM_TRYB_POJEMNOSC = 1
} LCM_TRYB_t;

typedef struct
{
    uint32_t indeks;
    uint32_t czestotliwosc_hz;
    uint32_t liczba_punktow;
    float wartosc;
    float srednia;
    float zmiennosc_proc;
    float reaktancja_ohm;
    float wspolczynnik_czulosc;
    float ocena;
} LCM_WYNIK_WYBORU_t;

typedef struct
{
    uint32_t indeks_zakresu;
    LCM_WYNIK_WYBORU_t wybor;
} LCM_WYNIK_DOBORU_ZAKRESU_t;

/*
 * Przeliczenia dla modelu elementu szeregowego Z = R + jX.
 * Funkcje odrzucają niewłaściwy znak reaktancji oraz dane niefinitywne.
 * Wynik L jest w uH, a C w pF.
 */
int LCM_ObliczIndukcyjnosc_uH(float reaktancja_ohm, uint32_t czestotliwosc_hz, float *wynik_uH);
int LCM_ObliczPojemnosc_pF(float reaktancja_ohm, uint32_t czestotliwosc_hz, float *wynik_pF);

/*
 * Wybiera reprezentatywny punkt z ciągłego obszaru poprawnych danych. Kryterium
 * łączy stabilność modelu L/C z liczbą próbek; dłuższy fragment dostaje premię,
 * ale nie wygrywa automatycznie z dużo stabilniejszym fragmentem. Zmienność to
 * współczynnik zmienności wartości L/C, a nie niepewność przyrządu.
 */
int LCM_WybierzPunkt(const float *reaktancja_ohm,
                     const uint32_t *czestotliwosc_hz,
                     const uint8_t *poprawny,
                     uint32_t liczba,
                     LCM_TRYB_t tryb,
                     LCM_WYNIK_WYBORU_t *wynik);

/*
 * Wersja przeznaczona dla automatu L/C. Oprocz stabilnosci modelu uwzglednia
 * czulosc mostka. Dla czysto reaktywnego elementu najmniejszy względny blad
 * wynikajacy z bledu fazy wypada w poblizu |X| = Z0, dlatego automat preferuje
 * taki punkt zamiast skrajnie malej lub skrajnie duzej reaktancji.
 *
 * z0_ohm powinno odpowiadac impedancji odniesienia toru, zwykle 50 omow.
 */
int LCM_WybierzPunktAutomatyczny(const float *reaktancja_ohm,
                                 const uint32_t *czestotliwosc_hz,
                                 const uint8_t *poprawny,
                                 uint32_t liczba,
                                 LCM_TRYB_t tryb,
                                 float z0_ohm,
                                 LCM_WYNIK_WYBORU_t *wynik);

/*
 * Porównuje kilka zdefiniowanych zakresów tym samym kryterium stabilności i
 * liczby próbek. Funkcja nie zmienia danych ani konfiguracji.
 */
int LCM_WybierzZakresAutomatyczny(const float *reaktancja_ohm,
                                   const uint32_t *czestotliwosc_hz,
                                   const uint8_t *poprawny,
                                   uint32_t liczba,
                                   const uint32_t *granice_hz,
                                   uint32_t liczba_zakresow,
                                   LCM_TRYB_t tryb,
                                   LCM_WYNIK_DOBORU_ZAKRESU_t *wynik);

#ifdef __cplusplus
}
#endif

#endif
