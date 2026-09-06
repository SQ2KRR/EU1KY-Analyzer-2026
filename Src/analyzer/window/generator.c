/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   Modified by KD8CEC and EU1KY-PL 2026.
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "LCD.h"
#include "config.h"
#include "dsp.h"
#include "font.h"
#include "gen.h"
#include "generator.h"
#include "gpio_control.h"
#include "jezyk.h"
#include "main.h"
#include "mainwnd.h"
#include "centrum_kalibracji.h"
#include "num_keypad.h"
#include "panfreq.h"
#include "si5351.h"
#include "si5351_hs.h"
#include "touch.h"
#include "ui_edytor_liczby.h"
#include "ui_wspolny.h"
#include "wejscia_uzytkownika.h"

extern uint32_t BackGrColor;
extern uint32_t TextColor;
extern void Sleep(uint32_t ms);

/*
 * Wartosci sa historycznym przyblizeniem dla ok. 2 MHz. Nie wolno ich
 * traktowac jako pomiaru mocy. Ekran zawsze opisuje je jako orientacyjne.
 */
uint32_t S2powerDBm[4] = {5U, 10U, 12U, 14U};

typedef enum
{
    GENERATOR_WIDOK_CZESTOTLIWOSC = 0,
    GENERATOR_WIDOK_WYJSCIE,
    GENERATOR_WIDOK_DIAGNOSTYKA
} GENERATOR_WIDOK_t;

static GENERATOR_WIDOK_t g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
static uint32_t g_krok_enkodera_hz = 10000U;
static uint8_t g_modulacja_aktywna = 0U;
static uint32_t g_fm_dewiacja_hz = 500U;
static uint32_t g_ostatnia_diagnostyka_ms = 0U;
static uint8_t g_wyjscie_strona = 0U;
static uint8_t g_wyjscie_fokus = 0U;
static uint8_t g_wyjscie_fokus_widoczny = 0U;

static uint32_t GENERATOR_MaksCzestotliwosc(void)
{
    const uint32_t maksimum_pasma = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint32_t maksimum_generatora = GEN_MaksCzestotliwoscEfektywna();
    return maksimum_pasma < maksimum_generatora ? maksimum_pasma : maksimum_generatora;
}

static uint32_t GENERATOR_MinCzestotliwosc(void)
{
    return CFG_GetParam(CFG_PARAM_BAND_FMIN);
}

static uint8_t GENERATOR_WyznaczHarmoniczna(uint32_t cel_hz, uint32_t *clk2_hz)
{
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(cel_hz);
    if (clk2_hz != 0)
        *clk2_hz = harmoniczna == 0U ? 0U : GEN_CzestotliwoscBazowa(cel_hz);
    return harmoniczna;
}

static uint32_t GENERATOR_DriveMA(void)
{
    static const uint8_t prady_ma[4] = {2U, 4U, 6U, 8U};
    uint8_t indeks = CLK2_drive;
    if (indeks > 3U)
        indeks = 3U;
    return prady_ma[indeks];
}

static void GENERATOR_WylaczRF(void)
{
    /*
     * Generator uzytkowy ma jednoznacznie korzystac z CLK2 / GEN OUT.
     * Tor pomiarowy CLK0+CLK1 jest gaszony osobno, aby nie mieszac roli
     * analizatora z rola zrodla sygnalu.
     */
    GEN_WylaczClk2();
    GEN_WylaczTorPomiarowy();
    SET_LED_STANU_RF(0U);
}

static void GENERATOR_UstawRF(void)
{
    const uint32_t cel_hz = CFG_GetParam(CFG_PARAM_GEN_F);

    if (!si5351_IsPresent())
    {
        GEN_WylaczTorPomiarowy();
        GEN_WylaczClk2();
        SET_LED_STANU_RF(0U);
        return;
    }

    /*
     * W normalnym widoku nie uruchamiamy F0 ani LO. Dzieki temu nowe gniazdo
     * GEN OUT moze byc fizycznie podlaczone do CLK2 bez niepotrzebnej emisji
     * z toru pomiarowego. CLK0+CLK1 wlaczamy tylko na czas diagnostyki.
     */
    GEN_WylaczTorPomiarowy();
    GEN_SetClk2Freq(cel_hz);
    HS_SetPower(HS_CLK2, CLK2_drive, HS_TRUE);
    SET_LED_STANU_RF(1U);
}

static void GENERATOR_ZapiszCzestotliwosc(uint32_t czestotliwosc_hz)
{
    const uint32_t minimum = GENERATOR_MinCzestotliwosc();
    const uint32_t maksimum = GENERATOR_MaksCzestotliwosc();

    if (czestotliwosc_hz < minimum)
        czestotliwosc_hz = minimum;
    if (czestotliwosc_hz > maksimum)
        czestotliwosc_hz = maksimum;

    CFG_SetParam(CFG_PARAM_GEN_F, czestotliwosc_hz);
    CFG_Flush();
    GENERATOR_UstawRF();
}

