#ifndef UI_LISTA_LOGIKA_H_
#define UI_LISTA_LOGIKA_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Logika przewijanej listy dla UI 2026.
 *
 * Moduł nie zna LCD, dotyku ani enkodera. Przechowuje wyłącznie pozycję
 * fokusu i pierwszy widoczny wiersz. Dzięki temu tę samą logikę można
 * stosować w ustawieniach, plikach, kalibracji i serwisie, a testy mogą
 * uruchamiać się na komputerze bez HAL STM32.
 */
typedef struct
{
    uint16_t pierwszy_widoczny;
    uint16_t zaznaczony;
    uint16_t liczba_pozycji;
    uint16_t liczba_widocznych;
    bool fokus_widoczny;
} UI_LISTA_STAN_t;

void UI_LISTA_Init(UI_LISTA_STAN_t *stan, uint16_t liczba_pozycji,
                   uint16_t liczba_widocznych);
void UI_LISTA_UstawLiczbe(UI_LISTA_STAN_t *stan, uint16_t liczba_pozycji);
void UI_LISTA_UstawZaznaczony(UI_LISTA_STAN_t *stan, uint16_t indeks,
                              bool pokaz_fokus);
bool UI_LISTA_PrzesunFokus(UI_LISTA_STAN_t *stan, int8_t kierunek,
                           const uint8_t *aktywne);
void UI_LISTA_Przewin(UI_LISTA_STAN_t *stan, int16_t wiersze);
bool UI_LISTA_CzyWidoczny(const UI_LISTA_STAN_t *stan, uint16_t indeks);
uint16_t UI_LISTA_OstatniWidoczny(const UI_LISTA_STAN_t *stan);

#endif /* UI_LISTA_LOGIKA_H_ */
