/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>
#include <string.h>
#include "config.h"
#include "LCD.h"
#include "font.h"
#include "touch.h"
#include "hit.h"
#include "textbox.h"
#include "dsp.h"
#include "gen.h"
#include "stm32f746xx.h"
#include "oslfile.h"
#include "osl70cm.h"
#include "stm32746g_discovery_lcd.h"
#include "match.h"
#include "num_keypad.h"
#include "screenshot.h"
#include "smith.h"
#include "measurement.h"
#include "panfreq.h"
#include "panvswr2.h"
#include "bitmaps/bitmaps.h"
#include "jezyk.h"
#include "wejscia_uzytkownika.h"
#include "ui_wspolny.h"
#include "ui_edytor_liczby.h"
#include "komunikaty.h"
#include "ff.h"
#include "kalibracja_meta.h"
#include "pomiar_s11.h"
#include "strojenie_antena.h"
#include "wersja_projektu.h"
#include "DS3231.h"

extern void Sleep(uint32_t ms);
extern uint32_t RTCpresent;
extern void TRACK_Beep(int duration);
//==============================================================================
static uint8_t MeasRqExit = 0;
static uint8_t MeasRedrawWindow = 0;
static uint8_t fChanged = 0;
static uint8_t isMatch = 0;
static uint32_t meas_maxstep = 500000;
static float vswr500[100];
static float complex zFine500[100] = {0};
static DSP_RX meas_ostatnia_impedancja = 50.0f + 0.0f * I;
static uint8_t DrawFine;
static int parallel;
static unsigned int freqOld = 0;
static uint32_t meas_warstwa_widoczna = 1U;
static uint8_t meas_strona = 0U; /* 0 = wynik, 1 = analiza. */

static void MEASUREMENT_SmithPelny(void);
static void MEASUREMENT_DaneMetrologiczne(void);

typedef struct
{
    uint32_t czestotliwosc_hz;
    float wzorzec_ohm;
    uint8_t ma_klasyczny;
    DSP_RX klasyczny;
    float swr_klasyczny;
} WERYFIKACJA_WYNIK_t;

static WERYFIKACJA_WYNIK_t weryfikacja_wynik;
static uint8_t weryfikacja_indeks_wzorca = 1U;

#define WERYFIKACJA_LICZBA_WZORCOW 4U

static float Weryfikacja_WzorzecOhm(uint8_t indeks)
{
    switch (indeks)
    {
    case 0U: return CFG_GetOslRshortOhm();
    case 1U: return CFG_GetOslRloadOhm();
    case 2U: return CFG_GetOslRopenOhm();
    case 3U: return (float)CFG_GetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM) / 1000.0f;
    default: return CFG_GetOslRloadOhm();
    }
}

static const char *Weryfikacja_NazwaWzorca(uint8_t indeks)
{
    switch (indeks)
    {
    case 0U: return JEZYK_Wybierz("Niski", "Low", "Niedrig", "Низкий");
    case 1U: return JEZYK_Wybierz("Środkowy", "Middle", "Mittel", "Средний");
    case 2U: return JEZYK_Wybierz("Wysoki", "High", "Hoch", "Высокий");
    case 3U: return JEZYK_Wybierz("Kontrolny", "Control", "Kontroll", "Контрольный");
    default: return "?";
    }
}

static const char *Weryfikacja_SciezkaPasma(uint8_t indeks)
{
    switch (indeks)
    {
    case 0U: return "/aa/vlow.csv";
    case 1U: return "/aa/vmid.csv";
    case 2U: return "/aa/vhigh.csv";
    case 3U: return "/aa/vctrl.csv";
    default: return "/aa/vband.csv";
    }
}

#define SCAN_ORIGIN_X 20
#define SCAN_ORIGIN_Y 192


static void ShowF()
{
    char czestotliwosc[24];
    uint32_t freq = CFG_GetParam(CFG_PARAM_MEAS_F);

    if (freqOld == freq)
        return;

    freqOld = freq;
    UI_FormatujCzestotliwoscMHz(freq, czestotliwosc, sizeof(czestotliwosc));

    /*
     * Częstotliwość jest najważniejszą wartością sterującą na tym ekranie.
     * Pokazujemy ją jednym formatem w całym programie, bez ręcznego
     * wstawiania kropki zależnego od liczby cyfr.
     */
    LCD_FillRect(LCD_MakePoint(0, 35), LCD_MakePoint(276, 61), BackGrColor);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_NIEAKTYWNY), BackGrColor,
               4, 39, JEZYK_Tekst(TEKST_CZESTOTLIWOSC));
    FONT_Write_RightAlign(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), BackGrColor,
                          92, 35, 274, czestotliwosc);

    GEN_SetMeasurementFreq(freq);
}

void MEASUREMENT_ParSerial(void)
{
    if (parallel == 1)
        parallel = 0;
    else
        parallel = 1;
    MeasRedrawWindow = 1;
    while (TOUCH_IsPressed())
        ;
}

/*
 * Smith nie ma już wersji miniaturowej. Każde jego użycie prowadzi do
 * MEASUREMENT_SmithPelny(), gdzie wykres dostaje cały obszar roboczy.
 * Usunięcie starego DrawSmallSmith() zapobiega przypadkowemu powrotowi do
 * nieczytelnego wykresu wciśniętego między wyniki i przyciski.
 */

void InitScan500(void)
{
    int l;
    for (l = 0; l < 100; l++)
    {
        zFine500[l] = 9999.f + 0.f * I;
        vswr500[l] = 9999.f;
    }
}

static POMIAR_S11_t meas_ostatni_pomiar;
static uint8_t meas_ostatni_pomiar_poprawny = 0U;

static uint8_t Measurement_WykonajPomiarZakres(uint32_t freq_hz, int korekcja_hw, int korekcja_osl,
                                                int liczba_skanow, uint32_t zakres_od_hz,
                                                uint32_t zakres_do_hz)
{
    POMIAR_S11_t pomiar;
    POMIAR_S11_USTAWIENIA_t ustawienia = {
        .tor = POMIAR_S11_TOR_STANDARD,
        .liczba_usrednien = (uint8_t)(liczba_skanow > 0 ? liczba_skanow : 1),
        .korekcja_hw = korekcja_hw != 0,
        .korekcja_osl = korekcja_osl != 0,
        .kompensacja_portu = korekcja_osl != 0,
        .kompensacja_kabla = korekcja_osl != 0 && CFG_GetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY) != 0U,
        .automatyczna_osl_pasmowa = korekcja_osl != 0,
        .zakres_pomiaru_od_hz = zakres_od_hz,
        .zakres_pomiaru_do_hz = zakres_do_hz};

    /*
     * OSL pasmowa dostaje granice calego widoku, nie tylko biezacy punkt.
     * Dzieki temu 70 cm nie wlaczy sie w srodku wykresu obejmujacego rowniez
     * czestotliwosci poza 420..450 MHz.
     */
    meas_ostatni_pomiar_poprawny = POMIAR_S11_PobierzPunkt(freq_hz, &ustawienia, &pomiar) ? 1U : 0U;
    meas_ostatni_pomiar = pomiar;
    return meas_ostatni_pomiar_poprawny;
}

static uint8_t Measurement_WykonajPomiar(uint32_t freq_hz, int korekcja_hw, int korekcja_osl, int liczba_skanow)
{
    return Measurement_WykonajPomiarZakres(freq_hz, korekcja_hw, korekcja_osl, liczba_skanow,
                                            freq_hz, freq_hz);
}

//Scan VSWR in +/- 500 kHz range around measurement frequency with 100 kHz step, to draw a small graph below the measurement
void Scan500(int i, int k)
{
    DSP_RX RX;
    int fq = (int)CFG_GetParam(CFG_PARAM_MEAS_F) + (5 * i + k - 50) * 10000;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    /*
     * Punkty poza skonfigurowanym pasmem są nieważne. Stary kod pilnował
     * tylko dolnej granicy; przy skanie blisko Fmax DSP zwracał dla punktów
     * poza zakresem domyślne 50 + j0, co mogło tworzyć fałszywy dołek SWR.
     */
    if (fq >= (int)fmin && fq <= (int)fmax)
    {
        GEN_SetMeasurementFreq(fq);
        Sleep(2); // was 10
        {
            const uint32_t srodek = CFG_GetParam(CFG_PARAM_MEAS_F);
            const uint32_t zakres_od = srodek > 500000U ? srodek - 500000U : fmin;
            const uint32_t zakres_do = (uint64_t)srodek + 500000ULL > fmax
                                      ? fmax : srodek + 500000U;
            Measurement_WykonajPomiarZakres(fq, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS),
                                             zakres_od, zakres_do);
        }
        RX = zFine500[5 * i + k] = meas_ostatni_pomiar.impedancja_ohm;
        vswr500[5 * i + k] = DSP_CalcVSWR(RX);
    }
    else
    {
        vswr500[5 * i + k] = 9999.f;
    }

    /*
     * Nie rysujemy postepu skanu bezposrednio na widocznej warstwie.
     * Wczesniej pojedyncza zielona linia byla dopisywana podczas kazdego
     * kroku skanu, co wraz z odswiezaniem Smitha powodowalo widoczne
     * rozrywanie obrazu. Wynik skanu trafia na LCD dopiero jako gotowa klatka.
     */
}

static float MeasMagDif;
//Display measured data
static void MeasurementModeDraw(DSP_RX rx)
{
    float VSWR = DSP_CalcVSWR(rx);
    float r = fabsf(crealf(rx));
    float im = cimagf(rx);
    float rp = 0.f, xp = 0.f;
    char str[56] = "";
    const char *opis_impedancji;
    LCDColor tlo_pola = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY);
    LCDColor kolor_tekstu = UI_KolorTekstu(UI_STYL_NORMALNY);
    LCDColor kolor_akcentu = UI_KolorRamki(UI_STYL_AKCENT);

    MeasMagDif = meas_ostatni_pomiar.stosunek_db;
    if (!meas_ostatni_pomiar_poprawny ||
        !isfinite(crealf(rx)) || !isfinite(cimagf(rx)))
    {
        LCD_FillRect(LCD_MakePoint(1, 63), LCD_MakePoint(274, 152), tlo_pola);
        LCD_Rectangle(LCD_MakePoint(1, 63), LCD_MakePoint(274, 152),
                      UI_KolorRamki(UI_STYL_OSTRZEZENIE));
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), tlo_pola,
                   8, 88, JEZYK_Tekst(TEKST_POMIAR_DANE_NIEWIARYGODNE));
        return;
    }

    if (r > 0.05f)
        rp = r + im * (im / r);
    else
        rp = 10000.0f;

    if (im * im > 0.0025f)
        xp = im + r * (r / im);
    else
        xp = 10000.0f;

    if (parallel == 1)
    {
        r = rp;
        im = xp;
        opis_impedancji = JEZYK_Tekst(TEKST_IMPEDANCJA_ROWNOLEGLE);
    }
    else
    {
        opis_impedancji = JEZYK_Tekst(TEKST_IMPEDANCJA_SZEREGOWA);
    }

    /*
     * Jedno zwarte pole wyników zastępuje kilka przypadkowo rozmieszczonych
     * napisów. Geometria pozostaje stała niezależnie od wybranego motywu.
     */
    LCD_FillRect(LCD_MakePoint(1, 63), LCD_MakePoint(274, 152), tlo_pola);
    LCD_Rectangle(LCD_MakePoint(1, 63), LCD_MakePoint(274, 152), UI_KolorRamki(UI_STYL_NORMALNY));
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_NIEAKTYWNY), tlo_pola, 6, 66, opis_impedancji);

    if (VSWR > 100.0f)
        snprintf(str, sizeof(str), "SWR: %.0f   Z0: %d Ohm", VSWR, (int)CFG_GetParam(CFG_PARAM_R0));
    else
        snprintf(str, sizeof(str), "SWR: %.1f   Z0: %d Ohm", VSWR, (int)CFG_GetParam(CFG_PARAM_R0));
    FONT_Write(FONT_FRANBIG, kolor_tekstu, tlo_pola, 6, 82, str);

    str[0] = '\0';
    if (parallel == 1)
    {
        if (r >= 5000.0f)
            snprintf(str, sizeof(str), "Rp > 5 kOhm   ");
        else if (r >= 999.5f)
            snprintf(str, sizeof(str), "Rp: %.1f kOhm   ", r / 1000.0f);
        else
            snprintf(str, sizeof(str), "Rp: %.1f Ohm   ", r);

        if (fabsf(im) >= 5000.0f)
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "|Xp| > 5 kOhm");
        else if (fabsf(im) > 999.5f)
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "Xp: %.1f kOhm", im / 1000.0f);
        else
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "Xp: %+.1f Ohm", im);
    }
    else
    {
        if (r >= 5000.0f)
            snprintf(str, sizeof(str), "Rs > 5 kOhm   ");
        else if (r >= 999.5f)
            snprintf(str, sizeof(str), "Rs: %.1f kOhm   ", r / 1000.0f);
        else
            snprintf(str, sizeof(str), "Rs: %.1f Ohm   ", r);

        if (fabsf(im) >= 5000.0f)
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "|Xs| > 5 kOhm");
        else if (fabsf(im) > 999.5f)
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "Xs: %.1f kOhm", im / 1000.0f);
        else
            snprintf(&str[strlen(str)], sizeof(str) - strlen(str), "Xs: %+.1f Ohm", im);
    }
    /* Dwie składowe impedancji muszą pozostać czytelne także dla wartości kOhm. */
    FONT_Write(FONT_FRAN, kolor_tekstu, tlo_pola, 6, 109, str);

    if (im >= 0.0f)
    {
        if ((im > 1.0f) && (im < 5000.0f))
        {
            float Luh = 1e6f * fabsf(im) / (2.0f * 3.1415926f * GEN_GetLastFreq());
            snprintf(str, sizeof(str), parallel == 1 ? "Lp = %.2f uH" : "Ls = %.2f uH", Luh);
        }
        else
        {
            snprintf(str, sizeof(str), "%s: %s",
                     parallel == 1 ? "Lp" : "Ls", JEZYK_Tekst(TEKST_POZA_ZAKRESEM));
        }
    }
    else
    {
        if ((im < -1.0f) && (im > -5000.0f))
        {
            float Cpf = 1e12f / (2.0f * 3.1415926f * GEN_GetLastFreq() * fabsf(im));
            snprintf(str, sizeof(str), parallel == 1 ? "Cp = %.0f pF" : "Cs = %.0f pF", Cpf);
        }
        else
        {
            snprintf(str, sizeof(str), "%s: %s",
                     parallel == 1 ? "Cp" : "Cs", JEZYK_Tekst(TEKST_POZA_ZAKRESEM));
        }
    }
    FONT_Write(FONT_FRAN, kolor_akcentu, tlo_pola, 6, 134, str);

    /* Strata dopasowanego kabla i moduł impedancji są informacją pomocniczą. */
    LCD_FillRect(LCD_MakePoint(0, 156), LCD_MakePoint(276, 171), BackGrColor);
    {
        float ga = cabsf(OSL_GFromZ(rx, CFG_GetParam(CFG_PARAM_R0)));
        if (ga > 0.01f)
        {
            float cl = -10.0f * log10f(ga);
            if (cl < 0.001f)
                cl = 0.0f;
            snprintf(str, sizeof(str), "%s: %.2f dB   |Z|: %.1f Ohm",
                     JEZYK_Tekst(TEKST_STRATA_DOPASOWANEGO_KABLA), cl, cabsf(rx));
            FONT_Write(FONT_FRAN, kolor_akcentu, BackGrColor, 0, 158, str);
        }
    }
}

