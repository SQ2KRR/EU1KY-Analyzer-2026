/*
 * Ustawianie częstotliwości i zakresu panoramy.
 *
 * Oryginalne okno było zbudowane z kilkudziesięciu niezależnych TEXTBOX-ów.
 * Od v2.03.2-test19 korzystamy ze wspólnych kontrolek UI 2026: osobnego
 * edytora cyfr, przewijanych list i wspólnego paska górnego. Logika pomiaru
 * oraz kontrakt PanFreqWindow() pozostają bez zmian.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "jezyk.h"
#include "panfreq.h"
#include "panvswr2.h"
#include "touch.h"
#include "ui_edytor_liczby.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern void Sleep(uint32_t);

#define PANFREQ_MAX_SPANOW  ((uint16_t)(BS1000M + 1U))
#define PANFREQ_MAX_PASM    19U
#define PANFREQ_LISTA_X     8U
#define PANFREQ_LISTA_Y     40U
#define PANFREQ_LISTA_W     464U
#define PANFREQ_LISTA_H     224U
#define PANFREQ_WIDOCZNE    5U

static uint32_t _f1; /* Hz; środek albo początek zależnie od konfiguracji. */
static BANDSPAN _bs;

/* Historyczne punkty startowe są używane wyłącznie do doboru sensownego
 * domyślnego zakresu. Nie są już geometrią ani listą przycisków ekranu. */
const int StartFreq0[] =
    {135, 472, 1800, 3500, 5350, 7000, 10100, 14000, 18000, 21000,
     24890, 28000, 50000, 70000, 144000, 220000, 430000, 902000, 1240000};

const int FBW1[] =
    {BS4, BS10, BS200, BS300, BS20, BS200, BS100, BS400, BS100, BS500,
     BS100, BS2M, BS2M, BS500, BS2M, -1, BS10M, -1, BS60M};
const int FBW2[] =
    {BS4, BS10, BS200, BS500, BS20, BS300, BS100, BS400, BS100, BS500,
     BS100, BS2M, BS2M, BS500, BS2M, BS4M, BS20M, BS30M, BS60M};
const int FBW3[] =
    {BS4, BS10, BS200, BS400, BS20, BS300, BS100, BS400, BS100, BS500,
     BS100, BS2M, BS2M, BS500, BS2M, -1, BS20M, -1, BS60M};

static const char *const g_nazwy_pasm_iaru[PANFREQ_MAX_PASM] =
{
    "2 km", "630 m", "160 m", "80 m", "60 m", "40 m", "30 m", "20 m",
    "17 m", "15 m", "12 m", "10 m", "6 m", "4 m", "2 m", "1.25 m",
    "70 cm", "33 cm", "23 cm"
};

static const char *const g_nazwy_pasm_lpd[6] =
{
    "6 m", "2 m", "1.25 m", "70 cm", "33 cm", "23 cm"
};

static const char *PAN_T(const char *pl, const char *en, const char *de, const char *ru)
{
    return JEZYK_Wybierz(pl, en, de, ru);
}

static uint16_t PanFreq_LiczbaPasm(void)
{
    return CFG_GetParam(CFG_PARAM_REGION) == 3U ? 6U : PANFREQ_MAX_PASM;
}

static const char *PanFreq_NazwaPasma(uint16_t indeks)
{
    if (CFG_GetParam(CFG_PARAM_REGION) == 3U)
        return indeks < 6U ? g_nazwy_pasm_lpd[indeks] : "?";
    return indeks < PANFREQ_MAX_PASM ? g_nazwy_pasm_iaru[indeks] : "?";
}

