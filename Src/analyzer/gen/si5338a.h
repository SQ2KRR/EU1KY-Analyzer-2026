#ifndef _SI5338A_H_
#define _SI5338A_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sterownik Si5338A dla EU1KY-PL 2026 test15.
 *
 * Bezpieczny tryb produkcyjny NIE przebudowuje petli PLL/VCO ukladu. Zaklada,
 * ze plytka ma juz poprawnie skonfigurowany i zablokowany PLL, a rzeczywista
 * czestotliwosc VCO jest wpisana w CFG_PARAM_SI5338_VCO_FREQ. Zmieniane sa
 * tylko dzielniki wyjsciowych MultiSynth. Pozwala to obslugiwac rozne plytki
 * bez zgadywania ich zrodla odniesienia i okablowania PLL.
 */
void SI5338A_Init(void);
void SI5338A_Off(void);
void SI5338A_SetF0(uint32_t fhz);
void SI5338A_SetLO(uint32_t fhz);

uint8_t SI5338A_Detect(void);
uint8_t SI5338A_IsPresent(void);
uint8_t SI5338A_GetBusAddress(void);     /* adres 8-bitowy zgodny z BSP */
uint8_t SI5338A_GetRevision(void);
uint32_t SI5338A_MinFreq(void);
uint32_t SI5338A_MaxFreq(void);
int SI5338A_CanSet(uint32_t fhz);

#ifdef __cplusplus
}
#endif

#endif
