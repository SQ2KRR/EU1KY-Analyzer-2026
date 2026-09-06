#include "zgloszenie_bledu.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "diagnostyka.h"
#include "DS3231.h"
#include "ff.h"
#include "screenshot.h"
#include "wersja_projektu.h"

extern uint32_t RTCpresent;

static uint32_t zgloszenie_ostatnie_bledy;

uint32_t ZGLOSZENIE_OstatnieBledy(void)
{
    return zgloszenie_ostatnie_bledy;
}

static int ZGLOSZENIE_ZapewnijKatalog(const char *sciezka)
{
    const FRESULT wynik = f_mkdir(sciezka);
    return wynik == FR_OK || wynik == FR_EXIST;
}

static int ZGLOSZENIE_CzyGrafika(const char *nazwa)
{
    const char *kropka;

    if (nazwa == NULL)
        return 0;
    kropka = strrchr(nazwa, '.');
    if (kropka == NULL)
        return 0;
    return strcasecmp(kropka, ".bmp") == 0 || strcasecmp(kropka, ".png") == 0;
}

static int ZGLOSZENIE_ZnajdzOstatniZrzut(char *sciezka, size_t rozmiar)
{
    DIR katalog = {0};
    FILINFO info = {0};
    uint32_t najlepszy_czas = 0U;
    char najlepsza_nazwa[64] = {0};

    if (sciezka == NULL || rozmiar == 0U)
        return 0;
    sciezka[0] = '\0';

    if (f_opendir(&katalog, "/aa/snapshot") != FR_OK)
        return 0;

    for (;;)
    {
        uint32_t znacznik;
        if (f_readdir(&katalog, &info) != FR_OK || info.fname[0] == '\0')
            break;
        if ((info.fattrib & AM_DIR) != 0U || !ZGLOSZENIE_CzyGrafika(info.fname))
            continue;

        znacznik = ((uint32_t)info.fdate << 16U) | (uint32_t)info.ftime;
        if (najlepsza_nazwa[0] == '\0' || znacznik > najlepszy_czas ||
            (znacznik == najlepszy_czas && strcmp(info.fname, najlepsza_nazwa) > 0))
        {
            najlepszy_czas = znacznik;
            strncpy(najlepsza_nazwa, info.fname, sizeof(najlepsza_nazwa) - 1U);
            najlepsza_nazwa[sizeof(najlepsza_nazwa) - 1U] = '\0';
        }
    }
    f_closedir(&katalog);

    if (najlepsza_nazwa[0] == '\0')
        return 0;

    snprintf(sciezka, rozmiar, "/aa/snapshot/%s", najlepsza_nazwa);
    return 1;
}

