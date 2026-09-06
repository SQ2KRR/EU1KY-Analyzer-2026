#include "ui_edytor_liczby.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "jezyk.h"
#include "touch.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

#define UI_EDYTOR_X0              10U
#define UI_EDYTOR_Y0              72U
#define UI_EDYTOR_SZEROKOSC       460U
#define UI_EDYTOR_WYSOKOSC_CYFRY  58U
#define UI_EDYTOR_ODSTEP           4U

/* Geometria edytora dokładnej rezystancji. Wszystkie wartości są dobrane dla
 * LCD 480x272 i pozostawiają minimum 10 px marginesu po obu stronach. */
#define UI_R_MARGINES_X            10U
#define UI_R_PANEL_Y               47U
#define UI_R_PANEL_H               106U
#define UI_R_POLE_Y                82U
#define UI_R_POLE_H                54U
#define UI_R_INFO_Y               159U
#define UI_R_INFO_H                52U
#define UI_R_PRZYCISKI_Y          220U
#define UI_R_PRZYCISKI_H           46U

static uint8_t UI_LiczbaCyfr(uint32_t maksimum)
{
    uint8_t cyfry = 1U;

    while (maksimum >= 10U && cyfry < 10U)
    {
        maksimum /= 10U;
        ++cyfry;
    }
    return cyfry;
}

static uint32_t UI_Potega10(uint8_t wykladnik)
{
    uint32_t wynik = 1U;

    while (wykladnik-- > 0U)
        wynik *= 10U;
    return wynik;
}

static uint32_t UI_ZmienLiczbeKrokiem(uint32_t wartosc,
                                      uint32_t minimum,
                                      uint32_t maksimum,
                                      uint32_t krok,
                                      int8_t kierunek)
{
    if (kierunek > 0)
    {
        if (krok > maksimum || wartosc > maksimum - krok)
            return maksimum;
        wartosc += krok;
        return wartosc > maksimum ? maksimum : wartosc;
    }

    if (wartosc <= minimum || wartosc - minimum < krok)
        return minimum;
    return wartosc - krok;
}

static uint16_t UI_SzerokoscPola(uint8_t liczba_cyfr)
{
    const uint16_t odstepy = (uint16_t)(liczba_cyfr - 1U) * UI_EDYTOR_ODSTEP;
    return (uint16_t)((UI_EDYTOR_SZEROKOSC - odstepy) / liczba_cyfr);
}

