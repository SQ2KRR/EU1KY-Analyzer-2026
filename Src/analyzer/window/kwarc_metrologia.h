#ifndef _KWARC_METROLOGIA_H_
#define _KWARC_METROLOGIA_H_

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    float fs_hz;
    float fp_hz;
    float rm_ohm;
    float c0_f;
    float cm_f;
    float lm_h;
    float q;
} KWARC_WYNIK_t;

/*
 * Oblicza pojemnosc rownolegla widziana w punkcie pomiarowym.
 * Funkcja jest przeznaczona do pomiaru C0 kwarcu i pojemnosci uchwytu
 * w czestotliwosci odsunietej od rezonansu. Zwraca false, gdy pomiar
 * nie odpowiada stabilnej reaktancji pojemnosciowej.
 */
bool KWARC_ObliczPojemnoscRownolegla(float complex z, float czestotliwosc_hz,
                                     float *pojemnosc_f);

/*
 * Szuka rezonansu szeregowego galezi motionalnej modelu BVD. Najpierw
 * od admitancji zmierzonego kwarcu odejmowana jest admitancja j*w*C0,
 * a dopiero potem szukane jest przejscie Im(Zm) przez zero. Dzieki temu
 * pojemnosc statyczna C0 nie przesuwa sztucznie kryterium Fs.
 */
bool KWARC_ZnajdzRezonansSzeregowy(const float complex *z, const uint8_t *maska,
                                    size_t liczba_punktow, float f_start_hz,
                                    float krok_hz, float f_oczekiwana_hz, float c0_f,
                                    float *fs_hz, float *rm_ohm);

/*
 * Szuka rezonansu rownoleglego (antyrezonansu) jako przejscia
 * susceptancji B = Im(1/Z) z wartosci ujemnej na dodatnia.
 * To kryterium odpowiada warunkowi zerowej susceptancji admitancji.
 */
bool KWARC_ZnajdzRezonansRownolegly(const float complex *z, const uint8_t *maska,
                                    size_t liczba_punktow, float f_start_hz,
                                    float krok_hz, float f_oczekiwana_hz,
                                    float *fp_hz);

/*
 * Wyznacza parametry klasycznego modelu Butterwortha-Van Dyke'a:
 * C0 rownolegle do galezi Rm-Lm-Cm. Cm jest liczone z pelnego zwiazku
 * fp/fs, bez przyblizenia 2*(fp-fs)/fs stosowanego w starym kodzie.
 */
bool KWARC_ObliczModel(float fs_hz, float fp_hz, float rm_ohm, float c0_f,
                       KWARC_WYNIK_t *wynik);

#endif