//Draw a small (100x30 pixels) VSWR graph for data collected by Scan500()
static void MeasurementModeGraph(DSP_RX in)
{
    LCDPoint p1, p2;
    int idx = 0;
    float vswr;

    LCD_FillRect(LCD_MakePoint(SCAN_ORIGIN_X, SCAN_ORIGIN_Y - 9), LCD_MakePoint(SCAN_ORIGIN_X + 200, SCAN_ORIGIN_Y + 21), BackGrColor); // Graph rectangle

    //LCD_FillRect(LCD_MakePoint(SCAN_ORIGIN_X,   SCAN_ORIGIN_Y -  9), LCD_MakePoint(SCAN_ORIGIN_X + 200, SCAN_ORIGIN_Y + 21), BackGrColor); // Graph rectangle
    LCD_Line(LCD_MakePoint(SCAN_ORIGIN_X + 100, SCAN_ORIGIN_Y + 1), LCD_MakePoint(SCAN_ORIGIN_X + 100, SCAN_ORIGIN_Y + 11), TextColor); // Measurement frequency line
    LCD_Line(LCD_MakePoint(SCAN_ORIGIN_X + 50, SCAN_ORIGIN_Y + 1), LCD_MakePoint(SCAN_ORIGIN_X + 50, SCAN_ORIGIN_Y + 11), TextColor);
    LCD_Line(LCD_MakePoint(SCAN_ORIGIN_X + 150, SCAN_ORIGIN_Y + 1), LCD_MakePoint(SCAN_ORIGIN_X + 150, SCAN_ORIGIN_Y + 11), TextColor);
    LCD_Line(LCD_MakePoint(SCAN_ORIGIN_X, SCAN_ORIGIN_Y + 17), LCD_MakePoint(SCAN_ORIGIN_X + 200, SCAN_ORIGIN_Y + 17), TextColor); // VSWR 2.0 line
    for (idx = 0; idx < 100; idx++)
    {
        if (TOUCH_IsPressed())
            return;
        vswr = vswr500[idx];
        if (vswr != 9999.0)
        {
            if (vswr > 12.0 || isnan(vswr) || isinf(vswr) || vswr < 1.0) //Graph limit is VSWR 3.0
                vswr = 12.0;
            vswr = 10.2f * log10f(vswr); // smooth curve ** WK **                                               //Including uninitialized values

            if (idx == 0)
                p1 = LCD_MakePoint(SCAN_ORIGIN_X + (idx - 1) * 2, SCAN_ORIGIN_Y + 11 - (int)((vswr * 30 - 140) / 11));
            else
            {
                p2 = LCD_MakePoint(SCAN_ORIGIN_X + idx * 2, SCAN_ORIGIN_Y + 11 - (int)((vswr * 30 - 140) / 11));
                LCD_Line(p1, p2, CurvColor);
                if (FatLines)
                    LCD_Line((LCDPoint){p1.x, p1.y + 1}, (LCDPoint){p2.x, p2.y + 1}, CurvColor);
                p1 = p2;
            }
        }
        else if (idx == 0)
            p1 = LCD_MakePoint(SCAN_ORIGIN_X - 2, SCAN_ORIGIN_Y - 9);
    }
}


static void Measurement_RysujStanKrotki(char *bufor, uint32_t rozmiar)
{
    const char *sygnal = (!meas_ostatni_pomiar_poprawny || meas_ostatni_pomiar.napiecie_v_mv < 0.3f)
        ? JEZYK_Wybierz("brak", "none", "fehlt", "нет")
        : "OK";
    const char *hw = OSL_IsErrCorrLoaded() ? "OK" : JEZYK_Wybierz("brak", "none", "fehlt", "нет");

    if (meas_ostatni_pomiar_poprawny &&
        meas_ostatni_pomiar.model_korekcji == POMIAR_S11_KOREKCJA_OSL_70CM)
    {
        snprintf(bufor, rozmiar, "Syg: %s   OSL: 70cm OK   HW: %s", sygnal, hw);
    }
    else if (-1 == OSL_GetSelected())
    {
        snprintf(bufor, rozmiar, "Syg: %s   OSL: -   HW: %s", sygnal, hw);
    }
    else
    {
        snprintf(bufor, rozmiar, "Syg: %s   OSL: %s %s   HW: %s", sygnal,
                 OSL_GetSelectedName(), OSL_IsSelectedValid() ? "OK" : "!", hw);
    }
}

static void Measurement_RysujWynikPodstawowy(DSP_RX rx)
{
    char wartosc[48];
    char stan[96];
    float swr;
    float faza_deg;

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Tekst(TEKST_POJEDYNCZY_TYTUL), false, false, 0);

    UI_FormatujCzestotliwoscMHz(CFG_GetParam(CFG_PARAM_MEAS_F), wartosc, sizeof(wartosc));
    UI_RysujPoleInformacyjne(8U, 36U, 464U, 42U,
                             JEZYK_Tekst(TEKST_CZESTOTLIWOSC), wartosc);

    MeasMagDif = meas_ostatni_pomiar.stosunek_db;
    if (!meas_ostatni_pomiar_poprawny || !isfinite(crealf(rx)) || !isfinite(cimagf(rx)))
    {
        UI_RysujPoleStatusu(8U, 86U, 464U, 104U,
                            JEZYK_Tekst(TEKST_POJEDYNCZY_TYTUL),
                            JEZYK_Tekst(TEKST_POMIAR_DANE_NIEWIARYGODNE),
                            UI_STYL_OSTRZEZENIE);
        Measurement_RysujStanKrotki(stan, sizeof(stan));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
                   8, 202, stan);
        return;
    }

    swr = DSP_CalcVSWR(rx);
    faza_deg = cargf(rx) * (180.0f / 3.1415926f);

    snprintf(wartosc, sizeof(wartosc), swr > 100.0f ? "%.0f" : "%.2f", (double)swr);
    UI_RysujPoleInformacyjne(8U, 86U, 150U, 50U, "SWR", wartosc);
    snprintf(wartosc, sizeof(wartosc), "%.2f Ohm", (double)crealf(rx));
    UI_RysujPoleInformacyjne(165U, 86U, 150U, 50U, "R", wartosc);
    snprintf(wartosc, sizeof(wartosc), "%+.2f Ohm", (double)cimagf(rx));
    UI_RysujPoleInformacyjne(322U, 86U, 150U, 50U, "X", wartosc);

    snprintf(wartosc, sizeof(wartosc), "%.2f Ohm", (double)cabsf(rx));
    UI_RysujPoleInformacyjne(8U, 144U, 150U, 50U, "|Z|", wartosc);
    snprintf(wartosc, sizeof(wartosc), "%+.1f deg", (double)faza_deg);
    UI_RysujPoleInformacyjne(165U, 144U, 150U, 50U,
                             JEZYK_Wybierz("Faza Z", "Z phase", "Z-Phase", "Фаза Z"), wartosc);
    snprintf(wartosc, sizeof(wartosc), "%lu Ohm", (unsigned long)CFG_GetParam(CFG_PARAM_R0));
    UI_RysujPoleInformacyjne(322U, 144U, 150U, 50U, "Z0", wartosc);

    Measurement_RysujStanKrotki(stan, sizeof(stan));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
               8, 202, stan);
}

static void Measurement_RysujAnalize(DSP_RX rx)
{
    char buf[64];
    float complex g;
    float modul_g;
    float faza_g;
    float strata_powrotna;
    const LCDColor tlo = UI_KolorTlaEkranu();
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);

    UI_WyczyscEkran();
    UI_RysujPasekGorny(JEZYK_Wybierz("Pojedynczy - analiza", "Single - analysis",
                                     "Einzelmessung - Analyse", "Одиночный - анализ"),
                       false, false, 0);
    UI_FormatujCzestotliwoscMHz(CFG_GetParam(CFG_PARAM_MEAS_F), buf, sizeof(buf));
    FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), tlo, 8, 32, buf);

    /* Zachowujemy sprawdzoną matematykę modelu szeregowego/równoległego,
     * ale na osobnej stronie, bez miniaturowego Smitha. */
    MeasurementModeDraw(rx);

    UI_RysujPanel(284U, 63U, 188U, 109U, 0, UI_STYL_NORMALNY);
    if (meas_ostatni_pomiar_poprawny && isfinite(crealf(rx)) && isfinite(cimagf(rx)))
    {
        g = OSL_GFromZ(rx, CFG_GetParam(CFG_PARAM_R0));
        modul_g = cabsf(g);
        faza_g = cargf(g) * (180.0f / 3.1415926f);
        strata_powrotna = modul_g > 0.000001f ? -20.0f * log10f(modul_g) : 120.0f;

        snprintf(buf, sizeof(buf), "|G| %.4f", (double)modul_g);
        FONT_Write(FONT_FRAN, tekst, UI_KolorTlaPola(), 292, 72, buf);
        snprintf(buf, sizeof(buf), "Faza G %+.1f deg", (double)faza_g);
        FONT_Write(FONT_FRAN, tekst, UI_KolorTlaPola(), 292, 94, buf);
        snprintf(buf, sizeof(buf), "RL %.2f dB", (double)strata_powrotna);
        FONT_Write(FONT_FRAN, tekst, UI_KolorTlaPola(), 292, 116, buf);
        snprintf(buf, sizeof(buf), "Syg %.1f dB", (double)meas_ostatni_pomiar.stosunek_db);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaPola(), 292, 138, buf);
    }
    else
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
                   292, 92, JEZYK_Tekst(TEKST_POMIAR_DANE_NIEWIARYGODNE));
    }

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 0,
               SCAN_ORIGIN_Y - 32,
               JEZYK_Wybierz("SWR 1,0...12,0, F +/-500 kHz",
                             "VSWR 1.0...12.0, F +/-500 kHz",
                             "SWR 1,0...12,0, F +/-500 kHz",
                             "КСВ 1,0...12,0, F +/-500 кГц"));
    if (meas_ostatni_pomiar_poprawny && isfinite(crealf(rx)) && isfinite(cimagf(rx)))
        MeasurementModeGraph(rx);
}

static void Measurement_RysujPasekDolny(void)
{
    const char *etykiety_wynik[5] = {
        JEZYK_Wybierz("Pasmo", "Band", "Band", "Диапазон"),
        "F",
        JEZYK_Wybierz("Analiza", "Analysis", "Analyse", "Анализ"),
        "Smith",
        JEZYK_Wybierz("Dane", "Data", "Daten", "Данные")};
    const char *etykiety_analiza[5] = {
        JEZYK_Wybierz("Wynik", "Result", "Ergebnis", "Результат"),
        JEZYK_Wybierz("Model", "Model", "Modell", "Модель"),
        "LC",
        "Smith",
        JEZYK_Wybierz("Zrzut", "Screenshot", "Bild", "Снимок")};
    const char *const *etykiety = meas_strona == 0U ? etykiety_wynik : etykiety_analiza;
    uint8_t i;

    UI_RysujWsteczDolny(false);
    for (i = 0U; i < 5U; ++i)
    {
        const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego((uint8_t)(i + 1U));
        UI_RysujPrzycisk(o.x, o.y, o.szerokosc, o.wysokosc,
                         etykiety[i], UI_STYL_NORMALNY, FONT_FRAN);
    }
}

static bool Measurement_CzyDotknietoPrzyciskuDolnego(LCDPoint punkt, uint8_t pozycja)
{
    const UI_PROSTOKAT_t obszar = UI_ObszarPrzyciskuDolnego(pozycja);
    return UI_CzyPunktWObszarze(punkt, &obszar);
}

static void Measurement_RysujStrone(DSP_RX rx)
{
    if (meas_strona == 0U)
        Measurement_RysujWynikPodstawowy(rx);
    else
        Measurement_RysujAnalize(rx);
    Measurement_RysujPasekDolny();
}

static void Measurement_RysujPasekDolny(void);
static void Measurement_RysujStrone(DSP_RX rx);

static void Measurement_OdswiezDynamiczne(DSP_RX rx)
{
    const uint32_t warstwa_ukryta = 1U - meas_warstwa_widoczna;

    /*
     * Cała strona jest składana poza ekranem. Dzięki temu przełączenie
     * Wynik/Analiza nie odsłania fragmentów poprzedniego układu i nie miga.
     */
    LCD_KopiujWarstwe(meas_warstwa_widoczna, warstwa_ukryta);
    BSP_LCD_SelectLayer(warstwa_ukryta);
    Measurement_RysujStrone(rx);

    LCD_ShowActiveLayerOnly();
    meas_warstwa_widoczna = warstwa_ukryta;
}

static void MEASUREMENT_Exit(void)
{
    MeasRqExit = 1;
}
static uint32_t fx = 14000000ul; //Scan range start frequency, in Hz

void FDecr(uint32_t step)
{
    uint32_t MeasurementFreq = CFG_GetParam(CFG_PARAM_MEAS_F);
    const uint32_t minimum = CFG_GetParam(CFG_PARAM_BAND_FMIN);

    /*
     * Nie odejmujemy przed sprawdzeniem granicy. Przy typie uint32_t
     * zejscie np. o 1 MHz z 100 kHz zawineloby wartosc do kilku GHz.
     */
    if (MeasurementFreq <= minimum || step >= (MeasurementFreq - minimum))
        MeasurementFreq = minimum;
    else
        MeasurementFreq -= step;

    CFG_SetParam(CFG_PARAM_MEAS_F, MeasurementFreq);
    fChanged = 1;
    freqMHzf = MeasurementFreq / 1000000.;
    fx = MeasurementFreq;
    Sleep(50);
}

void FIncr(uint32_t step)
{
    uint32_t MeasurementFreq = CFG_GetParam(CFG_PARAM_MEAS_F);
    const uint32_t maksimum = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    /* Analogicznie unikamy przepelnienia przy dodawaniu kroku. */
    if (MeasurementFreq >= maksimum || step >= (maksimum - MeasurementFreq))
        MeasurementFreq = maksimum;
    else
        MeasurementFreq += step;

    CFG_SetParam(CFG_PARAM_MEAS_F, MeasurementFreq);
    fChanged = 1;
    freqMHzf = MeasurementFreq / 1000000.;
    fx = MeasurementFreq;
    Sleep(50);
}

static void MEASUREMENT_SmithMatch(void)
{
    while (TOUCH_IsPressed())
        Sleep(10U);

    if (meas_ostatni_pomiar_poprawny &&
        isfinite(crealf(meas_ostatnia_impedancja)) &&
        isfinite(cimagf(meas_ostatnia_impedancja)))
    {
        MATCH_OtworzAsystenta(meas_ostatnia_impedancja,
                              CFG_GetParam(CFG_PARAM_MEAS_F),
                              (float)CFG_GetParam(CFG_PARAM_R0));
    }
    isMatch = 0U;
    freqOld = 0U;
    MeasRedrawWindow = 1U;
}

static uint32_t fxkHz; //Scan range start frequency, in kHz

static void MEASUREMENT_SetFreq(void)
{
    uint32_t nowa_hz = CFG_GetParam(CFG_PARAM_MEAS_F);

    /*
     * Pojedynczy pomiar nie potrzebuje ustawienia szerokości skanu.
     * Dlatego używa bezpośrednio tego samego edytora MHz, który obowiązuje
     * w generatorze, metrologii i pozostałych funkcjach. PanFreq pozostaje
     * wspólnym oknem dla funkcji skanujących, gdzie częstotliwość i zakres
     * tworzą jedną konfigurację.
     */
    if (UI_EdytujCzestotliwoscHzEx(nowa_hz,
                                    CFG_GetParam(CFG_PARAM_BAND_FMIN),
                                    CFG_GetParam(CFG_PARAM_BAND_FMAX),
                                    1000U,
                                    JEZYK_Tekst(TEKST_USTAW_CZESTOTLIWOSC),
                                    &nowa_hz))
    {
        CFG_SetParam(CFG_PARAM_MEAS_F, nowa_hz);
        CFG_Flush();
        fx = nowa_hz;
        fxkHz = nowa_hz / 1000U;
        freqMHzf = (float)nowa_hz / 1000000.0f;
        fChanged = 1U;
        freqOld = 0U;
    }
    Sleep(120U);
}

static void MEASUREMENT_Screenshot(void)
{
    char *fname = 0;
    fname = SCREENSHOT_SelectFileName();

    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();
    Date_Time_Stamp();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);
}


static void MEASUREMENT_WybierzPasmo(void)
{
    uint32_t nowa_hz = CFG_GetParam(CFG_PARAM_MEAS_F);
    while (TOUCH_IsPressed())
        Sleep(10U);
    if (PanFreq_WybierzPasmoCzestotliwosci(&nowa_hz))
    {
        CFG_SetParam(CFG_PARAM_MEAS_F, nowa_hz);
        CFG_Flush();
        fx = nowa_hz;
        fxkHz = nowa_hz / 1000U;
        freqMHzf = (float)nowa_hz / 1000000.0f;
        fChanged = 1U;
        freqOld = 0U;
        InitScan500();
    }
    MeasRedrawWindow = 1U;
}

