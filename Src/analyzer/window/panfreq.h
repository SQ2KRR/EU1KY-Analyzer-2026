/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef PANFREQ_H_
#define PANFREQ_H_

#include <stdint.h>
#include <stdbool.h>
#include "panvswr2.h"

bool PanFreqWindow(uint32_t *pFkhz, BANDSPAN *pBs);
/* Wspólny kafelkowy wybór pasma dla ekranów z pojedynczą częstotliwością. */
bool PanFreq_WybierzPasmoCzestotliwosci(uint32_t *czestotliwosc_hz);
bool PanFreq_WybierzPasmoCzestotliwosciEx(uint32_t *czestotliwosc_hz,
                                           uint32_t minimum_hz, uint32_t maksimum_hz);
/* Wspólny wybór pełnych granic pasma. Przydatny dla skanerów, które
 * potrzebują zakresu, a nie tylko częstotliwości środkowej. */
bool PanFreq_WybierzPasmoZakresEx(uint32_t *dol_hz, uint32_t *gora_hz,
                                  uint32_t minimum_hz, uint32_t maksimum_hz);
/* Wspólny kafelkowy wybór szerokości z możliwością ograniczenia maksymalnej
 * wartości. Konkretna funkcja przekazuje własny limit wynikający z dostępnego
 * zakresu RF i sposobu prezentacji. */
bool PanFreq_WybierzSzerokoscEx(uint32_t czestotliwosc_hz, BANDSPAN *zakres,
                                BANDSPAN maksimum);
void MultiSWR_Proc(void);
void GetBS(uint32_t);
void PANFREQ_DokumentacjaRealnaRysuj(uint32_t start_hz, BANDSPAN zakres);

#endif
