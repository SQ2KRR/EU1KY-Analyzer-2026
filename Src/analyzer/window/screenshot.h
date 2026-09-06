/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#ifndef SCREENSHOT_H_
#define SCREENSHOT_H_

#include <stdint.h>

#define SCREENSHOT_FILE_SIZE 391734
#define SCREENSHOT_NAZWA_MAX 13
#define SCREENSHOT_DATA_MAX 11
#define SCREENSHOT_CZAS_MAX 6
#define SCREENSHOT_PLIKOW_NA_STRONIE 8

typedef struct
{
    char nazwa[SCREENSHOT_NAZWA_MAX];
    char data[SCREENSHOT_DATA_MAX];
    char czas[SCREENSHOT_CZAS_MAX];
    uint32_t rozmiar_b;
} SCREENSHOT_PLIK_t;

typedef enum
{
    SCREENSHOT_ETAP_OK = 0,
    SCREENSHOT_ETAP_PARAMETRY,
    SCREENSHOT_ETAP_BUDOWA_OBRAZU,
    SCREENSHOT_ETAP_OTWARCIE,
    SCREENSHOT_ETAP_ZAPIS,
    SCREENSHOT_ETAP_SYNCHRONIZACJA,
    SCREENSHOT_ETAP_ZAMKNIECIE,
    SCREENSHOT_ETAP_WERYFIKACJA
} SCREENSHOT_ETAP_ZAPISU_t;

typedef struct
{
    SCREENSHOT_ETAP_ZAPISU_t etap;
    uint8_t kod_fatfs;
    uint32_t zapisano_b;
    uint32_t oczekiwano_b;
} SCREENSHOT_DIAGNOSTYKA_ZAPISU_t;

void SCREENSHOT_Show(const char *fname);
char *SCREENSHOT_SelectFileName(void);
int16_t SCREENSHOT_SelectFileNames(int k);
void SCREENSHOT_DeleteOldest(void);
void SCREENSHOT_Save(const char *fname);
uint8_t SCREENSHOT_ZapiszBMPDoPliku(const char *sciezka);
void SCREENSHOT_PobierzDiagnostykeZapisu(SCREENSHOT_DIAGNOSTYKA_ZAPISU_t *diagnostyka);
const char *SCREENSHOT_NazwaEtapuZapisu(SCREENSHOT_ETAP_ZAPISU_t etap);
void SCREENSHOT_SavePNG(const char *fname);
void SCREENSHOT_ShowPicture(uint16_t Pointer1);
void SCREENSHOT_DeleteFile(uint16_t Pointer1);
void Date_Time_Stamp(void);
int32_t ShowLogo(void);
int32_t SCREENSHOT_RysujPNGZPamieci(const uint8_t *dane, uint32_t rozmiar);

/*
 * Interfejs listy zrzutow. Warstwa plikowa zwraca tylko dane o plikach;
 * ekran listy jest rysowany w mainwnd.c. Dzieki temu odczyt katalogu nie
 * modyfikuje LCD i moze byc bezpiecznie uzywany przez dotyk oraz enkoder.
 */
int16_t SCREENSHOT_WczytajStrone(uint16_t pierwszy,
                                 SCREENSHOT_PLIK_t *pliki,
                                 uint16_t pojemnosc,
                                 uint16_t *liczba_wszystkich);
uint8_t SCREENSHOT_PokazPlik(const char *nazwa);
uint8_t SCREENSHOT_PokazSciezke(const char *sciezka);
uint8_t SCREENSHOT_UsunPlik(const char *nazwa);

#endif
