#include "menedzer_plikow.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "LCD.h"
#include "config.h"
#include "ff.h"
#include "font.h"
#include "jezyk.h"
#include "komunikaty.h"
#include "screenshot.h"
#include "touch.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

#define PLIKI_NA_STRONIE 7U
#define PLIKI_SCIEZKA_MAX 96U
#define PLIKI_TEKST_PODGLAD_B 2048U
#define PLIKI_TEKST_ZNAKOW_W_WIERSZU 62U
#define PLIKI_TEKST_WIERSZY 10U

typedef struct
{
    char nazwa[13];
    DWORD rozmiar_b;
    WORD data;
    WORD czas;
    BYTE atrybuty;
} PLIKI_POZYCJA_t;

static char __attribute__((section(".user_sdram"))) pliki_bufor_tekstu[PLIKI_TEKST_PODGLAD_B];

static void PLIKI_ZbudujSciezke(const char *katalog, const char *nazwa,
                                char *wynik, size_t rozmiar);

static const char *PLIKI_Rozszerzenie(const char *nazwa)
{
    const char *kropka = strrchr(nazwa, '.');
    return kropka != NULL ? kropka : "";
}

static uint8_t PLIKI_CzyTekst(const char *nazwa)
{
    const char *rozszerzenie = PLIKI_Rozszerzenie(nazwa);
    return strcasecmp(rozszerzenie, ".txt") == 0 ||
           strcasecmp(rozszerzenie, ".log") == 0 ||
           strcasecmp(rozszerzenie, ".csv") == 0 ||
           strcasecmp(rozszerzenie, ".s1p") == 0;
}

static uint8_t PLIKI_CzyGrafika(const char *nazwa)
{
    const char *rozszerzenie = PLIKI_Rozszerzenie(nazwa);
    return strcasecmp(rozszerzenie, ".bmp") == 0 ||
           strcasecmp(rozszerzenie, ".png") == 0;
}

static uint8_t PLIKI_CzySystemowy(const PLIKI_POZYCJA_t *pozycja)
{
    const char *rozszerzenie;

    if (pozycja == NULL)
        return 0U;
    if ((pozycja->atrybuty & (AM_HID | AM_SYS)) != 0U)
        return 1U;

    rozszerzenie = PLIKI_Rozszerzenie(pozycja->nazwa);
    return strcasecmp(rozszerzenie, ".bin") == 0 ||
           strcasecmp(rozszerzenie, ".bak") == 0 ||
           strcasecmp(rozszerzenie, ".tmp") == 0 ||
           strcasecmp(rozszerzenie, ".osl") == 0;
}

static uint8_t PLIKI_CzySciezkaZaczynaSieOd(const char *sciezka, const char *prefiks)
{
    const size_t dlugosc = strlen(prefiks);
    return sciezka != NULL && strncmp(sciezka, prefiks, dlugosc) == 0 &&
           (sciezka[dlugosc] == '\0' || sciezka[dlugosc] == '/');
}