static bool PanFreq_Granice(uint32_t czestotliwosc_hz, BANDSPAN zakres,
                            uint32_t *poczatek_hz, uint32_t *koniec_hz)
{
    const uint64_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint64_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint64_t szerokosc_hz = (uint64_t)BSVALUES[zakres] * 1000ULL;
    uint64_t poczatek;
    uint64_t koniec;

    if ((uint32_t)zakres > (uint32_t)BS1000M || szerokosc_hz == 0ULL)
        return false;

    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
    {
        const uint64_t polowa = szerokosc_hz / 2ULL;
        if ((uint64_t)czestotliwosc_hz < polowa)
            return false;
        poczatek = (uint64_t)czestotliwosc_hz - polowa;
        koniec = (uint64_t)czestotliwosc_hz + polowa;
    }
    else
    {
        poczatek = czestotliwosc_hz;
        koniec = (uint64_t)czestotliwosc_hz + szerokosc_hz;
    }

    if (poczatek < fmin || koniec > fmax || poczatek >= koniec || koniec > UINT32_MAX)
        return false;

    if (poczatek_hz != NULL)
        *poczatek_hz = (uint32_t)poczatek;
    if (koniec_hz != NULL)
        *koniec_hz = (uint32_t)koniec;
    return true;
}

static bool PanFreq_CzyZakresPoprawny(void)
{
    return PanFreq_Granice(_f1, _bs, NULL, NULL);
}

static bool PanFreq_DopasujZakres(void)
{
    int zakres = (int)_bs;

    if (zakres > (int)BS1000M)
        zakres = (int)BS1000M;
    while (zakres >= (int)BS2)
    {
        if (PanFreq_Granice(_f1, (BANDSPAN)zakres, NULL, NULL))
        {
            _bs = (BANDSPAN)zakres;
            return true;
        }
        --zakres;
    }
    return false;
}

void GetBS(uint32_t FrequkHz)
{
    const int liczba_pasm = (int)(sizeof(StartFreq0) / sizeof(StartFreq0[0]));
    const int region = (int)CFG_GetParam(CFG_PARAM_REGION);
    int wybrany = 0;
    int i;
    int wybrany_zakres;

    /*
     * Wybieramy ostatni próg nie większy od częstotliwości. Stary algorytm
     * brał pierwszy próg >= f, więc np. 14.175 MHz mogło dostać zakres
     * przeznaczony dla 17 m. To było szczególnie widoczne po przejściu na
     * częstotliwość środkową zamiast początku pasma.
     */
    for (i = 1; i < liczba_pasm; ++i)
    {
        if ((uint32_t)StartFreq0[i] > FrequkHz)
            break;
        wybrany = i;
    }

    if (region == 0)
        wybrany_zakres = FBW1[wybrany];
    else if (region == 1)
        wybrany_zakres = FBW2[wybrany];
    else if (region == 2)
        wybrany_zakres = FBW3[wybrany];
    else
        wybrany_zakres = (int)BS500;

    if (wybrany_zakres < (int)BS2 || wybrany_zakres > (int)BS1000M)
        wybrany_zakres = (int)BS500;
    _bs = (BANDSPAN)wybrany_zakres;
}

static void PanFreq_FormatujZakres(char *bufor, uint32_t rozmiar)
{
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
    {
        snprintf(bufor, rozmiar, "%s %s",
                 PAN_T("±", "+/-", "+/-", "+/-"), BSSTR_HALF[_bs]);
    }
    else
    {
        snprintf(bufor, rozmiar, "+ %s", BSSTR[_bs]);
    }
}

static void PanFreq_FormatujGranice(char *bufor, uint32_t rozmiar)
{
    uint32_t poczatek;
    uint32_t koniec;

    if (!PanFreq_Granice(_f1, _bs, &poczatek, &koniec))
    {
        snprintf(bufor, rozmiar, "%s", PAN_T("Poza zakresem", "Out of range",
                                              "Außerhalb Bereich", "Вне диапазона"));
        return;
    }

    snprintf(bufor, rozmiar, "%.3f - %.3f MHz",
             (double)poczatek / 1000000.0,
             (double)koniec / 1000000.0);
}

