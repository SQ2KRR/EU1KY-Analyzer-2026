/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef _OSLCAL_H_
#define _OSLCAL_H_

#include <stdint.h>

uint8_t OSL_WybierzProfilWnd(void);
void OSL_CalWnd(void);
/* 0=anulowano/brak nowej HW, 1=HW OK i powrot, 2=HW OK i Dalej do OSL. */
uint8_t OSL_CalErrCorr(void);
void OSL_CalTXCorr(void);

uint32_t OSL_DokumentacjaLiczbaStron(void);
const char *OSL_DokumentacjaNazwaStrony(uint32_t strona);
void OSL_DokumentacjaRysujStrone(uint32_t strona);

#endif