static void UI_RysujEdytorLiczby(uint32_t wartosc,
                                 uint32_t minimum,
                                 uint32_t maksimum,
                                 const char *tytul,
                                 const char *jednostka,
                                 uint8_t pole,
                                 uint8_t liczba_cyfr)
{
    char tekst[16];
    char zakres[96];
    uint8_t i;
    const uint16_t szerokosc_pola = UI_SzerokoscPola(liczba_cyfr);

    snprintf(tekst, sizeof(tekst), "%0*lu", (int)liczba_cyfr, (unsigned long)wartosc);

    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, false, false, 0);

    for (i = 0U; i < liczba_cyfr; ++i)
    {
        char cyfra[2] = {tekst[i], '\0'};
        const uint16_t x = (uint16_t)(UI_EDYTOR_X0 + i * (szerokosc_pola + UI_EDYTOR_ODSTEP));

        UI_RysujPrzyciskZaznaczony(x, UI_EDYTOR_Y0, szerokosc_pola,
                                    UI_EDYTOR_WYSOKOSC_CYFRY, cyfra,
                                    UI_STYL_NORMALNY, FONT_BDIGITS, i == pole);
    }

    snprintf(zakres, sizeof(zakres),
             JEZYK_Wybierz("Zakres: %lu - %lu %s",
                           "Range: %lu - %lu %s",
                           "Bereich: %lu - %lu %s",
                           "Диапазон: %lu - %lu %s"),
             (unsigned long)minimum,
             (unsigned long)maksimum,
             jednostka != NULL ? jednostka : "");

    UI_RysujPoleInformacyjne(10U, 140U, 460U, 66U,
                             JEZYK_Wybierz("Edycja cyfr",
                                           "Digit editing",
                                           "Ziffernbearbeitung",
                                           "Редактирование цифр"),
                             zakres);

    UI_RysujPrzycisk(10U, 220U, 112U, 46U, JEZYK_Tekst(TEKST_ANULUJ),
                     UI_STYL_POWROT, FONT_FRAN);
    UI_RysujPrzycisk(130U, 220U, 70U, 46U, "-", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(208U, 220U, 70U, 46U, "+", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(286U, 220U, 184U, 46U, JEZYK_Tekst(TEKST_ZAPISZ),
                     UI_STYL_AKTYWNY, FONT_FRANBIG);
}

bool UI_EdytujLiczbePolamiEx(uint32_t poczatkowa,
                             uint32_t minimum,
                             uint32_t maksimum,
                             const char *tytul,
                             const char *jednostka,
                             uint32_t *wynik)
{
    uint32_t wartosc = poczatkowa;
    const uint8_t liczba_cyfr = UI_LiczbaCyfr(maksimum);
    uint8_t pole = 0U;

    if (wynik != NULL)
        *wynik = poczatkowa;
    if (minimum > maksimum || wynik == NULL)
        return false;
    if (wartosc < minimum)
        wartosc = minimum;
    if (wartosc > maksimum)
        wartosc = maksimum;

    while (TOUCH_IsPressed())
        Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();
    UI_RysujEdytorLiczby(wartosc, minimum, maksimum, tytul, jednostka, pole, liczba_cyfr);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int8_t kierunek = 0;

        if (TOUCH_Poll(&punkt))
        {
            const uint16_t szerokosc_pola = UI_SzerokoscPola(liczba_cyfr);
            uint8_t i;

            if (punkt.y >= 68U && punkt.y < 136U)
            {
                for (i = 0U; i < liczba_cyfr; ++i)
                {
                    const uint16_t x = (uint16_t)(UI_EDYTOR_X0 + i * (szerokosc_pola + UI_EDYTOR_ODSTEP));
                    if (punkt.x >= x && punkt.x < (uint16_t)(x + szerokosc_pola))
                    {
                        pole = i;
                        TOUCH_CzekajNaPuszczenie(30U);
                        UI_RysujEdytorLiczby(wartosc, minimum, maksimum,
                                             tytul, jednostka, pole, liczba_cyfr);
                        break;
                    }
                }
            }
            else if (punkt.y >= 214U)
            {
                TOUCH_CzekajNaPuszczenie(30U);
                if (punkt.x < 126U)
                    return false;
                if (punkt.x < 204U)
                    kierunek = -1;
                else if (punkt.x < 282U)
                    kierunek = 1;
                else
                {
                    *wynik = wartosc;
                    return true;
                }
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return false;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            kierunek = -1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            kierunek = 1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (pole + 1U < liczba_cyfr)
            {
                ++pole;
                UI_RysujEdytorLiczby(wartosc, minimum, maksimum,
                                     tytul, jednostka, pole, liczba_cyfr);
            }
            else
            {
                *wynik = wartosc;
                return true;
            }
        }

        if (kierunek != 0)
        {
            const uint32_t krok = UI_Potega10((uint8_t)(liczba_cyfr - 1U - pole));
            const uint32_t nowa = UI_ZmienLiczbeKrokiem(wartosc, minimum, maksimum, krok, kierunek);

            if (nowa != wartosc)
            {
                wartosc = nowa;
                UI_RysujEdytorLiczby(wartosc, minimum, maksimum,
                                     tytul, jednostka, pole, liczba_cyfr);
            }
        }
        Sleep(10U);
    }
}

uint32_t UI_EdytujLiczbePolami(uint32_t poczatkowa,
                               uint32_t minimum,
                               uint32_t maksimum,
                               const char *tytul,
                               const char *jednostka)
{
    uint32_t wynik = poczatkowa;
    (void)UI_EdytujLiczbePolamiEx(poczatkowa, minimum, maksimum,
                                  tytul, jednostka, &wynik);
    return wynik;
}


static void UI_R_Wycentruj(uint32_t font, uint16_t x, uint16_t y, uint16_t szerokosc,
                           LCDColor fg, LCDColor bg, const char *tekst);

/* ------------------------------------------------------------------------- */
/* Wspólny edytor częstotliwości                                             */
/* ------------------------------------------------------------------------- */

#define UI_F_PANEL_Y               47U
#define UI_F_PANEL_H              106U
#define UI_F_POLE_Y                82U
#define UI_F_POLE_H                54U
#define UI_F_INFO_Y               159U
#define UI_F_INFO_H                52U
#define UI_F_PRZYCISKI_Y          220U
#define UI_F_PRZYCISKI_H           46U
#define UI_F_SEPARATOR_W           20U

static uint8_t UI_F_MiejscaPoPrzecinku(uint32_t rozdzielczosc_hz)
{
    uint8_t miejsca = 6U;
    uint32_t r = rozdzielczosc_hz;

    if (r == 0U || r > 1000000U)
        return 0xFFU;

    while (r > 1U && (r % 10U) == 0U)
    {
        r /= 10U;
        if (miejsca > 0U)
            --miejsca;
    }
    if (r != 1U)
        return 0xFFU;
    return miejsca;
}

static uint32_t UI_F_MinJednostki(uint32_t minimum_hz, uint32_t krok_hz)
{
    return (uint32_t)(((uint64_t)minimum_hz + (uint64_t)krok_hz - 1ULL) /
                      (uint64_t)krok_hz);
}

static uint32_t UI_F_MaxJednostki(uint32_t maksimum_hz, uint32_t krok_hz)
{
    return maksimum_hz / krok_hz;
}

static uint32_t UI_F_HzNaJednostki(uint32_t hz, uint32_t krok_hz,
                                   uint32_t minimum_j, uint32_t maksimum_j)
{
    uint64_t j = ((uint64_t)hz + (uint64_t)krok_hz / 2ULL) / (uint64_t)krok_hz;
    if (j < minimum_j) j = minimum_j;
    if (j > maksimum_j) j = maksimum_j;
    return (uint32_t)j;
}

static uint32_t UI_F_JednostkiNaHz(uint32_t jednostki, uint32_t krok_hz)
{
    uint64_t hz = (uint64_t)jednostki * (uint64_t)krok_hz;
    return hz > UINT32_MAX ? UINT32_MAX : (uint32_t)hz;
}

static uint8_t UI_F_LiczbaCyfr(uint32_t maksimum_j, uint8_t miejsca)
{
    uint8_t cyfry = UI_LiczbaCyfr(maksimum_j);
    const uint8_t minimum = (uint8_t)(miejsca + 1U);
    if (cyfry < minimum)
        cyfry = minimum;
    return cyfry;
}

static uint16_t UI_F_SzerokoscPola(uint8_t liczba_cyfr, uint8_t miejsca)
{
    const uint16_t separator = miejsca > 0U ? UI_F_SEPARATOR_W : 0U;
    const uint16_t odstepy = liczba_cyfr > 1U ?
        (uint16_t)(liczba_cyfr - 1U) * UI_EDYTOR_ODSTEP : 0U;
    return (uint16_t)((452U - separator - odstepy) / liczba_cyfr);
}

static uint16_t UI_F_XPola(uint8_t pole, uint8_t liczba_cyfr, uint8_t miejsca)
{
    const uint8_t cyfry_calkowite = (uint8_t)(liczba_cyfr - miejsca);
    const uint16_t szerokosc = UI_F_SzerokoscPola(liczba_cyfr, miejsca);
    uint16_t x = (uint16_t)(14U + (uint16_t)pole *
                            (uint16_t)(szerokosc + UI_EDYTOR_ODSTEP));
    if (miejsca > 0U && pole >= cyfry_calkowite)
        x = (uint16_t)(x + UI_F_SEPARATOR_W);
    return x;
}

static uint16_t UI_F_XSeparatora(uint8_t liczba_cyfr, uint8_t miejsca)
{
    const uint8_t cyfry_calkowite = (uint8_t)(liczba_cyfr - miejsca);
    const uint16_t szerokosc = UI_F_SzerokoscPola(liczba_cyfr, miejsca);
    return (uint16_t)(14U + (uint16_t)cyfry_calkowite *
                      (uint16_t)(szerokosc + UI_EDYTOR_ODSTEP) - UI_EDYTOR_ODSTEP);
}

static void UI_F_Formatuj(uint32_t hz, uint8_t miejsca, char *bufor, uint32_t rozmiar)
{
    const char sep = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    const uint32_t mhz = hz / 1000000U;
    const uint32_t reszta = hz % 1000000U;
    uint32_t dzielnik = 1U;
    uint8_t i;

    if (miejsca == 0U)
    {
        snprintf(bufor, rozmiar, "%lu MHz", (unsigned long)mhz);
        return;
    }
    for (i = miejsca; i < 6U; ++i)
        dzielnik *= 10U;
    switch (miejsca)
    {
    case 1U: snprintf(bufor, rozmiar, "%lu%c%01lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    case 2U: snprintf(bufor, rozmiar, "%lu%c%02lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    case 3U: snprintf(bufor, rozmiar, "%lu%c%03lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    case 4U: snprintf(bufor, rozmiar, "%lu%c%04lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    case 5U: snprintf(bufor, rozmiar, "%lu%c%05lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    default: snprintf(bufor, rozmiar, "%lu%c%06lu MHz", (unsigned long)mhz, sep, (unsigned long)(reszta / dzielnik)); break;
    }
}

static void UI_F_RozdzielczoscTekst(uint32_t krok_hz, char *bufor, uint32_t rozmiar)
{
    if (krok_hz >= 1000000U && (krok_hz % 1000000U) == 0U)
        snprintf(bufor, rozmiar, "%lu MHz", (unsigned long)(krok_hz / 1000000U));
    else if (krok_hz >= 1000U && (krok_hz % 1000U) == 0U)
        snprintf(bufor, rozmiar, "%lu kHz", (unsigned long)(krok_hz / 1000U));
    else
        snprintf(bufor, rozmiar, "%lu Hz", (unsigned long)krok_hz);
}

static void UI_F_RysujPole(uint16_t x, uint16_t szerokosc,
                           const char *wartosc, uint8_t aktywne)
{
    const LCDColor tlo = aktywne ? UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY) : UI_KolorTlaPola();
    const LCDColor ramka = aktywne ? UI_KolorRamki(UI_STYL_AKTYWNY) : UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor tekst = aktywne ? UI_KolorTekstu(UI_STYL_AKTYWNY) : UI_KolorTekstu(UI_STYL_NORMALNY);
    uint32_t font = FONT_BDIGITS;
    uint16_t y_tekstu;
    uint16_t wysokosc_fontu;

    /* Dla hipotetycznego zakresu wymagającego >8 pól BDigits może być za
       szeroki. Wtedy zachowujemy ten sam układ, ale schodzimy do FRANBIG. */
    if (FONT_GetStrPixelWidth(font, wartosc) + 6U > szerokosc)
        font = FONT_FRANBIG;
    wysokosc_fontu = (uint16_t)FONT_GetHeight(font);
    y_tekstu = UI_F_POLE_Y;
    if (wysokosc_fontu < UI_F_POLE_H)
        y_tekstu = (uint16_t)(UI_F_POLE_Y + (UI_F_POLE_H - wysokosc_fontu) / 2U);

    LCD_FillRect(LCD_MakePoint(x, UI_F_POLE_Y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(UI_F_POLE_Y + UI_F_POLE_H - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, UI_F_POLE_Y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(UI_F_POLE_Y + UI_F_POLE_H - 1U)), ramka);
    if (aktywne && szerokosc > 4U)
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(UI_F_POLE_Y + 1U)),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 2U),
                                    (uint16_t)(UI_F_POLE_Y + UI_F_POLE_H - 2U)), ramka);
    UI_R_Wycentruj(font, x, y_tekstu, szerokosc, tekst, tlo, wartosc);
}