static uint8_t PLIKI_CzyMoznaUsunac(const char *katalog, const PLIKI_POZYCJA_t *pozycja)
{
    if (katalog == NULL || pozycja == NULL)
        return 0U;
    if ((pozycja->atrybuty & AM_DIR) != 0U)
        return 0U;
    if (PLIKI_CzySystemowy(pozycja))
        return 0U;

    /*
     * Kasowanie jest celowo konserwatywne. Nie wystarcza samo rozszerzenie:
     * w /aa/cal znajduje się np. log.csv kalibracji, a /aa/logo.png jest
     * grafiką startową. Takich plików nie wolno traktować jak zwykłego raportu.
     * Użytkownik może usuwać jedynie wyniki, raporty i zrzuty w katalogach
     * przeznaczonych na dane robocze. Katalogów nigdy nie kasujemy z GUI.
     */
    if (strcmp(katalog, "/aa") == 0)
    {
        return strcasecmp(pozycja->nazwa, "verify.csv") == 0 ||
               strcasecmp(pozycja->nazwa, "vlow.csv") == 0 ||
               strcasecmp(pozycja->nazwa, "vmid.csv") == 0 ||
               strcasecmp(pozycja->nazwa, "vhigh.csv") == 0 ||
               strcasecmp(pozycja->nazwa, "vctrl.csv") == 0 ||
               strcasecmp(pozycja->nazwa, "vband.csv") == 0;
    }

    if (PLIKI_CzySciezkaZaczynaSieOd(katalog, "/aa/diag") ||
        PLIKI_CzySciezkaZaczynaSieOd(katalog, "/aa/zgl") ||
        PLIKI_CzySciezkaZaczynaSieOd(katalog, "/aa/snapshot") ||
        PLIKI_CzySciezkaZaczynaSieOd(katalog, "/aa/osl_diag") ||
        PLIKI_CzySciezkaZaczynaSieOd(katalog, "/aa/cw"))
        return PLIKI_CzyTekst(pozycja->nazwa) || PLIKI_CzyGrafika(pozycja->nazwa);

    if (strcmp(katalog, "/aa/anteny") == 0 &&
        strcasecmp(pozycja->nazwa, "raport.txt") == 0)
        return 1U;

    return 0U;
}

static bool PLIKI_PotwierdzUsuniecie(const char *nazwa)
{
    UI_AKCJA_t akcje[2];
    uint8_t fokus = 0U;
    char tytul[64];
    char opis[180];

    snprintf(tytul, sizeof(tytul),
             JEZYK_Wybierz("Usunąć %s?", "Delete %s?", "%s löschen?", "Удалить %s?"),
             nazwa != NULL ? nazwa : "?");
    snprintf(opis, sizeof(opis), "%s",
             JEZYK_Wybierz("Plik zostanie trwale usunięty z karty SD. Tej operacji nie można cofnąć.",
                           "The file will be permanently deleted from the SD card. This cannot be undone.",
                           "Die Datei wird dauerhaft von der SD-Karte gelöscht. Dies kann nicht rückgängig gemacht werden.",
                           "Файл будет безвозвратно удалён с SD-карты. Отменить это действие нельзя."));

    while (TOUCH_IsPressed()) Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja;

        akcje[0] = (UI_AKCJA_t){ .id = 0,
            .tekst = JEZYK_Wybierz("Nie - wróć", "No - back", "Nein - zurück", "Нет - назад"),
            .styl = UI_STYL_POWROT, .aktywna = true, .zaznaczona = fokus == 0U };
        akcje[1] = (UI_AKCJA_t){ .id = 1,
            .tekst = JEZYK_Wybierz("Tak - usuń", "Yes - delete", "Ja - löschen", "Да - удалить"),
            .styl = UI_STYL_OSTRZEZENIE, .aktywna = true, .zaznaczona = fokus == 1U };

        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, true, false, 0);
        UI_RysujPoleInformacyjne(14U, 58U, 452U, 128U,
            JEZYK_Wybierz("Potwierdzenie", "Confirmation", "Bestätigung", "Подтверждение"), opis);
        UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 2U);

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                            UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 2U);
                if (UI_CzyDotknietoWstecz(punkt) || akcja == 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return false;
                }
                if (akcja == 1)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return true;
                }
            }
            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ) return false;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO || zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus ^= 1U;
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK) return fokus == 1U;
            Sleep(10U);
        }
    }
}

static uint8_t PLIKI_UsunPozycje(const char *katalog, const PLIKI_POZYCJA_t *pozycja)
{
    char sciezka[PLIKI_SCIEZKA_MAX];
    if (katalog == NULL || pozycja == NULL || !PLIKI_CzyMoznaUsunac(katalog, pozycja))
        return 0U;
    if (!PLIKI_PotwierdzUsuniecie(pozycja->nazwa))
        return 0U;
    PLIKI_ZbudujSciezke(katalog, pozycja->nazwa, sciezka, sizeof(sciezka));
    if (f_unlink(sciezka) != FR_OK)
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
        return 0U;
    }
    return 1U;
}