void FDecrG(uint32_t krok_hz)
{
    uint32_t czestotliwosc = CFG_GetParam(CFG_PARAM_GEN_F);
    const uint32_t minimum = GENERATOR_MinCzestotliwosc();

    if (czestotliwosc <= minimum || krok_hz >= czestotliwosc - minimum)
        czestotliwosc = minimum;
    else
        czestotliwosc -= krok_hz;

    GENERATOR_ZapiszCzestotliwosc(czestotliwosc);
}

void FIncrG(uint32_t krok_hz)
{
    uint32_t czestotliwosc = CFG_GetParam(CFG_PARAM_GEN_F);
    const uint32_t maksimum = GENERATOR_MaksCzestotliwosc();

    if (czestotliwosc >= maksimum || krok_hz >= maksimum - czestotliwosc)
        czestotliwosc = maksimum;
    else
        czestotliwosc += krok_hz;

    GENERATOR_ZapiszCzestotliwosc(czestotliwosc);
}

static void GENERATOR_FormatujLiczbeMHz(uint32_t hz, char *bufor, size_t rozmiar)
{
    char tekst[24];
    char *jednostka;

    if (bufor == 0 || rozmiar == 0U)
        return;

    UI_FormatujCzestotliwoscMHz(hz, tekst, sizeof(tekst));
    jednostka = strstr(tekst, " MHz");
    if (jednostka != 0)
        *jednostka = '\0';

    strncpy(bufor, tekst, rozmiar - 1U);
    bufor[rozmiar - 1U] = '\0';
}

static void GENERATOR_RysujPrzyciskDolny(uint8_t pozycja, const char *tekst,
                                            UI_STYL_t styl, uint8_t zaznaczony)
{
    const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego(pozycja);
    UI_RysujPrzyciskZaznaczony(o.x, o.y, o.szerokosc, o.wysokosc,
                               tekst, styl, FONT_FRAN, zaznaczony);
}

static void GENERATOR_RysujPrzyciski(void)
{
    char numer[8];

    UI_RysujWsteczDolny(false);
    if (g_widok == GENERATOR_WIDOK_WYJSCIE)
    {
        snprintf(numer, sizeof(numer), "%u/2", (unsigned)(g_wyjscie_strona + 1U));
        GENERATOR_RysujPrzyciskDolny(1U, "<",
                                     g_wyjscie_strona > 0U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, 0U);
        GENERATOR_RysujPrzyciskDolny(2U, numer, UI_STYL_NIEAKTYWNY, 0U);
        GENERATOR_RysujPrzyciskDolny(3U, ">",
                                     g_wyjscie_strona < 1U ? UI_STYL_NORMALNY : UI_STYL_NIEAKTYWNY, 0U);
        GENERATOR_RysujPrzyciskDolny(4U, "F", UI_STYL_NORMALNY, 0U);
        GENERATOR_RysujPrzyciskDolny(5U, "Diag.", UI_STYL_NORMALNY, 0U);
        return;
    }

    if (g_widok == GENERATOR_WIDOK_DIAGNOSTYKA)
    {
        GENERATOR_RysujPrzyciskDolny(1U, "Pasmo", UI_STYL_NORMALNY, 0U);
        GENERATOR_RysujPrzyciskDolny(2U, "F", UI_STYL_NORMALNY, 0U);
        GENERATOR_RysujPrzyciskDolny(3U, "Wyjście", UI_STYL_NORMALNY, 0U);
        GENERATOR_RysujPrzyciskDolny(4U, JEZYK_Wybierz("Kalibr.", "Calib.", "Kalibr.", "Калибр."), UI_STYL_NORMALNY, 0U);
        return;
    }

    GENERATOR_RysujPrzyciskDolny(1U, "Pasmo", UI_STYL_NORMALNY, 0U);
    GENERATOR_RysujPrzyciskDolny(2U, "F", UI_STYL_NORMALNY, 0U);
    GENERATOR_RysujPrzyciskDolny(3U, "Presety", UI_STYL_NORMALNY, 0U);
    GENERATOR_RysujPrzyciskDolny(4U, "Wyjście", UI_STYL_NORMALNY, 0U);
    GENERATOR_RysujPrzyciskDolny(5U, "Diag.", UI_STYL_NORMALNY, 0U);
}