static void PanFreq_RysujEkranGlowny(uint8_t fokus)
{
    char czestotliwosc[40];
    char zakres[32];
    char granice[48];
    const bool poprawny = PanFreq_CzyZakresPoprawny();
    UI_AKCJA_t akcje[4] =
    {
        {0, PAN_T("Pasmo", "Band", "Band", "Диапазон"), UI_STYL_NORMALNY, true, fokus == 0U},
        {1, PAN_T("Częst.", "Freq.", "Freq.", "Частота"), UI_STYL_NORMALNY, true, fokus == 1U},
        {2, PAN_T("Zakres", "Span", "Spanne", "Полоса"), UI_STYL_NORMALNY, true, fokus == 2U},
        {3, JEZYK_Tekst(TEKST_ZAPISZ), poprawny ? UI_STYL_AKTYWNY : UI_STYL_NIEAKTYWNY,
            poprawny, fokus == 3U}
    };

    UI_FormatujCzestotliwoscMHz(_f1, czestotliwosc, sizeof(czestotliwosc));
    PanFreq_FormatujZakres(zakres, sizeof(zakres));
    PanFreq_FormatujGranice(granice, sizeof(granice));

    UI_WyczyscEkran();
    UI_RysujPasekGorny(PAN_T("Ustawienie wykresu", "Graph setup",
                              "Diagramm einstellen", "Настройка графика"),
                        true, false, 0);

    UI_RysujPoleStatusu(10U, 42U, 460U, 60U,
                        CFG_GetParam(CFG_PARAM_PAN_CENTER_F)
                            ? PAN_T("Częstotliwość środkowa", "Center frequency",
                                    "Mittenfrequenz", "Центральная частота")
                            : PAN_T("Częstotliwość początkowa", "Start frequency",
                                    "Startfrequenz", "Начальная частота"),
                        czestotliwosc, fokus == 1U ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY);

    UI_RysujPoleStatusu(10U, 108U, 460U, 48U,
                        PAN_T("Zakres pomiaru", "Sweep span", "Messspanne", "Полоса измерения"),
                        zakres, fokus == 2U ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY);

    UI_RysujPoleStatusu(10U, 162U, 460U, 48U,
                        PAN_T("Rzeczywisty zakres", "Actual range", "Tatsächlicher Bereich", "Фактический диапазон"),
                        granice, poprawny ? UI_STYL_AKCENT : UI_STYL_OSTRZEZENIE);

    /* Jeden raster dolnych przycisków: każdy ma dokładnie 70 x 45 px. */
    UI_RysujWsteczDolny(false);
    {
        uint8_t i;
        for (i = 0U; i < 4U; ++i)
        {
            const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego((uint8_t)(i + 1U));
            UI_STYL_t styl = fokus == i ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY;
            if (i == 3U && !poprawny)
                styl = UI_STYL_NIEAKTYWNY;
            UI_RysujPrzycisk(o.x, o.y, o.szerokosc, o.wysokosc, akcje[i].tekst, styl, FONT_FRAN);
        }
    }
}

void PANFREQ_DokumentacjaRealnaRysuj(uint32_t start_hz, BANDSPAN zakres)
{
    /*
     * Ten renderer pokazuje prawdziwe okno ustawiania zakresu, ale nie
     * uruchamia pomiaru ani nie czeka na dotyk. Generator nadrzędny przywraca
     * konfigurację po sesji, więc ustawienie pozostaje wyłącznie robocze.
     */
    CFG_SetParam(CFG_PARAM_PAN_CENTER_F, 0U);
    _f1 = start_hz;
    _bs = zakres;
    PanFreq_RysujEkranGlowny(2U);
}

static uint16_t PanFreq_LiczbaAktywnychPasm(void)
{
    return PanFreq_LiczbaPasm();
}

static bool PanFreq_UstawPasmo(uint16_t indeks)
{
    const uint32_t dol = GetLower((int)indeks);
    const uint32_t gora = GetUpper((int)indeks);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    if (gora <= dol || dol < CFG_GetParam(CFG_PARAM_BAND_FMIN) || gora > fmax)
        return false;

    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
        _f1 = dol + (gora - dol) / 2U;
    else
        _f1 = dol;

    GetBS(_f1 / 1000U);
    return PanFreq_DopasujZakres();
}