static const char *PLIKI_Typ(const PLIKI_POZYCJA_t *pozycja)
{
    const char *rozszerzenie;

    if (pozycja == NULL)
        return "?";
    if ((pozycja->atrybuty & AM_DIR) != 0U)
        return "DIR";
    if (PLIKI_CzySystemowy(pozycja))
        return "SYS";

    rozszerzenie = PLIKI_Rozszerzenie(pozycja->nazwa);
    if (strcasecmp(rozszerzenie, ".txt") == 0) return "TXT";
    if (strcasecmp(rozszerzenie, ".log") == 0) return "LOG";
    if (strcasecmp(rozszerzenie, ".csv") == 0) return "CSV";
    if (strcasecmp(rozszerzenie, ".s1p") == 0) return "S1P";
    if (strcasecmp(rozszerzenie, ".bmp") == 0) return "BMP";
    if (strcasecmp(rozszerzenie, ".png") == 0) return "PNG";
    return "PLIK";
}

static uint16_t PLIKI_WczytajStrone(const char *sciezka, uint16_t pierwszy,
                                    PLIKI_POZYCJA_t *pozycje, uint16_t pojemnosc,
                                    uint16_t *liczba_wszystkich)
{
    DIR katalog = {0};
    FILINFO info = {0};
    uint16_t indeks = 0U;
    uint16_t zapisano = 0U;

    if (liczba_wszystkich != NULL)
        *liczba_wszystkich = 0U;
    if (sciezka == NULL || pozycje == NULL || pojemnosc == 0U)
        return 0U;
    if (f_opendir(&katalog, sciezka) != FR_OK)
        return 0U;

    for (;;)
    {
        if (f_readdir(&katalog, &info) != FR_OK || info.fname[0] == '\0')
            break;
        if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
            continue;

        if (indeks >= pierwszy && zapisano < pojemnosc)
        {
            PLIKI_POZYCJA_t *cel = &pozycje[zapisano++];
            memset(cel, 0, sizeof(*cel));
            strncpy(cel->nazwa, info.fname, sizeof(cel->nazwa) - 1U);
            cel->rozmiar_b = info.fsize;
            cel->data = info.fdate;
            cel->czas = info.ftime;
            cel->atrybuty = info.fattrib;
        }
        ++indeks;
    }
    (void)f_closedir(&katalog);

    if (liczba_wszystkich != NULL)
        *liczba_wszystkich = indeks;
    return zapisano;
}

static void PLIKI_FormatujDateCzas(WORD data, WORD czas, char *bufor, size_t rozmiar)
{
    const unsigned rok = 1980U + ((data >> 9U) & 0x7FU);
    const unsigned miesiac = (data >> 5U) & 0x0FU;
    const unsigned dzien = data & 0x1FU;
    const unsigned godzina = (czas >> 11U) & 0x1FU;
    const unsigned minuta = (czas >> 5U) & 0x3FU;

    snprintf(bufor, rozmiar, "%04u-%02u-%02u %02u:%02u",
             rok, miesiac, dzien, godzina, minuta);
}

static void PLIKI_ZbudujSciezke(const char *katalog, const char *nazwa,
                                char *wynik, size_t rozmiar)
{
    if (strcmp(katalog, "/") == 0)
        snprintf(wynik, rozmiar, "/%s", nazwa);
    else
        snprintf(wynik, rozmiar, "%s/%s", katalog, nazwa);
}

static void PLIKI_IdzPoziomWyzej(char *sciezka)
{
    char *separator;

    if (sciezka == NULL || strcmp(sciezka, "/") == 0)
        return;
    separator = strrchr(sciezka, '/');
    if (separator == NULL || separator == sciezka)
    {
        strcpy(sciezka, "/");
        return;
    }
    *separator = '\0';
}