static void UI_F_RysujEdytor(uint32_t wartosc_j,
                             uint32_t minimum_j,
                             uint32_t maksimum_j,
                             uint32_t krok_hz,
                             uint8_t miejsca,
                             const char *tytul,
                             uint8_t pole)
{
    char cyfry[16];
    char min_txt[32];
    char max_txt[32];
    char info[104];
    char rozdz[24];
    char separator[2] = { JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.', '\0' };
    const uint8_t liczba_cyfr = UI_F_LiczbaCyfr(maksimum_j, miejsca);
    const uint8_t cyfry_calkowite = (uint8_t)(liczba_cyfr - miejsca);
    const uint16_t szerokosc_pola = UI_F_SzerokoscPola(liczba_cyfr, miejsca);
    uint8_t i;

    snprintf(cyfry, sizeof(cyfry), "%0*lu", (int)liczba_cyfr,
             (unsigned long)wartosc_j);
    UI_F_Formatuj(UI_F_JednostkiNaHz(minimum_j, krok_hz), miejsca,
                  min_txt, sizeof(min_txt));
    UI_F_Formatuj(UI_F_JednostkiNaHz(maksimum_j, krok_hz), miejsca,
                  max_txt, sizeof(max_txt));
    UI_F_RozdzielczoscTekst(krok_hz, rozdz, sizeof(rozdz));
    snprintf(info, sizeof(info),
             JEZYK_Wybierz("Zakres: %s - %s",
                           "Range: %s - %s",
                           "Bereich: %s - %s",
                           "Диапазон: %s - %s"),
             min_txt, max_txt);

    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, false, false, 0);
    UI_RysujPanel(10U, UI_F_PANEL_Y, 460U, UI_F_PANEL_H,
                  JEZYK_Wybierz("Częstotliwość", "Frequency", "Frequenz", "Частота"),
                  UI_STYL_NORMALNY);

    if (cyfry_calkowite > 0U)
    {
        const uint16_t x0 = UI_F_XPola(0U, liczba_cyfr, miejsca);
        const uint16_t x1 = UI_F_XPola((uint8_t)(cyfry_calkowite - 1U), liczba_cyfr, miejsca);
        UI_R_Wycentruj(FONT_FRAN, x0, 63U,
                       (uint16_t)(x1 + szerokosc_pola - x0),
                       UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(), "MHz");
    }
    if (miejsca > 0U)
    {
        const uint16_t x0 = UI_F_XPola(cyfry_calkowite, liczba_cyfr, miejsca);
        const uint16_t x1 = UI_F_XPola((uint8_t)(liczba_cyfr - 1U), liczba_cyfr, miejsca);
        UI_R_Wycentruj(FONT_FRAN, x0, 63U,
                       (uint16_t)(x1 + szerokosc_pola - x0),
                       UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                       miejsca == 3U ? "kHz" : (miejsca == 6U ? "Hz" : "MHz frac."));
    }

    for (i = 0U; i < liczba_cyfr; ++i)
    {
        char znak[2] = { cyfry[i], '\0' };
        UI_F_RysujPole(UI_F_XPola(i, liczba_cyfr, miejsca), szerokosc_pola,
                       znak, pole == i);
    }
    if (miejsca > 0U)
    {
        UI_R_Wycentruj(FONT_BDIGITS, UI_F_XSeparatora(liczba_cyfr, miejsca),
                       (uint16_t)(UI_F_POLE_Y + 5U), UI_F_SEPARATOR_W,
                       UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), separator);
    }

    {
        char naglowek[48];
        snprintf(naglowek, sizeof(naglowek),
                 JEZYK_Wybierz("Rozdzielczość: %s", "Resolution: %s",
                               "Auflösung: %s", "Разрешение: %s"), rozdz);
        UI_RysujPoleInformacyjne(10U, UI_F_INFO_Y, 460U, UI_F_INFO_H,
                                 naglowek, info);
    }

    UI_RysujPrzycisk(10U, UI_F_PRZYCISKI_Y, 112U, UI_F_PRZYCISKI_H,
                     JEZYK_Tekst(TEKST_ANULUJ), UI_STYL_POWROT, FONT_FRAN);
    UI_RysujPrzycisk(130U, UI_F_PRZYCISKI_Y, 70U, UI_F_PRZYCISKI_H,
                     "-", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(208U, UI_F_PRZYCISKI_Y, 70U, UI_F_PRZYCISKI_H,
                     "+", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(286U, UI_F_PRZYCISKI_Y, 184U, UI_F_PRZYCISKI_H,
                     JEZYK_Tekst(TEKST_ZAPISZ), UI_STYL_AKTYWNY, FONT_FRANBIG);
}

static uint8_t UI_F_PoleDotyku(const LCDPoint *punkt, uint8_t liczba_cyfr, uint8_t miejsca)
{
    uint8_t i;
    const uint16_t szerokosc = UI_F_SzerokoscPola(liczba_cyfr, miejsca);
    if (punkt == NULL || punkt->y < (UI_F_POLE_Y - 8U) ||
        punkt->y >= (UI_F_POLE_Y + UI_F_POLE_H + 8U))
        return 0xFFU;
    for (i = 0U; i < liczba_cyfr; ++i)
    {
        const uint16_t x = UI_F_XPola(i, liczba_cyfr, miejsca);
        if (punkt->x >= x && punkt->x < (uint16_t)(x + szerokosc))
            return i;
    }
    return 0xFFU;
}

bool UI_EdytujCzestotliwoscHzEx(uint32_t poczatkowa_hz,
                                 uint32_t minimum_hz,
                                 uint32_t maksimum_hz,
                                 uint32_t rozdzielczosc_hz,
                                 const char *tytul,
                                 uint32_t *wynik_hz)
{
    const uint8_t miejsca = UI_F_MiejscaPoPrzecinku(rozdzielczosc_hz);
    uint32_t minimum_j;
    uint32_t maksimum_j;
    uint32_t wartosc_j;
    uint8_t liczba_cyfr;
    uint8_t pole = 0U;

    if (wynik_hz != NULL)
        *wynik_hz = poczatkowa_hz;
    if (wynik_hz == NULL || minimum_hz > maksimum_hz || miejsca == 0xFFU)
        return false;

    minimum_j = UI_F_MinJednostki(minimum_hz, rozdzielczosc_hz);
    maksimum_j = UI_F_MaxJednostki(maksimum_hz, rozdzielczosc_hz);
    if (minimum_j > maksimum_j)
        return false;
    wartosc_j = UI_F_HzNaJednostki(poczatkowa_hz, rozdzielczosc_hz,
                                   minimum_j, maksimum_j);
    liczba_cyfr = UI_F_LiczbaCyfr(maksimum_j, miejsca);

    while (TOUCH_IsPressed())
        Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();
    UI_F_RysujEdytor(wartosc_j, minimum_j, maksimum_j, rozdzielczosc_hz,
                     miejsca, tytul, pole);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int8_t kierunek = 0;

        if (TOUCH_Poll(&punkt))
        {
            const uint8_t dotkniete = UI_F_PoleDotyku(&punkt, liczba_cyfr, miejsca);
            if (dotkniete != 0xFFU)
            {
                pole = dotkniete;
                TOUCH_CzekajNaPuszczenie(30U);
                UI_F_RysujEdytor(wartosc_j, minimum_j, maksimum_j,
                                 rozdzielczosc_hz, miejsca, tytul, pole);
            }
            else if (punkt.y >= 214U)
            {
                TOUCH_CzekajNaPuszczenie(30U);
                if (punkt.x < 126U)
                    return false;
                if (punkt.x < 204U)
                    kierunek = -1;
                else if (punkt.x < 282U)
                    kierunek = 1;
                else
                {
                    *wynik_hz = UI_F_JednostkiNaHz(wartosc_j, rozdzielczosc_hz);
                    return true;
                }
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return false;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            kierunek = -1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            kierunek = 1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (pole + 1U < liczba_cyfr)
            {
                ++pole;
                UI_F_RysujEdytor(wartosc_j, minimum_j, maksimum_j,
                                 rozdzielczosc_hz, miejsca, tytul, pole);
            }
            else
            {
                *wynik_hz = UI_F_JednostkiNaHz(wartosc_j, rozdzielczosc_hz);
                return true;
            }
        }

        if (kierunek != 0)
        {
            const uint32_t krok_j = UI_Potega10((uint8_t)(liczba_cyfr - 1U - pole));
            const uint32_t nowa = UI_ZmienLiczbeKrokiem(wartosc_j, minimum_j,
                                                        maksimum_j, krok_j, kierunek);
            if (nowa != wartosc_j)
            {
                wartosc_j = nowa;
                UI_F_RysujEdytor(wartosc_j, minimum_j, maksimum_j,
                                 rozdzielczosc_hz, miejsca, tytul, pole);
            }
        }
        Sleep(10U);
    }
}

uint32_t UI_EdytujCzestotliwoscHz(uint32_t poczatkowa_hz,
                                  uint32_t minimum_hz,
                                  uint32_t maksimum_hz,
                                  uint32_t rozdzielczosc_hz,
                                  const char *tytul)
{
    uint32_t wynik = poczatkowa_hz;
    (void)UI_EdytujCzestotliwoscHzEx(poczatkowa_hz, minimum_hz, maksimum_hz,
                                     rozdzielczosc_hz, tytul, &wynik);
    return wynik;
}

static void UI_R_Formatuj(uint32_t miliohm, char *bufor, uint32_t rozmiar)
{
    const char separator = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    snprintf(bufor, rozmiar, "%lu%c%03lu Ohm",
             (unsigned long)(miliohm / 1000U), separator,
             (unsigned long)(miliohm % 1000U));
}

static void UI_R_Wycentruj(uint32_t font, uint16_t x, uint16_t y, uint16_t szerokosc,
                           LCDColor fg, LCDColor bg, const char *tekst)
{
    int szerokosc_tekstu = FONT_GetStrPixelWidth(font, tekst);
    int pozycja = (int)x + ((int)szerokosc - szerokosc_tekstu) / 2;
    if (pozycja < (int)x + 2)
        pozycja = (int)x + 2;
    FONT_Write(font, fg, bg, (uint16_t)pozycja, y, tekst);
}

static uint16_t UI_R_SzerokoscPola(uint8_t liczba_cyfr)
{
    const uint16_t szerokosc_uzyteczna = 452U;
    const uint16_t separator = 20U;
    const uint16_t odstepy = (uint16_t)(liczba_cyfr - 1U) * UI_EDYTOR_ODSTEP;
    return (uint16_t)((szerokosc_uzyteczna - separator - odstepy) / liczba_cyfr);
}

static uint16_t UI_R_XPola(uint8_t pole, uint8_t liczba_cyfr)
{
    const uint8_t cyfry_calkowite = (liczba_cyfr > 3U) ? (uint8_t)(liczba_cyfr - 3U) : 1U;
    const uint16_t szerokosc = UI_R_SzerokoscPola(liczba_cyfr);
    uint16_t x = 14U + (uint16_t)pole * (uint16_t)(szerokosc + UI_EDYTOR_ODSTEP);

    if (pole >= cyfry_calkowite)
        x = (uint16_t)(x + 20U);
    return x;
}

static uint16_t UI_R_XSeparatora(uint8_t liczba_cyfr)
{
    const uint8_t cyfry_calkowite = (liczba_cyfr > 3U) ? (uint8_t)(liczba_cyfr - 3U) : 1U;
    const uint16_t szerokosc = UI_R_SzerokoscPola(liczba_cyfr);
    return (uint16_t)(14U + (uint16_t)cyfry_calkowite *
                      (uint16_t)(szerokosc + UI_EDYTOR_ODSTEP) - UI_EDYTOR_ODSTEP);
}

static void UI_R_RysujPole(uint16_t x, uint16_t szerokosc,
                           const char *wartosc, uint8_t aktywne)
{
    const LCDColor tlo = aktywne ? UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY) : UI_KolorTlaPola();
    const LCDColor ramka = aktywne ? UI_KolorRamki(UI_STYL_AKTYWNY) : UI_KolorRamki(UI_STYL_NORMALNY);
    const LCDColor tekst = aktywne ? UI_KolorTekstu(UI_STYL_AKTYWNY) : UI_KolorTekstu(UI_STYL_NORMALNY);
    uint16_t y_tekstu = UI_R_POLE_Y;
    const uint16_t wysokosc_fontu = (uint16_t)FONT_GetHeight(FONT_BDIGITS);

    LCD_FillRect(LCD_MakePoint(x, UI_R_POLE_Y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(UI_R_POLE_Y + UI_R_POLE_H - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, UI_R_POLE_Y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                                (uint16_t)(UI_R_POLE_Y + UI_R_POLE_H - 1U)), ramka);
    if (aktywne && szerokosc > 4U)
        LCD_Rectangle(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(UI_R_POLE_Y + 1U)),
                      LCD_MakePoint((uint16_t)(x + szerokosc - 2U),
                                    (uint16_t)(UI_R_POLE_Y + UI_R_POLE_H - 2U)), ramka);

    if (wysokosc_fontu < UI_R_POLE_H)
        y_tekstu = (uint16_t)(UI_R_POLE_Y + (UI_R_POLE_H - wysokosc_fontu) / 2U);
    UI_R_Wycentruj(FONT_BDIGITS, x, y_tekstu, szerokosc, tekst, tlo, wartosc);
}

