/*
 *   (c) Yury Kuchura / Wolfgang Kiefer
 *   Przebudowa EU1KY-PL 2026: matematyka oddzielona od LCD.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "match.h"
#include "dopasowanie_l.h"
#include "config.h"
#include "LCD.h"
#include "font.h"
#include "jezyk.h"
#include "touch.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

static void MATCH_FormatujElement(float x_ohm, uint32_t f_hz, char *bufor, size_t rozmiar)
{
    const DOPASOWANIE_ELEMENT_WARTOSC_t e = DOPASOWANIE_ReaktancjaNaElement(x_ohm, f_hz);

    if (bufor == 0 || rozmiar == 0U)
        return;

    if (e.typ == DOPASOWANIE_ELEMENT_L)
    {
        const float uh = e.wartosc_si * 1.0e6f;
        if (uh >= 100.0f)
            snprintf(bufor, rozmiar, "L %.1f uH", (double)uh);
        else
            snprintf(bufor, rozmiar, "L %.3f uH", (double)uh);
    }
    else if (e.typ == DOPASOWANIE_ELEMENT_C)
    {
        const float pf = e.wartosc_si * 1.0e12f;
        if (pf >= 1000.0f)
            snprintf(bufor, rozmiar, "C %.3f nF", (double)(pf / 1000.0f));
        else
            snprintf(bufor, rozmiar, "C %.1f pF", (double)pf);
    }
    else
    {
        snprintf(bufor, rozmiar, "--");
    }
}

static void MATCH_FormatujPozycje(const DOPASOWANIE_WARIANT_t *w, uint32_t f_hz,
                                  char *zrodlo, size_t nz,
                                  char *szereg, size_t ns,
                                  char *obciazenie, size_t no)
{
    if (w->ma_rownolegle_zrodlo)
        MATCH_FormatujElement(w->x_rownolegle_zrodlo_ohm, f_hz, zrodlo, nz);
    else
        snprintf(zrodlo, nz, "--");

    if (w->ma_szereg)
        MATCH_FormatujElement(w->x_szereg_ohm, f_hz, szereg, ns);
    else
        snprintf(szereg, ns, "--");

    if (w->ma_rownolegle_obciazenie)
        MATCH_FormatujElement(w->x_rownolegle_obciazenie_ohm, f_hz, obciazenie, no);
    else
        snprintf(obciazenie, no, "--");
}

void MATCH_Calc(int X0, int Y0, float complex ZL)
{
    DOPASOWANIE_WYNIK_t wynik;
    char a[24], b[24], c[24], linia[88];
    uint8_t i;
    const uint32_t f_hz = CFG_GetParam(CFG_PARAM_MEAS_F);
    const float z0 = (float)CFG_GetParam(CFG_PARAM_R0);
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor tlo = UI_KolorTlaEkranu();

    if (!DOPASOWANIE_Oblicz(ZL, z0, f_hz, &wynik))
    {
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), tlo,
                   X0, Y0, JEZYK_Tekst(TEKST_BRAK_DOPASOWANIA_LC));
        return;
    }

    for (i = 0U; i < wynik.liczba_wariantow && i < 4U; ++i)
    {
        MATCH_FormatujPozycje(&wynik.wariant[i], f_hz, a, sizeof(a), b, sizeof(b), c, sizeof(c));
        snprintf(linia, sizeof(linia), "%u: %s | %s | %s", (unsigned)(i + 1U), a, b, c);
        FONT_Write(FONT_FRAN, tekst, tlo, X0, Y0, linia);
        Y0 += FONT_GetHeight(FONT_FRAN) + 3;
    }
}

static void MATCH_RysujWariant(const DOPASOWANIE_WYNIK_t *wynik, uint8_t indeks)
{
    const DOPASOWANIE_WARIANT_t *w = &wynik->wariant[indeks];
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor akcent = UI_KolorRamki(UI_STYL_AKCENT);
    char buf[96];
    char a[28], b[28], c[28];

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Asystent dopasowania", "Matching assistant",
                                   "Anpassungsassistent",
                                   "Помощник согласования"));

    UI_RysujPanel(6, 32, 468, 54, 0, UI_STYL_NORMALNY);
    snprintf(buf, sizeof(buf), "f %.6f MHz   Z %.1f %+.1fj Ohm   Z0 %.0f Ohm",
             (double)wynik->czestotliwosc_hz / 1.0e6,
             (double)crealf(wynik->z_obciazenia), (double)cimagf(wynik->z_obciazenia),
             (double)wynik->z0_ohm);
    FONT_Write(FONT_FRAN, tekst, UI_KolorTlaPola(), 16, 44, buf);
    snprintf(buf, sizeof(buf), "%s %u/%u",
             JEZYK_Wybierz("Wariant", "Variant", "Variante", "Вариант"),
             (unsigned)(indeks + 1U), (unsigned)wynik->liczba_wariantow);
    FONT_Write(FONT_FRANBIG, akcent, UI_KolorTlaPola(), 16, 62, buf);

    UI_RysujPanel(6, 92, 468, 126, 0, UI_STYL_NORMALNY);
    MATCH_FormatujPozycje(w, wynik->czestotliwosc_hz, a, sizeof(a), b, sizeof(b), c, sizeof(c));

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(), 18, 104,
               JEZYK_Wybierz("Od nadajnika do anteny:", "From source to antenna:",
                             "Von Quelle zur Antenne:",
                             "От источника к антенне:"));

    snprintf(buf, sizeof(buf), "%s: %s",
             JEZYK_Wybierz("Równolegle wejście", "Input shunt", "Parallel Eingang", "Параллельно вход"), a);
    FONT_Write(FONT_FRANBIG, tekst, UI_KolorTlaPola(), 18, 126, buf);
    snprintf(buf, sizeof(buf), "%s: %s",
             JEZYK_Wybierz("Szeregowo", "Series", "Serie", "Последовательно"), b);
    FONT_Write(FONT_FRANBIG, tekst, UI_KolorTlaPola(), 18, 153, buf);
    snprintf(buf, sizeof(buf), "%s: %s",
             JEZYK_Wybierz("Równolegle antena", "Load shunt", "Parallel Last", "Параллельно нагрузке"), c);
    FONT_Write(FONT_FRANBIG, tekst, UI_KolorTlaPola(), 18, 180, buf);

    snprintf(buf, sizeof(buf), "Zin = %.2f %+.2fj Ohm   blad %.3f Ohm",
             (double)crealf(w->z_wejscie_teoretyczne),
             (double)cimagf(w->z_wejscie_teoretyczne),
             (double)w->blad_ohm);
    FONT_Write(FONT_FRAN, akcent, UI_KolorTlaPola(), 18, 205, buf);

    UI_RysujWsteczDolny(false);
    UI_RysujPrzycisk(74U, 220U, 98U, 45U, "<", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(176U, 220U, 98U, 45U, ">", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(278U, 220U, 201U, 45U,
                     JEZYK_Wybierz("Idealne L/C", "Ideal L/C", "Ideale L/C", "Идеальные L/C"),
                     UI_STYL_NIEAKTYWNY, FONT_FRAN);
}

void MATCH_OtworzAsystenta(float complex ZL, uint32_t f_hz, float z0_ohm)
{
    DOPASOWANIE_WYNIK_t wynik;
    uint8_t indeks = 0U;
    uint8_t koniec = 0U;

    if (!DOPASOWANIE_Oblicz(ZL, z0_ohm, f_hz, &wynik))
        return;

    while (TOUCH_IsPressed())
        Sleep(10U);

    MATCH_RysujWariant(&wynik, indeks);
    LCD_ShowActiveLayerOnly();

    while (!koniec)
    {
        LCDPoint p;
        const WEJSCIE_ZDARZENIE_t z = WEJSCIA_PobierzZdarzenie();

        if (z == WEJSCIE_ZDARZENIE_WSTECZ)
            koniec = 1U;
        else if (z == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            indeks = (indeks == 0U) ? (uint8_t)(wynik.liczba_wariantow - 1U) : (uint8_t)(indeks - 1U);
            MATCH_RysujWariant(&wynik, indeks);
        }
        else if (z == WEJSCIE_ZDARZENIE_OBROT_PRAWO || z == WEJSCIE_ZDARZENIE_OK)
        {
            indeks = (uint8_t)((indeks + 1U) % wynik.liczba_wariantow);
            MATCH_RysujWariant(&wynik, indeks);
        }
        else if (TOUCH_Poll(&p) && p.y >= 220U)
        {
            while (TOUCH_IsPressed()) Sleep(10U);
            if (UI_CzyDotknietoWstecz(p))
                koniec = 1U;
            else if (p.x >= 74U && p.x < 176U)
            {
                indeks = (indeks == 0U) ? (uint8_t)(wynik.liczba_wariantow - 1U) : (uint8_t)(indeks - 1U);
                MATCH_RysujWariant(&wynik, indeks);
            }
            else if (p.x >= 176U && p.x < 278U)
            {
                indeks = (uint8_t)((indeks + 1U) % wynik.liczba_wariantow);
                MATCH_RysujWariant(&wynik, indeks);
            }
        }
        Sleep(10U);
    }

    while (TOUCH_IsPressed())
        Sleep(10U);
}