static void PLIKI_RysujListe(const char *sciezka, const PLIKI_POZYCJA_t *pozycje,
                             uint16_t liczba, uint16_t wybrany,
                             uint16_t pierwszy, uint16_t razem)
{
    uint16_t i;
    char naglowek[112];

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Karta SD / pliki", "SD card / files",
                                   "SD-Karte / Dateien",
                                   "SD-карта / файлы"));
    snprintf(naglowek, sizeof(naglowek), "%.88s  [%u-%u/%u]",
             sciezka, (unsigned)(razem != 0U ? pierwszy + 1U : 0U),
             (unsigned)(pierwszy + liczba), (unsigned)razem);
    FONT_Write(FONT_FRAN, LCD_GRAY, UI_KolorTlaEkranu(), 8, 29, naglowek);

    for (i = 0U; i < liczba; ++i)
    {
        const uint16_t y = (uint16_t)(50U + i * 23U);
        const uint8_t zaznaczony = i == wybrany;
        const LCDColor tlo = zaznaczony ? UI_KolorTlaPrzycisku(UI_STYL_AKCENT) : UI_KolorTlaEkranu();
        const LCDColor tekst = zaznaczony ? UI_KolorTekstu(UI_STYL_AKCENT) : UI_KolorTekstu(UI_STYL_NORMALNY);
        char lewa[28];
        char prawa[44];
        char data_czas[20];

        LCD_FillRect(LCD_MakePoint(4, y), LCD_MakePoint(475, y + 20U), tlo);
        snprintf(lewa, sizeof(lewa), "%-3.3s %-12.12s", PLIKI_Typ(&pozycje[i]), pozycje[i].nazwa);
        PLIKI_FormatujDateCzas(pozycje[i].data, pozycje[i].czas, data_czas, sizeof(data_czas));
        if ((pozycje[i].atrybuty & AM_DIR) != 0U)
            snprintf(prawa, sizeof(prawa), "%s", data_czas);
        else
            snprintf(prawa, sizeof(prawa), "%s  %lu B", data_czas, (unsigned long)pozycje[i].rozmiar_b);
        FONT_Write(FONT_FRAN, tekst, tlo, 8, y + 2U, lewa);
        FONT_Write_RightAlign(FONT_FRAN, tekst, tlo, 180, y + 2U, 472, prawa);
    }

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t otworz = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(2U);
        const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
        const UI_PROSTOKAT_t usun = UI_ObszarPrzyciskuDolnego(4U);
        UI_RysujPrzycisk(otworz.x, otworz.y, otworz.szerokosc, otworz.wysokosc,
                         JEZYK_Wybierz("Otwórz", "Open", "Öffnen", "Открыть"),
                         UI_STYL_AKTYWNY, FONT_FRAN);
        UI_RysujPrzycisk(poprzednia.x, poprzednia.y, poprzednia.szerokosc, poprzednia.wysokosc,
                         JEZYK_Wybierz("Poprz.", "Prev.", "Zurück", "Назад"),
                         UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(nastepna.x, nastepna.y, nastepna.szerokosc, nastepna.wysokosc,
                         JEZYK_Wybierz("Nast.", "Next", "Weiter", "Далее"),
                         UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(usun.x, usun.y, usun.szerokosc, usun.wysokosc,
                         JEZYK_Wybierz("Usuń", "Delete", "Löschen", "Удалить"),
                         (liczba != 0U && PLIKI_CzyMoznaUsunac(sciezka, &pozycje[wybrany]))
                             ? UI_STYL_OSTRZEZENIE : UI_STYL_NIEAKTYWNY, FONT_FRAN);
    }
}

static void PLIKI_RysujWierszTekstu(uint16_t numer, const char *tekst)
{
    const uint16_t y = (uint16_t)(48U + numer * 17U);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 8, y, tekst);
}

