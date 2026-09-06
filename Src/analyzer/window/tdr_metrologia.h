#ifndef TDR_METROLOGIA_H_
#define TDR_METROLOGIA_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Czysta matematyka osi czasu i odleglosci TDR.
 *
 * Modul nie odwoluje sie do LCD, DSP ani konfiguracji STM32. Dzieki temu
 * rownania uzywane przez firmware mozna kompilowac i sprawdzac rowniez
 * zwyklym kompilatorem na komputerze.
 */

#define TDRM_PREDKOSC_SWIATLA_M_NA_NS 0.299792458f

typedef struct
{
    float vf;
    float offset_m;
} TDRM_KALIBRACJA_DWU_PUNKTOWA_t;

/*
 * Dla probek widma rozstawionych o krok_hz i odwrotnej FFT o dlugosci
 * liczba_probek_czasu krok czasu wynosi:
 *
 *     dt = 1 / (krok_hz * liczba_probek_czasu)
 */
bool TDRM_ObliczKrokCzasuNs(uint32_t krok_hz, uint32_t liczba_probek_czasu,
                            float *krok_ns);

/* Zwraca czas odpowiadajacy indeksowi probki odpowiedzi czasowej. */
bool TDRM_ObliczCzasNs(float indeks_probki, uint32_t krok_hz,
                       uint32_t liczba_probek_czasu, float *czas_ns);

/*
 * Przelicza czas przelotu tam i z powrotem na odleglosc jednokierunkowa:
 *
 *     L = c * Vf * t / 2
 */
bool TDRM_ObliczOdlegloscM(float czas_ns, float vf, float *odleglosc_m);

/*
 * Wyznacza efektywny wspolczynnik Vf z kabla o znanej dlugosci i znanym
 * czasie odbicia. Jest to kalibracja jednopunktowa: koryguje skale, ale
 * nie potrafi oddzielic stalego przesuniecia plaszczyzny odniesienia.
 */
bool TDRM_WyznaczVf(float czas_ns, float znana_dlugosc_m, float *vf);

/*
 * Kalibracja dwupunktowa na podstawie dwoch znanych markerow odbicia.
 * Model:
 *
 *     L = c * Vf * t / 2 + offset
 *
 * Pozwala oddzielic skale (Vf) od stalego przesuniecia plaszczyzny
 * odniesienia. Funkcja jest przygotowana do pozniejszego trybu
 * zaawansowanego; test30 nie wymaga jej do normalnego pomiaru.
 */
bool TDRM_WyznaczKalibracjeDwuPunktowa(float czas1_ns, float dlugosc1_m,
                                       float czas2_ns, float dlugosc2_m,
                                       TDRM_KALIBRACJA_DWU_PUNKTOWA_t *wynik);

/* Przelicza czas przez opcjonalna kalibracje dwupunktowa. */
bool TDRM_ObliczOdlegloscSkorygowanaM(float czas_ns,
                                      const TDRM_KALIBRACJA_DWU_PUNKTOWA_t *kalibracja,
                                      float *odleglosc_m);

/* Standardowy zwiazek miedzy wspolczynnikiem odbicia i impedancja. */
bool TDRM_ObliczImpedancjeZGamma(float z0_ohm, float gamma, float *z_ohm);

#endif /* TDR_METROLOGIA_H_ */