static int16_t PanFreq_ZnajdzPasmoDlaCzestotliwosci(uint32_t czestotliwosc_hz)
{
    const uint16_t liczba = PanFreq_LiczbaAktywnychPasm();
    uint16_t i;

    for (i = 0U; i < liczba; ++i)
    {
        const uint32_t dol = GetLower((int)i);
        const uint32_t gora = GetUpper((int)i);
        if (gora > dol && czestotliwosc_hz >= dol && czestotliwosc_hz <= gora)
            return (int16_t)i;
    }
    return -1;
}

/*
 * Jeden selektor pasma dla panoramy, pomiaru pojedynczego i generatora.
 * Zwraca wyłącznie indeks pasma. Sposób ustawienia częstotliwości pozostaje
 * decyzją ekranu wywołującego: panorama może użyć początku lub środka,
 * natomiast pomiar punktowy i generator wybierają środek pasma.
 */
static bool PanFreq_WybierzIndeksPasma(uint32_t biezaca_czestotliwosc_hz,
                                       uint32_t minimum_hz, uint32_t maksimum_hz,
                                       uint16_t *wybrany_indeks)
{
    const uint16_t liczba = PanFreq_LiczbaAktywnychPasm();
    const uint16_t na_strone = UI_SIATKA_KOMPAKT_NA_STRONE;
    const uint16_t liczba_stron = liczba == 0U ? 1U :
        (uint16_t)((liczba + na_strone - 1U) / na_strone);
    uint8_t aktywne[PANFREQ_MAX_PASM];
    int16_t pasmo_biezace;
    uint16_t zaznaczony = 0U;
    uint8_t fokus_widoczny = 0U;
    uint16_t strona = 0U;
    uint16_t i;

    if (wybrany_indeks == NULL || liczba == 0U)
        return false;

    memset(aktywne, 0, sizeof(aktywne));
    for (i = 0U; i < liczba; ++i)
    {
        const uint32_t dol = GetLower((int)i);
        const uint32_t gora = GetUpper((int)i);
        aktywne[i] = (gora > dol && dol >= minimum_hz && gora <= maksimum_hz) ? 1U : 0U;
    }

    pasmo_biezace = PanFreq_ZnajdzPasmoDlaCzestotliwosci(biezaca_czestotliwosc_hz);
    if (pasmo_biezace >= 0 && aktywne[(uint16_t)pasmo_biezace])
        zaznaczony = (uint16_t)pasmo_biezace;
    else
    {
        while (zaznaczony < liczba && !aktywne[zaznaczony])
            zaznaczony++;
        if (zaznaczony >= liczba)
            return false;
    }
    strona = (uint16_t)(zaznaczony / na_strone);

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        const uint16_t poczatek = (uint16_t)(strona * na_strone);
        uint16_t koniec = (uint16_t)(poczatek + na_strone);
        char numer[12];

        if (koniec > liczba)
            koniec = liczba;

        UI_WyczyscEkran();
        UI_RysujPasekGorny(PAN_T("Wybór pasma", "Band selection",
                                  "Bandauswahl", "Выбор диапазона"),
                           false, false, 0);
        for (i = poczatek; i < koniec; ++i)
        {
            UI_RysujKafelKompaktowy((uint16_t)(i - poczatek), UI_IKONA_MENU_LICZBA,
                                     PanFreq_NazwaPasma(i),
                                     fokus_widoczny && i == zaznaczony,
                                     aktywne[i] != 0U);
        }
        UI_RysujWsteczDolny(false);
        if (liczba_stron > 1U)
        {
            const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
            const UI_PROSTOKAT_t informacja = UI_ObszarPrzyciskuDolnego(2U);
            const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
            UI_RysujPrzycisk(poprzednia.x, poprzednia.y, poprzednia.szerokosc, poprzednia.wysokosc,
                             "<", strona > 0U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, FONT_FRANBIG);
            snprintf(numer, sizeof(numer), "%u/%u", (unsigned)(strona + 1U), (unsigned)liczba_stron);
            UI_RysujPrzycisk(informacja.x, informacja.y, informacja.szerokosc, informacja.wysokosc,
                             numer, UI_STYL_NORMALNY, FONT_FRAN);
            UI_RysujPrzycisk(nastepna.x, nastepna.y, nastepna.szerokosc, nastepna.wysokosc,
                             ">", strona + 1U < liczba_stron ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY,
                             FONT_FRANBIG);
        }

        while (TOUCH_IsPressed())
            Sleep(10U);
        WEJSCIA_WyczyscZdarzenia();

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                int16_t lokalny;
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return false;
                }

                if (liczba_stron > 1U)
                {
                    const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
                    const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
                    if (UI_CzyPunktWObszarze(punkt, &poprzednia) && strona > 0U)
                    {
                        strona--;
                        zaznaczony = (uint16_t)(strona * na_strone);
                        while (zaznaczony < liczba && !aktywne[zaznaczony])
                            zaznaczony++;
                        fokus_widoczny = 1U;
                        TOUCH_CzekajNaPuszczenie(30U);
                        break;
                    }
                    if (UI_CzyPunktWObszarze(punkt, &nastepna) && strona + 1U < liczba_stron)
                    {
                        strona++;
                        zaznaczony = (uint16_t)(strona * na_strone);
                        while (zaznaczony < liczba && !aktywne[zaznaczony])
                            zaznaczony++;
                        fokus_widoczny = 1U;
                        TOUCH_CzekajNaPuszczenie(30U);
                        break;
                    }
                }

                lokalny = UI_KafelKompaktowyPoDotyku(punkt, (uint16_t)(koniec - poczatek));
                if (lokalny >= 0)
                {
                    const uint16_t indeks = (uint16_t)(poczatek + (uint16_t)lokalny);
                    if (indeks < liczba && aktywne[indeks])
                    {
                        TOUCH_CzekajNaPuszczenie(30U);
                        *wybrany_indeks = indeks;
                        return true;
                    }
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return false;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                const int kierunek = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
                uint16_t prob = zaznaczony;
                uint16_t n;
                fokus_widoczny = 1U;
                for (n = 0U; n < liczba; ++n)
                {
                    prob = kierunek > 0 ? (uint16_t)((prob + 1U) % liczba)
                                        : (prob == 0U ? (uint16_t)(liczba - 1U) : (uint16_t)(prob - 1U));
                    if (aktywne[prob])
                    {
                        zaznaczony = prob;
                        strona = (uint16_t)(zaznaczony / na_strone);
                        break;
                    }
                }
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK && fokus_widoczny &&
                zaznaczony < liczba && aktywne[zaznaczony])
            {
                *wybrany_indeks = zaznaczony;
                return true;
            }
            Sleep(10U);
        }
    }
}