static void GENERATOR_RysujCzestotliwosc(void)
{
    char czestotliwosc[24];
    char zakres[56];
    char krok[24];

    UI_RysujNaglowek(JEZYK_Tekst(TEKST_GENERATOR_CZESTOTLIWOSC));
    GENERATOR_FormatujLiczbeMHz(CFG_GetParam(CFG_PARAM_GEN_F),
                                czestotliwosc, sizeof(czestotliwosc));
    UI_RysujPoleLiczboweGlowne(8U, 40U, 464U, 82U,
                               JEZYK_Tekst(TEKST_CZESTOTLIWOSC),
                               czestotliwosc, "MHz");

    if (g_krok_enkodera_hz >= 1000U)
        snprintf(krok, sizeof(krok), "%lu kHz", (unsigned long)(g_krok_enkodera_hz / 1000U));
    else
        snprintf(krok, sizeof(krok), "%lu Hz", (unsigned long)g_krok_enkodera_hz);
    UI_RysujPoleInformacyjne(8U, 132U, 228U, 58U,
                             JEZYK_Tekst(TEKST_GENERATOR_KROK_STROJENIA), krok);

    snprintf(zakres, sizeof(zakres), "%.3f ... %.3f MHz",
             (double)GENERATOR_MinCzestotliwosc() / 1000000.0,
             (double)GENERATOR_MaksCzestotliwosc() / 1000000.0);
    UI_RysujPoleInformacyjne(244U, 132U, 228U, 58U,
                             JEZYK_Wybierz("Zakres", "Range", "Bereich", "Диапазон"), zakres);

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
               12U, 199U,
               JEZYK_Wybierz("Enkoder stroi wybranym krokiem. Pasmo i F są wspólne z pomiarami.",
                             "Encoder tunes by the selected step. Band and F are shared with measurements.",
                             "Der Encoder stimmt mit dem gewählten Schritt. Band und F sind gemeinsam.",
                             "Энкодер настраивает выбранным шагом. Диапазон и F общие."));
}

static void GENERATOR_RysujWyjscie(void)
{
    static const char *const etykiety[11] = {
        "2 mA", "4 mA", "6 mA", "8 mA", "Nośna", "AM",
        "FM", "df 0.5 kHz", "df 1 kHz", "df 2.5 kHz", "df 5 kHz"};
    static const UI_IKONA_MENU_t ikony[11] = {
        UI_IKONA_GENERATOR, UI_IKONA_GENERATOR, UI_IKONA_GENERATOR, UI_IKONA_GENERATOR,
        UI_IKONA_GENERATOR, UI_IKONA_GENERATOR, UI_IKONA_GENERATOR,
        UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA};
    const uint8_t poczatek = (uint8_t)(g_wyjscie_strona * UI_SIATKA_KOMPAKT_NA_STRONE);
    uint8_t koniec = (uint8_t)(poczatek + UI_SIATKA_KOMPAKT_NA_STRONE);
    uint8_t i;
    char tytul[48];

    if (koniec > 11U)
        koniec = 11U;
    snprintf(tytul, sizeof(tytul), "%s %u/2",
             JEZYK_Tekst(TEKST_GENERATOR_WYJSCIE),
             (unsigned)(g_wyjscie_strona + 1U));
    UI_RysujPasekGorny(tytul, false, false, 0);

    for (i = poczatek; i < koniec; ++i)
    {
        UI_RysujKafelKompaktowy((uint16_t)(i - poczatek), ikony[i], etykiety[i],
                                 g_wyjscie_fokus_widoczny && i == g_wyjscie_fokus,
                                 true);
    }
}

static void GENERATOR_RysujDiagnostykeStala(void)
{
    char opis[96];
    uint32_t clk2_hz;
    const uint8_t harmoniczna = GENERATOR_WyznaczHarmoniczna(CFG_GetParam(CFG_PARAM_GEN_F), &clk2_hz);

    UI_RysujNaglowek(JEZYK_Tekst(TEKST_GENERATOR_DIAGNOSTYKA));
    UI_RysujPanel(6, 36, 468, 62, JEZYK_Tekst(TEKST_GENERATOR_STAN), UI_STYL_NORMALNY);
    snprintf(opis, sizeof(opis), "Si5351: %s   I2C 0x%02X   XTAL %.3f MHz",
             si5351_IsPresent() ? "OK" : "--",
             (unsigned)(si5351_GetBusAddress() >> 1),
             (double)CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ) / 1000000.0);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 16, 58, opis);
    snprintf(opis, sizeof(opis), "%s: %u. %s; CLK2 %.6f MHz; drive %lu mA",
             JEZYK_Wybierz("Tryb", "Mode", "Modus", "Режим"),
             (unsigned)harmoniczna,
             JEZYK_Tekst(TEKST_GENERATOR_HARMONICZNA),
             (double)clk2_hz / 1000000.0,
             (unsigned long)GENERATOR_DriveMA());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(), 16, 78, opis);

    UI_RysujPanel(6, 104, 468, 92, JEZYK_Tekst(TEKST_DIAGNOSTYKA_TORU_RF), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               16, 126, "I -- mV   V -- mV   V/I -- dB");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               16, 148, "Faza -- deg   R -- Ohm   X -- Ohm");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               16, 170, JEZYK_Tekst(TEKST_GENERATOR_SPRAWDZ));

    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), UI_KolorTlaEkranu(),
               10, 204, JEZYK_Tekst(TEKST_GENERATOR_POZIOM_NIESKALIBROWANY));
    g_ostatnia_diagnostyka_ms = 0U;
}