static void MEASUREMENT_Akcja1(void)
{
    if (meas_strona == 0U)
        MEASUREMENT_WybierzPasmo();
    else
    {
        meas_strona = 0U;
        MeasRedrawWindow = 1U;
    }
}

static void MEASUREMENT_Akcja2(void)
{
    if (meas_strona == 0U)
        MEASUREMENT_SetFreq();
    else
    {
        MEASUREMENT_ParSerial();
        MeasRedrawWindow = 1U;
    }
}

static void MEASUREMENT_Akcja3(void)
{
    if (meas_strona == 0U)
    {
        meas_strona = 1U;
        MeasRedrawWindow = 1U;
    }
    else
        MEASUREMENT_SmithMatch();
}

static void MEASUREMENT_Akcja4(void)
{
    MEASUREMENT_SmithPelny();
}

static void MEASUREMENT_Akcja5(void)
{
    if (meas_strona == 0U)
        MEASUREMENT_DaneMetrologiczne();
    else
        MEASUREMENT_Screenshot();
    MeasRedrawWindow = 1U;
}

#define MEAS_SMITH_PELNY_CX 240
#define MEAS_SMITH_PELNY_CY 112
#define MEAS_SMITH_PELNY_R 104

static void MEASUREMENT_RysujSmithPelny(void)
{
    const uint32_t flagi_siatki = SMITH_R50 | SMITH_R25 | SMITH_R10 | SMITH_R100 |
                                   SMITH_R200 | SMITH_J50 | SMITH_J100 | SMITH_J200 |
                                   SMITH_J25 | SMITH_J10 | SMITH_SWR2 | SMITH_Y50;
    const uint32_t flagi_opisow = SMITH_R25 | SMITH_R50 | SMITH_R100 | SMITH_R200 |
                                  SMITH_J25 | SMITH_J50 | SMITH_J100 | SMITH_J200;
    const LCDColor tlo = UI_KolorTlaEkranu();
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor akcent = UI_KolorRamki(UI_STYL_AKCENT);
    float complex g;
    uint32_t i;

    /* Smith ma własny ekran. Nie dokładamy paneli po bokach ani danych,
     * które zmniejszałyby wykres; cała przestrzeń nad paskiem jest wykresem. */
    UI_WyczyscEkran();
    SMITH_DrawGrid(MEAS_SMITH_PELNY_CX, MEAS_SMITH_PELNY_CY, MEAS_SMITH_PELNY_R,
                   UI_KolorRamki(UI_STYL_NIEAKTYWNY), tlo, flagi_siatki);
    SMITH_DrawLabels(tekst, tlo, flagi_opisow);

    SMITH_ResetStartPoint();
    for (i = 0U; i < 100U; ++i)
    {
        if (crealf(zFine500[i]) == 9999.0f ||
            !isfinite(crealf(zFine500[i])) || !isfinite(cimagf(zFine500[i])))
            continue;
        g = OSL_GFromZ(zFine500[i], (float)CFG_GetParam(CFG_PARAM_R0));
        if (isfinite(crealf(g)) && isfinite(cimagf(g)))
            SMITH_DrawG((int)i, g, akcent);
    }

    g = OSL_GFromZ(meas_ostatnia_impedancja, (float)CFG_GetParam(CFG_PARAM_R0));
    if (isfinite(crealf(g)) && isfinite(cimagf(g)) && cabsf(g) <= 1.1f)
    {
        const int x = MEAS_SMITH_PELNY_CX + (int)roundf(crealf(g) * MEAS_SMITH_PELNY_R);
        const int y = MEAS_SMITH_PELNY_CY - (int)roundf(cimagf(g) * MEAS_SMITH_PELNY_R);
        const LCDColor k = UI_KolorRamki(UI_STYL_OSTRZEZENIE);
        LCD_Circle(LCD_MakePoint(x, y), 5, k);
        LCD_HLine(LCD_MakePoint(x - 8, y), 17, k);
        LCD_VLine(LCD_MakePoint(x, y - 8), 17, k);
    }

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t lc = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t zrzut = UI_ObszarPrzyciskuDolnego(2U);
        UI_RysujPrzycisk(lc.x, lc.y, lc.szerokosc, lc.wysokosc, "LC",
                         UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(zrzut.x, zrzut.y, zrzut.szerokosc, zrzut.wysokosc,
                         JEZYK_Wybierz("Zrzut", "Screenshot", "Bild", "Снимок"),
                         UI_STYL_NORMALNY, FONT_FRAN);
    }
}

static void MEASUREMENT_SmithPelny(void)
{
    uint8_t koniec = 0U;
    isMatch = 0U;
    while (TOUCH_IsPressed())
        Sleep(10);

    MEASUREMENT_RysujSmithPelny();
    LCD_ShowActiveLayerOnly();

    while (!koniec)
    {
        LCDPoint punkt;
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            koniec = 1U;
        else if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                while (TOUCH_IsPressed()) Sleep(10);
                koniec = 1U;
            }
            else
            {
                const UI_PROSTOKAT_t lc = UI_ObszarPrzyciskuDolnego(1U);
                const UI_PROSTOKAT_t zrzut = UI_ObszarPrzyciskuDolnego(2U);
                if (UI_CzyPunktWObszarze(punkt, &lc))
                {
                    while (TOUCH_IsPressed()) Sleep(10U);
                    if (meas_ostatni_pomiar_poprawny)
                        MATCH_OtworzAsystenta(meas_ostatnia_impedancja,
                                              CFG_GetParam(CFG_PARAM_MEAS_F),
                                              (float)CFG_GetParam(CFG_PARAM_R0));
                    MEASUREMENT_RysujSmithPelny();
                }
                else if (UI_CzyPunktWObszarze(punkt, &zrzut))
                {
                    while (TOUCH_IsPressed()) Sleep(10U);
                    MEASUREMENT_Screenshot();
                    MEASUREMENT_RysujSmithPelny();
                }
            }
        }
        Sleep(10);
    }

    while (TOUCH_IsPressed())
        Sleep(10);
    freqOld = 0U;
    MeasRedrawWindow = 1U;
}

static float Weryfikacja_OczekiwanySWR(float rezystancja_ohm)
{
    return DSP_CalcVSWR(rezystancja_ohm + 0.0f * I);
}

static void Weryfikacja_WykonajPomiar(void)
{
    const uint32_t f = CFG_GetParam(CFG_PARAM_MEAS_F);
    int n = (int)CFG_GetParam(CFG_PARAM_MEAS_NSCANS);

    memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
    weryfikacja_wynik.czestotliwosc_hz = f;
    weryfikacja_wynik.wzorzec_ohm = Weryfikacja_WzorzecOhm(weryfikacja_indeks_wzorca);

    /* Weryfikacja korzysta z czystego wyniku po OSL. Lokalna kompensacja
     * przewodu nie jest w tej ścieżce nakładana. */
    Measurement_WykonajPomiar(f, 1, 1, n);
    if (meas_ostatni_pomiar_poprawny && isfinite(crealf(meas_ostatni_pomiar.impedancja_ohm)) && isfinite(cimagf(meas_ostatni_pomiar.impedancja_ohm)))
    {
        weryfikacja_wynik.ma_klasyczny = 1U;
        weryfikacja_wynik.klasyczny = meas_ostatni_pomiar.impedancja_ohm;
        weryfikacja_wynik.swr_klasyczny = DSP_CalcVSWR(weryfikacja_wynik.klasyczny);
    }

    /* Weryfikujemy dokładnie tę samą ścieżkę, której używa pomiar użytkowy. */
    GEN_SetMeasurementFreq(0);
}

static int Weryfikacja_ZapiszCSV(void)
{
    FIL plik = {0};
    FRESULT wynik;
    UINT zapisano = 0;
    char sciezka[64];
    char wiersz[256];
    const char *naglowek = "freq_hz;standard_ohm;expected_swr;classic_ok;classic_r_ohm;classic_x_ohm;classic_swr\r\n";
    float swr_oczekiwany;

    if (!CFG_CzyKartaSDDostepna() || !weryfikacja_wynik.ma_klasyczny)
        return 0;

    (void)f_mkdir(g_aa_dir);
    snprintf(sciezka, sizeof(sciezka), "%s/verify.csv", g_aa_dir);
    wynik = f_open(&plik, sciezka, FA_WRITE | FA_OPEN_ALWAYS);
    if (wynik != FR_OK)
        return 0;

    if (f_size(&plik) == 0U)
    {
        wynik = f_write(&plik, naglowek, (UINT)strlen(naglowek), &zapisano);
        if (wynik != FR_OK || zapisano != strlen(naglowek))
        {
            f_close(&plik);
            return 0;
        }
    }
    if (f_lseek(&plik, f_size(&plik)) != FR_OK)
    {
        f_close(&plik);
        return 0;
    }

    swr_oczekiwany = Weryfikacja_OczekiwanySWR(weryfikacja_wynik.wzorzec_ohm);
    snprintf(wiersz, sizeof(wiersz),
             "%lu;%.3f;%.5f;%u;%.5f;%.5f;%.5f\r\n",
             (unsigned long)weryfikacja_wynik.czestotliwosc_hz,
             (double)weryfikacja_wynik.wzorzec_ohm,
             (double)swr_oczekiwany,
             (unsigned int)weryfikacja_wynik.ma_klasyczny,
             (double)crealf(weryfikacja_wynik.klasyczny),
             (double)cimagf(weryfikacja_wynik.klasyczny),
             (double)weryfikacja_wynik.swr_klasyczny);
    zapisano = 0;
    wynik = f_write(&plik, wiersz, (UINT)strlen(wiersz), &zapisano);
    f_close(&plik);
    return wynik == FR_OK && zapisano == strlen(wiersz);
}


#define WERYFIKACJA_PASMA_MAX_PUNKTOW 320U

static void Weryfikacja_DodajPunktPasma(uint32_t *punkty, uint16_t *liczba,
                                        uint32_t f_hz, uint32_t fmin_hz, uint32_t fmax_hz)
{
    uint16_t i;

    if (punkty == NULL || liczba == NULL || f_hz < fmin_hz || f_hz > fmax_hz)
        return;

    for (i = 0U; i < *liczba; ++i)
    {
        if (punkty[i] == f_hz)
            return;
    }

    if (*liczba < WERYFIKACJA_PASMA_MAX_PUNKTOW)
        punkty[(*liczba)++] = f_hz;
}

static uint16_t Weryfikacja_ZbudujPunktyPasma(uint32_t *punkty, uint32_t fmin_hz, uint32_t fmax_hz)
{
    uint16_t liczba = 0U;
    uint16_t i;
    uint16_t j;
    const uint32_t granica_osl_hz = 150000000U;
    const uint32_t krok_dol_hz = 1000000U;
    const uint32_t krok_gora_hz = 3000000U;
    const uint32_t max_si_hz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    const uint8_t harmoniczna_max = GEN_MaksHarmoniczna();
    uint64_t f;
    const uint8_t harmoniczne_graniczne[] = {1U, 3U, 5U};

    Weryfikacja_DodajPunktPasma(punkty, &liczba, fmin_hz, fmin_hz, fmax_hz);

    /*
     * To sprawdzenie ma odróżnić błąd samej kalibracji od błędu interpolacji.
     * Dlatego większość punktów leży dokładnie na siatce OSL:
     * - poniżej 150 MHz co 1 MHz (siatka OSL ma krok 100 kHz),
     * - od 150 MHz co 3 MHz (siatka OSL ma krok 300 kHz).
     *
     * Jeżeli ten sam wzorzec, którym wykonano OSL, nie jest płaski również
     * w tych punktach, przyczyny trzeba szukać w powtarzalności toru, szumie
     * albo różnicy ścieżki kalibracja/pomiar, a nie w interpolacji.
     */
    f = ((uint64_t)fmin_hz + krok_dol_hz - 1ULL) / krok_dol_hz * krok_dol_hz;
    for (; f <= fmax_hz && f <= granica_osl_hz; f += krok_dol_hz)
        Weryfikacja_DodajPunktPasma(punkty, &liczba, (uint32_t)f, fmin_hz, fmax_hz);

    if (fmax_hz > granica_osl_hz)
    {
        f = granica_osl_hz;
        for (; f <= fmax_hz; f += krok_gora_hz)
            Weryfikacja_DodajPunktPasma(punkty, &liczba, (uint32_t)f, fmin_hz, fmax_hz);
    }

    /*
     * Przy zmianie harmonicznej dokładamy trzy najbliższe węzły OSL.
     * Pozwala to zobaczyć osobno ostatni punkt starej harmonicznej, granicę
     * oraz pierwszy punkt nowej harmonicznej bez mieszania ich w wykresie.
     */
    for (i = 0U; i < sizeof(harmoniczne_graniczne) / sizeof(harmoniczne_graniczne[0]); ++i)
    {
        const uint8_t harmoniczna_przed = harmoniczne_graniczne[i];
        const uint64_t granica = (uint64_t)harmoniczna_przed * (uint64_t)max_si_hz;

        if (harmoniczna_przed >= harmoniczna_max)
            break;
        if (granica <= UINT32_MAX)
        {
            const uint32_t g = (uint32_t)granica;
            if (g >= 300000U)
                Weryfikacja_DodajPunktPasma(punkty, &liczba, g - 300000U, fmin_hz, fmax_hz);
            Weryfikacja_DodajPunktPasma(punkty, &liczba, g, fmin_hz, fmax_hz);
            if (g <= UINT32_MAX - 300000U)
                Weryfikacja_DodajPunktPasma(punkty, &liczba, g + 300000U, fmin_hz, fmax_hz);
        }
    }

    Weryfikacja_DodajPunktPasma(punkty, &liczba, fmax_hz, fmin_hz, fmax_hz);

    for (i = 1U; i < liczba; ++i)
    {
        const uint32_t wartosc = punkty[i];
        j = i;
        while (j > 0U && punkty[j - 1U] > wartosc)
        {
            punkty[j] = punkty[j - 1U];
            --j;
        }
        punkty[j] = wartosc;
    }

    return liczba;
}

static int Weryfikacja_CzyPrzerwacPasmo(void)
{
    LCDPoint punkt;
    const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

    if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        return 1;

    if (TOUCH_Poll(&punkt))
    {
        while (TOUCH_IsPressed())
            ;
        return 1;
    }
    return 0;
}

static int Weryfikacja_ZapiszNaglowekPasma(FIL *plik, float wzorzec_ohm,
                                             uint32_t port_extension_config_ps)
{
    char tekst[768];
    UINT zapisano = 0U;
    int dlugosc;
    uint32_t data = 0U;
    uint32_t godzina = 0U;
    unsigned char sekunda = 0U;
    short ampm = 0;
    KAL_META_DANE_t meta_hw;
    KAL_META_DANE_t meta_osl;
    char czas_hw[32];
    char czas_osl[32];

    memset(&meta_hw, 0, sizeof(meta_hw));
    memset(&meta_osl, 0, sizeof(meta_osl));
    (void)KAL_META_Pobierz(KAL_META_HW, -1, &meta_hw);
    if (OSL_GetSelected() >= 0)
        (void)KAL_META_Pobierz(KAL_META_OSL, OSL_GetSelected(), &meta_osl);
    KAL_META_FormatujCzas(&meta_hw, czas_hw, sizeof(czas_hw));
    KAL_META_FormatujCzas(&meta_osl, czas_osl, sizeof(czas_osl));

    if (RTCpresent)
    {
        getDate(&data);
        getTime(&godzina, &sekunda, &ampm, 0);
    }

    dlugosc = snprintf(tekst, sizeof(tekst),
                       "# EU1KY-PL 2026;%s;format=4\r\n"
                       "# data=%08lu;czas=%04lu%02u;wzorzec_ohm=%.3f;fmin_hz=%lu;fmax_hz=%lu;si5351_max_hz=%lu;harmoniczna_max=%u;osl=%s;hw=%s\r\n"
                       "# osl_config_ok=%u;hw_config_ok=%u;osl_older_than_hw=%u;port_ext_config_ps=%lu;port_ext_applied=0\r\n"
                       "freq_hz;standard_ohm;harmonic;clk_base_hz;ok;v_mv;i_mv;phase_deg;phase_coherence;spread_v_pct;spread_i_pct;raw_r_ohm;raw_x_ohm;corr_r_ohm;corr_x_ohm;swr;delta_r_ohm;abs_x_ohm;hw_requested;osl_requested\r\n",
                       PROJEKT_WERSJA,
                       (unsigned long)data, (unsigned long)godzina, (unsigned int)sekunda,
                       (double)wzorzec_ohm,
                       (unsigned long)CFG_GetParam(CFG_PARAM_BAND_FMIN),
                       (unsigned long)CFG_GetParam(CFG_PARAM_BAND_FMAX),
                       (unsigned long)CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ),
                       (unsigned)GEN_MaksHarmoniczna(),
                       czas_osl, czas_hw,
                       (unsigned)(meta_osl.istnieje && KAL_META_CzyZgodnaZKonfiguracja(&meta_osl)),
                       (unsigned)(meta_hw.istnieje && KAL_META_CzyZgodnaZKonfiguracja(&meta_hw)),
                       (unsigned)KAL_META_CzyStarsza(&meta_osl, &meta_hw),
                       (unsigned long)port_extension_config_ps);
    if (dlugosc <= 0 || (size_t)dlugosc >= sizeof(tekst))
        return 0;

    return f_write(plik, tekst, (UINT)dlugosc, &zapisano) == FR_OK && zapisano == (UINT)dlugosc;
}

