#ifndef SPECTR_H_
#define SPECTR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    float CalcBin107(uint32_t frequency, int iterations, float *, float *, float *);
    void SPECTR_FindFreq(void); // ** WK **

    /*
     * Serwisowy pomiar względnego tła RF. Dla równomiernie rozłożonych
     * częstotliwości LO zwraca średnie tło FFT (bez najsilniejszego prążka
     * i jego sąsiadów) oraz lokalny szczyt. Wartości są surowe i służą
     * wyłącznie do porównań A/B wykonywanych tym samym torem.
     * Zwraca liczbę wykonanych punktów, -1 po przerwaniu, 0 po błędzie.
     */
    typedef void (*SPECTR_POSTEP_CB_t)(uint16_t wykonane, uint16_t calosc, uint32_t czestotliwosc_hz);

    int SPECTR_PomiarTlaSeria(uint32_t fmin_hz, uint32_t fmax_hz, uint16_t liczba_punktow,
                              float *tlo, float *szczyt);
    int SPECTR_PomiarTlaSeriaZPostepem(uint32_t fmin_hz, uint32_t fmax_hz, uint16_t liczba_punktow,
                                       float *tlo, float *szczyt, SPECTR_POSTEP_CB_t postep);

    /*
     * Generator dokumentacji: wykonuje prawdziwy wodospad na aktualnie
     * podłączonej antenie, a nie ekran demonstracyjny. Funkcja pozostawia
     * narysowany widok normalny; druga funkcja rysuje tę samą historię na
     * pełnym ekranie. Nie zapisują ani nie zmieniają konfiguracji użytkownika.
     */
    int SPECTR_DokumentacjaWodospadRealny(uint32_t fmin_hz, uint32_t fmax_hz, uint8_t przebiegi);
    void SPECTR_DokumentacjaWodospadPelny(uint32_t fmin_hz, uint32_t fmax_hz);
    extern float value1, value2, value3;
#endif