static void GENERATOR_OdswiezDiagnostyke(uint8_t wymus)
{
    char linia[112];
    DSP_RX impedancja_surowa;
    DSP_RX impedancja_osl;
    uint32_t teraz = HAL_GetTick();
    LCDColor tlo = UI_KolorTlaPola();
    int stan_sygnalu;

    if (!wymus && (teraz - g_ostatnia_diagnostyka_ms) < 800U)
        return;
    g_ostatnia_diagnostyka_ms = teraz;

    /*
     * Najpierw mierzymy bez OSL. Dopiero gdy tor rzeczywiscie ma sygnal,
     * wykonujemy drugi pomiar z korekcja. Chroni to uzytkownika przed
     * pozornie wiarygodnym R/X policzonym z samego szumu.
     *
     * Normalny Generator pracuje tylko na CLK2. Na czas tej diagnostyki
     * wlaczamy F0+LO, wykonujemy pomiar i natychmiast je gasimy. GEN OUT
     * pozostaje aktywne i nie zmienia czestotliwosci.
     */
    GEN_SetMeasurementFreq(CFG_GetParam(CFG_PARAM_GEN_F));
    DSP_Measure(0, 1, 0, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
    stan_sygnalu = DSP_MeasuredMagVmv() > 0.3f ? 1 : 0;
    impedancja_surowa = DSP_MeasuredZ();

    LCD_FillRect(LCD_MakePoint(12, 124), LCD_MakePoint(468, 193), tlo);
    snprintf(linia, sizeof(linia), "I %.3f mV   V %.3f mV   V/I %.2f dB",
             (double)DSP_MeasuredMagImv(),
             (double)DSP_MeasuredMagVmv(),
             (double)DSP_MeasuredDiffdB());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 16, 126, linia);

    snprintf(linia, sizeof(linia), "Faza %.2f deg   R %.2f Ohm   X %.2f Ohm",
             (double)DSP_MeasuredPhaseDeg(),
             (double)crealf(impedancja_surowa),
             (double)cimagf(impedancja_surowa));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo, 16, 145, linia);

    if (stan_sygnalu == 1)
    {
        DSP_Measure(0, 1, 1, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
        impedancja_osl = DSP_MeasuredZ();

        snprintf(linia, sizeof(linia), "%s   OSL: R %.2f   X %.2f",
                 JEZYK_Tekst(TEKST_SYGNAL_OK),
                 (double)crealf(impedancja_osl),
                 (double)cimagf(impedancja_osl));
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKTYWNY), tlo, 16, 164, linia);

        snprintf(linia, sizeof(linia), "%s S2: ~%lu dBm",
                 JEZYK_Tekst(TEKST_MOC_WYJSCIOWA),
                 (unsigned long)S2powerDBm[CLK2_drive <= 3U ? CLK2_drive : 3U]);
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo, 16, 183, linia);
    }
    else
    {
        FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_OSTRZEZENIE), tlo,
                   16, 162, JEZYK_Tekst(TEKST_BRAK_SYGNALU_KROTKI));
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), tlo, 150, 164,
                   si5351_IsPresent()
                       ? JEZYK_Tekst(TEKST_BRAK_SYGNALU_SPRAWDZ_KWARC)
                       : JEZYK_Tekst(TEKST_BRAK_SYGNALU_SPRAWDZ_I2C));
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo,
                   16, 183, JEZYK_Tekst(TEKST_BRAK_SYGNALU_DIAGNOSTYKA));
    }

    /* Po diagnostyce wracamy do czystego GEN OUT na CLK2. */
    GEN_WylaczTorPomiarowy();
}

static void GENERATOR_RysujWidok(void)
{
    UI_WyczyscEkran();
    BackGrColor = UI_KolorTlaEkranu();
    TextColor = UI_KolorTekstu(UI_STYL_NORMALNY);

    switch (g_widok)
    {
    case GENERATOR_WIDOK_WYJSCIE:
        GENERATOR_RysujWyjscie();
        break;
    case GENERATOR_WIDOK_DIAGNOSTYKA:
        GENERATOR_RysujDiagnostykeStala();
        GENERATOR_OdswiezDiagnostyke(1U);
        break;
    case GENERATOR_WIDOK_CZESTOTLIWOSC:
    default:
        GENERATOR_RysujCzestotliwosc();
        break;
    }

    GENERATOR_RysujPrzyciski();
}

