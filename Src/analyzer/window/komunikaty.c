#include "komunikaty.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "main.h"
#include "wejscia_uzytkownika.h"
#include "ui_wspolny.h"
#include <string.h>

#define KOMUNIKAT_X 20
#define KOMUNIKAT_Y 36
#define KOMUNIKAT_SZEROKOSC 440
#define KOMUNIKAT_WYSOKOSC 200
#define KOMUNIKAT_MARGINES 14
#define KOMUNIKAT_ODSTEP_LINII 18
#define KOMUNIKAT_PRZYCISK_X (KOMUNIKAT_X + KOMUNIKAT_SZEROKOSC - 154)
#define KOMUNIKAT_PRZYCISK_Y (KOMUNIKAT_Y + KOMUNIKAT_WYSOKOSC - 52)
#define KOMUNIKAT_PRZYCISK_SZEROKOSC 140
#define KOMUNIKAT_PRZYCISK_WYSOKOSC 38

static void KOMUNIKAT_RysujLinie(const char *tekst, uint16_t y)
{
    FONT_Write(FONT_FRAN, LCD_WHITE, 0,
               KOMUNIKAT_X + KOMUNIKAT_MARGINES, y, tekst);
}

static void KOMUNIKAT_RysujPrzyciskZamknij(void)
{
    const char *napis = JEZYK_Wybierz("Zamknij", "Close", "Schließen", "Закрыть");
    const uint16_t szerokosc_tekstu = FONT_GetStrPixelWidth(FONT_FRANBIG, napis);
    uint16_t x_tekstu = KOMUNIKAT_PRZYCISK_X + 8;

    if (szerokosc_tekstu < KOMUNIKAT_PRZYCISK_SZEROKOSC)
        x_tekstu = KOMUNIKAT_PRZYCISK_X +
                   (KOMUNIKAT_PRZYCISK_SZEROKOSC - szerokosc_tekstu) / 2;

    LCD_FillRect(LCD_MakePoint(KOMUNIKAT_PRZYCISK_X, KOMUNIKAT_PRZYCISK_Y),
                 LCD_MakePoint(KOMUNIKAT_PRZYCISK_X + KOMUNIKAT_PRZYCISK_SZEROKOSC,
                               KOMUNIKAT_PRZYCISK_Y + KOMUNIKAT_PRZYCISK_WYSOKOSC),
                 LCD_RGB(110, 20, 35));
    LCD_Rectangle(LCD_MakePoint(KOMUNIKAT_PRZYCISK_X, KOMUNIKAT_PRZYCISK_Y),
                  LCD_MakePoint(KOMUNIKAT_PRZYCISK_X + KOMUNIKAT_PRZYCISK_SZEROKOSC,
                                KOMUNIKAT_PRZYCISK_Y + KOMUNIKAT_PRZYCISK_WYSOKOSC),
                  LCD_WHITE);
    FONT_Write(FONT_FRANBIG, LCD_WHITE, LCD_RGB(110, 20, 35),
               x_tekstu, KOMUNIKAT_PRZYCISK_Y + 7, napis);
}

static uint16_t KOMUNIKAT_RysujTekstZawijany(const char *tekst, uint16_t y)
{
    char linia[128];
    size_t dlugosc_linii = 0;
    const char *p = tekst;

    linia[0] = '\0';
    while (*p != '\0')
    {
        const char *poczatek_slowa;
        size_t dlugosc_slowa;
        char proba[128];
        size_t dlugosc_proby;

        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;

        poczatek_slowa = p;
        while (*p != '\0' && *p != ' ')
            p++;
        dlugosc_slowa = (size_t)(p - poczatek_slowa);
        if (dlugosc_slowa >= sizeof(linia))
            dlugosc_slowa = sizeof(linia) - 1;

        dlugosc_proby = dlugosc_linii;
        memcpy(proba, linia, dlugosc_linii);
        if (dlugosc_proby != 0)
            proba[dlugosc_proby++] = ' ';
        if (dlugosc_proby + dlugosc_slowa >= sizeof(proba))
            dlugosc_slowa = sizeof(proba) - dlugosc_proby - 1;
        memcpy(&proba[dlugosc_proby], poczatek_slowa, dlugosc_slowa);
        dlugosc_proby += dlugosc_slowa;
        proba[dlugosc_proby] = '\0';

        if (linia[0] != '\0' &&
            FONT_GetStrPixelWidth(FONT_FRAN, proba) >
                (KOMUNIKAT_SZEROKOSC - 2 * KOMUNIKAT_MARGINES))
        {
            KOMUNIKAT_RysujLinie(linia, y);
            y += KOMUNIKAT_ODSTEP_LINII;
            dlugosc_linii = 0;
            linia[0] = '\0';
        }

        if (dlugosc_linii != 0)
            linia[dlugosc_linii++] = ' ';
        if (dlugosc_linii + dlugosc_slowa >= sizeof(linia))
            dlugosc_slowa = sizeof(linia) - dlugosc_linii - 1;
        memcpy(&linia[dlugosc_linii], poczatek_slowa, dlugosc_slowa);
        dlugosc_linii += dlugosc_slowa;
        linia[dlugosc_linii] = '\0';
    }

    if (linia[0] != '\0')
    {
        KOMUNIKAT_RysujLinie(linia, y);
        y += KOMUNIKAT_ODSTEP_LINII;
    }

    return y;
}