static bool PanFreq_WybierzPasmo(void)
{
    uint16_t indeks;
    if (!PanFreq_WybierzIndeksPasma(_f1,
                                      CFG_GetParam(CFG_PARAM_BAND_FMIN),
                                      CFG_GetParam(CFG_PARAM_BAND_FMAX),
                                      &indeks))
        return false;
    return PanFreq_UstawPasmo(indeks);
}

bool PanFreq_WybierzPasmoCzestotliwosciEx(uint32_t *czestotliwosc_hz,
                                           uint32_t minimum_hz, uint32_t maksimum_hz)
{
    uint16_t indeks;
    uint32_t dol;
    uint32_t gora;

    if (czestotliwosc_hz == NULL || minimum_hz >= maksimum_hz ||
        !PanFreq_WybierzIndeksPasma(*czestotliwosc_hz, minimum_hz, maksimum_hz, &indeks))
        return false;

    dol = GetLower((int)indeks);
    gora = GetUpper((int)indeks);
    if (gora <= dol || dol < minimum_hz || gora > maksimum_hz)
        return false;

    /* Dla pomiaru punktowego środek pasma jest bezpiecznym, jednoznacznym
     * punktem startowym. Użytkownik może potem dostroić wartość edytorem F. */
    *czestotliwosc_hz = dol + (gora - dol) / 2U;
    return true;
}