static int Weryfikacja_Pasma(void)
{
    uint32_t punkty[WERYFIKACJA_PASMA_MAX_PUNKTOW];
    uint16_t liczba_punktow;
    uint16_t i;
    const uint32_t fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const float wzorzec_ohm = Weryfikacja_WzorzecOhm(weryfikacja_indeks_wzorca);
    const uint32_t port_extension_ps = CFG_GetParam(CFG_PARAM_PORT_EXT_PS);
    int n = (int)CFG_GetParam(CFG_PARAM_MEAS_NSCANS);
    FIL plik = {0};
    FRESULT wynik;
    UINT zapisano;
    char wiersz[384];
    char postep[96];
    int zakonczono = 1;

    if (!CFG_CzyKartaSDDostepna() || fmin_hz >= fmax_hz)
        return 0;
    if (n < 5)
    {
        /* Weryfikacja pasma jest narzędziem diagnostycznym, nie szybkim ekranem.
         * Minimum pięć powtórzeń daje sensowną informację o rozrzucie i
         * spójności fazy bez zmieniania ustawień zwykłego pomiaru. */
        n = 5;
    }

    liczba_punktow = Weryfikacja_ZbudujPunktyPasma(punkty, fmin_hz, fmax_hz);
    if (liczba_punktow < 2U)
        return 0;

    (void)f_mkdir(g_aa_dir);
    wynik = f_open(&plik, Weryfikacja_SciezkaPasma(weryfikacja_indeks_wzorca), FA_WRITE | FA_CREATE_ALWAYS);
    if (wynik != FR_OK)
        return 0;
    if (!Weryfikacja_ZapiszNaglowekPasma(&plik, wzorzec_ohm, port_extension_ps))
    {
        f_close(&plik);
        return 0;
    }

    /*
     * Przycisk „Weryfikacja pasma” jest dotykowy. Bez odczekania na puszczenie
     * procedura widziała ten sam dotyk ponownie i traktowała go jako żądanie
     * przerwania już przed pierwszym punktem. To był powód powtarzalnego
     * komunikatu „nie udało się wykonać lub zapisać testu pasma”.
     */
    while (TOUCH_IsPressed())
        Sleep(5U);
    WEJSCIA_WyczyscZdarzenia();

    /* Wzorce oceniamy w płaszczyźnie OSL; Port Extension nie jest tu nakładane. */
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_WERYFIKACJA_PASMA_TYTUL));
    UI_RysujPanel(16, 54, 448, 126, JEZYK_Tekst(TEKST_INFORMACJA), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(),
               28, 82, JEZYK_Tekst(TEKST_WERYFIKACJA_PASMA_TRWA));
    snprintf(postep, sizeof(postep), "%.0f Ohm   %u %s", (double)wzorzec_ohm,
             (unsigned)liczba_punktow,
             JEZYK_Wybierz("punktów", "points", "Punkte", "точек"));
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaPola(), 28, 112, postep);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(), 28, 148,
               JEZYK_Wybierz("Wstecz / START-STOP / dotyk: przerwij", "Back / START-STOP / touch: cancel", "Zurück / START-STOP / Touch: Abbruch", "Назад / START-STOP / касание: отмена"));

    WEJSCIA_WyczyscZdarzenia();
    for (i = 0U; i < liczba_punktow; ++i)
    {
        DSP_DANE_METROLOGICZNE_t dane;
        const uint32_t f_hz = punkty[i];
        float swr = NAN;
        float delta_r = NAN;
        float abs_x = NAN;
        int dlugosc;

        if (Weryfikacja_CzyPrzerwacPasmo())
        {
            zakonczono = 0;
            break;
        }

        Measurement_WykonajPomiarZakres(f_hz, 1, 1, n, fmin_hz, fmax_hz);
        memset(&dane, 0, sizeof(dane));
        DSP_PobierzDaneMetrologiczne(&dane);

        if (dane.poprawny && isfinite(crealf(dane.impedancja_po_osl)) &&
            isfinite(cimagf(dane.impedancja_po_osl)))
        {
            swr = DSP_CalcVSWR(dane.impedancja_po_osl);
            delta_r = crealf(dane.impedancja_po_osl) - wzorzec_ohm;
            abs_x = fabsf(cimagf(dane.impedancja_po_osl));
        }

        dlugosc = snprintf(wiersz, sizeof(wiersz),
                           "%lu;%.3f;%u;%lu;%u;%.6f;%.6f;%.4f;%.6f;%.4f;%.4f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%.6f;%u;%u\r\n",
                           (unsigned long)f_hz, (double)wzorzec_ohm,
                           (unsigned)dane.harmoniczna_generatora,
                           (unsigned long)dane.czestotliwosc_bazowa_hz,
                           (unsigned)dane.poprawny,
                           (double)dane.napiecie_v_mv, (double)dane.napiecie_i_mv,
                           (double)dane.faza_stopnie, (double)dane.spojnosc_fazy,
                           (double)dane.rozrzut_v_proc, (double)dane.rozrzut_i_proc,
                           (double)crealf(dane.impedancja_przed_osl),
                           (double)cimagf(dane.impedancja_przed_osl),
                           (double)crealf(dane.impedancja_po_osl),
                           (double)cimagf(dane.impedancja_po_osl),
                           (double)swr, (double)delta_r, (double)abs_x,
                           (unsigned)dane.korekcja_hw_zadana,
                           (unsigned)dane.korekcja_osl_zadana);
        if (dlugosc <= 0 || (size_t)dlugosc >= sizeof(wiersz))
        {
            zakonczono = 0;
            break;
        }
        zapisano = 0U;
        if (f_write(&plik, wiersz, (UINT)dlugosc, &zapisano) != FR_OK || zapisano != (UINT)dlugosc)
        {
            zakonczono = 0;
            break;
        }

        if ((i % 4U) == 0U || i + 1U == liczba_punktow)
        {
            LCD_FillRect(LCD_MakePoint(28, 130), LCD_MakePoint(448, 146), UI_KolorTlaPola());
            snprintf(postep, sizeof(postep), "%u/%u   %.3f MHz   H%u",
                     (unsigned)(i + 1U), (unsigned)liczba_punktow,
                     (double)f_hz / 1000000.0, (unsigned)dane.harmoniczna_generatora);
            FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaPola(), 28, 130, postep);
        }
    }

    GEN_SetMeasurementFreq(0);
    (void)f_sync(&plik);
    f_close(&plik);
    return zakonczono && i == liczba_punktow;
}

static void Weryfikacja_EdytujRezystorKontrolny(void)
{
    const uint32_t biezacy = CFG_GetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM);
    const uint32_t nowy = NumKeypadMiliohm(
        biezacy,
        100U,
        9999999U,
        JEZYK_Wybierz("Rezystor kontrolny [Ohm]",
                      "Control resistor [ohm]",
                      "Kontrollwiderstand [Ohm]",
                      "Контрольный резистор [Ом]"));

    if (nowy == 0U)
        return;

    CFG_SetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM, nowy);
    if (!CFG_FlushSprawdzony())
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
        return;
    }

    /*
     * Ten rezystor jest niezależnym punktem kontrolnym: nie bierze udziału
     * w kalibracji OSL. Po jego zmianie przełączamy widok na punkt kontrolny
     * i kasujemy poprzedni wynik, aby nie zestawić nowej wartości ze starym
     * pomiarem.
     */
    weryfikacja_indeks_wzorca = 3U;
    memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
}

static void Weryfikacja_Rysuj(void)
{
    char tekst[128];
    char ftekst[24];
    float rstd = Weryfikacja_WzorzecOhm(weryfikacja_indeks_wzorca);
    float swr_oczekiwany = Weryfikacja_OczekiwanySWR(rstd);
    unsigned int i;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_WERYFIKACJA_WZORCAMI));

    for (i = 0; i < WERYFIKACJA_LICZBA_WZORCOW; ++i)
    {
        /*
         * Cztery punkty mieszczą się w jednym wierszu. Wartość dokładna jest
         * pokazana w polu „Oczekiwane” poniżej, dzięki czemu nazwy przycisków
         * pozostają czytelne także w niemieckim i rosyjskim.
         */
        UI_RysujPrzycisk((uint16_t)(8U + i * 116U), 40U, 112U, 38U,
                         Weryfikacja_NazwaWzorca((uint8_t)i),
                         i == weryfikacja_indeks_wzorca ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY,
                         FONT_FRAN);
    }

    UI_FormatujCzestotliwoscMHz(CFG_GetParam(CFG_PARAM_MEAS_F), ftekst, sizeof(ftekst));
    snprintf(tekst, sizeof(tekst), "%s: %.3f Ohm   SWR %.3f   f %s",
             JEZYK_Tekst(TEKST_OCZEKIWANE), (double)rstd, (double)swr_oczekiwany, ftekst);
    UI_RysujPoleStatusu(8, 82, 464, 42, JEZYK_Tekst(TEKST_WZORZEC), tekst, UI_STYL_NORMALNY);

    if (!weryfikacja_wynik.ma_klasyczny)
    {
        UI_RysujPoleStatusu(8, 130, 464, 82, JEZYK_Tekst(TEKST_INFORMACJA),
                            JEZYK_Tekst(TEKST_WERYFIKACJA_BRAK_WYNIKU), UI_STYL_NIEAKTYWNY);
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "R=%+.2f  X=%+.2f Ohm  SWR=%.3f  dR=%+.2f",
                 (double)crealf(weryfikacja_wynik.klasyczny),
                 (double)cimagf(weryfikacja_wynik.klasyczny),
                 (double)weryfikacja_wynik.swr_klasyczny,
                 (double)(crealf(weryfikacja_wynik.klasyczny) - rstd));
        UI_RysujPoleStatusu(8, 130, 464, 82, JEZYK_Tekst(TEKST_WYNIK_KLASYCZNY), tekst,
                            UI_STYL_NORMALNY);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                   18, 188,
                   weryfikacja_indeks_wzorca == 3U
                       ? JEZYK_Wybierz("Niezależny rezystor: nie był użyty do OSL.",
                                      "Independent resistor: not used for OSL.",
                                      "Unabhängiger Widerstand: nicht für OSL verwendet.",
                                      "Независимый резистор: не использовался для OSL.")
                       : JEZYK_Wybierz("Ta sama ścieżka OSL co w pomiarze.",
                                      "Same OSL path as measurement.",
                                      "Gleicher OSL-Pfad wie bei der Messung.",
                                      "Тот же тракт OSL, что и при измерении."));
    }

    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t zmierz = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t pasma = UI_ObszarPrzyciskuDolnego(2U);
        const UI_PROSTOKAT_t rezystor = UI_ObszarPrzyciskuDolnego(3U);
        const UI_PROSTOKAT_t csv = UI_ObszarPrzyciskuDolnego(4U);
        UI_RysujPrzycisk(zmierz.x, zmierz.y, zmierz.szerokosc, zmierz.wysokosc,
                         JEZYK_Tekst(TEKST_ZMIERZ), UI_STYL_AKCENT, FONT_FRAN);
        UI_RysujPrzycisk(pasma.x, pasma.y, pasma.szerokosc, pasma.wysokosc,
                         JEZYK_Tekst(TEKST_WERYFIKACJA_PASMA), UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(rezystor.x, rezystor.y, rezystor.szerokosc, rezystor.wysokosc,
                         JEZYK_Wybierz("Rezystor", "Resistor", "Widerstand", "Резистор"),
                         weryfikacja_indeks_wzorca == 3U ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY,
                         FONT_FRAN);
        UI_RysujPrzycisk(csv.x, csv.y, csv.szerokosc, csv.wysokosc,
                         JEZYK_Wybierz("Raport CSV", "CSV report", "CSV-Bericht", "Отчёт CSV"),
                         UI_STYL_NORMALNY, FONT_FRAN);
    }
}

void MEASUREMENT_WeryfikacjaWzorcami(void)
{
    LCDPoint punkt;
    memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
    while (TOUCH_IsPressed())
        ;

    Weryfikacja_Rysuj();
    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            weryfikacja_indeks_wzorca = weryfikacja_indeks_wzorca == 0U ? (WERYFIKACJA_LICZBA_WZORCOW - 1U) : weryfikacja_indeks_wzorca - 1U;
            memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
            Weryfikacja_Rysuj();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            weryfikacja_indeks_wzorca = (uint8_t)((weryfikacja_indeks_wzorca + 1U) % WERYFIKACJA_LICZBA_WZORCOW);
            memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
            Weryfikacja_Rysuj();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        {
            Weryfikacja_WykonajPomiar();
            Weryfikacja_Rysuj();
        }

        if (TOUCH_Poll(&punkt))
        {
            if (punkt.y >= 38 && punkt.y <= 80)
            {
                int idx;
                for (idx = 0; idx < (int)WERYFIKACJA_LICZBA_WZORCOW; ++idx)
                {
                    const int x0 = 8 + idx * 116;
                    if ((int)punkt.x >= x0 && (int)punkt.x < x0 + 112)
                    {
                        weryfikacja_indeks_wzorca = (uint8_t)idx;
                        memset(&weryfikacja_wynik, 0, sizeof(weryfikacja_wynik));
                        Weryfikacja_Rysuj();
                        break;
                    }
                }
            }
            else if (UI_CzyDotknietoWstecz(punkt))
                break;
            else if (Measurement_CzyDotknietoPrzyciskuDolnego(punkt, 1U))
            {
                Weryfikacja_WykonajPomiar();
                Weryfikacja_Rysuj();
            }
            else if (Measurement_CzyDotknietoPrzyciskuDolnego(punkt, 2U))
            {
                const TEKST_ID_t komunikat = Weryfikacja_Pasma() ? TEKST_WERYFIKACJA_PASMA_OK
                                                                      : TEKST_WERYFIKACJA_PASMA_BLAD;
                KOMUNIKAT_Pokaz(TEKST_INFORMACJA, komunikat);
                Weryfikacja_Rysuj();
            }
            else if (Measurement_CzyDotknietoPrzyciskuDolnego(punkt, 3U))
            {
                while (TOUCH_IsPressed())
                    ;
                Weryfikacja_EdytujRezystorKontrolny();
                Weryfikacja_Rysuj();
            }
            else if (Measurement_CzyDotknietoPrzyciskuDolnego(punkt, 4U))
            {
                const TEKST_ID_t komunikat = Weryfikacja_ZapiszCSV() ? TEKST_WERYFIKACJA_CSV_OK
                                                                      : TEKST_WERYFIKACJA_CSV_BLAD;
                KOMUNIKAT_Pokaz(TEKST_INFORMACJA, komunikat);
                Weryfikacja_Rysuj();
            }

            while (TOUCH_IsPressed())
                ;
        }
        Sleep(10);
    }
    GEN_SetMeasurementFreq(0);
}