static void KOMUNIKAT_CzekajNaZamkniecie(void)
{
    WEJSCIA_WyczyscZdarzenia();
    while (TOUCH_IsPressed())
        Sleep(10);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(10);
        if (TOUCH_Poll(&punkt))
        {
            const int w_przycisku =
                punkt.x >= KOMUNIKAT_PRZYCISK_X &&
                punkt.x <= KOMUNIKAT_PRZYCISK_X + KOMUNIKAT_PRZYCISK_SZEROKOSC &&
                punkt.y >= KOMUNIKAT_PRZYCISK_Y &&
                punkt.y <= KOMUNIKAT_PRZYCISK_Y + KOMUNIKAT_PRZYCISK_WYSOKOSC;

            while (TOUCH_IsPressed())
                Sleep(10);
            if (w_przycisku)
                break;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
            zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
    }

    WEJSCIA_WyczyscZdarzenia();
}


void KOMUNIKAT_PokazTekst(const char *tytul, const char *tresc)
{
    uint16_t y;
    uint8_t *kopia_ekranu = LCD_Push();

    if (tytul == NULL)
        tytul = "";
    if (tresc == NULL)
        tresc = "";

    /* Komunikat jest modalny także wizualnie: nie pozostawiamy pod nim
     * aktywnych przycisków poprzedniego ekranu (np. Wstecz nad Wstecz). */
    LCD_FillAll(UI_KolorTlaEkranu());
    LCD_FillRect(LCD_MakePoint(KOMUNIKAT_X, KOMUNIKAT_Y),
                 LCD_MakePoint(KOMUNIKAT_X + KOMUNIKAT_SZEROKOSC,
                               KOMUNIKAT_Y + KOMUNIKAT_WYSOKOSC),
                 LCD_BLACK);
    LCD_Rectangle(LCD_MakePoint(KOMUNIKAT_X, KOMUNIKAT_Y),
                  LCD_MakePoint(KOMUNIKAT_X + KOMUNIKAT_SZEROKOSC,
                                KOMUNIKAT_Y + KOMUNIKAT_WYSOKOSC),
                  LCD_YELLOW);

    FONT_Write(FONT_FRANBIG, LCD_YELLOW, 0,
               KOMUNIKAT_X + KOMUNIKAT_MARGINES,
               KOMUNIKAT_Y + KOMUNIKAT_MARGINES,
               tytul);

    y = KOMUNIKAT_Y + 52;
    KOMUNIKAT_RysujTekstZawijany(tresc, y);

    KOMUNIKAT_RysujPrzyciskZamknij();
    KOMUNIKAT_CzekajNaZamkniecie();
    if (kopia_ekranu != 0)
        LCD_Pop();
}

void KOMUNIKAT_PokazDwa(TEKST_ID_t tytul, TEKST_ID_t tresc1, TEKST_ID_t tresc2)
{
    uint16_t y;
    uint8_t *kopia_ekranu = LCD_Push();

    LCD_FillAll(UI_KolorTlaEkranu());
    LCD_FillRect(LCD_MakePoint(KOMUNIKAT_X, KOMUNIKAT_Y),
                 LCD_MakePoint(KOMUNIKAT_X + KOMUNIKAT_SZEROKOSC,
                               KOMUNIKAT_Y + KOMUNIKAT_WYSOKOSC),
                 LCD_BLACK);
    LCD_Rectangle(LCD_MakePoint(KOMUNIKAT_X, KOMUNIKAT_Y),
                 LCD_MakePoint(KOMUNIKAT_X + KOMUNIKAT_SZEROKOSC,
                               KOMUNIKAT_Y + KOMUNIKAT_WYSOKOSC),
                 LCD_YELLOW);

    FONT_Write(FONT_FRANBIG, LCD_YELLOW, 0,
               KOMUNIKAT_X + KOMUNIKAT_MARGINES,
               KOMUNIKAT_Y + KOMUNIKAT_MARGINES,
               JEZYK_Tekst(tytul));

    y = KOMUNIKAT_Y + 52;
    y = KOMUNIKAT_RysujTekstZawijany(JEZYK_Tekst(tresc1), y);
    if (tresc2 != TEKST_LICZBA_TEKSTOW)
    {
        y += 6;
        KOMUNIKAT_RysujTekstZawijany(JEZYK_Tekst(tresc2), y);
    }

    KOMUNIKAT_RysujPrzyciskZamknij();
    KOMUNIKAT_CzekajNaZamkniecie();
    if (kopia_ekranu != 0)
        LCD_Pop();
}

void KOMUNIKAT_Pokaz(TEKST_ID_t tytul, TEKST_ID_t tresc)
{
    KOMUNIKAT_PokazDwa(tytul, tresc, TEKST_LICZBA_TEKSTOW);
}