static void PLIKI_PodgladTekstu(const char *sciezka)
{
    FIL plik = {0};
    UINT odczytano = 0U;
    size_t pozycja = 0U;
    uint16_t wiersz = 0U;

    if (f_open(&plik, sciezka, FA_READ) != FR_OK)
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
        return;
    }

    if (f_read(&plik, pliki_bufor_tekstu, sizeof(pliki_bufor_tekstu) - 1U, &odczytano) != FR_OK)
    {
        (void)f_close(&plik);
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
        return;
    }
    (void)f_close(&plik);
    pliki_bufor_tekstu[odczytano] = '\0';

    UI_WyczyscEkran();
    UI_RysujNaglowek(sciezka);

    while (pozycja < odczytano && wiersz < PLIKI_TEKST_WIERSZY)
    {
        char linia[PLIKI_TEKST_ZNAKOW_W_WIERSZU + 1U];
        size_t dlugosc = 0U;

        while (pozycja < odczytano &&
               pliki_bufor_tekstu[pozycja] != '\n' &&
               pliki_bufor_tekstu[pozycja] != '\r' &&
               dlugosc < PLIKI_TEKST_ZNAKOW_W_WIERSZU)
        {
            unsigned char znak = (unsigned char)pliki_bufor_tekstu[pozycja++];
            linia[dlugosc++] = (znak >= 32U || znak == '\t') ? (char)znak : '.';
        }
        linia[dlugosc] = '\0';
        PLIKI_RysujWierszTekstu(wiersz++, linia);

        while (pozycja < odczytano &&
               (pliki_bufor_tekstu[pozycja] == '\r' || pliki_bufor_tekstu[pozycja] == '\n'))
            ++pozycja;
    }

    if (odczytano == sizeof(pliki_bufor_tekstu) - 1U)
        FONT_Write(FONT_FRAN, LCD_GRAY, UI_KolorTlaEkranu(), 8, 199,
                   JEZYK_Wybierz("Podgląd: pierwsze 2 kB pliku", "Preview: first 2 kB of file",
                                  "Vorschau: erste 2 kB",
                                  "Просмотр: первые 2 кБ"));

    UI_RysujWsteczDolny(false);
    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        LCDPoint punkt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_OK)
            break;
        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                ;
            if (UI_CzyDotknietoWstecz(punkt))
                break;
        }
        Sleep(20);
    }
}

static void PLIKI_OtworzPozycje(char *katalog, const PLIKI_POZYCJA_t *pozycja)
{
    char sciezka[PLIKI_SCIEZKA_MAX];

    if (katalog == NULL || pozycja == NULL)
        return;
    PLIKI_ZbudujSciezke(katalog, pozycja->nazwa, sciezka, sizeof(sciezka));

    if ((pozycja->atrybuty & AM_DIR) != 0U)
    {
        if (strlen(sciezka) < PLIKI_SCIEZKA_MAX)
            strcpy(katalog, sciezka);
        return;
    }
    if (PLIKI_CzyTekst(pozycja->nazwa))
    {
        PLIKI_PodgladTekstu(sciezka);
        return;
    }
    if (PLIKI_CzyGrafika(pozycja->nazwa))
    {
        if (!SCREENSHOT_PokazSciezke(sciezka))
            KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ODCZYTU_PLIKU);
        return;
    }

    KOMUNIKAT_PokazTekst(JEZYK_Tekst(TEKST_INFORMACJA),
                         JEZYK_Wybierz("Ten typ pliku ma tylko wpis informacyjny.",
                                       "This file type is listed for information only.",
                                       "Dieser Dateityp wird nur angezeigt.",
                                       "Этот тип файла показан только для информации."));
}