static uint8_t GENERATOR_CzyPrzerwacModulacje(void)
{
    LCDPoint punkt;
    WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

    if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ ||
        zdarzenie == WEJSCIE_ZDARZENIE_OK ||
        zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        return 1U;

    if (TOUCH_Poll(&punkt))
    {
        while (TOUCH_IsPressed())
            ;
        return 1U;
    }
    return 0U;
}

void GENERATOR_AM(void)
{
    const uint32_t czestotliwosc = CFG_GetParam(CFG_PARAM_GEN_F);
    uint32_t faza = 0U;

    g_modulacja_aktywna = 1U;
    GENERATOR_RysujWidok();
    TOUCH_CzekajNaPuszczenie(35U);
    WEJSCIA_WyczyscZdarzenia();
    while (!GENERATOR_CzyPrzerwacModulacje())
    {
        /* Prosta modulacja OOK do celow testowych, zachowana z galezi CEC. */
        if ((faza++ & 1U) == 0U)
            GENERATOR_UstawRF();
        else
            GENERATOR_WylaczRF();
        Sleep(2);
    }

    (void)czestotliwosc;
    g_modulacja_aktywna = 0U;
    GENERATOR_UstawRF();
    GENERATOR_RysujWidok();
}

void GENERATOR_FM(void)
{
    const uint32_t czestotliwosc = CFG_GetParam(CFG_PARAM_GEN_F);
    uint32_t faza = 0U;

    g_modulacja_aktywna = 2U;
    GENERATOR_RysujWidok();
    TOUCH_CzekajNaPuszczenie(35U);
    WEJSCIA_WyczyscZdarzenia();
    while (!GENERATOR_CzyPrzerwacModulacje())
    {
        uint32_t ustawiona;
        const uint32_t minimum = GENERATOR_MinCzestotliwosc();
        const uint32_t maksimum = GENERATOR_MaksCzestotliwosc();

        if ((faza++ & 1U) == 0U)
        {
            /* Porownujemy odleglosc od granicy, zamiast dodawac do minimum.
             * Chroni to przed przepelnieniem uint32_t przy nietypowej konfiguracji. */
            if (czestotliwosc <= minimum || (czestotliwosc - minimum) <= g_fm_dewiacja_hz)
                ustawiona = minimum;
            else
                ustawiona = czestotliwosc - g_fm_dewiacja_hz;
        }
        else
        {
            /* Analogicznie dla gornej granicy nie odejmujemy wybranej dewiacji od maksimum
             * dopoki nie wiemy, ze taka roznica jest poprawna. */
            if (czestotliwosc >= maksimum || (maksimum - czestotliwosc) <= g_fm_dewiacja_hz)
                ustawiona = maksimum;
            else
                ustawiona = czestotliwosc + g_fm_dewiacja_hz;
        }

        GEN_SetClk2Freq(ustawiona);
        Sleep(2);
    }

    g_modulacja_aktywna = 0U;
    GENERATOR_UstawRF();
    GENERATOR_RysujWidok();
}

static void GENERATOR_WpiszCzestotliwosc(void)
{
    uint32_t nowa_hz = CFG_GetParam(CFG_PARAM_GEN_F);

    if (UI_EdytujCzestotliwoscHzEx(nowa_hz,
                                    GENERATOR_MinCzestotliwosc(),
                                    GENERATOR_MaksCzestotliwosc(),
                                    1000U,
                                    JEZYK_Tekst(TEKST_GENERATOR_WPISZ_CZESTOTLIWOSC),
                                    &nowa_hz))
        GENERATOR_ZapiszCzestotliwosc(nowa_hz);
    GENERATOR_RysujWidok();
}