static void UI_R_RysujEdytor(uint32_t wartosc_mohm,
                             uint32_t minimum_mohm,
                             uint32_t maksimum_mohm,
                             const char *tytul,
                             uint8_t pole)
{
    char cyfry[12];
    char zakres_min[24];
    char zakres_max[24];
    char informacja[96];
    char separator[2] = { JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.', '\0' };
    const uint8_t liczba_cyfr = UI_LiczbaCyfr(maksimum_mohm);
    const uint8_t cyfry_calkowite = (liczba_cyfr > 3U) ? (uint8_t)(liczba_cyfr - 3U) : 1U;
    const uint16_t szerokosc_pola = UI_R_SzerokoscPola(liczba_cyfr);
    const uint16_t x_sep = UI_R_XSeparatora(liczba_cyfr);
    uint8_t i;

    snprintf(cyfry, sizeof(cyfry), "%0*lu", (int)liczba_cyfr, (unsigned long)wartosc_mohm);
    UI_R_Formatuj(minimum_mohm, zakres_min, sizeof(zakres_min));
    UI_R_Formatuj(maksimum_mohm, zakres_max, sizeof(zakres_max));
    snprintf(informacja, sizeof(informacja),
             JEZYK_Wybierz("Zakres: %s - %s",
                           "Range: %s - %s",
                           "Bereich: %s - %s",
                           "Диапазон: %s - %s"),
             zakres_min, zakres_max);

    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, false, false, 0);
    UI_RysujPanel(UI_R_MARGINES_X, UI_R_PANEL_Y, 460U, UI_R_PANEL_H,
                  JEZYK_Wybierz("Rezystancja wzorca", "Standard resistance",
                                "Widerstand des Normals", "Сопротивление эталона"),
                  UI_STYL_NORMALNY);

    /* Dwa logiczne człony jak w edycji daty/czasu: Ohm i trzy miejsca
       dziesiętne. Każda cyfra jest jednak osobnym polem, dzięki czemu nawet
       2000,000 Ohm mieści się w 480 px przy FONT_BDIGITS (48 px/cyfrę). */
    if (cyfry_calkowite > 0U)
    {
        const uint16_t x0 = UI_R_XPola(0U, liczba_cyfr);
        const uint16_t x1 = UI_R_XPola((uint8_t)(cyfry_calkowite - 1U), liczba_cyfr);
        UI_R_Wycentruj(FONT_FRAN, x0, 63U,
                       (uint16_t)(x1 + szerokosc_pola - x0),
                       UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                       JEZYK_Wybierz("Ohm", "ohm", "Ohm", "Ом"));
    }
    {
        const uint16_t x0 = UI_R_XPola(cyfry_calkowite, liczba_cyfr);
        const uint16_t x1 = UI_R_XPola((uint8_t)(liczba_cyfr - 1U), liczba_cyfr);
        UI_R_Wycentruj(FONT_FRAN, x0, 63U,
                       (uint16_t)(x1 + szerokosc_pola - x0),
                       UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                       "mOhm");
    }

    for (i = 0U; i < liczba_cyfr; ++i)
    {
        char znak[2] = { cyfry[i], '\0' };
        UI_R_RysujPole(UI_R_XPola(i, liczba_cyfr), szerokosc_pola,
                       znak, pole == i);
    }
    UI_R_Wycentruj(FONT_BDIGITS, x_sep, (uint16_t)(UI_R_POLE_Y + 5U),
                   20U, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), separator);

    UI_RysujPoleInformacyjne(10U, UI_R_INFO_Y, 460U, UI_R_INFO_H,
                             JEZYK_Wybierz("Dokładność 1 mOhm", "1 mOhm resolution",
                                           "Auflösung 1 mOhm", "Разрешение 1 мОм"),
                             informacja);

    UI_RysujPrzycisk(10U, UI_R_PRZYCISKI_Y, 112U, UI_R_PRZYCISKI_H,
                     JEZYK_Tekst(TEKST_ANULUJ), UI_STYL_POWROT, FONT_FRAN);
    UI_RysujPrzycisk(130U, UI_R_PRZYCISKI_Y, 70U, UI_R_PRZYCISKI_H,
                     "-", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(208U, UI_R_PRZYCISKI_Y, 70U, UI_R_PRZYCISKI_H,
                     "+", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(286U, UI_R_PRZYCISKI_Y, 184U, UI_R_PRZYCISKI_H,
                     JEZYK_Tekst(TEKST_ZAPISZ), UI_STYL_AKTYWNY, FONT_FRANBIG);
}

static uint8_t UI_R_PoleDotyku(const LCDPoint *punkt, uint8_t liczba_cyfr)
{
    uint8_t i;
    const uint16_t szerokosc = UI_R_SzerokoscPola(liczba_cyfr);

    if (punkt == NULL || punkt->y < (UI_R_POLE_Y - 8U) ||
        punkt->y >= (UI_R_POLE_Y + UI_R_POLE_H + 8U))
        return 0xFFU;

    for (i = 0U; i < liczba_cyfr; ++i)
    {
        const uint16_t x = UI_R_XPola(i, liczba_cyfr);
        if (punkt->x >= x && punkt->x < (uint16_t)(x + szerokosc))
            return i;
    }
    return 0xFFU;
}

bool UI_EdytujRezystancjeMiliohmEx(uint32_t poczatkowa_mohm,
                                   uint32_t minimum_mohm,
                                   uint32_t maksimum_mohm,
                                   const char *tytul,
                                   uint32_t *wynik_mohm)
{
    uint32_t wartosc = poczatkowa_mohm;
    const uint8_t liczba_cyfr = UI_LiczbaCyfr(maksimum_mohm);
    uint8_t pole = 0U;

    if (wynik_mohm != NULL)
        *wynik_mohm = poczatkowa_mohm;
    if (wynik_mohm == NULL || minimum_mohm > maksimum_mohm)
        return false;
    if (wartosc < minimum_mohm)
        wartosc = minimum_mohm;
    if (wartosc > maksimum_mohm)
        wartosc = maksimum_mohm;

    while (TOUCH_IsPressed())
        Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();
    UI_R_RysujEdytor(wartosc, minimum_mohm, maksimum_mohm, tytul, pole);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int8_t kierunek = 0;

        if (TOUCH_Poll(&punkt))
        {
            const uint8_t dotkniete_pole = UI_R_PoleDotyku(&punkt, liczba_cyfr);
            if (dotkniete_pole != 0xFFU)
            {
                pole = dotkniete_pole;
                TOUCH_CzekajNaPuszczenie(30U);
                UI_R_RysujEdytor(wartosc, minimum_mohm, maksimum_mohm, tytul, pole);
            }
            else if (punkt.y >= 214U)
            {
                TOUCH_CzekajNaPuszczenie(30U);
                if (punkt.x < 126U)
                    return false;
                if (punkt.x < 204U)
                    kierunek = -1;
                else if (punkt.x < 282U)
                    kierunek = 1;
                else
                {
                    *wynik_mohm = wartosc;
                    return true;
                }
            }
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return false;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            kierunek = -1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            kierunek = 1;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (pole + 1U < liczba_cyfr)
            {
                ++pole;
                UI_R_RysujEdytor(wartosc, minimum_mohm, maksimum_mohm, tytul, pole);
            }
            else
            {
                *wynik_mohm = wartosc;
                return true;
            }
        }

        if (kierunek != 0)
        {
            const uint32_t krok = UI_Potega10((uint8_t)(liczba_cyfr - 1U - pole));
            const uint32_t nowa = UI_ZmienLiczbeKrokiem(wartosc, minimum_mohm,
                                                        maksimum_mohm, krok, kierunek);
            if (nowa != wartosc)
            {
                wartosc = nowa;
                UI_R_RysujEdytor(wartosc, minimum_mohm, maksimum_mohm, tytul, pole);
            }
        }
        Sleep(10U);
    }
}

uint32_t UI_EdytujRezystancjeMiliohm(uint32_t poczatkowa_mohm,
                                     uint32_t minimum_mohm,
                                     uint32_t maksimum_mohm,
                                     const char *tytul)
{
    uint32_t wynik = poczatkowa_mohm;
    (void)UI_EdytujRezystancjeMiliohmEx(poczatkowa_mohm, minimum_mohm,
                                        maksimum_mohm, tytul, &wynik);
    return wynik;
}
