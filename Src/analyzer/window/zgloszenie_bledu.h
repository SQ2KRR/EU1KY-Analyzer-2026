#ifndef ZGLOSZENIE_BLEDU_H_
#define ZGLOSZENIE_BLEDU_H_

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    ZGLOSZENIE_BLAD_BRAK_SD = 1UL << 0,
    ZGLOSZENIE_BLAD_KATALOG_AA = 1UL << 1,
    ZGLOSZENIE_BLAD_KATALOG_ZGLOSZEN = 1UL << 2,
    ZGLOSZENIE_BLAD_KATALOG_PAKIETU = 1UL << 3,
    ZGLOSZENIE_BLAD_EKRAN = 1UL << 4,
    ZGLOSZENIE_BLAD_RAPORT = 1UL << 5,
    ZGLOSZENIE_BLAD_README = 1UL << 6
} ZGLOSZENIE_BLAD_t;

/*
 * Tworzy samowystarczalny pakiet dla testera: bieżący ekran, raport
 * diagnostyczny i - jeśli istnieje - ostatni zrzut zapisany wcześniej.
 * Funkcja nie zmienia konfiguracji ani kalibracji urządzenia.
 */
int ZGLOSZENIE_UtworzPakiet(char *folder_wynik, size_t rozmiar_folderu);
uint32_t ZGLOSZENIE_OstatnieBledy(void);

#endif /* ZGLOSZENIE_BLEDU_H_ */
