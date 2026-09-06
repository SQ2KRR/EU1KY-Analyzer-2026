#ifndef GEN_H_
#define GEN_H_

#include <stdint.h>

#define GEN_PLAN_HARMONICZNY_WERSJA 4U

/*
 * RF160: bezpieczny punkt odniesienia dla Si5351. Sterownik historycznie
 * deklaruje 160 MHz jako maksymalna czestotliwosc MultiSynth. W tej wersji
 * szerokopasmowy pomiar Si5351 startuje od tego limitu zamiast 200 MHz.
 */
#define GEN_SI5351_LIMIT_BEZPIECZNY_HZ 160000000U
#define GEN_SI5351_HMAX_BEZPIECZNE 3U
#define GEN_SI5351_FMAX_EFEKTYWNE_HZ (GEN_SI5351_LIMIT_BEZPIECZNY_HZ * GEN_SI5351_HMAX_BEZPIECZNE)

#ifdef __cplusplus
extern "C"
{
#endif
    void GEN_Init(void);
    void GEN_SetMeasurementFreq(uint32_t fhz);
    uint32_t GEN_GetLastFreq(void);
    void GEN_SetLOFreq(uint32_t frqu1);
    void GEN_SetF0Freq(uint32_t frqu1);
    void GEN_SetClk2Freq(uint32_t frqu1);
    void GEN_SetTXFreq(uint32_t fhz);
    void GEN_WylaczTorPomiarowy(void);
    void GEN_WylaczClk2(void);


    uint8_t GEN_MaksHarmoniczna(void);
    uint8_t GEN_WyznaczHarmoniczna(uint32_t czestotliwosc_hz);
    uint32_t GEN_CzestotliwoscBazowa(uint32_t czestotliwosc_hz);
    uint32_t GEN_MinCzestotliwoscEfektywna(void);
    uint32_t GEN_MaksCzestotliwoscEfektywna(void);
    int GEN_CzyCzestotliwoscObslugiwana(uint32_t czestotliwosc_hz);
    uint32_t GEN_RezimPomiarowy(uint32_t czestotliwosc_hz);
    int GEN_CzyTenSamRezimPomiarowy(uint32_t a_hz, uint32_t b_hz);
    int GEN_CzyWyjscieDodatkoweObslugiwane(void);
    const char *GEN_PobierzNazweSyntezera(void);
#ifdef __cplusplus
}
#endif

#endif