static void Measurement_RysujDaneMetrologiczne(void)
{
    DSP_DANE_METROLOGICZNE_t dane;
    char tekst[96];
    char czestotliwosc[24];
    float swr;
    const char *nazwa_osl = OSL_GetSelected() >= 0 ? OSL_GetSelectedName() : "-";

    DSP_PobierzDaneMetrologiczne(&dane);
    swr = DSP_CalcVSWR(dane.impedancja_po_osl);
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_POMIAR_DANE_TYTUL));

    UI_RysujPanel(8, 40, 464, 108,
                  JEZYK_Wybierz("DSP / mostek", "DSP / bridge", "DSP / Brücke", "DSP / мост"),
                  UI_STYL_NORMALNY);
    UI_FormatujCzestotliwoscMHz(dane.czestotliwosc_hz, czestotliwosc, sizeof(czestotliwosc));
    snprintf(tekst, sizeof(tekst), "f: %s   IF: %lu Hz   n: %u",
             czestotliwosc, (unsigned long)DSP_GetIF(), (unsigned int)dane.liczba_pomiarow);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 61, tekst);

    if (meas_ostatni_pomiar_poprawny &&
        meas_ostatni_pomiar.model_korekcji == POMIAR_S11_KOREKCJA_OSL_70CM)
    {
        snprintf(tekst, sizeof(tekst), "%s H%u CLK %.3f MHz  HW:%s OSL:70cm",
                 JEZYK_Wybierz("MAD + kołowa", "MAD + circular", "MAD + zirkulär", "MAD + круговая"),
                 (unsigned)dane.harmoniczna_generatora,
                 (double)dane.czestotliwosc_bazowa_hz / 1000000.0,
                 OSL_IsErrCorrLoaded() ? "OK" : "--");
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "%s H%u CLK %.3f MHz  HW:%s OSL:%s %s",
                 JEZYK_Wybierz("MAD + kołowa", "MAD + circular", "MAD + zirkulär", "MAD + круговая"),
                 (unsigned)dane.harmoniczna_generatora,
                 (double)dane.czestotliwosc_bazowa_hz / 1000000.0,
                 OSL_IsErrCorrLoaded() ? "OK" : "--", nazwa_osl, OSL_IsSelectedValid() ? "OK" : "--");
    }
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 80, tekst);

    snprintf(tekst, sizeof(tekst), "V: %.3f mV   I: %.3f mV   |V/I|: %.5f",
             dane.napiecie_v_mv, dane.napiecie_i_mv, dane.stosunek_amplitud);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 99, tekst);

    snprintf(tekst, sizeof(tekst), "V/I: %+.2f dB   faza: %+.2f deg   %s: %.1f%%",
             dane.stosunek_db, dane.faza_stopnie, JEZYK_Tekst(TEKST_POMIAR_SPOJNOSC_FAZY),
             fminf(100.0f, fmaxf(0.0f, dane.spojnosc_fazy * 100.0f)));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 118, tekst);

    {
        const char *zakres_rf;
        UI_STYL_t styl_zakresu = UI_STYL_AKCENT;
        if (dane.harmoniczna_generatora >= 5U || dane.czestotliwosc_hz > 600000000U)
        {
            zakres_rf = JEZYK_Wybierz("eksperymentalny", "experimental", "experimentell", "экспериментальный");
            styl_zakresu = UI_STYL_OSTRZEZENIE;
        }
        else if (dane.harmoniczna_generatora == 3U)
        {
            zakres_rf = JEZYK_Wybierz("rozszerzony", "extended", "erweitert", "расширенный");
        }
        else
        {
            zakres_rf = JEZYK_Wybierz("podstawowy", "basic", "Basis", "базовый");
        }
        snprintf(tekst, sizeof(tekst), "%s V/I: %.2f%% / %.2f%%   H%u: %s",
                 JEZYK_Tekst(TEKST_POMIAR_ROZRZUT), dane.rozrzut_v_proc, dane.rozrzut_i_proc,
                 (unsigned)dane.harmoniczna_generatora, zakres_rf);
        FONT_Write(FONT_FRAN, UI_KolorRamki(styl_zakresu), UI_KolorTlaEkranu(), 18, 137, tekst);
    }

    UI_RysujPanel(8, 152, 464, 76,
                  JEZYK_Wybierz("Impedancja zespolona", "Complex impedance", "Komplexe Impedanz", "Комплексный импеданс"),
                  dane.poprawny ? UI_STYL_NORMALNY : UI_STYL_OSTRZEZENIE);

    snprintf(tekst, sizeof(tekst), "%s: R=%+.2f Ohm   X=%+.2f Ohm",
             JEZYK_Tekst(TEKST_POMIAR_PRZED_OSL),
             crealf(dane.impedancja_przed_osl), cimagf(dane.impedancja_przed_osl));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 171, tekst);

    if (dane.port_extension_aktywna)
    {
        snprintf(tekst, sizeof(tekst), "%s: R=%+.2f   X=%+.2f Ohm",
                 JEZYK_Tekst(TEKST_POMIAR_PO_OSL),
                 crealf(dane.impedancja_po_osl_przed_portext),
                 cimagf(dane.impedancja_po_osl_przed_portext));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 189, tekst);

        if (dane.port_extension_ps >= 1000U)
            snprintf(tekst, sizeof(tekst), "Port +%.3f ns: R=%+.2f   X=%+.2f   SWR=%.3f",
                     (double)dane.port_extension_ps / 1000.0,
                     crealf(dane.impedancja_po_osl), cimagf(dane.impedancja_po_osl), swr);
        else
            snprintf(tekst, sizeof(tekst), "Port +%lu ps: R=%+.2f   X=%+.2f   SWR=%.3f",
                     (unsigned long)dane.port_extension_ps,
                     crealf(dane.impedancja_po_osl), cimagf(dane.impedancja_po_osl), swr);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaEkranu(), 18, 207, tekst);
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "%s: R=%+.2f Ohm   X=%+.2f Ohm   SWR=%.3f",
                 JEZYK_Tekst(TEKST_POMIAR_PO_OSL),
                 crealf(dane.impedancja_po_osl), cimagf(dane.impedancja_po_osl), swr);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 18, 193, tekst);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
                   18, 211,
                   JEZYK_Wybierz("Kompensacja kabla: wyłączona", "Cable compensation: off", "Kabelkompensation: aus", "Компенсация кабеля: выкл"));
    }
    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t wzorce = UI_ObszarPrzyciskuDolnego(1U);
        UI_RysujPrzycisk(wzorce.x, wzorce.y, wzorce.szerokosc, wzorce.wysokosc,
                         JEZYK_Wybierz("Wzorce", "Standards", "Normale", "Эталоны"),
                         UI_STYL_NORMALNY, FONT_FRAN);
    }
}

static void MEASUREMENT_DaneMetrologiczne(void)
{
    LCDPoint punkt;
    const uint32_t warstwa = meas_warstwa_widoczna;
    const uint32_t warstwa_zapasowa = 1U - warstwa;

    /*
     * Pokazujemy migawkę ostatniego zakończonego pomiaru. RF jest wyłączone
     * na czas czytania danych; po powrocie główna pętla sama wznowi pomiar.
     */
    GEN_SetMeasurementFreq(0);
    BSP_LCD_SelectLayer(warstwa);
    Measurement_RysujDaneMetrologiczne();
    LCD_ShowActiveLayerOnly();

    while (TOUCH_IsPressed())
        ;

    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie;
        Sleep(10);
        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            MEASUREMENT_WeryfikacjaWzorcami();
            Measurement_RysujDaneMetrologiczne();
            continue;
        }
        if (TOUCH_Poll(&punkt) && punkt.y >= 216U)
        {
            if (UI_CzyDotknietoWstecz(punkt))
                break;
            if (Measurement_CzyDotknietoPrzyciskuDolnego(punkt, 1U))
            {
                while (TOUCH_IsPressed())
                    ;
                MEASUREMENT_WeryfikacjaWzorcami();
                Measurement_RysujDaneMetrologiczne();
                continue;
            }
        }
    }

    while (TOUCH_IsPressed())
        ;

    /* Przywracamy poprzednią klatkę bez widocznego rysowania od zera. */
    LCD_KopiujWarstwe(warstwa_zapasowa, warstwa);
    BSP_LCD_SelectLayer(warstwa);
    LCD_ShowActiveLayerOnly();
    freqOld = 0;
    InitScan500();
    MeasRedrawWindow = 1;
}

static const struct HitRect MeasHitArr[] =
{
    /* Wspólny raster 6 x 70 px. Znaczenie pięciu akcji zależy od strony. */
    HITRECT(0,   220, 70, 45, MEASUREMENT_Exit),
    HITRECT(82,  220, 70, 45, MEASUREMENT_Akcja1),
    HITRECT(164, 220, 70, 45, MEASUREMENT_Akcja2),
    HITRECT(246, 220, 70, 45, MEASUREMENT_Akcja3),
    HITRECT(328, 220, 70, 45, MEASUREMENT_Akcja4),
    HITRECT(410, 220, 70, 45, MEASUREMENT_Akcja5),
    HITEND
};

void DrawHeader(void)
{
    /* Zachowane API historyczne. W nowym ekranie sterowanie F jest w jednym
     * edytorze i w selektorze Pasmo, więc nie rysujemy sześciu małych +/- . */
    Measurement_RysujPasekDolny();
}

//Measurement mode window. To change it to VSWR tap the lower part of display.
//To change frequency, in steps of +/- 500, 100 and 10 kHz, tap top part of the display,
//the step depends on how far you tap from the center.

/* ========================================================================
 * Dokumentacja z rzeczywistym pomiarem
 * ======================================================================== */
uint8_t MEASUREMENT_DokumentacjaRealnaZmierz(uint32_t czestotliwosc_hz, uint8_t liczba_usrednien)
{
    uint32_t i;
    uint32_t k;
    uint8_t ok;

    if (liczba_usrednien == 0U)
        liczba_usrednien = 1U;

    /*
     * Ustawienie jest tylko robocze. Generator dokumentacji zapisuje i
     * przywraca konfigurację po całej sesji, a tutaj niczego nie utrwalamy.
     */
    CFG_SetParam(CFG_PARAM_MEAS_F, czestotliwosc_hz);
    InitScan500();

    /*
     * Strona „Analiza” pokazuje mały przebieg +/-500 kHz. W normalnym trybie
     * dane te narastają w pętli interfejsu. Generator nie ma tej pętli, więc
     * zbieramy komplet 100 prawdziwych punktów jawnie, bez symulacji.
     */
    for (k = 0U; k < 5U; ++k)
    {
        for (i = 0U; i < 20U; ++i)
            Scan500((int)i, (int)k);
    }

    GEN_SetMeasurementFreq(czestotliwosc_hz);
    Sleep(2U);
    ok = Measurement_WykonajPomiar(czestotliwosc_hz, 1, 1, liczba_usrednien);
    meas_ostatnia_impedancja = meas_ostatni_pomiar.impedancja_ohm;
    GEN_SetMeasurementFreq(0U);
    return ok;
}

void MEASUREMENT_DokumentacjaRealnaRysuj(POMIAR_DOK_WIDOK_t widok)
{
    meas_strona = (widok == POMIAR_DOK_WIDOK_ANALIZA) ? 1U : 0U;
    Measurement_RysujStrone(meas_ostatnia_impedancja);
}

void Single_Frequency_Proc(void)
{
    float delta, r, im;
    uint32_t fbkup, f_mess;

    int l, k;
    uint32_t speedcnt = 0;
    bool first = true;
    DSP_RX rx, rx0, rmid;
    LCDPoint pt;
    parallel = 0; // selects parallel/serial calculation
    DrawFine = 0;
    MeasRqExit = 0;
    MeasRedrawWindow = 0;
    fChanged = 1;
    isMatch = 0;
    meas_strona = 0U;
    /* SetColours ustala paletę samego wykresu. Nie nadpisujemy jej
     * paletą menu, bo wtedy przełącznik jasne/ciemne nie miał żadnego efektu. */
    SetColours();
    BSP_LCD_SelectLayer(0);
    LCD_FillAll(BackGrColor);
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BackGrColor);
    meas_warstwa_widoczna = 1U;
    LCD_ShowActiveLayerOnly();

    //Load saved middle frequency value from BKUP registers 2, 3
    //to MeasurementFreq
    while (TOUCH_IsPressed())
        ;
    fbkup = CFG_GetParam(CFG_PARAM_MEAS_F);
    if (!(fbkup >= BAND_FMIN &&
          fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) &&
          (fbkup % 1000) == 0))
    {
        CFG_SetParam(CFG_PARAM_MEAS_F, 14000000ul);
        CFG_Flush();
    }

    UI_RysujEkranPrzejsciowy(
        JEZYK_Tekst(TEKST_POJEDYNCZY_TYTUL),
        JEZYK_Tekst(TEKST_PRZYGOTOWANIE_FUNKCJI));
    /*
     * Ekran przejściowy jest celowo widoczny około 1,4 s. Opóźnienie występuje
     * tylko raz przy otwieraniu; częstotliwość samego pomiaru pozostaje bez zmian.
     */
    Sleep(1400);
    LCD_FillRect(LCD_MakePoint(1, 1), LCD_MakePoint(479, 190), BackGrColor); // Graph rectangle
    if (-1 == OSL_GetSelected())
    {
        FONT_Write(FONT_FRANBIG, LCD_RED, BackGrColor, 80, 120, JEZYK_Tekst(TEKST_BRAK_KALIBRACJI));
        Sleep(200);
    }

    /* Pojedynczy pomiar ma dwie czytelne strony. Smith jest osobnym ekranem. */
    Measurement_RysujStrone(50.0f + 0.0f * I);

    freqOld = 0;

    //   LCD_Rectangle(LCD_MakePoint(SCAN_ORIGIN_X - 1, SCAN_ORIGIN_Y-10), LCD_MakePoint(SCAN_ORIGIN_X + 201, SCAN_ORIGIN_Y + 22), LCD_BLUE);
    InitScan500();
    //ShowHitRect(MeasHitArr);
    //ShowIncDec();

    //MEASUREMENT_REDRAW:
    for (;;)
    {
        while (TOUCH_Poll(&pt))
        {
            if (HitTest(MeasHitArr, pt.x, pt.y) == 1)
            { //any button pressed?
                if (fChanged)
                {
                    MeasRedrawWindow = 1;
                    ShowF();
                }
                if (MeasRqExit + MeasRedrawWindow)
                    break;
            }
            speedcnt++;
            if (speedcnt < 20)
                Sleep(100);
            else
                Sleep(30);
            if (speedcnt > 50)
                meas_maxstep = 2000000;
        }

        MeasRedrawWindow = 0;
        f_mess = CFG_GetParam(CFG_PARAM_MEAS_F);
        GEN_SetMeasurementFreq(f_mess);
        Measurement_WykonajPomiar(f_mess, 1, 1, 1);
        Sleep(2);
        rmid = rx = meas_ostatni_pomiar.impedancja_ohm;
        meas_ostatnia_impedancja = rmid;

        Measurement_RysujStrone(rx);

        if (MeasMagDif >= 10.5f)
        {
            Sleep(1000);
        }
        speedcnt = 0; // ********************** ??
                      //Main window cycle
        l = k = 0;
        while (!MeasRedrawWindow)
        {
            while (TOUCH_Poll(&pt))
            {
                if (HitTest(MeasHitArr, pt.x, pt.y) == 1)
                { //any button pressed?
                    if (fChanged)
                    {
                        MeasRedrawWindow = 1;
                        ShowF();
                    }
                    if (MeasRqExit + MeasRedrawWindow)
                        break;
                }
                speedcnt++;
                if (speedcnt < 20)
                    Sleep(100);
                else
                    Sleep(30);
                if (speedcnt > 50)
                    meas_maxstep = 2000000;
            }
            Scan500(l++, k);

            f_mess = CFG_GetParam(CFG_PARAM_MEAS_F);
            GEN_SetMeasurementFreq(f_mess);
            Sleep(2);
            Measurement_WykonajPomiar(f_mess, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
            rx0 = meas_ostatni_pomiar.impedancja_ohm;
            if (first || (l == 1))
            {
                first = false;
                rmid = rx0;
            }
            else
            {
                r = crealf(rx0);
                im = cimagf(rx0);
                rmid = 0.95 * crealf(rmid) + 0.05f * r + I * (0.95f * cimagf(rmid) + 0.05f * im);
            }
            if (l >= 20)
            {
                l = 0;
                k = (k + 2) % 5; //0 2 4 1 3 0...

                meas_ostatnia_impedancja = rmid;
                Measurement_OdswiezDynamiczne(rmid);
                if (MeasMagDif >= 10.5f)
                    Sleep(10); // open - nothing connected
            }

            // DSP_Measure(CFG_GetParam(CFG_PARAM_MEAS_F), 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
            // rx0 = meas_ostatni_pomiar.impedancja_ohm;
            delta = fabsf(crealf(rx0) - crealf(rx));
            delta += fabsf(cimagf(rx0) - cimagf(rx));
            {
                const float mianownik = fabsf(crealf(rx0)) + fabsf(crealf(rx)) + 1.0f;
                delta /= mianownik;
            }
            if (isfinite(delta) && delta > 0.4f)
            { // was 0.06
                MeasRedrawWindow = 1;
                InitScan500();
                rx = rx0;
            }
            uint32_t speedcnt = 0;
            meas_maxstep = 500000;

            while (TOUCH_Poll(&pt))
            {
                if (HitTest(MeasHitArr, pt.x, pt.y) == 1)
                { //any button pressed?
                    if (fChanged)
                    {
                        MeasRedrawWindow = 1;
                        ShowF();
                    }
                    if (MeasRqExit + MeasRedrawWindow)
                        break;
                }
                speedcnt++;
                if (speedcnt < 20)
                    Sleep(100);
                else
                    Sleep(30);
                if (speedcnt > 50)
                    meas_maxstep = 2000000;
            }

            if (MeasRqExit)
            {
                GEN_SetMeasurementFreq(0);
                TRACK_Beep(1);

                return;
            }
            speedcnt = 0;
            meas_maxstep = 500000;
            if (fChanged)
            {
                MeasRedrawWindow = 1;
                InitScan500();
                CFG_Flush();
                fChanged = 0;
            }
            Sleep(5);
        }
    }
}