bool PanFreq_WybierzPasmoCzestotliwosci(uint32_t *czestotliwosc_hz)
{
    return PanFreq_WybierzPasmoCzestotliwosciEx(czestotliwosc_hz,
                                                 CFG_GetParam(CFG_PARAM_BAND_FMIN),
                                                 CFG_GetParam(CFG_PARAM_BAND_FMAX));
}

bool PanFreq_WybierzPasmoZakresEx(uint32_t *dol_hz, uint32_t *gora_hz,
                                  uint32_t minimum_hz, uint32_t maksimum_hz)
{
    uint16_t indeks;
    uint32_t punkt_hz;
    uint32_t dol;
    uint32_t gora;

    if (dol_hz == NULL || gora_hz == NULL || minimum_hz >= maksimum_hz)
        return false;

    punkt_hz = (*dol_hz < *gora_hz) ? (*dol_hz + (*gora_hz - *dol_hz) / 2U) : minimum_hz;
    if (!PanFreq_WybierzIndeksPasma(punkt_hz, minimum_hz, maksimum_hz, &indeks))
        return false;

    dol = (uint32_t)GetLower((int)indeks);
    gora = (uint32_t)GetUpper((int)indeks);
    if (dol < minimum_hz)
        dol = minimum_hz;
    if (gora > maksimum_hz)
        gora = maksimum_hz;
    if (gora <= dol)
        return false;

    *dol_hz = dol;
    *gora_hz = gora;
    return true;
}