static int16_t GENERATOR_WybierzKafelki(const char *tytul,
                                        const char *const *etykiety,
                                        const UI_IKONA_MENU_t *ikony,
                                        uint8_t liczba,
                                        uint8_t zaznaczony)
{
    uint8_t fokus_widoczny = 0U;

    if (etykiety == 0 || liczba == 0U || liczba > UI_SIATKA_KOMPAKT_NA_STRONE)
        return -1;
    if (zaznaczony >= liczba)
        zaznaczony = 0U;

    for (;;)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie;
        uint8_t i;

        UI_WyczyscEkran();
        UI_RysujPasekGorny(tytul, false, false, 0);
        for (i = 0U; i < liczba; ++i)
        {
            UI_RysujKafelKompaktowy(i,
                                     ikony != 0 ? ikony[i] : UI_IKONA_GENERATOR,
                                     etykiety[i],
                                     fokus_widoczny && i == zaznaczony,
                                     true);
        }
        UI_RysujWsteczDolny(false);
        TOUCH_CzekajNaPuszczenie(30U);
        WEJSCIA_WyczyscZdarzenia();

        for (;;)
        {
            if (TOUCH_Poll(&punkt))
            {
                const int16_t indeks = UI_KafelKompaktowyPoDotyku(punkt, liczba);
                if (UI_CzyDotknietoWstecz(punkt))
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return -1;
                }
                if (indeks >= 0)
                {
                    TOUCH_CzekajNaPuszczenie(30U);
                    return indeks;
                }
            }

            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                return -1;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            {
                fokus_widoczny = 1U;
                if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                    zaznaczony = (uint8_t)((zaznaczony + 1U) % liczba);
                else
                    zaznaczony = zaznaczony == 0U ? (uint8_t)(liczba - 1U) : (uint8_t)(zaznaczony - 1U);
                break;
            }
            if ((zdarzenie == WEJSCIE_ZDARZENIE_OK ||
                 zdarzenie == WEJSCIE_ZDARZENIE_START_STOP) && fokus_widoczny)
                return (int16_t)zaznaczony;
            Sleep(10U);
        }
    }
}

static void GENERATOR_WybierzKrok(void)
{
    static const char *const etykiety[4] = {"100 Hz", "1 kHz", "10 kHz", "100 kHz"};
    static const UI_IKONA_MENU_t ikony[4] = {
        UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA, UI_IKONA_USTAWIENIA};
    static const uint32_t kroki_hz[4] = {100U, 1000U, 10000U, 100000U};
    uint8_t zaznaczony = 0U;
    uint8_t i;
    int16_t wybor;

    for (i = 0U; i < 4U; ++i)
        if (g_krok_enkodera_hz == kroki_hz[i])
            zaznaczony = i;

    wybor = GENERATOR_WybierzKafelki(JEZYK_Tekst(TEKST_GENERATOR_KROK_STROJENIA),
                                     etykiety, ikony, 4U, zaznaczony);
    if (wybor >= 0)
        g_krok_enkodera_hz = kroki_hz[(uint8_t)wybor];
}

static void GENERATOR_WybierzPresety(void)
{
    static const char *const etykiety[6] = {
        "7.100 MHz", "14.200 MHz", "28.500 MHz",
        "145.500 MHz", "433.500 MHz", "Krok"};
    static const UI_IKONA_MENU_t ikony[6] = {
        UI_IKONA_GENERATOR, UI_IKONA_GENERATOR, UI_IKONA_GENERATOR,
        UI_IKONA_GENERATOR, UI_IKONA_GENERATOR, UI_IKONA_USTAWIENIA};
    int16_t wybor = GENERATOR_WybierzKafelki(
        JEZYK_Tekst(TEKST_GENERATOR_GOTOWE_MHZ), etykiety, ikony, 6U, 0U);

    if (wybor >= 0 && wybor < 5)
        GENERATOR_ZapiszCzestotliwosc((uint32_t[]){7100000U, 14200000U, 28500000U, 145500000U, 433500000U}[(uint8_t)wybor]);
    else if (wybor == 5)
        GENERATOR_WybierzKrok();

    g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
    GENERATOR_RysujWidok();
}

static void GENERATOR_WybierzPasmo(void)
{
    uint32_t nowa_hz = CFG_GetParam(CFG_PARAM_GEN_F);
    if (PanFreq_WybierzPasmoCzestotliwosciEx(&nowa_hz,
                                              GENERATOR_MinCzestotliwosc(),
                                              GENERATOR_MaksCzestotliwosc()))
        GENERATOR_ZapiszCzestotliwosc(nowa_hz);
    g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
    GENERATOR_RysujWidok();
}

static void GENERATOR_UstawDrive(uint8_t indeks)
{
    if (indeks > 3U)
        return;

    CLK2_drive = indeks;
    if (si5351_IsPresent())
        HS_SetPower(HS_CLK2, CLK2_drive, HS_TRUE);
    GENERATOR_RysujWidok();
}

static uint8_t GENERATOR_ObsluzDotykCzestotliwosc(LCDPoint punkt)
{
    if (punkt.x >= 8U && punkt.x < 472U && punkt.y >= 40U && punkt.y < 122U)
    {
        GENERATOR_WpiszCzestotliwosc();
        return 1U;
    }
    return 0U;
}