void setup_GPIO(void) // GPIO I Pin 2 for buzzer
{
    GPIO_InitTypeDef gpioInitStructure;

    __HAL_RCC_GPIOI_CLK_ENABLE();
    gpioInitStructure.Pin = GPIO_PIN_2;
    gpioInitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInitStructure.Pull = GPIO_NOPULL;
    gpioInitStructure.Speed = GPIO_SPEED_MEDIUM;
    HAL_GPIO_Init(GPIOI, &gpioInitStructure);
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_2, 0);
    //AUDIO1=0;
}

static int muted;
static uint32_t ToneFreq, last_TuneFreq;
static int ToneTrigger;
int Tone;
uint8_t SWRLimit;
int rqExitSWR3;
extern float freqMHzf;

//extern void ShowFr(int);

static const char *Strojenie_T(const char *pl, const char *en, const char *de, const char *ru)
{
    switch (JEZYK_Aktualny())
    {
    case JEZYK_ANGIELSKI: return en;
    case JEZYK_NIEMIECKI: return de;
    case JEZYK_ROSYJSKI: return ru;
    case JEZYK_POLSKI:
    default: return pl;
    }
}

static void Strojenie_RysujPrzyciski(void)
{
    UI_STYL_t styl_dzwieku = muted ? UI_STYL_NIEAKTYWNY : UI_STYL_AKTYWNY;
    UI_STYL_t styl_progu = (SWRLimit == 1U) ? UI_STYL_NORMALNY : UI_STYL_AKCENT;
    char prog[24];

    if (SWRLimit == 2U)
        snprintf(prog, sizeof(prog), "SWR <= 2");
    else if (SWRLimit == 3U)
        snprintf(prog, sizeof(prog), "SWR <= 3");
    else
        snprintf(prog, sizeof(prog), "%s", Strojenie_T("Próg: wył.", "Limit: off", "Limit: aus", "Порог: выкл"));

    /*
     * Wszystkie akcje używają wspólnego rastra 70 x 45 px. Strojenie zmienia
     * częstotliwość, więc Pasmo jest dostępne dokładnie tak samo jak w
     * pojedynczym pomiarze i generatorze.
     */
    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t pasmo = UI_ObszarPrzyciskuDolnego(1U);
        const UI_PROSTOKAT_t czestotliwosc = UI_ObszarPrzyciskuDolnego(2U);
        const UI_PROSTOKAT_t analiza = UI_ObszarPrzyciskuDolnego(3U);
        const UI_PROSTOKAT_t dzwiek = UI_ObszarPrzyciskuDolnego(4U);
        const UI_PROSTOKAT_t prog_swr = UI_ObszarPrzyciskuDolnego(5U);

        UI_RysujPrzycisk(pasmo.x, pasmo.y, pasmo.szerokosc, pasmo.wysokosc,
                         Strojenie_T("Pasmo", "Band", "Band", "Диапазон"),
                         UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(czestotliwosc.x, czestotliwosc.y, czestotliwosc.szerokosc, czestotliwosc.wysokosc,
                         "F", UI_STYL_NORMALNY, FONT_FRAN);
        UI_RysujPrzycisk(analiza.x, analiza.y, analiza.szerokosc, analiza.wysokosc,
                         Strojenie_T("Analiza", "Analyze", "Analyse", "Анализ"),
                         UI_STYL_AKCENT, FONT_FRAN);
        UI_RysujPrzycisk(dzwiek.x, dzwiek.y, dzwiek.szerokosc, dzwiek.wysokosc,
                         JEZYK_Tekst(TEKST_DZWIEK), styl_dzwieku, FONT_FRAN);
        UI_RysujPrzycisk(prog_swr.x, prog_swr.y, prog_swr.szerokosc, prog_swr.wysokosc,
                         prog, styl_progu, FONT_FRAN);
    }
}

void DrawTuneButtonText(void)
{
    Strojenie_RysujPrzyciski();
}

void SWR_Mute(void)
{
    if (muted == 1)
    {
        muted = 0;
        SWRTone = 1;
        ToneTrigger = 1;
    }
    else
    {
        muted = 1;
        SWRTone = 0;
        UB_TIMER2_Stop();
    }
    Strojenie_RysujPrzyciski();
    while (TOUCH_IsPressed())
        ;
    Sleep(50);
}

static void SWR_Prog(void)
{
    if (SWRLimit == 1U)
        SWRLimit = 2U;
    else if (SWRLimit == 2U)
        SWRLimit = 3U;
    else
        SWRLimit = 1U;

    SWRTone = muted ? 0U : 1U;
    ToneTrigger = 1;
    Strojenie_RysujPrzyciski();
    while (TOUCH_IsPressed())
        ;
    Sleep(50);
}

void SWR_SetFrequencyMuted(void)
{
    if (Tone == 1)
    {
        Tone = 0;
        UB_TIMER2_Stop();
    }
    else
    {
        Tone = 1;
        SWR_SetFrequency();
        UB_TIMER2_Init_FRQ(last_TuneFreq);
        UB_TIMER2_Start();
    }
}


#define STROJENIE_SKAN_PUNKTY 161U
#define STROJENIE_SKAN_MIN_POL_ZAKRESU_HZ 100000U
#define STROJENIE_SKAN_MAX_POL_ZAKRESU_HZ 10000000U

static float complex g_strojenie_skan_z[STROJENIE_SKAN_PUNKTY]
    __attribute__((section(".user_sdram")));
static uint8_t g_strojenie_skan_maska[STROJENIE_SKAN_PUNKTY]
    __attribute__((section(".user_sdram")));
static uint8_t g_strojenie_prosba_analizy = 0U;
static uint8_t g_strojenie_prosba_odswiezenia_ukladu = 0U;

static void Strojenie_ZlecAnalize(void)
{
    g_strojenie_prosba_analizy = 1U;
}

static void Strojenie_UstawCzestotliwosc(void)
{
    MEASUREMENT_SetFreq();
    g_strojenie_prosba_odswiezenia_ukladu = 1U;
}

static void Strojenie_WybierzPasmo(void)
{
    MEASUREMENT_WybierzPasmo();
    g_strojenie_prosba_odswiezenia_ukladu = 1U;
}

static uint8_t Strojenie_ZbierzSkan(uint32_t cel_hz,
                                    uint32_t *f_start_hz,
                                    uint32_t *krok_hz,
                                    STROJENIE_ANTENA_WYNIK_t *wynik)
{
    POMIAR_S11_USTAWIENIA_t ustawienia;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    uint32_t pol_zakresu = cel_hz / 20U; /* +/- 5% częstotliwości celu. */
    uint32_t start_hz;
    uint32_t koniec_hz;
    uint32_t krok;
    uint32_t i;
    uint32_t poprawne = 0U;
    char tekst[96];

    if (f_start_hz == NULL || krok_hz == NULL || wynik == NULL ||
        cel_hz < fmin || cel_hz > fmax || fmax <= fmin)
        return 0U;

    if (pol_zakresu < STROJENIE_SKAN_MIN_POL_ZAKRESU_HZ)
        pol_zakresu = STROJENIE_SKAN_MIN_POL_ZAKRESU_HZ;
    if (pol_zakresu > STROJENIE_SKAN_MAX_POL_ZAKRESU_HZ)
        pol_zakresu = STROJENIE_SKAN_MAX_POL_ZAKRESU_HZ;

    start_hz = cel_hz > pol_zakresu ? cel_hz - pol_zakresu : fmin;
    if (start_hz < fmin)
        start_hz = fmin;

    if ((uint64_t)cel_hz + (uint64_t)pol_zakresu > (uint64_t)fmax)
        koniec_hz = fmax;
    else
        koniec_hz = cel_hz + pol_zakresu;

    if (koniec_hz <= start_hz)
        return 0U;

    krok = (koniec_hz - start_hz) / (STROJENIE_SKAN_PUNKTY - 1U);
    if (krok == 0U)
        krok = 1U;

    memset(g_strojenie_skan_maska, 0, sizeof(g_strojenie_skan_maska));
    memset(g_strojenie_skan_z, 0, sizeof(g_strojenie_skan_z));

    memset(&ustawienia, 0, sizeof(ustawienia));
    ustawienia.tor = POMIAR_S11_TOR_STANDARD;
    {
        uint32_t liczba_usrednien = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
        /*
         * Asystent ma pozostać responsywny. Używamy tego samego ustawienia co
         * panorama, ale bronimy się przed uszkodzoną/starą konfiguracją, która
         * mogłaby zamienić pojedynczy skan w bardzo długą serię pomiarów.
         */
        if (liczba_usrednien < 1U)
            liczba_usrednien = 1U;
        if (liczba_usrednien > 20U)
            liczba_usrednien = 20U;
        ustawienia.liczba_usrednien = (uint8_t)liczba_usrednien;
    }
    ustawienia.korekcja_hw = true;
    ustawienia.korekcja_osl = true;
    ustawienia.kompensacja_portu = true;
    ustawienia.kompensacja_kabla = CFG_GetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY) != 0U;
    ustawienia.automatyczna_osl_pasmowa = true;
    ustawienia.zakres_pomiaru_od_hz = start_hz;
    ustawienia.zakres_pomiaru_do_hz = koniec_hz;

    UI_WyczyscEkran();
    UI_RysujNaglowek(Strojenie_T("Asystent strojenia", "Tuning assistant", "Abstimmassistent", "Помощник настройки"));
    UI_FormatujCzestotliwoscMHz(cel_hz, tekst, sizeof(tekst));
    UI_RysujPoleStatusu(18, 38, 444, 50,
                        Strojenie_T("Częstotliwość docelowa", "Target frequency", "Zielfrequenz", "Целевая частота"),
                        tekst, UI_STYL_AKCENT);
    UI_RysujPoleStatusu(18, 96, 444, 58,
                        Strojenie_T("Skanowanie", "Scanning", "Messlauf", "Сканирование"),
                        "0%", UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
               22, 176,
               Strojenie_T("Dotknij ekranu lub Wstecz, aby przerwać.", "Touch screen or Back to cancel.", "Bildschirm berühren oder Zurück zum Abbruch.", "Коснитесь экрана или Назад для отмены."));

    while (TOUCH_IsPressed())
        Sleep(10U);

    /* Pierwszy punkt stabilizuje tor po przejściu z pomiaru ciągłego. */
    {
        POMIAR_S11_t pomiar_wstepny;
        (void)POMIAR_S11_PobierzPunkt(start_hz, &ustawienia, &pomiar_wstepny);
    }

    for (i = 0U; i < STROJENIE_SKAN_PUNKTY; ++i)
    {
        POMIAR_S11_t pomiar;
        uint32_t f_hz = start_hz + i * krok;
        WEJSCIE_ZDARZENIE_t zdarzenie;

        if (i == STROJENIE_SKAN_PUNKTY - 1U)
            f_hz = koniec_hz;

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || TOUCH_IsPressed())
        {
            GEN_SetMeasurementFreq(0U);
            while (TOUCH_IsPressed()) Sleep(10U);
            return 0U;
        }

        if (POMIAR_S11_PobierzPunkt(f_hz, &ustawienia, &pomiar) &&
            isfinite(crealf(pomiar.impedancja_ohm)) && isfinite(cimagf(pomiar.impedancja_ohm)))
        {
            g_strojenie_skan_z[i] = pomiar.impedancja_ohm;
            g_strojenie_skan_maska[i] = 1U;
            poprawne++;
        }
        else
        {
            g_strojenie_skan_z[i] = NAN + NAN * I;
        }

        if ((i % 8U) == 0U || i == STROJENIE_SKAN_PUNKTY - 1U)
        {
            const unsigned procent = (unsigned)(((i + 1U) * 100U) / STROJENIE_SKAN_PUNKTY);
            snprintf(tekst, sizeof(tekst), "%u%%   %lu/%u   OK %lu",
                     procent, (unsigned long)(i + 1U), (unsigned)STROJENIE_SKAN_PUNKTY,
                     (unsigned long)poprawne);
            UI_RysujPoleStatusu(18, 96, 444, 58,
                                Strojenie_T("Skanowanie", "Scanning", "Messlauf", "Сканирование"),
                                tekst, UI_STYL_NORMALNY);
            LCD_FillRect(LCD_MakePoint(22, 160), LCD_MakePoint(458, 169), UI_KolorTlaPola());
            if (procent > 0U)
            {
                uint16_t szer = (uint16_t)((432U * procent) / 100U);
                if (szer > 432U) szer = 432U;
                LCD_FillRect(LCD_MakePoint(24, 162), LCD_MakePoint((uint16_t)(24U + szer), 167),
                             UI_KolorRamki(UI_STYL_AKCENT));
            }
        }
    }

    GEN_SetMeasurementFreq(0U);
    *f_start_hz = start_hz;
    *krok_hz = krok;
    *wynik = STROJENIE_ANTENA_Analizuj(g_strojenie_skan_z, g_strojenie_skan_maska,
                                       STROJENIE_SKAN_PUNKTY, start_hz, krok,
                                       (float)CFG_GetParam(CFG_PARAM_R0), cel_hz);
    return wynik->poprawny;
}

uint8_t MEASUREMENT_SkanujRezonansAntena(uint32_t czestotliwosc_docelowa_hz,
                                          STROJENIE_ANTENA_WYNIK_t *wynik)
{
    uint32_t f_start_hz = 0U;
    uint32_t krok_hz = 0U;

    if (wynik == NULL)
        return 0U;

    return Strojenie_ZbierzSkan(czestotliwosc_docelowa_hz,
                                &f_start_hz, &krok_hz, wynik);
}

