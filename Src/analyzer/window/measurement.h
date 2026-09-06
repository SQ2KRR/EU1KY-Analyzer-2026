/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef MEASUREMENT_H_
#define MEASUREMENT_H_

#include <stdint.h>
#include "strojenie_antena.h"
extern uint32_t Timer5Value;
extern uint16_t TimeFlag;
void Single_Frequency_Proc(void);
void FIncr(uint32_t step);
void FDecr(uint32_t step);
void MEASUREMENT_WeryfikacjaWzorcami(void);

/*
 * Wspólny skan rezonansu dla asystenta strojenia i modułu projektowania anten.
 * Zwraca 1 tylko wtedy, gdy seria pomiarowa pozwala wykonać analizę.
 */
uint8_t MEASUREMENT_SkanujRezonansAntena(uint32_t czestotliwosc_docelowa_hz,
                                          STROJENIE_ANTENA_WYNIK_t *wynik);

/*
 * Interfejs specjalnego buildu dokumentacyjnego. Wykonuje prawdziwy pomiar
 * przez ten sam tor S11 co ekran użytkownika i rysuje wynik z zatrzymanych
 * danych. Nie podstawia danych przykładowych.
 */
typedef enum
{
    POMIAR_DOK_WIDOK_WYNIK = 0,
    POMIAR_DOK_WIDOK_ANALIZA
} POMIAR_DOK_WIDOK_t;

uint8_t MEASUREMENT_DokumentacjaRealnaZmierz(uint32_t czestotliwosc_hz, uint8_t liczba_usrednien);
void MEASUREMENT_DokumentacjaRealnaRysuj(POMIAR_DOK_WIDOK_t widok);

#endif