static void GENERATOR_WykonajOpcjeWyjscia(uint8_t indeks)
{
    static const uint32_t dewiacje_hz[4] = {500U, 1000U, 2500U, 5000U};

    if (indeks < 4U)
    {
        GENERATOR_UstawDrive(indeks);
        return;
    }
    if (indeks == 4U)
    {
        g_modulacja_aktywna = 0U;
        GENERATOR_UstawRF();
        GENERATOR_RysujWidok();
        return;
    }
    if (indeks == 5U)
    {
        GENERATOR_AM();
        return;
    }
    if (indeks == 6U)
    {
        GENERATOR_FM();
        return;
    }
    if (indeks >= 7U && indeks <= 10U)
    {
        g_fm_dewiacja_hz = dewiacje_hz[indeks - 7U];
        GENERATOR_RysujWidok();
    }
}

static uint8_t GENERATOR_ObsluzDotykWyjscie(LCDPoint punkt)
{
    const uint8_t poczatek = (uint8_t)(g_wyjscie_strona * UI_SIATKA_KOMPAKT_NA_STRONE);
    const uint8_t liczba = g_wyjscie_strona == 0U ? 6U : 5U;
    const int16_t lokalny = UI_KafelKompaktowyPoDotyku(punkt, liczba);

    if (lokalny < 0)
        return 0U;
    g_wyjscie_fokus = (uint8_t)(poczatek + (uint8_t)lokalny);
    g_wyjscie_fokus_widoczny = 1U;
    GENERATOR_WykonajOpcjeWyjscia(g_wyjscie_fokus);
    return 1U;
}

static uint8_t GENERATOR_ObsluzDolnyPasek(LCDPoint punkt, uint8_t *wyjscie)
{
    uint8_t i;

    if (punkt.y < 220U)
        return 0U;
    if (UI_CzyDotknietoWstecz(punkt))
    {
        *wyjscie = 1U;
        return 1U;
    }

    for (i = 1U; i <= 5U; ++i)
    {
        const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego(i);
        if (!UI_CzyPunktWObszarze(punkt, &o))
            continue;

        if (g_widok == GENERATOR_WIDOK_WYJSCIE)
        {
            if (i == 1U && g_wyjscie_strona > 0U)
                g_wyjscie_strona--;
            else if (i == 3U && g_wyjscie_strona < 1U)
                g_wyjscie_strona++;
            else if (i == 4U)
            {
                g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
                GENERATOR_WpiszCzestotliwosc();
                return 1U;
            }
            else if (i == 5U)
                g_widok = GENERATOR_WIDOK_DIAGNOSTYKA;
            else
                return 1U; /* Numer strony nie jest przyciskiem akcji. */
            g_wyjscie_fokus_widoczny = 0U;
            GENERATOR_RysujWidok();
            return 1U;
        }

        if (g_widok == GENERATOR_WIDOK_DIAGNOSTYKA)
        {
            if (i == 1U)
                GENERATOR_WybierzPasmo();
            else if (i == 2U)
            {
                g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
                GENERATOR_WpiszCzestotliwosc();
            }
            else if (i == 3U)
            {
                g_widok = GENERATOR_WIDOK_WYJSCIE;
                GENERATOR_RysujWidok();
            }
            else if (i == 4U)
            {
                GENERATOR_WylaczRF();
                CENTRUM_KALIBRACJI_Otworz();
                GENERATOR_UstawRF();
                g_widok = GENERATOR_WIDOK_DIAGNOSTYKA;
                GENERATOR_RysujWidok();
            }
            else if (i == 5U)
                return 1U; /* Puste pole dolnego paska. */
            else
                return 1U;
            return 1U;
        }

        switch (i)
        {
        case 1U:
            GENERATOR_WybierzPasmo();
            break;
        case 2U:
            GENERATOR_WpiszCzestotliwosc();
            break;
        case 3U:
            GENERATOR_WybierzPresety();
            break;
        case 4U:
            g_widok = GENERATOR_WIDOK_WYJSCIE;
            g_wyjscie_fokus_widoczny = 0U;
            GENERATOR_RysujWidok();
            break;
        case 5U:
            g_widok = GENERATOR_WIDOK_DIAGNOSTYKA;
            GENERATOR_RysujWidok();
            break;
        default:
            break;
        }
        return 1U;
    }
    return 0U;
}

/* Zachowana funkcja serwisowa uzywana w starszych wydaniach. */
int testGen(void)
{
    GEN_SetMeasurementFreq(3500000UL);
    Sleep(200);
    DSP_Measure(0, 1, 0, CFG_GetParam(CFG_PARAM_MEAS_NSCANS));
    return DSP_MeasuredMagVmv() > 0.3f ? 0 : -1;
}