static void Strojenie_RysujWynikSkanu(const STROJENIE_ANTENA_WYNIK_t *wynik)
{
    char tekst[112];
    char fbuf[32];
    UI_STYL_t styl_wskazowki = UI_STYL_AKCENT;
    UI_AKCJA_t akcje[3];

    if (wynik == NULL)
        return;

    UI_WyczyscEkran();
    UI_RysujNaglowek(Strojenie_T("Asystent strojenia", "Tuning assistant", "Abstimmassistent", "Помощник настройки"));

    if (wynik->punkt_docelowy_znaleziony)
    {
        UI_FormatujCzestotliwoscMHz(wynik->f_punkt_docelowy_hz, fbuf, sizeof(fbuf));
        snprintf(tekst, sizeof(tekst), "%s  SWR %.2f  R %.1f  X %+.1f Ohm",
                 fbuf, (double)wynik->swr_docelowy,
                 (double)wynik->r_docelowy_ohm, (double)wynik->x_docelowy_ohm);
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "--");
    }
    UI_RysujPoleStatusu(8, 32, 464, 40,
                        Strojenie_T("W punkcie docelowym", "At target", "Am Zielpunkt", "В целевой точке"),
                        tekst, UI_STYL_NORMALNY);

    if (wynik->rezonans_znaleziony)
    {
        UI_FormatujCzestotliwoscMHz(wynik->f_rezonans_hz, fbuf, sizeof(fbuf));
        snprintf(tekst, sizeof(tekst), "%s   delta %+.1f kHz   R %.1f Ohm",
                 fbuf, (double)wynik->odchylenie_rezonansu_hz / 1000.0,
                 (double)wynik->r_rezonans_ohm);
        UI_RysujPoleStatusu(8, 76, 464, 40,
                            Strojenie_T("Najbliższy rezonans X=0", "Nearest X=0 resonance", "Nächste X=0 Resonanz", "Ближайший резонанс X=0"),
                            tekst, UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(8, 76, 464, 40,
                            Strojenie_T("Najbliższy rezonans X=0", "Nearest X=0 resonance", "Nächste X=0 Resonanz", "Ближайший резонанс X=0"),
                            Strojenie_T("Brak w skanie - użyj szerszego zakresu.", "Not in scan - use a wider span.", "Nicht im Scan - größeren Bereich wählen.", "Нет в скане - увеличьте диапазон."),
                            UI_STYL_OSTRZEZENIE);
    }

    if (!wynik->rezonans_znaleziony)
    {
        snprintf(tekst, sizeof(tekst), "%s",
                 Strojenie_T("Nie zgaduję kierunku bez znalezionego rezonansu.", "No tuning direction without a detected resonance.", "Keine Richtung ohne gefundenen Resonanzpunkt.", "Без резонанса направление не определяется."));
        styl_wskazowki = UI_STYL_OSTRZEZENIE;
    }
    else if (wynik->kierunek == STROJENIE_ANTENA_KIERUNEK_SKROC)
    {
        snprintf(tekst, sizeof(tekst), "%s  (%.2f%%)",
                 Strojenie_T("Rezonans za nisko: skróć prosty element rezonansowy ~", "Resonance too low: shorten simple resonant element ~", "Resonanz zu tief: einfachen Strahler kürzen ~", "Резонанс ниже цели: укоротите элемент ~"),
                 (double)fabsf(wynik->korekta_dlugosci_procent));
        styl_wskazowki = UI_STYL_OSTRZEZENIE;
    }
    else if (wynik->kierunek == STROJENIE_ANTENA_KIERUNEK_WYDLUZ)
    {
        snprintf(tekst, sizeof(tekst), "%s  (%.2f%%)",
                 Strojenie_T("Rezonans za wysoko: wydłuż prosty element rezonansowy ~", "Resonance too high: lengthen simple resonant element ~", "Resonanz zu hoch: einfachen Strahler verlängern ~", "Резонанс выше цели: удлините элемент ~"),
                 (double)fabsf(wynik->korekta_dlugosci_procent));
        styl_wskazowki = UI_STYL_OSTRZEZENIE;
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "%s",
                 Strojenie_T("Rezonans jest blisko częstotliwości docelowej.", "Resonance is close to the target frequency.", "Resonanz liegt nahe der Zielfrequenz.", "Резонанс близок к целевой частоте."));
        styl_wskazowki = UI_STYL_AKTYWNY;
    }
    UI_RysujPoleStatusu(8, 120, 464, 44,
                        Strojenie_T("Wskazówka", "Guidance", "Hinweis", "Подсказка"),
                        tekst, styl_wskazowki);

    snprintf(tekst, sizeof(tekst), "%.2f @ %.6f MHz  R %.1f X %+.1f",
             (double)wynik->min_swr, (double)wynik->f_min_swr_hz / 1000000.0,
             (double)wynik->r_min_ohm, (double)wynik->x_min_ohm);
    UI_RysujPoleStatusu(8, 168, 228, 54, Strojenie_T("Minimum SWR", "Minimum SWR", "SWR-Minimum", "Минимум SWR"),
                        tekst, UI_STYL_AKCENT);

    if (wynik->pasmo_swr_20_znalezione)
        snprintf(tekst, sizeof(tekst), "%.0f kHz   %.3f-%.3f MHz",
                 (double)(wynik->f_swr_20_gora_hz - wynik->f_swr_20_dol_hz) / 1000.0,
                 (double)wynik->f_swr_20_dol_hz / 1000000.0,
                 (double)wynik->f_swr_20_gora_hz / 1000000.0);
    else
        snprintf(tekst, sizeof(tekst), "%s", Strojenie_T("poza skanem", "outside scan", "außerhalb Scan", "вне скана"));
    UI_RysujPoleStatusu(244, 168, 228, 54, "SWR < 2", tekst,
                        wynik->pasmo_swr_20_znalezione ? UI_STYL_AKTYWNY : UI_STYL_NIEAKTYWNY);

    akcje[0] = (UI_AKCJA_t){.id = 1, .tekst = JEZYK_Tekst(TEKST_WSTECZ), .styl = UI_STYL_POWROT, .aktywna = true};
    akcje[1] = (UI_AKCJA_t){.id = 2, .tekst = Strojenie_T("Ustaw f0", "Set f0", "f0 setzen", "Уст. f0"),
                             .styl = UI_STYL_AKCENT, .aktywna = wynik->rezonans_znaleziony != 0U};
    akcje[2] = (UI_AKCJA_t){.id = 3, .tekst = JEZYK_Tekst(TEKST_DOPASOWANIE_LC), .styl = UI_STYL_NORMALNY,
                             .aktywna = wynik->punkt_docelowy_znaleziony != 0U};
    UI_RysujPasekAkcji(230, 40, akcje, 3U);
}

/*
 * Zwraca 1, jeżeli użytkownik ustawił częstotliwość pracy na znaleziony rezonans.
 */
static uint8_t Strojenie_OtworzAnalize(void)
{
    STROJENIE_ANTENA_WYNIK_t wynik;
    uint32_t f_start_hz = 0U;
    uint32_t krok_hz = 0U;
    const uint32_t cel_hz = CFG_GetParam(CFG_PARAM_MEAS_F);
    uint8_t zmieniono_f = 0U;
    uint8_t koniec = 0U;

    UB_TIMER2_Stop();
    SWRTone = 0;

    if (!Strojenie_ZbierzSkan(cel_hz, &f_start_hz, &krok_hz, &wynik))
        return 0U;

    (void)f_start_hz;
    (void)krok_hz;
    while (!koniec)
    {
        UI_AKCJA_t akcje[3];
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        int16_t akcja = -1;

        Strojenie_RysujWynikSkanu(&wynik);
        LCD_ShowActiveLayerOnly();
        while (TOUCH_IsPressed()) Sleep(10U);

        for (;;)
        {
            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            {
                akcja = 1;
                break;
            }
            if (zdarzenie == WEJSCIE_ZDARZENIE_OK && wynik.rezonans_znaleziony)
            {
                akcja = 2;
                break;
            }
            if (TOUCH_Poll(&punkt))
            {
                akcje[0] = (UI_AKCJA_t){.id = 1, .tekst = JEZYK_Tekst(TEKST_WSTECZ), .styl = UI_STYL_POWROT, .aktywna = true};
                akcje[1] = (UI_AKCJA_t){.id = 2, .tekst = Strojenie_T("Ustaw f0", "Set f0", "f0 setzen", "Уст. f0"),
                                         .styl = UI_STYL_AKCENT, .aktywna = wynik.rezonans_znaleziony != 0U};
                akcje[2] = (UI_AKCJA_t){.id = 3, .tekst = JEZYK_Tekst(TEKST_DOPASOWANIE_LC), .styl = UI_STYL_NORMALNY,
                                         .aktywna = wynik.punkt_docelowy_znaleziony != 0U};
                akcja = UI_ZnajdzAkcjePaska(punkt, 230, 40, akcje, 3U);
                if (akcja >= 0)
                    break;
            }
            Sleep(10U);
        }

        while (TOUCH_IsPressed()) Sleep(10U);
        if (akcja == 1)
        {
            koniec = 1U;
        }
        else if (akcja == 2 && wynik.rezonans_znaleziony)
        {
            CFG_SetParam(CFG_PARAM_MEAS_F, wynik.f_rezonans_hz);
            /* Zapisujemy od razu: „Ustaw f0” jest jawną decyzją użytkownika. */
            CFG_Flush();
            fChanged = 1U;
            zmieniono_f = 1U;
            koniec = 1U;
        }
        else if (akcja == 3 && wynik.punkt_docelowy_znaleziony)
        {
            MATCH_OtworzAsystenta(wynik.r_docelowy_ohm + wynik.x_docelowy_ohm * I,
                                  wynik.czestotliwosc_docelowa_hz,
                                  (float)CFG_GetParam(CFG_PARAM_R0));
        }
    }

    GEN_SetMeasurementFreq(0U);
    return zmieniono_f;
}

static const struct HitRect TuneSWR_HitArr[] =
    {
        /* Wspólny dolny raster: Wstecz | Pasmo | F | Analiza | Dźwięk | Próg. */
        HITRECT(0,   220, 70, 45, MEASUREMENT_Exit),
        HITRECT(82,  220, 70, 45, Strojenie_WybierzPasmo),
        HITRECT(164, 220, 70, 45, Strojenie_UstawCzestotliwosc),
        HITRECT(246, 220, 70, 45, Strojenie_ZlecAnalize),
        HITRECT(328, 220, 70, 45, SWR_Mute),
        HITRECT(410, 220, 70, 45, SWR_Prog),
        HITEND};

static const uint32_t kroki_enkodera_hz[] = {1000U, 10000U, 100000U, 1000000U};

static void Strojenie_RysujKrokEnkodera(uint8_t indeks_kroku)
{
    char tekst[96];
    uint32_t krok = kroki_enkodera_hz[indeks_kroku];

    if (krok < 1000000U)
        snprintf(tekst, sizeof(tekst), "%s: %lu kHz   (%s)",
                 JEZYK_Tekst(TEKST_KROK_ENKODERA),
                 (unsigned long)(krok / 1000U),
                 JEZYK_Tekst(TEKST_OK_ZMIENIA_KROK));
    else
        snprintf(tekst, sizeof(tekst), "%s: %lu MHz   (%s)",
                 JEZYK_Tekst(TEKST_KROK_ENKODERA),
                 (unsigned long)(krok / 1000000U),
                 JEZYK_Tekst(TEKST_OK_ZMIENIA_KROK));

    LCD_FillRect(LCD_MakePoint(4, 188), LCD_MakePoint(475, 216), BackGrColor);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 8, 194, tekst);
}

static UI_STYL_t Strojenie_StylSWR(float swr)
{
    if (!isfinite(swr) || swr < 1.0f)
        return UI_STYL_OSTRZEZENIE;
    if (swr < 2.0f)
        return UI_STYL_AKTYWNY;
    if (swr < 3.0f)
        return UI_STYL_AKCENT;
    return UI_STYL_OSTRZEZENIE;
}

/*
 * Geometria ekranu strojenia jest skupiona w jednym miejscu. Dzięki temu
 * kolejne poprawki nie rozjeżdżają pól rysowanych statycznie i dynamicznie.
 */
#define STROJENIE_SWR_X 4U
#define STROJENIE_SWR_Y 40U
#define STROJENIE_SWR_W 176U
#define STROJENIE_SWR_H 146U
#define STROJENIE_FREQ_X 186U
#define STROJENIE_FREQ_Y 40U
#define STROJENIE_FREQ_W 290U
#define STROJENIE_FREQ_H 50U
#define STROJENIE_R_X 186U
#define STROJENIE_X_X 332U
#define STROJENIE_RX_Y 96U
#define STROJENIE_R_W 140U
#define STROJENIE_X_W 144U
#define STROJENIE_RX_H 90U
#define STROJENIE_PASEK_X 8U
#define STROJENIE_PASEK_Y 129U
#define STROJENIE_PASEK_W 168U
#define STROJENIE_PASEK_H 9U
#define STROJENIE_OKRES_ODSWIEZANIA_UI_MS 280U

typedef struct
{
    char swr[20];
    char czestotliwosc[32];
    char r[24];
    char x[24];
    char charakter[40];
    char najlepszy_linia1[40];
    char najlepszy_linia2[40];
    UI_STYL_t styl_swr;
    uint32_t szerokosc_paska;
    LCDColor kolor_paska;
    uint8_t wazny;
} STROJENIE_CACHE_t;

static STROJENIE_CACHE_t g_strojenie_cache;

static void Strojenie_ResetujCache(void)
{
    memset(&g_strojenie_cache, 0, sizeof(g_strojenie_cache));
    g_strojenie_cache.styl_swr = (UI_STYL_t)0xFF;
    g_strojenie_cache.szerokosc_paska = 0xFFFFFFFFU;
    g_strojenie_cache.kolor_paska = (LCDColor)0xFFFFFFFFU;
}

static void Strojenie_RysujTekstWycentrowany(uint16_t x, uint16_t y,
                                              uint16_t szerokosc, uint16_t wysokosc,
                                              FONTS font, LCDColor kolor,
                                              const char *tekst)
{
    const LCDColor tlo = UI_KolorTlaPola();
    int px;
    int py;
    int szerokosc_tekstu;
    int wysokosc_tekstu;

    if (tekst == 0)
        tekst = "";

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U),
                               (uint16_t)(y + wysokosc - 1U)), tlo);

    szerokosc_tekstu = FONT_GetStrPixelWidth(font, tekst);
    wysokosc_tekstu = FONT_GetHeight(font);
    px = (int)x + ((int)szerokosc - szerokosc_tekstu) / 2;
    py = (int)y + ((int)wysokosc - wysokosc_tekstu) / 2;

    if (px < (int)x)
        px = (int)x;
    if (py < (int)y)
        py = (int)y;

    FONT_Write(font, kolor, tlo, (uint16_t)px, (uint16_t)py, tekst);
}

static void Strojenie_RysujUkladStaly(void)
{
    /*
     * Pola są rysowane raz. Później zmieniają się tylko ich wnętrza. To usuwa
     * główne źródło migotania widoczne na rzeczywistym wyświetlaczu.
     */
    UI_RysujPoleWartosci(STROJENIE_SWR_X, STROJENIE_SWR_Y,
                         STROJENIE_SWR_W, STROJENIE_SWR_H, "SWR", 0);
    UI_RysujPoleWartosci(STROJENIE_FREQ_X, STROJENIE_FREQ_Y,
                         STROJENIE_FREQ_W, STROJENIE_FREQ_H,
                         JEZYK_Tekst(TEKST_CZESTOTLIWOSC), 0);
    UI_RysujPoleWartosci(STROJENIE_R_X, STROJENIE_RX_Y,
                         STROJENIE_R_W, STROJENIE_RX_H, "R [Ohm]", 0);
    UI_RysujPoleWartosci(STROJENIE_X_X, STROJENIE_RX_Y,
                         STROJENIE_X_W, STROJENIE_RX_H, "X [Ohm]", 0);

    LCD_FillRect(LCD_MakePoint(STROJENIE_PASEK_X, STROJENIE_PASEK_Y),
                 LCD_MakePoint((uint16_t)(STROJENIE_PASEK_X + STROJENIE_PASEK_W - 1U),
                               (uint16_t)(STROJENIE_PASEK_Y + STROJENIE_PASEK_H - 1U)),
                 UI_KolorTlaPrzycisku(UI_STYL_NIEAKTYWNY));
}

static void Strojenie_RysujPasekSWR(float swr)
{
    float ograniczony = swr;
    uint32_t szerokosc;
    LCDColor kolor = UI_KolorRamki(Strojenie_StylSWR(swr));

    if (!isfinite(ograniczony) || ograniczony < 1.0f)
        ograniczony = 5.0f;
    if (ograniczony > 5.0f)
        ograniczony = 5.0f;

    szerokosc = (uint32_t)(((5.0f - ograniczony) / 4.0f) * (float)STROJENIE_PASEK_W);
    if (szerokosc > STROJENIE_PASEK_W)
        szerokosc = STROJENIE_PASEK_W;

    if (g_strojenie_cache.wazny &&
        g_strojenie_cache.szerokosc_paska == szerokosc &&
        g_strojenie_cache.kolor_paska == kolor)
        return;

    LCD_FillRect(LCD_MakePoint(STROJENIE_PASEK_X, STROJENIE_PASEK_Y),
                 LCD_MakePoint((uint16_t)(STROJENIE_PASEK_X + STROJENIE_PASEK_W - 1U),
                               (uint16_t)(STROJENIE_PASEK_Y + STROJENIE_PASEK_H - 1U)),
                 UI_KolorTlaPrzycisku(UI_STYL_NIEAKTYWNY));
    if (szerokosc > 0U)
    {
        LCD_FillRect(LCD_MakePoint(STROJENIE_PASEK_X, STROJENIE_PASEK_Y),
                     LCD_MakePoint((uint16_t)(STROJENIE_PASEK_X + szerokosc - 1U),
                                   (uint16_t)(STROJENIE_PASEK_Y + STROJENIE_PASEK_H - 1U)),
                     kolor);
    }

    g_strojenie_cache.szerokosc_paska = szerokosc;
    g_strojenie_cache.kolor_paska = kolor;
}