void PLIKI_OtworzKatalog(const char *sciezka_poczatkowa)
{
    PLIKI_POZYCJA_t pozycje[PLIKI_NA_STRONIE];
    char katalog[PLIKI_SCIEZKA_MAX] = "/";
    uint16_t pierwszy = 0U;
    uint16_t wybrany = 0U;
    uint16_t liczba = 0U;
    uint16_t razem = 0U;
    uint8_t odswiez = 1U;

    if (sciezka_poczatkowa != NULL && sciezka_poczatkowa[0] == '/' &&
        strlen(sciezka_poczatkowa) < sizeof(katalog))
    {
        DIR test = {0};
        if (f_opendir(&test, sciezka_poczatkowa) == FR_OK)
        {
            (void)f_closedir(&test);
            strcpy(katalog, sciezka_poczatkowa);
        }
    }

    if (!CFG_CzyKartaSDDostepna())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        if (odswiez)
        {
            liczba = PLIKI_WczytajStrone(katalog, pierwszy, pozycje,
                                         PLIKI_NA_STRONIE, &razem);
            if (wybrany >= liczba && liczba != 0U)
                wybrany = (uint16_t)(liczba - 1U);
            PLIKI_RysujListe(katalog, pozycje, liczba, wybrany, pierwszy, razem);
            odswiez = 0U;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO && liczba != 0U)
        {
            if (wybrany > 0U)
                --wybrany;
            else if (pierwszy >= PLIKI_NA_STRONIE)
            {
                pierwszy = (uint16_t)(pierwszy - PLIKI_NA_STRONIE);
                wybrany = 0U;
            }
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO && liczba != 0U)
        {
            if (wybrany + 1U < liczba)
                ++wybrany;
            else if (pierwszy + liczba < razem)
            {
                pierwszy = (uint16_t)(pierwszy + PLIKI_NA_STRONIE);
                wybrany = 0U;
            }
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK && liczba != 0U)
        {
            const uint8_t katalog_przed = (pozycje[wybrany].atrybuty & AM_DIR) != 0U;
            PLIKI_OtworzPozycje(katalog, &pozycje[wybrany]);
            if (katalog_przed)
                pierwszy = wybrany = 0U;
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            if (strcmp(katalog, "/") == 0)
                return;
            PLIKI_IdzPoziomWyzej(katalog);
            pierwszy = wybrany = 0U;
            odswiez = 1U;
        }

        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                ;
            if (punkt.y >= 50 && punkt.y < 211)
            {
                const uint16_t indeks = (uint16_t)((punkt.y - 50) / 23);
                if (indeks < liczba)
                {
                    wybrany = indeks;
                    odswiez = 1U;
                }
            }
            else if (punkt.y >= 220U)
            {
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    if (strcmp(katalog, "/") == 0)
                        return;
                    PLIKI_IdzPoziomWyzej(katalog);
                    pierwszy = wybrany = 0U;
                    odswiez = 1U;
                }
                else
                {
                    const UI_PROSTOKAT_t otworz = UI_ObszarPrzyciskuDolnego(1U);
                    const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(2U);
                    const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
                    const UI_PROSTOKAT_t usun = UI_ObszarPrzyciskuDolnego(4U);
                    if (UI_CzyPunktWObszarze(punkt, &otworz) && liczba != 0U)
                    {
                        const uint8_t katalog_przed = (pozycje[wybrany].atrybuty & AM_DIR) != 0U;
                        PLIKI_OtworzPozycje(katalog, &pozycje[wybrany]);
                        if (katalog_przed)
                            pierwszy = wybrany = 0U;
                        odswiez = 1U;
                    }
                    else if (UI_CzyPunktWObszarze(punkt, &poprzednia))
                    {
                        if (pierwszy >= PLIKI_NA_STRONIE)
                            pierwszy = (uint16_t)(pierwszy - PLIKI_NA_STRONIE);
                        wybrany = 0U;
                        odswiez = 1U;
                    }
                    else if (UI_CzyPunktWObszarze(punkt, &nastepna))
                    {
                        if (pierwszy + liczba < razem)
                            pierwszy = (uint16_t)(pierwszy + PLIKI_NA_STRONIE);
                        wybrany = 0U;
                        odswiez = 1U;
                    }
                    else if (UI_CzyPunktWObszarze(punkt, &usun) && liczba != 0U &&
                             PLIKI_CzyMoznaUsunac(katalog, &pozycje[wybrany]))
                    {
                        if (PLIKI_UsunPozycje(katalog, &pozycje[wybrany]))
                        {
                            if (pierwszy > 0U && razem <= pierwszy + 1U)
                                pierwszy = (uint16_t)(pierwszy >= PLIKI_NA_STRONIE
                                    ? pierwszy - PLIKI_NA_STRONIE : 0U);
                            wybrany = 0U;
                        }
                        odswiez = 1U;
                    }
                }
            }
        }
        Sleep(15);
    }
}

void PLIKI_Otworz(void)
{
    PLIKI_OtworzKatalog("/");
}