void GENERATOR_Window_Proc(void)
{
    uint8_t wyjscie = 0U;
    uint32_t czestotliwosc;

    /*
     * W topologii 2x ADF CLK2 Si5351 jest zegarem odniesienia 27 MHz dla PLL.
     * Generator uzytkowy nie moze wtedy przejac CLK2, bo rozstroilby oba ADF.
     * Zabezpieczenie jest takze tutaj, a nie tylko w menu, aby wywolanie tego
     * okna z diagnostyki lub starszej sciezki nie moglo uszkodzic toru RF.
     */
    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        UI_RysujEkranPrzejsciowy(
            JEZYK_Wybierz("Generator RF", "RF generator", "HF-Generator", "ВЧ-генератор"),
            JEZYK_Wybierz("Niedostępne: CLK2 pracuje jako wzorzec ADF.",
                          "Unavailable: CLK2 is used as the ADF reference.",
                          "Nicht verfügbar: CLK2 ist ADF-Referenz.",
                          "Недоступно: CLK2 используется как опора ADF."));
        Sleep(1800U);
        return;
    }

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();
    SetColours();

    if (CLK2_drive > 3U)
        CLK2_drive = 3U;

    czestotliwosc = CFG_GetParam(CFG_PARAM_GEN_F);
    if (czestotliwosc < GENERATOR_MinCzestotliwosc() ||
        czestotliwosc > GENERATOR_MaksCzestotliwosc())
    {
        czestotliwosc = 14000000U;
        if (czestotliwosc < GENERATOR_MinCzestotliwosc())
            czestotliwosc = GENERATOR_MinCzestotliwosc();
        if (czestotliwosc > GENERATOR_MaksCzestotliwosc())
            czestotliwosc = GENERATOR_MaksCzestotliwosc();
        CFG_SetParam(CFG_PARAM_GEN_F, czestotliwosc);
        CFG_Flush();
    }

    g_widok = GENERATOR_WIDOK_CZESTOTLIWOSC;
    g_wyjscie_strona = 0U;
    g_wyjscie_fokus = 0U;
    g_wyjscie_fokus_widoczny = 0U;
    g_modulacja_aktywna = 0U;
    GENERATOR_UstawRF();
    GENERATOR_RysujWidok();

    while (!wyjscie)
    {
        LCDPoint punkt;
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            wyjscie = 1U;
        else if (g_widok == GENERATOR_WIDOK_CZESTOTLIWOSC &&
                 zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            FDecrG(g_krok_enkodera_hz);
            GENERATOR_RysujWidok();
        }
        else if (g_widok == GENERATOR_WIDOK_CZESTOTLIWOSC &&
                 zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            FIncrG(g_krok_enkodera_hz);
            GENERATOR_RysujWidok();
        }
        else if (g_widok == GENERATOR_WIDOK_CZESTOTLIWOSC &&
                 (zdarzenie == WEJSCIE_ZDARZENIE_OK || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP))
        {
            GENERATOR_WpiszCzestotliwosc();
        }
        else if (g_widok == GENERATOR_WIDOK_WYJSCIE &&
                 (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                  zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO))
        {
            g_wyjscie_fokus_widoczny = 1U;
            if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                g_wyjscie_fokus = (uint8_t)((g_wyjscie_fokus + 1U) % 11U);
            else
                g_wyjscie_fokus = g_wyjscie_fokus == 0U ? 10U : (uint8_t)(g_wyjscie_fokus - 1U);
            g_wyjscie_strona = (uint8_t)(g_wyjscie_fokus / UI_SIATKA_KOMPAKT_NA_STRONE);
            GENERATOR_RysujWidok();
        }
        else if (g_widok == GENERATOR_WIDOK_WYJSCIE &&
                 (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
                  zdarzenie == WEJSCIE_ZDARZENIE_START_STOP) &&
                 g_wyjscie_fokus_widoczny)
        {
            GENERATOR_WykonajOpcjeWyjscia(g_wyjscie_fokus);
        }

        if (TOUCH_Poll(&punkt))
        {
            while (TOUCH_IsPressed())
                ;

            if (!GENERATOR_ObsluzDolnyPasek(punkt, &wyjscie))
            {
                if (g_widok == GENERATOR_WIDOK_CZESTOTLIWOSC)
                    (void)GENERATOR_ObsluzDotykCzestotliwosc(punkt);
                else if (g_widok == GENERATOR_WIDOK_WYJSCIE)
                    (void)GENERATOR_ObsluzDotykWyjscie(punkt);
            }
        }

        if (g_widok == GENERATOR_WIDOK_DIAGNOSTYKA)
            GENERATOR_OdswiezDiagnostyke(0U);
        Sleep(15);
    }

    GENERATOR_WylaczRF();
    CLK2_drive = 3U;
    SetColours();
    WEJSCIA_WyczyscZdarzenia();
}