static const char *Strojenie_KrotkiCharakter(float x, uint8_t pomiar_poprawny)
{
    const float prog_rezonansu = fmaxf(3.0f, 0.1f * (float)CFG_GetParam(CFG_PARAM_R0));

    if (!pomiar_poprawny)
        return JEZYK_Tekst(TEKST_BRAK_POMIARU_KROTKI);
    if (fabsf(x) <= prog_rezonansu)
        return JEZYK_Tekst(TEKST_BLISKO_REZONANSU);
    if (x > 0.0f)
        return JEZYK_Tekst(TEKST_INDUKCYJNY_KROTKI);
    return JEZYK_Tekst(TEKST_POJEMNOSCIOWY_KROTKI);
}

static void Strojenie_RysujWynik(DSP_RX z, float swr, float najlepszy_swr,
                                  uint32_t najlepsza_f, uint8_t pomiar_poprawny)
{
    char swr_txt[20];
    char czestotliwosc[32];
    char r_txt[24];
    char x_txt[24];
    char charakter_txt[40];
    char najlepszy_linia1[40];
    char najlepszy_linia2[40];
    float r = crealf(z);
    float x = cimagf(z);
    UI_STYL_t styl_swr = Strojenie_StylSWR(swr);
    const LCDColor tlo = UI_KolorTlaPola();

    if (pomiar_poprawny && isfinite(swr) && swr >= 1.0f && swr < 100.0f)
        snprintf(swr_txt, sizeof(swr_txt), "%.2f", swr);
    else
        snprintf(swr_txt, sizeof(swr_txt), "---");

    UI_FormatujCzestotliwoscMHz(CFG_GetParam(CFG_PARAM_MEAS_F),
                                czestotliwosc, sizeof(czestotliwosc));

    if (pomiar_poprawny)
    {
        snprintf(r_txt, sizeof(r_txt), "%.1f", r);
        snprintf(x_txt, sizeof(x_txt), "%+.1f", x);
    }
    else
    {
        snprintf(r_txt, sizeof(r_txt), "--");
        snprintf(x_txt, sizeof(x_txt), "--");
    }

    snprintf(charakter_txt, sizeof(charakter_txt), "%s",
             Strojenie_KrotkiCharakter(x, pomiar_poprawny));

    if (najlepsza_f != 0U)
    {
        char najlepsza_f_txt[32];
        UI_FormatujCzestotliwoscMHz(najlepsza_f, najlepsza_f_txt, sizeof(najlepsza_f_txt));
        snprintf(najlepszy_linia1, sizeof(najlepszy_linia1), "%s: %.2f",
                 JEZYK_Tekst(TEKST_NAJLEPSZY_WYNIK), najlepszy_swr);
        snprintf(najlepszy_linia2, sizeof(najlepszy_linia2), "%s", najlepsza_f_txt);
    }
    else
    {
        snprintf(najlepszy_linia1, sizeof(najlepszy_linia1), "%s: --",
                 JEZYK_Tekst(TEKST_NAJLEPSZY_WYNIK));
        najlepszy_linia2[0] = '\0';
    }

    /*
     * Najważniejszy wynik używa tego samego FONT_BDIGITS co częstotliwość
     * WSPR/FT8, ale jest ustawiony znacznie wyżej niż w poprzednim układzie.
     */
    if (!g_strojenie_cache.wazny || strcmp(g_strojenie_cache.swr, swr_txt) != 0)
    {
        Strojenie_RysujTekstWycentrowany(8, 66, 168, 58,
                                         FONT_BDIGITS, UI_KolorTekstu(UI_STYL_NORMALNY),
                                         swr_txt);
        snprintf(g_strojenie_cache.swr, sizeof(g_strojenie_cache.swr), "%s", swr_txt);
    }

    if (!g_strojenie_cache.wazny || g_strojenie_cache.styl_swr != styl_swr)
    {
        LCD_Rectangle(LCD_MakePoint(STROJENIE_SWR_X, STROJENIE_SWR_Y),
                      LCD_MakePoint((uint16_t)(STROJENIE_SWR_X + STROJENIE_SWR_W - 1U),
                                    (uint16_t)(STROJENIE_SWR_Y + STROJENIE_SWR_H - 1U)),
                      UI_KolorRamki(styl_swr));
        g_strojenie_cache.styl_swr = styl_swr;
    }

    if (!g_strojenie_cache.wazny || strcmp(g_strojenie_cache.czestotliwosc, czestotliwosc) != 0)
    {
        Strojenie_RysujTekstWycentrowany(192, 57, 278, 31,
                                         FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY),
                                         czestotliwosc);
        snprintf(g_strojenie_cache.czestotliwosc, sizeof(g_strojenie_cache.czestotliwosc), "%s", czestotliwosc);
    }

    if (!g_strojenie_cache.wazny || strcmp(g_strojenie_cache.r, r_txt) != 0)
    {
        Strojenie_RysujTekstWycentrowany(192, 116, 128, 34,
                                         FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY),
                                         r_txt);
        snprintf(g_strojenie_cache.r, sizeof(g_strojenie_cache.r), "%s", r_txt);
    }

    if (!g_strojenie_cache.wazny || strcmp(g_strojenie_cache.x, x_txt) != 0)
    {
        Strojenie_RysujTekstWycentrowany(338, 116, 132, 34,
                                         FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY),
                                         x_txt);
        snprintf(g_strojenie_cache.x, sizeof(g_strojenie_cache.x), "%s", x_txt);
    }

    if (!g_strojenie_cache.wazny || strcmp(g_strojenie_cache.charakter, charakter_txt) != 0)
    {
        LCD_FillRect(LCD_MakePoint(338, 157), LCD_MakePoint(470, 179), tlo);
        FONT_Write(FONT_FRAN,
                   pomiar_poprawny ? UI_KolorRamki(fabsf(x) <= fmaxf(3.0f, 0.1f * (float)CFG_GetParam(CFG_PARAM_R0))
                                                       ? UI_STYL_AKTYWNY : UI_STYL_AKCENT)
                                   : UI_KolorRamki(UI_STYL_OSTRZEZENIE),
                   tlo, 338, 160, charakter_txt);
        snprintf(g_strojenie_cache.charakter, sizeof(g_strojenie_cache.charakter), "%s", charakter_txt);
    }

    if (!g_strojenie_cache.wazny ||
        strcmp(g_strojenie_cache.najlepszy_linia1, najlepszy_linia1) != 0 ||
        strcmp(g_strojenie_cache.najlepszy_linia2, najlepszy_linia2) != 0)
    {
        LCD_FillRect(LCD_MakePoint(10, 144), LCD_MakePoint(174, 183), tlo);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKTYWNY), tlo,
                   10, 145, najlepszy_linia1);
        if (najlepszy_linia2[0] != '\0')
            FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
                       10, 164, najlepszy_linia2);

        snprintf(g_strojenie_cache.najlepszy_linia1,
                 sizeof(g_strojenie_cache.najlepszy_linia1), "%s", najlepszy_linia1);
        snprintf(g_strojenie_cache.najlepszy_linia2,
                 sizeof(g_strojenie_cache.najlepszy_linia2), "%s", najlepszy_linia2);
    }

    Strojenie_RysujPasekSWR(pomiar_poprawny ? swr : NAN);
    g_strojenie_cache.wazny = 1U;
}

static uint8_t Strojenie_CzyPomiarPoprawny(DSP_RX z, float swr)
{
    if (!meas_ostatni_pomiar_poprawny)
        return 0U;
    if (!isfinite(crealf(z)) || !isfinite(cimagf(z)))
        return 0U;
    if (!isfinite(swr) || swr < 1.0f)
        return 0U;
    return 1U;
}

static uint32_t Strojenie_WyznaczCzestotliwoscTonu(float swr)
{
    float wartosc = swr;
    float ton;

    if (!isfinite(wartosc) || wartosc < 1.0f)
        wartosc = 5.0f;
    if (wartosc > 10.0f)
        wartosc = 10.0f;

    /* Wyzszy dzwiek oznacza lepsze dopasowanie; zakres pozostaje przyjemny dla ucha. */
    ton = 1100.0f - ((wartosc - 1.0f) * 110.0f);
    if (ton < 180.0f)
        ton = 180.0f;
    if (ton > 1100.0f)
        ton = 1100.0f;
    return (uint32_t)ton;
}

void Tune_SWR_Proc(void)
{
    float vswrf = 1.0f;
    float vswrf_old = 0.0f;
    float najlepszy_swr = 9999.0f;
    uint32_t najlepsza_f = 0U;
    uint32_t SWRToneOld = 0U;
    uint32_t licznik_pomiaru = 0U;
    uint32_t ostatnie_odswiezenie_ui = 0U;
    uint8_t indeks_kroku = 1U; /* 10 kHz jest wygodnym stanem poczatkowym. */
    uint8_t zmieniono_czestotliwosc = 0U;
    uint8_t wymus_odswiezenie_ui = 1U;
    static LCDPoint pt;

    meas_maxstep = 500000;
    freqMHzf = CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.0f;
    SWRLimit = 1;
    g_strojenie_prosba_analizy = 0U;
    g_strojenie_prosba_odswiezenia_ukladu = 0U;
    ToneTrigger = 0;
    setup_GPIO();
    MeasRqExit = 0;
    /* Strojenie korzysta z tej samej, wybranej przez użytkownika palety wykresu. */
    SetColours();
    LCD_FillAll(BackGrColor);
    DrawHeader();

    while (TOUCH_IsPressed())
        ;

    muted = 0;
    SWRTone = 1;
    SWRToneOld = 0;
    Strojenie_RysujPrzyciski();
    Strojenie_ResetujCache();
    Strojenie_RysujUkladStaly();
    Strojenie_RysujKrokEnkodera(indeks_kroku);

    ToneFreq = 440;
    UB_TIMER2_Init_FRQ(ToneFreq);
    /* Pierwszy ton pojawi się dopiero po pierwszym poprawnym pomiarze. */
    UB_TIMER2_Stop();

    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(0); /* podtrzymuje obsluge automatycznego uspienia i wejsc */

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            FDecr(kroki_enkodera_hz[indeks_kroku]);
            zmieniono_czestotliwosc = 1U;
            wymus_odswiezenie_ui = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            FIncr(kroki_enkodera_hz[indeks_kroku]);
            zmieniono_czestotliwosc = 1U;
            wymus_odswiezenie_ui = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            indeks_kroku++;
            if (indeks_kroku >= (sizeof(kroki_enkodera_hz) / sizeof(kroki_enkodera_hz[0])))
                indeks_kroku = 0U;
            Strojenie_RysujKrokEnkodera(indeks_kroku);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            MeasRqExit = 1;
        }

        if ((SWRToneOld != SWRTone) || (ToneTrigger == 1))
        {
            SWRToneOld = SWRTone;
            ToneTrigger = 0;
            if (SWRTone == 0)
                UB_TIMER2_Stop();
        }

        if (TOUCH_Poll(&pt))
        {
            if (HitTest(TuneSWR_HitArr, pt.x, pt.y) == 1)
            {
                if (fChanged)
                {
                    zmieniono_czestotliwosc = 1U;
                    wymus_odswiezenie_ui = 1U;
                }
            }
        }

        if (g_strojenie_prosba_odswiezenia_ukladu)
        {
            g_strojenie_prosba_odswiezenia_ukladu = 0U;
            LCD_FillAll(BackGrColor);
            Strojenie_RysujPrzyciski();
            Strojenie_ResetujCache();
            Strojenie_RysujUkladStaly();
            Strojenie_RysujKrokEnkodera(indeks_kroku);
            freqMHzf = CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.0f;
            wymus_odswiezenie_ui = 1U;
            ostatnie_odswiezenie_ui = 0U;
        }

        if (g_strojenie_prosba_analizy)
        {
            g_strojenie_prosba_analizy = 0U;
            while (TOUCH_IsPressed()) Sleep(10U);
            if (Strojenie_OtworzAnalize())
                zmieniono_czestotliwosc = 1U;

            /* Powrót do tego samego ekranu pomiaru ciągłego, bez drugiej ścieżki UI. */
            LCD_FillAll(BackGrColor);
            DrawHeader();
            Strojenie_RysujPrzyciski();
            Strojenie_ResetujCache();
            Strojenie_RysujUkladStaly();
            Strojenie_RysujKrokEnkodera(indeks_kroku);
            freqMHzf = CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.0f;
            wymus_odswiezenie_ui = 1U;
            ostatnie_odswiezenie_ui = 0U;
            licznik_pomiaru = 5U; /* natychmiast odśwież wynik po zamknięciu analizy. */
            SWRTone = muted ? 0U : 1U;
            ToneTrigger = 1;
        }

        if (MeasRqExit == 1)
        {
            SWRTone = 0;
            MeasRqExit = 0;
            GEN_SetMeasurementFreq(0);
            UB_TIMER2_Stop();
            if (zmieniono_czestotliwosc || fChanged)
            {
                fChanged = 0;
                CFG_Flush();
            }
            return;
        }

        licznik_pomiaru++;
        if (licznik_pomiaru >= 5U)
        {
            DSP_RX z;
            float roznica;

            licznik_pomiaru = 0U;
            freqMHzf = CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.0f;
            Measurement_WykonajPomiar(CFG_GetParam(CFG_PARAM_MEAS_F), 1, 1,
                                        CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
            z = meas_ostatni_pomiar.impedancja_ohm;
            vswrf = DSP_CalcVSWR(z);

            {
                const uint8_t pomiar_poprawny = Strojenie_CzyPomiarPoprawny(z, vswrf);

                if (pomiar_poprawny && vswrf < najlepszy_swr)
                {
                    najlepszy_swr = vswrf;
                    najlepsza_f = CFG_GetParam(CFG_PARAM_MEAS_F);
                }

                {
                    const uint32_t teraz = HAL_GetTick();
                    if (wymus_odswiezenie_ui || ostatnie_odswiezenie_ui == 0U ||
                        (teraz - ostatnie_odswiezenie_ui) >= STROJENIE_OKRES_ODSWIEZANIA_UI_MS)
                    {
                        Strojenie_RysujWynik(z, vswrf, najlepszy_swr, najlepsza_f, pomiar_poprawny);
                        ostatnie_odswiezenie_ui = teraz;
                        wymus_odswiezenie_ui = 0U;
                    }
                }

                if (!pomiar_poprawny)
                {
                    /*
                     * Brak wiarygodnego SWR nie może być kodowany dźwiękiem.
                     * Zatrzymujemy brzęczyk i wymuszamy pełne odświeżenie
                     * tonu, gdy poprawny pomiar pojawi się ponownie.
                     */
                    SWRTone = 0;
                    ToneTrigger = 1;
                    vswrf_old = 0.0f;
                    UB_TIMER2_Stop();
                }
                else
                {
                    roznica = fabsf(vswrf_old - vswrf);
                    if (roznica > 0.01f * vswrf || vswrf_old == 0.0f)
                    {
                        ToneTrigger = 1;
                        vswrf_old = vswrf;
                        if (muted == 0)
                        {
                            SWRTone = 1;
                            if ((SWRLimit == 2 && vswrf > 2.0f) ||
                                (SWRLimit == 3 && vswrf > 3.0f))
                            {
                                SWRTone = 0;
                            }
                        }
                    }

                    ToneFreq = Strojenie_WyznaczCzestotliwoscTonu(vswrf);
                    if ((SWRTone == 1) && (muted == 0))
                    {
                        UB_TIMER2_Init_FRQ(ToneFreq);
                        UB_TIMER2_Start();
                    }
                    else
                    {
                        UB_TIMER2_Stop();
                    }
                }
            }
        }

        Sleep(10);
    }
}
