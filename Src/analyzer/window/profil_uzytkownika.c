#include "profil_uzytkownika.h"
#include "uzytkownik.h"
#include "keyboard.h"
#include "textbox.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "main.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include <string.h>

static volatile uint32_t profil_wyjscie;
static char profil_znak[UZYTKOWNIK_ZNAK_MAX + 1];

static void PROFIL_Wyjdz(void)
{
    profil_wyjscie = 1;
}

static void PROFIL_ZmienZnak(void)
{
    char bufor[UZYTKOWNIK_ZNAK_MAX + 1] = {0};

    UZYTKOWNIK_PobierzZnak(bufor, sizeof(bufor));
    if (KeyboardWindow(bufor, UZYTKOWNIK_ZNAK_MAX, JEZYK_Tekst(TEKST_WPISZ_ZNAK)))
        UZYTKOWNIK_UstawZnak(bufor);
}

void PROFIL_UZYTKOWNIKA_Otworz(void)
{
    TEXTBOX_CTX_t kontekst;

    profil_wyjscie = 0;
    while (!profil_wyjscie)
    {
        uint32_t font_znaku;
        int szerokosc_znaku;
        int x_znaku;

        UZYTKOWNIK_PobierzZnak(profil_znak, sizeof(profil_znak));
        if (profil_znak[0] == '\0')
            strcpy(profil_znak, "---");

        UI_WyczyscEkran();
        while (TOUCH_IsPressed())
            Sleep(10);

        UI_RysujNaglowek(JEZYK_Tekst(TEKST_UZYTKOWNIK));
        UI_RysujPanel(28, 48, 424, 116, JEZYK_Tekst(TEKST_ZNAK), UI_STYL_NORMALNY);

        /*
         * Znak krótkofalarski nie jest liczbą. FONT_BDIGITS ma tylko część
         * potrzebnych glifów, dlatego na sprzęcie z SQ2KRR było widać jedynie
         * fragment „Q2”. Używamy pełnej czcionki tekstowej i w razie bardzo
         * długiego znaku przechodzimy na mniejszy wariant.
         */
        font_znaku = FONT_CONSBIG;
        szerokosc_znaku = FONT_GetStrPixelWidth(font_znaku, profil_znak);
        if (szerokosc_znaku > 390)
        {
            font_znaku = FONT_FRANBIG;
            szerokosc_znaku = FONT_GetStrPixelWidth(font_znaku, profil_znak);
        }
        x_znaku = (480 - szerokosc_znaku) / 2;
        if (x_znaku < 38)
            x_znaku = 38;
        FONT_Write(font_znaku, UI_KolorRamki(UI_STYL_AKTYWNY), UI_KolorTlaPola(),
                   (uint16_t)x_znaku, 86U, profil_znak);

        TEXTBOX_t wstecz = {
            .x0 = 10, .y0 = 218,
            .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ), .rola = TEXTBOX_ROLA_WSTECZ,
            .font = FONT_FRANBIG, .width = 120, .height = 42,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_POWROT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT),
            .cb = PROFIL_Wyjdz,
        };
        TEXTBOX_t zmien = {
            .x0 = 140, .y0 = 218,
            .tekst_id = TEXTBOX_TEKST(TEKST_ZMIEN_ZNAK),
            .font = FONT_FRANBIG, .width = 330, .height = 42,
            .center = 1, .border = 1,
            .fgcolor = UI_KolorTekstu(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT),
            .cb = PROFIL_ZmienZnak,
        };

        TEXTBOX_InitContext(&kontekst);
        TEXTBOX_Append(&kontekst, &wstecz);
        TEXTBOX_Append(&kontekst, &zmien);
        TEXTBOX_DrawContext(&kontekst);

        while (!profil_wyjscie)
        {
            uint32_t wynik = TEXTBOX_HitTest(&kontekst);
            if ((wynik == 1U || wynik == 2U) && !profil_wyjscie)
                break;
            Sleep(10);
        }
    }
}