static bool PanFreq_WybierzZakresDo(BANDSPAN maksimum)
{
    const uint16_t na_strone = UI_SIATKA_KOMPAKT_NA_STRONE;
    const uint16_t liczba_dostepnych = (uint16_t)((uint16_t)maksimum + 1U);
    const uint16_t liczba_stron = (uint16_t)((liczba_dostepnych + na_strone - 1U) / na_strone);
    uint8_t aktywne[PANFREQ_MAX_SPANOW];
    uint16_t zaznaczony = (uint16_t)(_bs <= maksimum ? _bs : maksimum);
    uint8_t fokus_widoczny = 1U;
    uint16_t strona = (uint16_t)(zaznaczony / na_strone);
    uint16_t i;

    memset(aktywne, 0, sizeof(aktywne));
    for (i = 0U; i < PANFREQ_MAX_SPANOW; ++i)
        aktywne[i] = (i <= (uint16_t)maksimum &&
                      PanFreq_Granice(_f1, (BANDSPAN)i, NULL, NULL)) ? 1U : 0U;

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        const uint16_t poczatek = (uint16_t)(strona * na_strone);
        uint16_t koniec = (uint16_t)(poczatek + na_strone);
        char tytul[64];
        char numer[12];
        if (koniec > liczba_dostepnych)
            koniec = liczba_dostepnych;

        snprintf(tytul, sizeof(tytul), "%s",
                 PAN_T("Zakres wykresu", "Graph span", "Diagrammspanne", "Полоса графика"));
        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, false, false, 0);
        for (i = poczatek; i < koniec; ++i)
        {
            UI_RysujKafelKompaktowy((uint16_t)(i - poczatek), UI_IKONA_MENU_LICZBA,
                                     BSSTR[i], fokus_widoczny && i == zaznaczony,
                                     aktywne[i] != 0U);
        }
        UI_RysujWsteczDolny(false);
        if (liczba_stron > 1U)
        {
            const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
            const UI_PROSTOKAT_t informacja = UI_ObszarPrzyciskuDolnego(2U);
            const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
            UI_RysujPrzycisk(poprzednia.x, poprzednia.y, poprzednia.szerokosc, poprzednia.wysokosc,
                             "<", strona > 0U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, FONT_FRANBIG);
            snprintf(numer, sizeof(numer), "%u/%u", (unsigned)(strona + 1U), (unsigned)liczba_stron);
            UI_RysujPrzycisk(informacja.x, informacja.y, informacja.szerokosc, informacja.wysokosc,
                             numer, UI_STYL_NORMALNY, FONT_FRAN);
            UI_RysujPrzycisk(nastepna.x, nastepna.y, nastepna.szerokosc, nastepna.wysokosc,
                             ">", strona + 1U < liczba_stron ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY,
                             FONT_FRANBIG);
        }

        while (TOUCH_IsPressed())
            Sleep(10U);
        WEJSCIA_WyczyscZdarzenia();
        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                int16_t lokalny;
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return false;
                }
                if (liczba_stron > 1U)
                {
                    const UI_PROSTOKAT_t poprzednia = UI_ObszarPrzyciskuDolnego(1U);
                    const UI_PROSTOKAT_t nastepna = UI_ObszarPrzyciskuDolnego(3U);
                    if (UI_CzyPunktWObszarze(punkt, &poprzednia) && strona > 0U)
                    {
                        strona--;
                        zaznaczony = (uint16_t)(strona * na_strone);
                        while (zaznaczony < liczba_dostepnych && !aktywne[zaznaczony])
                            zaznaczony++;
                        if (zaznaczony >= liczba_dostepnych)
                            zaznaczony = (uint16_t)_bs;
                        fokus_widoczny = 1U;
                        TOUCH_CzekajNaPuszczenie(30U);
                        break;
                    }
                    if (UI_CzyPunktWObszarze(punkt, &nastepna) && strona + 1U < liczba_stron)
                    {
                        strona++;
                        zaznaczony = (uint16_t)(strona * na_strone);
                        while (zaznaczony < liczba_dostepnych && !aktywne[zaznaczony])
                            zaznaczony++;
                        if (zaznaczony >= liczba_dostepnych)
                            zaznaczony = (uint16_t)_bs;
                        fokus_widoczny = 1U;
                        TOUCH_CzekajNaPuszczenie(30U);
                        break;
                    }
                }
                lokalny = UI_KafelKompaktowyPoDotyku(punkt, (uint16_t)(koniec - poczatek));
                if (lokalny >= 0)
                {
                    const uint16_t indeks = (uint16_t)(poczatek + (uint16_t)lokalny);
                    if (indeks < liczba_dostepnych && aktywne[indeks])
                    {
                        TOUCH_CzekajNaPuszczenie(30U);
                        _bs = (BANDSPAN)indeks;
                        return true;
                    }
                }
            }
            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return false;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO || zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                const int kierunek = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
                uint16_t prob = zaznaczony;
                uint16_t n;
                for (n = 0U; n < liczba_dostepnych; ++n)
                {
                    prob = kierunek > 0 ? (uint16_t)((prob + 1U) % liczba_dostepnych)
                                        : (prob == 0U ? (uint16_t)(liczba_dostepnych - 1U) : (uint16_t)(prob - 1U));
                    if (aktywne[prob])
                    {
                        zaznaczony = prob;
                        break;
                    }
                }
                strona = (uint16_t)(zaznaczony / na_strone);
                fokus_widoczny = 1U;
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK && zaznaczony < liczba_dostepnych && aktywne[zaznaczony])
            {
                _bs = (BANDSPAN)zaznaczony;
                return true;
            }
            Sleep(10U);
        }
    }
}

bool PanFreq_WybierzSzerokoscEx(uint32_t czestotliwosc_hz, BANDSPAN *zakres,
                                BANDSPAN maksimum)
{
    bool zmieniono;
    BANDSPAN poprzedni;

    if (zakres == NULL || maksimum > BS1000M)
        return false;

    poprzedni = *zakres;
    _f1 = czestotliwosc_hz;
    _bs = *zakres <= maksimum ? *zakres : maksimum;
    if (!PanFreq_Granice(_f1, _bs, NULL, NULL))
    {
        while (_bs > BS2 && !PanFreq_Granice(_f1, _bs, NULL, NULL))
            _bs = (BANDSPAN)((uint32_t)_bs - 1U);
    }

    LCD_Push();
    zmieniono = PanFreq_WybierzZakresDo(maksimum);
    LCD_Pop();
    if (!zmieniono)
        return false;

    *zakres = _bs;
    return *zakres != poprzedni;
}