static int ZGLOSZENIE_KopiujPlik(const char *zrodlo, const char *cel)
{
    FIL wejscie = {0};
    FIL wyjscie = {0};
    uint8_t bufor[1024];
    int poprawny = 0;

    if (zrodlo == NULL || cel == NULL)
        return 0;
    if (f_open(&wejscie, zrodlo, FA_READ) != FR_OK)
        return 0;
    if (f_open(&wyjscie, cel, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        f_close(&wejscie);
        return 0;
    }

    for (;;)
    {
        UINT odczytano = 0U;
        UINT zapisano = 0U;
        FRESULT fr = f_read(&wejscie, bufor, sizeof(bufor), &odczytano);
        if (fr != FR_OK)
            break;
        if (odczytano == 0U)
        {
            poprawny = 1;
            break;
        }
        if (f_write(&wyjscie, bufor, odczytano, &zapisano) != FR_OK || zapisano != odczytano)
            break;
    }

    if (f_close(&wejscie) != FR_OK)
        poprawny = 0;
    if (f_close(&wyjscie) != FR_OK)
        poprawny = 0;
    if (!poprawny)
        f_unlink(cel);
    return poprawny;
}

static int ZGLOSZENIE_ZapiszOpis(const char *sciezka, const char *ostatni_zrzut)
{
    FIL plik = {0};
    char tekst[768];
    int n;
    UINT zapisano = 0U;

    if (f_open(&plik, sciezka, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return 0;

    n = snprintf(tekst, sizeof(tekst),
                 "EU1KY-PL 2026 - pakiet zgłoszenia błędu\r\n"
                 "format=EU1KY-BUG-1\r\n"
                 "firmware=%s\r\n"
                 "\r\n"
                 "Wyślij cały ten katalog razem z krótkim opisem:\r\n"
                 "1. Co robiłeś przed wystąpieniem problemu.\r\n"
                 "2. Jakiego wyniku oczekiwałeś.\r\n"
                 "3. Co pokazał analizator.\r\n"
                 "4. Czy problem jest powtarzalny.\r\n"
                 "\r\n"
                 "ekran_biezacy=ekran.bmp\r\n"
                 "raport=raport.txt\r\n"
                 "ostatni_zrzut=%s\r\n"
                 "\r\n"
                 "Pakiet ma charakter diagnostyczny. Jego utworzenie nie zmienia\r\n"
                 "kalibracji ani konfiguracji przyrządu.\r\n",
                 PROJEKT_WERSJA,
                 (ostatni_zrzut != NULL && ostatni_zrzut[0] != '\0') ? "dolaczony" : "brak");

    if (n < 0 || (size_t)n >= sizeof(tekst) ||
        f_write(&plik, tekst, (UINT)n, &zapisano) != FR_OK || zapisano != (UINT)n)
    {
        f_close(&plik);
        f_unlink(sciezka);
        return 0;
    }
    return f_close(&plik) == FR_OK;
}

int ZGLOSZENIE_UtworzPakiet(char *folder_wynik, size_t rozmiar_folderu)
{
    uint32_t data = 0U;
    uint32_t czas = 0U;
    uint32_t bledy = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;
    char folder[72];
    char sciezka[96];
    char ostatni_zrzut[96];
    char rozszerzenie[8] = ".bmp";
    const char *kropka;

    zgloszenie_ostatnie_bledy = 0U;
    if (folder_wynik != NULL && rozmiar_folderu > 0U)
        folder_wynik[0] = '\0';

    if (!CFG_CzyKartaSDDostepna())
    {
        zgloszenie_ostatnie_bledy = ZGLOSZENIE_BLAD_BRAK_SD;
        return 0;
    }

    if (RTCpresent)
    {
        getDate(&data);
        getTime(&czas, &sekunda, &ampm, 0);
    }

    if (!ZGLOSZENIE_ZapewnijKatalog("/aa"))
        bledy |= ZGLOSZENIE_BLAD_KATALOG_AA;
    if (bledy == 0U && !ZGLOSZENIE_ZapewnijKatalog("/aa/zgl"))
        bledy |= ZGLOSZENIE_BLAD_KATALOG_ZGLOSZEN;

    if (data >= 19800101U && data <= 20991231U && czas <= 2359U)
    {
        char katalog_daty[40];
        snprintf(katalog_daty, sizeof(katalog_daty), "/aa/zgl/%08lu",
                 (unsigned long)data);
        if (bledy == 0U && !ZGLOSZENIE_ZapewnijKatalog(katalog_daty))
            bledy |= ZGLOSZENIE_BLAD_KATALOG_PAKIETU;
        snprintf(folder, sizeof(folder), "%s/%04lu%02u", katalog_daty,
                 (unsigned long)czas, (unsigned)sekunda);
    }
    else
    {
        snprintf(folder, sizeof(folder), "/aa/zgl/LAST");
    }

    if (bledy == 0U && !ZGLOSZENIE_ZapewnijKatalog(folder))
        bledy |= ZGLOSZENIE_BLAD_KATALOG_PAKIETU;

    if (bledy == 0U)
    {
        snprintf(sciezka, sizeof(sciezka), "%s/ekran.bmp", folder);
        if (!SCREENSHOT_ZapiszBMPDoPliku(sciezka))
            bledy |= ZGLOSZENIE_BLAD_EKRAN;

        snprintf(sciezka, sizeof(sciezka), "%s/raport.txt", folder);
        if (!DIAGNOSTYKA_ZapiszRaportDo(sciezka))
            bledy |= ZGLOSZENIE_BLAD_RAPORT;

        ostatni_zrzut[0] = '\0';
        if (ZGLOSZENIE_ZnajdzOstatniZrzut(ostatni_zrzut, sizeof(ostatni_zrzut)))
        {
            kropka = strrchr(ostatni_zrzut, '.');
            if (kropka != NULL && strlen(kropka) < sizeof(rozszerzenie))
                strncpy(rozszerzenie, kropka, sizeof(rozszerzenie) - 1U);
            rozszerzenie[sizeof(rozszerzenie) - 1U] = '\0';
            snprintf(sciezka, sizeof(sciezka), "%s/ostatni%s", folder, rozszerzenie);
            if (!ZGLOSZENIE_KopiujPlik(ostatni_zrzut, sciezka))
                ostatni_zrzut[0] = '\0';
        }

        snprintf(sciezka, sizeof(sciezka), "%s/README.txt", folder);
        if (!ZGLOSZENIE_ZapiszOpis(sciezka, ostatni_zrzut))
            bledy |= ZGLOSZENIE_BLAD_README;
    }
    else
    {
        ostatni_zrzut[0] = '\0';
    }

    if (folder_wynik != NULL && rozmiar_folderu > 0U &&
        (bledy & (ZGLOSZENIE_BLAD_KATALOG_AA | ZGLOSZENIE_BLAD_KATALOG_ZGLOSZEN |
                  ZGLOSZENIE_BLAD_KATALOG_PAKIETU)) == 0U)
    {
        strncpy(folder_wynik, folder, rozmiar_folderu - 1U);
        folder_wynik[rozmiar_folderu - 1U] = '\0';
    }

    /* Ostatni zrzut jest opcjonalny; rdzeń pakietu to ekran + raport + opis. */
    zgloszenie_ostatnie_bledy = bledy;
    return bledy == 0U;
}
