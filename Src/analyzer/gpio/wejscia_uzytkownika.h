#ifndef WEJSCIA_UZYTKOWNIKA_H_
#define WEJSCIA_UZYTKOWNIKA_H_

#include <stdint.h>

/*
 * Fizyczne wejscia dodawane do nowej obudowy analizatora.
 *
 * Nie korzystamy z dawnego wejscia manipulatora CW. Piny PG6 i PG7 sa
 * przeznaczone na enkoder, bo w aktualnym firmware funkcja manipulatora
 * nie jest uzywana. Pozostale piny zostaly wybrane po sprawdzeniu calego
 * projektu oraz mapy zlacz Arduino plytki STM32F746G-DISCO.
 *
 * Wszystkie wejscia sa aktywne stanem niskim i korzystaja z wewnetrznych
 * rezystorow podciagajacych STM32. Do przyciskow nie nalezy podawac 5 V.
 */

typedef enum
{
    WEJSCIE_ZDARZENIE_BRAK = 0,
    WEJSCIE_ZDARZENIE_OBROT_LEWO,
    WEJSCIE_ZDARZENIE_OBROT_PRAWO,
    WEJSCIE_ZDARZENIE_OK,
    WEJSCIE_ZDARZENIE_WSTECZ,
    WEJSCIE_ZDARZENIE_START_STOP,
} WEJSCIE_ZDARZENIE_t;

typedef enum
{
    WEJSCIA_PRZYCISK_ENKODERA_OK = 0,
    WEJSCIA_PRZYCISK_ENKODERA_ZRZUT = 1
} WEJSCIA_TRYB_PRZYCISKU_ENKODERA_t;

void WEJSCIA_Init(void);
void WEJSCIA_Aktualizuj(void);
WEJSCIE_ZDARZENIE_t WEJSCIA_PobierzZdarzenie(void);
void WEJSCIA_WyczyscZdarzenia(void);

/*
 * W trybie ZRZUT nacisniecie osi enkodera nie trafia do kolejki jako OK.
 * Zamiast tego ustawiane jest jednorazowe zadanie zrzutu, odbierane przez
 * warstwe aplikacji. Rozdziela to obsluge stykow od zapisu na karte SD.
 */
void WEJSCIA_UstawTrybPrzyciskuEnkodera(WEJSCIA_TRYB_PRZYCISKU_ENKODERA_t tryb);
WEJSCIA_TRYB_PRZYCISKU_ENKODERA_t WEJSCIA_PobierzTrybPrzyciskuEnkodera(void);
uint8_t WEJSCIA_PobierzZadanieZrzutu(void);

#endif /* WEJSCIA_UZYTKOWNIKA_H_ */