static bool PanFreq_WybierzZakres(void)
{
    return PanFreq_WybierzZakresDo(BS1000M);
}

static void PanFreq_EdytujCzestotliwosc(void)
{
    const uint32_t nowa_hz = UI_EdytujCzestotliwoscHz(
        _f1,
        CFG_GetParam(CFG_PARAM_BAND_FMIN),
        CFG_GetParam(CFG_PARAM_BAND_FMAX),
        1000U,
        CFG_GetParam(CFG_PARAM_PAN_CENTER_F)
            ? PAN_T("Częstotliwość środkowa", "Center frequency",
                    "Mittenfrequenz", "Центральная частота")
            : PAN_T("Częstotliwość początkowa", "Start frequency",
                    "Startfrequenz", "Начальная частота"));

    _f1 = nowa_hz;
    (void)PanFreq_DopasujZakres();
}

bool PanFreqWindow(uint32_t *pFkhz, BANDSPAN *pBs)
{
    uint8_t fokus = 1U;
    bool zmieniono = false;
    const uint32_t stara_f = pFkhz != NULL ? *pFkhz : 0U;
    const BANDSPAN stary_zakres = pBs != NULL ? *pBs : BS400;

    if (pFkhz == NULL || pBs == NULL)
        return false;

    LCD_Push();
    _f1 = (*pFkhz) * 1000U;
    _bs = *pBs <= BS1000M ? *pBs : BS400;
    if (!PanFreq_DopasujZakres())
    {
        GetBS(*pFkhz);
        (void)PanFreq_DopasujZakres();
    }

    while (TOUCH_IsPressed())
        Sleep(10U);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        PanFreq_RysujEkranGlowny(fokus);

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                int16_t akcja = -1;

                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    *pFkhz = 0U;
                    LCD_Pop();
                    return false;
                }

                if (punkt.x >= 10U && punkt.x < 470U && punkt.y >= 42U && punkt.y < 103U)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    PanFreq_EdytujCzestotliwosc();
                    break;
                }
                if (punkt.x >= 10U && punkt.x < 470U && punkt.y >= 108U && punkt.y < 157U)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    (void)PanFreq_WybierzZakres();
                    break;
                }

                if (punkt.y >= 220U && punkt.y < 266U)
                {
                    uint8_t i;
                    for (i = 0U; i < 4U; ++i)
                    {
                        const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego((uint8_t)(i + 1U));
                        if (UI_CzyPunktWObszarze(punkt, &o))
                        {
                            akcja = (int16_t)i;
                            break;
                        }
                    }
                }
                if (akcja >= 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    if (akcja == 0)
                        (void)PanFreq_WybierzPasmo();
                    else if (akcja == 1)
                        PanFreq_EdytujCzestotliwosc();
                    else if (akcja == 2)
                        (void)PanFreq_WybierzZakres();
                    else if (akcja == 3 && PanFreq_CzyZakresPoprawny())
                        goto zapisz;
                    break;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            {
                *pFkhz = 0U;
                LCD_Pop();
                return false;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            {
                fokus = fokus == 0U ? 3U : (uint8_t)(fokus - 1U);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus = (uint8_t)((fokus + 1U) % 4U);
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
            {
                if (fokus == 0U)
                    (void)PanFreq_WybierzPasmo();
                else if (fokus == 1U)
                    PanFreq_EdytujCzestotliwosc();
                else if (fokus == 2U)
                    (void)PanFreq_WybierzZakres();
                else if (PanFreq_CzyZakresPoprawny())
                    goto zapisz;
                break;
            }
            Sleep(10U);
        }
    }

zapisz:
    *pFkhz = _f1 / 1000U;
    *pBs = _bs;
    zmieniono = (*pFkhz != stara_f) || (*pBs != stary_zakres);

    if (zmieniono)
    {
        CFG_SetParam(CFG_PARAM_PAN_F1, _f1);
        CFG_SetParam(CFG_PARAM_PAN_SPAN, _bs);
        CFG_Flush();
    }

    LCD_Pop();
    return zmieniono;
}
