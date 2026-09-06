#include <math.h>
#include <complex.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "LCD.h"
#include "font.h"
#include "config.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "smith.h"
#include "match.h"
#include "oslfile.h"
#include "dokumentacja_przyklady.h"

#define DOK_WYKRES_X0 34
#define DOK_WYKRES_Y0 58
#define DOK_WYKRES_X1 458
#define DOK_WYKRES_Y1 202
#define DOK_PI 3.14159265358979323846f

static const char *const dok_przyklady_nazwy[] = {
    /* Pojedynczy pomiar - 8. */
    "demo_single_50ohm",
    "demo_single_inductive",
    "demo_single_capacitive",
    "demo_single_high_swr",
    "demo_single_weak_signal",
    "demo_single_parallel",
    "demo_single_smith_full",
    "demo_single_metrology_data",

    /* SWR i Smith - 12. */
    "demo_swr_narrow_good",
    "demo_swr_wide_good",
    "demo_swr_dual_resonance",
    "demo_swr_high",
    "demo_swr_rx",
    "demo_swr_s11_mag",
    "demo_swr_s11_phase",
    "demo_smith_resonant",
    "demo_smith_inductive",
    "demo_smith_capacitive",
    "demo_smith_open_short_load",
    "demo_smith_wide_trace",

    /* Wpływ ustawień - 10. */
    "demo_setting_z0_50",
    "demo_setting_z0_75",
    "demo_setting_osl_off",
    "demo_setting_osl_on",
    "demo_setting_cable_off",
    "demo_setting_cable_on",
    "demo_setting_avg_1",
    "demo_setting_avg_8",
    "demo_setting_cursor_auto",
    "demo_setting_cursor_manual",

    /* Poprawna kalibracja i weryfikacja - 10. */
    "demo_cal_status_ok",
    "demo_cal_osl_values",
    "demo_cal_short_ok",
    "demo_cal_load_ok",
    "demo_cal_open_ok",
    "demo_cal_verify_25",
    "demo_cal_verify_50",
    "demo_cal_verify_75",
    "demo_cal_verify_100",
    "demo_cal_s21_ok",

    /* DSP - 8. */
    "demo_dsp_bypass",
    "demo_dsp_bpf50",
    "demo_dsp_bpf100",
    "demo_dsp_bpf150",
    "demo_dsp_lpf",
    "demo_dsp_hpf",
    "demo_dsp_ssb",
    "demo_dsp_weak_signal",

    /* Strojenie - 4. */
    "demo_tune_inductive",
    "demo_tune_capacitive",
    "demo_tune_matched",
    "demo_tune_scan_result",

    /* TDR - 4. */
    "demo_tdr_open",
    "demo_tdr_short",
    "demo_tdr_matched",
    "demo_tdr_discontinuity",

    /* S21 - 4. */
    "demo_s21_thru",
    "demo_s21_3db",
    "demo_s21_10db",
    "demo_s21_filter",

    /* L/C - 3. */
    "demo_lc_inductor",
    "demo_lc_capacitor",
    "demo_lc_quality",

    /* Kwarc - 3. */
    "demo_quartz_resonance",
    "demo_quartz_parameters",
    "demo_quartz_series",


    /* Pliki - 3. */
    "demo_files_screenshots",
    "demo_files_manager",
    "demo_files_usb",


    /* Wiele pasm - 3. */
    "demo_multiband_overview",
    "demo_multiband_table",
    "demo_multiband_resonances",

    /* Szukaj F - 2. */
    "demo_find_start",
    "demo_find_result",

    /* Generator RF - 3. */
    "demo_generator_cw",
    "demo_generator_level",
    "demo_generator_sweep",

    /* WSPR / FT8 - 4. */
    "demo_wspr_ready",
    "demo_wspr_band",
    "demo_ft8_ready",
    "demo_wspr_tx_status",

    /* Skaner RF - 2. */
    "demo_rfscan_result",
    "demo_rfscan_waterfall"
};

uint32_t DOK_PRZYKLADY_LiczbaStron(void)
{
    return (uint32_t)(sizeof(dok_przyklady_nazwy) / sizeof(dok_przyklady_nazwy[0]));
}

const char *DOK_PRZYKLADY_NazwaStrony(uint32_t strona)
{
    if (strona >= DOK_PRZYKLADY_LiczbaStron())
        return "demo";
    return dok_przyklady_nazwy[strona];
}

static const char *DOK_T(const char *pl, const char *en, const char *de, const char *ru)
{
    return JEZYK_Wybierz(pl, en, de, ru);
}

static void DOK_PasekDolny3(const char *a, const char *b, const char *c)
{
    const UI_PROSTOKAT_t akcja_b = UI_ObszarPrzyciskuDolnego(1U);
    const UI_PROSTOKAT_t akcja_c = UI_ObszarPrzyciskuDolnego(2U);

    /* Ilustracje poglądowe używają dokładnie tego samego rastra 70 x 45 px
     * co działające ekrany. Dzięki temu wcześniejszej weryfikacji kontroluje rzeczywisty standard. */
    (void)a;
    UI_RysujWsteczDolny(false);
    UI_RysujPrzycisk(akcja_b.x, akcja_b.y, akcja_b.szerokosc, akcja_b.wysokosc,
                     b, UI_STYL_NORMALNY, FONT_FRAN);
    UI_RysujPrzycisk(akcja_c.x, akcja_c.y, akcja_c.szerokosc, akcja_c.wysokosc,
                     c, UI_STYL_AKCENT, FONT_FRAN);
}

static void DOK_RamkaWykresu(const char *opis_y, const char *opis_x)
{
    LCDColor siatka = UI_KolorRamki(UI_STYL_NIEAKTYWNY);
    uint16_t x, y;
    LCD_Rectangle(LCD_MakePoint(DOK_WYKRES_X0, DOK_WYKRES_Y0),
                  LCD_MakePoint(DOK_WYKRES_X1, DOK_WYKRES_Y1), siatka);
    for (x = DOK_WYKRES_X0 + 53; x < DOK_WYKRES_X1; x += 53)
        LCD_VLine(LCD_MakePoint(x, DOK_WYKRES_Y0), DOK_WYKRES_Y1 - DOK_WYKRES_Y0, siatka);
    for (y = DOK_WYKRES_Y0 + 36; y < DOK_WYKRES_Y1; y += 36)
        LCD_HLine(LCD_MakePoint(DOK_WYKRES_X0, y), DOK_WYKRES_X1 - DOK_WYKRES_X0, siatka);
    if (opis_y != 0)
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 2, 38, opis_y);
    if (opis_x != 0)
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaEkranu(), 350, 205, opis_x);
}

static void DOK_Linia(float (*funkcja)(float), float ymin, float ymax, LCDColor kolor)
{
    int i;
    LCDPoint p0 = LCD_MakePoint(DOK_WYKRES_X0, DOK_WYKRES_Y1);
    const float zakres = ymax - ymin;
    for (i = 0; i <= (DOK_WYKRES_X1 - DOK_WYKRES_X0); ++i)
    {
        const float t = (float)i / (float)(DOK_WYKRES_X1 - DOK_WYKRES_X0);
        float v = funkcja(t);
        int y;
        LCDPoint p1;
        if (v < ymin) v = ymin;
        if (v > ymax) v = ymax;
        y = DOK_WYKRES_Y1 - (int)(((v - ymin) / zakres) * (float)(DOK_WYKRES_Y1 - DOK_WYKRES_Y0));
        p1 = LCD_MakePoint(DOK_WYKRES_X0 + i, y);
        if (i != 0)
        {
            LCD_Line(p0, p1, kolor);
            LCD_Line(LCD_MakePoint(p0.x, p0.y + 1), LCD_MakePoint(p1.x, p1.y + 1), kolor);
        }
        p0 = p1;
    }
}

static float DOK_SwrDobryWaski(float t)
{
    float d = (t - 0.50f) / 0.105f;
    return 1.08f + 5.4f * (1.0f - expf(-d * d));
}

static float DOK_SwrDobrySzeroki(float t)
{
    float d = (t - 0.50f) / 0.22f;
    return 1.18f + 3.2f * (1.0f - expf(-d * d));
}

static float DOK_SwrDwa(float t)
{
    float d1 = (t - 0.32f) / 0.09f;
    float d2 = (t - 0.71f) / 0.11f;
    float m1 = expf(-d1 * d1);
    float m2 = expf(-d2 * d2);
    return 5.8f - 4.55f * (m1 > m2 ? m1 : m2);
}

static float DOK_SwrZly(float t)
{
    return 3.8f + 0.5f * sinf(2.0f * DOK_PI * t) + 0.25f * cosf(6.0f * DOK_PI * t);
}

static float DOK_R(float t)
{
    float d = t - 0.5f;
    return 50.0f + 36.0f * d * d * 4.0f;
}

static float DOK_X(float t)
{
    return -70.0f + 140.0f * t;
}

static float DOK_S11Mag(float t)
{
    float d = (t - 0.5f) / 0.15f;
    return -3.0f - 28.0f * expf(-d * d);
}

static float DOK_Faza(float t)
{
    return -170.0f + 340.0f * t;
}

static float DOK_TdrOpen(float t)
{
    if (t < 0.56f) return 0.02f * sinf(40.0f * t);
    return 0.92f - 0.08f * expf(-(t - 0.56f) * 25.0f);
}

static float DOK_TdrShort(float t)
{
    if (t < 0.54f) return 0.02f * sinf(40.0f * t);
    return -0.92f + 0.08f * expf(-(t - 0.54f) * 25.0f);
}

static float DOK_TdrMatched(float t)
{
    return 0.025f * sinf(16.0f * DOK_PI * t) * expf(-3.0f * t);
}

static float DOK_TdrStep(float t)
{
    if (t < 0.36f) return 0.0f;
    if (t < 0.62f) return 0.34f;
    return 0.72f;
}

static float DOK_S21Thru(float t) { (void)t; return -0.25f; }
static float DOK_S213dB(float t) { (void)t; return -3.02f; }
static float DOK_S2110dB(float t) { (void)t; return -10.1f; }
static float DOK_S21Filter(float t)
{
    float d = (t - 0.52f) / 0.16f;
    return -38.0f + 37.0f * expf(-d * d * d * d);
}

static float DOK_Kwarc(float t)
{
    float d1 = (t - 0.48f) / 0.018f;
    float d2 = (t - 0.53f) / 0.022f;
    return -26.0f + 24.0f * expf(-d1*d1) - 18.0f * expf(-d2*d2);
}

static void DOK_RysujWykres(const char *tytul, const char *podtytul,
                            float (*f)(float), float ymin, float ymax,
                            const char *yopis, const char *xopis)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);
    if (podtytul != 0)
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaEkranu(), 36, 36, podtytul);
    DOK_RamkaWykresu(yopis, xopis);
    DOK_Linia(f, ymin, ymax, UI_KolorRamki(UI_STYL_AKCENT));
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    DOK_T("Zakres / ustawienia","Range / settings","Bereich / Einstellungen","Диапазон / настройки"),
                    DOK_T("Pomiar","Measure","Messen","Измерить"));
}

static void DOK_RysujPojedynczy(float r, float x, const char *opis, uint8_t sygnal_ok, uint8_t rownolegle)
{
    char b[64];
    float swr;
    float complex z = r + x * I;
    float complex g = OSL_GFromZ(z, 50U);
    float mag = cabsf(g);

    swr = mag < 0.999f ? (1.0f + mag) / (1.0f - mag) : 999.0f;
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Pojedynczy pomiar","Single measurement","Einzelmessung","Одиночное измерение"), true, false, 0);
    UI_RysujPoleWartosci(8, 38, 264, 48, DOK_T("Częstotliwość","Frequency","Frequenz","Частота"), "14.200000 MHz");
    UI_RysujPoleStatusu(280, 38, 192, 48, DOK_T("Sygnał","Signal","Signal","Сигнал"),
                        sygnal_ok ? "OK" : DOK_T("za słaby","too weak","zu schwach","слишком слабый"),
                        sygnal_ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
    snprintf(b, sizeof(b), "SWR %.2f   Z0 50 Ohm", (double)swr);
    UI_RysujPoleWartosci(8, 94, 264, 54, rownolegle ? DOK_T("Impedancja równoległa","Parallel impedance","Parallelimpedanz","Паралл. импеданс") : DOK_T("Impedancja szeregowa","Series impedance","Serienimpedanz","Послед. импеданс"), b);
    snprintf(b, sizeof(b), "R %.1f Ohm   X %+.1f Ohm", (double)r, (double)x);
    UI_RysujPoleStatusu(8, 154, 264, 48, opis, b, sygnal_ok ? UI_STYL_NORMALNY : UI_STYL_OSTRZEZENIE);

    SMITH_DrawGrid(382, 148, 72, UI_KolorRamki(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
                   SMITH_R25 | SMITH_R50 | SMITH_R100 | SMITH_J50 | SMITH_J100 | SMITH_SWR2);
    if (sygnal_ok)
    {
        SMITH_ResetStartPoint();
        SMITH_DrawG(0, g, UI_KolorRamki(UI_STYL_AKCENT));
        SMITH_DrawGEndMark(UI_KolorRamki(UI_STYL_OSTRZEZENIE));
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    DOK_T("Ustaw częstotliwość","Set frequency","Frequenz setzen","Задать частоту"),
                    DOK_T("Dane","Data","Daten","Данные"));
}

static void DOK_RysujSmith(const char *tytul, uint8_t wariant)
{
    uint32_t i;
    float complex g;
    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);
    SMITH_DrawGrid(244, 138, 104, UI_KolorRamki(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(),
                   SMITH_R10 | SMITH_R25 | SMITH_R50 | SMITH_R100 | SMITH_R200 |
                   SMITH_J25 | SMITH_J50 | SMITH_J100 | SMITH_SWR2);
    SMITH_ResetStartPoint();
    for (i = 0U; i < 121U; ++i)
    {
        float t = (float)i / 120.0f;
        float r = 50.0f;
        float x = 0.0f;
        switch (wariant)
        {
        case 0: r = 43.0f + 14.0f*t; x = -38.0f + 76.0f*t; break;
        case 1: r = 35.0f + 20.0f*t; x = 15.0f + 95.0f*t; break;
        case 2: r = 35.0f + 20.0f*t; x = -110.0f + 95.0f*t; break;
        case 3:
            if (i < 40U) { r = 2.0f + t*4.0f; x = 0.0f; }
            else if (i < 80U) { r = 50.0f; x = 0.0f; }
            else { r = 800.0f; x = 0.0f; }
            break;
        default: r = 18.0f + 150.0f*t*t; x = -130.0f + 260.0f*t; break;
        }
        g = OSL_GFromZ(r + x*I, 50U);
        SMITH_DrawG((int)i, g, UI_KolorRamki(UI_STYL_AKCENT));
    }
    SMITH_DrawGEndMark(UI_KolorRamki(UI_STYL_OSTRZEZENIE));
    UI_RysujPoleWartosci(8, 44, 116, 46, "Z0", "50 Ohm");
    UI_RysujPoleWartosci(354, 44, 118, 46, DOK_T("Zakres","Span","Bereich","Диапазон"), "1.0 MHz");
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), "Smith", DOK_T("Zrzut","Snapshot","Bild","Снимок"));
}

static void DOK_RysujPorownanie(const char *tytul, const char *lewa_nazwa, const char *lewa,
                                const char *prawa_nazwa, const char *prawa,
                                const char *wniosek, uint8_t lewa_ok, uint8_t prawa_ok)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);
    UI_RysujPoleStatusu(8, 42, 226, 80, lewa_nazwa, lewa, lewa_ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
    UI_RysujPoleStatusu(246, 42, 226, 80, prawa_nazwa, prawa, prawa_ok ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY);
    UI_RysujPanel(8, 132, 464, 80, DOK_T("Wpływ na wynik","Effect on result","Einfluss auf Ergebnis","Влияние на результат"), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaPola(), 20, 162, wniosek);
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Ustawienia","Settings","Einstellungen","Настройки"), DOK_T("Pomiar","Measure","Messen","Измерить"));
}

static void DOK_RysujKalibracje(const char *tytul, const char *wzorzec, const char *wartosc,
                                const char *wynik, uint8_t ok)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(tytul, true, false, 0);
    UI_RysujPoleWartosci(12, 44, 216, 58, DOK_T("Wzorzec","Standard","Standard","Эталон"), wzorzec);
    UI_RysujPoleWartosci(252, 44, 216, 58, DOK_T("Wartość","Value","Wert","Значение"), wartosc);
    UI_RysujPoleStatusu(12, 112, 456, 86, DOK_T("Wynik weryfikacji","Verification result","Prüfergebnis","Результат проверки"), wynik,
                        ok ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Szczegóły","Details","Details","Подробно"), DOK_T("Zapisz","Save","Speichern","Сохранить"));
}

static void DOK_RysujWidmoSyntetyczne(uint8_t wariant)
{
    int x;
    int center = 225;
    int szer = 80;
    const char *opis = DOK_T("Bez filtra","Bypass","Ohne Filter","Без фильтра");
    const char *param = "300-3000 Hz";

    if (wariant == 1U) { szer = 8; opis = "BPF 50 Hz"; param = "700 Hz"; }
    if (wariant == 2U) { szer = 15; opis = "BPF 100 Hz"; param = "700 Hz"; }
    if (wariant == 3U) { szer = 22; opis = "BPF 150 Hz"; param = "700 Hz"; }
    if (wariant == 4U) { szer = 65; center = 130; opis = "LPF"; param = "< 1000 Hz"; }
    if (wariant == 5U) { szer = 90; center = 340; opis = "HPF"; param = "> 1200 Hz"; }
    if (wariant == 6U) { szer = 110; center = 240; opis = "SSB"; param = "300-2700 Hz"; }
    if (wariant == 7U) { szer = 10; center = 270; opis = DOK_T("Słaby sygnał CW","Weak CW signal","Schwaches CW-Signal","Слабый CW"); param = "925 Hz"; }

    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("DSP audio / widmo","Audio DSP / spectrum","Audio-DSP / Spektrum","Аудио DSP / спектр"), true, false, 0);
    LCD_Rectangle(LCD_MakePoint(8, 38), LCD_MakePoint(472, 150), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    for (x = 10; x < 470; ++x)
    {
        float dx1 = ((float)x - 145.0f) / 16.0f;
        float dx2 = ((float)x - 270.0f) / 10.0f;
        float dx3 = ((float)x - 365.0f) / 22.0f;
        float h = 12.0f + 48.0f*expf(-dx1*dx1) + 72.0f*expf(-dx2*dx2) + 35.0f*expf(-dx3*dx3);
        int y0 = 146 - (int)h;
        LCD_VLine(LCD_MakePoint(x, y0), 146-y0, UI_KolorRamki(UI_STYL_AKCENT));
    }
    if (wariant != 0U)
    {
        LCD_Rectangle(LCD_MakePoint(center - szer, 42), LCD_MakePoint(center + szer, 146), UI_KolorRamki(UI_STYL_OSTRZEZENIE));
    }
    UI_RysujPoleWartosci(8, 158, 226, 58, DOK_T("Filtr","Filter","Filter","Фильтр"), opis);
    UI_RysujPoleWartosci(246, 158, 226, 58, DOK_T("Zakres","Range","Bereich","Диапазон"), param);
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Filtr","Filter","Filter","Фильтр"), DOK_T("Zapisz","Save","Speichern","Сохранить"));
}

static void DOK_RysujStrojenie(uint8_t wariant)
{
    const char *kier = DOK_T("Dostrój antenę","Tune antenna","Antenne abstimmen","Настройте антенну");
    const char *reak = "X = 0.0 Ohm";
    const char *swr = "1.06";
    UI_STYL_t styl = UI_STYL_AKTYWNY;
    if (wariant == 0U) { kier = DOK_T("Skróć antenę / kompensuj L","Shorten antenna / compensate L","Antenne kürzen / L kompensieren","Укоротить / компенсировать L"); reak = "X = +34.7 Ohm"; swr = "1.83"; styl = UI_STYL_OSTRZEZENIE; }
    if (wariant == 1U) { kier = DOK_T("Wydłuż antenę / kompensuj C","Lengthen antenna / compensate C","Antenne verlängern / C kompensieren","Удлинить / компенсировать C"); reak = "X = -28.2 Ohm"; swr = "1.67"; styl = UI_STYL_OSTRZEZENIE; }
    if (wariant == 3U) { kier = DOK_T("Minimum znalezione: 7.108 MHz","Minimum found: 7.108 MHz","Minimum gefunden: 7.108 MHz","Минимум: 7.108 МГц"); reak = "R = 49.1 Ohm"; swr = "1.04"; }
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Asystent strojenia","Tuning assistant","Abstimmassistent","Помощник настройки"), true, false, 0);
    UI_RysujPoleWartosci(8, 42, 150, 60, "SWR", swr);
    UI_RysujPoleWartosci(166, 42, 150, 60, DOK_T("Częstotliwość","Frequency","Frequenz","Частота"), "7.100 MHz");
    UI_RysujPoleWartosci(324, 42, 148, 60, "R / X", reak);
    UI_RysujPoleStatusu(8, 112, 464, 78, DOK_T("Wskazówka","Guidance","Hinweis","Подсказка"), kier, styl);
    if (wariant == 3U)
        UI_RysujPoleStatusu(8, 192, 464, 22, "SWR < 2", "6.980 - 7.228 MHz", UI_STYL_AKTYWNY);
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Analiza","Analyze","Analyse","Анализ"), DOK_T("Dźwięk","Sound","Ton","Звук"));
}

static void DOK_RysujTDR(uint8_t wariant)
{
    float (*f)(float) = DOK_TdrMatched;
    const char *opis = DOK_T("Linia dopasowana","Matched line","Angepasste Leitung","Согласованная линия");
    if (wariant == 0U) { f = DOK_TdrOpen; opis = DOK_T("Rozwarcie na końcu kabla","Open at cable end","Leerlauf am Kabelende","Обрыв на конце кабеля"); }
    if (wariant == 1U) { f = DOK_TdrShort; opis = DOK_T("Zwarcie na końcu kabla","Short at cable end","Kurzschluss am Kabelende","КЗ на конце кабеля"); }
    if (wariant == 3U) { f = DOK_TdrStep; opis = DOK_T("Dwie nieciągłości impedancji","Two impedance discontinuities","Zwei Impedanzsprünge","Две неоднородности"); }
    DOK_RysujWykres(DOK_T("TDR - przykład","TDR - example","TDR - Beispiel","TDR - пример"), opis, f, -1.0f, 1.0f, "Gamma", "m");
}

static void DOK_RysujS21(uint8_t wariant)
{
    float (*f)(float) = DOK_S21Thru;
    const char *opis = DOK_T("Połączenie THRU","THRU connection","THRU-Verbindung","Соединение THRU");
    if (wariant == 1U) { f = DOK_S213dB; opis = DOK_T("Tłumik 3 dB","3 dB attenuator","3-dB-Dämpfer","Аттенюатор 3 дБ"); }
    if (wariant == 2U) { f = DOK_S2110dB; opis = DOK_T("Tłumik 10 dB","10 dB attenuator","10-dB-Dämpfer","Аттенюатор 10 дБ"); }
    if (wariant == 3U) { f = DOK_S21Filter; opis = DOK_T("Przykładowy filtr pasmowy","Example band-pass filter","Beispiel Bandpass","Пример полосового фильтра"); }
    DOK_RysujWykres("S21", opis, f, -45.0f, 2.0f, "dB", "MHz");
}

static void DOK_RysujLC(uint8_t wariant)
{
    const char *typ = "L";
    const char *wart = "1.02 uH";
    const char *q = "Q = 82";
    const char *model = "Rs = 0.55 Ohm   X = +44.8 Ohm";
    if (wariant == 1U) { typ = "C"; wart = "101.7 pF"; q = "ESR = 0.18 Ohm"; model = "Rs = 0.18 Ohm   X = -110.0 Ohm"; }
    if (wariant == 2U) { typ = "Q"; wart = "Q = 146"; q = "L = 2.21 uH"; model = "Rs = 0.95 Ohm   X = +138.7 Ohm"; }
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Elementy RF - przykład","RF components - example","HF-Bauteile - Beispiel","ВЧ элементы - пример"), true, false, 0);
    UI_RysujPoleLiczboweGlowne(20, 48, 200, 92, typ, wart, "");
    UI_RysujPoleStatusu(240, 48, 220, 92, DOK_T("Parametr dodatkowy","Additional parameter","Zusatzparameter","Доп. параметр"), q, UI_STYL_AKTYWNY);
    UI_RysujPoleStatusu(20, 152, 440, 62, DOK_T("Model impedancji","Impedance model","Impedanzmodell","Модель импеданса"), model, UI_STYL_NORMALNY);
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Częstotliwość","Frequency","Frequenz","Частота"), DOK_T("Pomiar","Measure","Messen","Измерить"));
}

static void DOK_RysujKwarc(uint8_t wariant)
{
    if (wariant == 0U)
    {
        DOK_RysujWykres(DOK_T("Kwarc - rezonans","Quartz - resonance","Quarz - Resonanz","Кварц - резонанс"), "14.318 MHz", DOK_Kwarc, -50.0f, 5.0f, "dB", "kHz");
        return;
    }
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Kwarc - parametry","Quartz - parameters","Quarz - Parameter","Кварц - параметры"), true, false, 0);
    if (wariant == 1U)
    {
        UI_RysujPoleWartosci(8, 44, 224, 58, "Fs", "14.31818 MHz");
        UI_RysujPoleWartosci(248, 44, 224, 58, "Fp", "14.32674 MHz");
        UI_RysujPoleWartosci(8, 112, 224, 58, "Rm", "18.4 Ohm");
        UI_RysujPoleWartosci(248, 112, 224, 58, "Q", "98600");
        UI_RysujPoleStatusu(8, 176, 464, 38, "Lm / Cm / C0", "18.7 mH / 6.6 fF / 3.1 pF", UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(8, 44, 464, 42, DOK_T("Seria pomiarowa","Measurement series","Messreihe","Серия измерений"), "10 / 10", UI_STYL_AKTYWNY);
        UI_RysujPoleWartosci(8, 96, 224, 58, DOK_T("Średnia Fs","Average Fs","Mittel Fs","Средняя Fs"), "14.318181 MHz");
        UI_RysujPoleWartosci(248, 96, 224, 58, DOK_T("Rozrzut","Spread","Streuung","Разброс"), "2.4 Hz");
        UI_RysujPoleStatusu(8, 164, 464, 50, DOK_T("Ocena powtarzalności","Repeatability","Wiederholbarkeit","Повторяемость"), DOK_T("bardzo dobra","very good","sehr gut","очень хорошая"), UI_STYL_AKTYWNY);
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Zapis CSV","Save CSV","CSV speichern","Сохранить CSV"), DOK_T("Powtórz","Repeat","Wiederholen","Повторить"));
}


static void DOK_RysujPliki(uint8_t wariant)
{
    UI_WyczyscEkran();
    if (wariant == 0U)
    {
        UI_RysujPasekGorny(DOK_T("Zrzuty ekranu","Screenshots","Screenshots","Снимки экрана"), true, false, 0);
        UI_RysujPoleStatusu(8, 42, 464, 38, "001.bmp", "23.08.2026 15:42   382 kB", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(8, 86, 464, 38, "002.bmp", "23.08.2026 15:43   382 kB", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 130, 464, 38, "003.bmp", "23.08.2026 15:44   382 kB", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 174, 464, 38, "004.bmp", "23.08.2026 15:45   382 kB", UI_STYL_NORMALNY);
        DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Pokaż","Show","Anzeigen","Показать"), DOK_T("Usuń","Delete","Löschen","Удалить"));
    }
    else if (wariant == 1U)
    {
        UI_RysujPasekGorny(DOK_T("Menedżer plików","File manager","Dateimanager","Файловый менеджер"), true, false, 0);
        UI_RysujPoleStatusu(8, 42, 464, 38, "/aa", "<DIR>", UI_STYL_AKCENT);
        UI_RysujPoleStatusu(8, 86, 464, 38, "anteny", "<DIR>", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 130, 464, 38, "cw", "<DIR>", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 174, 464, 38, "manual", "<DIR>", UI_STYL_NORMALNY);
        DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Otwórz","Open","Öffnen","Открыть"), DOK_T("Opcje","Options","Optionen","Опции"));
    }
    else
    {
        UI_RysujPasekGorny("USB", true, false, 0);
        UI_RysujPoleStatusu(30, 66, 420, 70, DOK_T("Tryb pamięci masowej","Mass storage mode","Massenspeichermodus","Режим накопителя"), DOK_T("Karta SD udostępniona przez USB","SD card shared over USB","SD-Karte über USB freigegeben","SD-карта доступна по USB"), UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(30, 150, 420, 54, DOK_T("Stan","State","Status","Состояние"), DOK_T("Połączono z komputerem","Connected to computer","Mit Computer verbunden","Подключено к компьютеру"), UI_STYL_AKTYWNY);
        DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), "USB", DOK_T("Odłącz","Disconnect","Trennen","Отключить"));
    }
}

static void DOK_RysujWielePasm(uint8_t wariant)
{
    if (wariant == 2U)
    {
        DOK_RysujWykres(DOK_T("Wiele pasm - rezonanse","Multi-band - resonances","Mehrband - Resonanzen","Многодиапазонная - резонансы"), "3.6 / 7.1 / 14.2 / 28.5 MHz", DOK_SwrDwa, 1.0f, 6.5f, "SWR", "HF");
        return;
    }
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Wiele pasm","Multiple bands","Mehrere Bänder","Несколько диапазонов"), true, false, 0);
    if (wariant == 0U)
    {
        UI_RysujPoleStatusu(8, 42, 224, 54, "80 m", "3.650 MHz  SWR 1.24", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(248, 42, 224, 54, "40 m", "7.108 MHz  SWR 1.06", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(8, 106, 224, 54, "20 m", "14.205 MHz SWR 1.18", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(248, 106, 224, 54, "10 m", "28.510 MHz SWR 1.31", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(8, 170, 464, 44, DOK_T("Ocena","Assessment","Bewertung","Оценка"), DOK_T("cztery rezonanse w zadanych pasmach","four resonances in selected bands","vier Resonanzen in gewählten Bändern","четыре резонанса"), UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(8, 42, 464, 34, DOK_T("Pasmo","Band","Band","Диапазон"), DOK_T("Minimum / szerokość SWR<2","Minimum / SWR<2 bandwidth","Minimum / SWR<2-Bandbreite","Минимум / полоса КСВ<2"), UI_STYL_AKCENT);
        UI_RysujPoleStatusu(8, 82, 464, 30, "80 m", "3.650 MHz / 82 kHz", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 118, 464, 30, "40 m", "7.108 MHz / 248 kHz", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 154, 464, 30, "20 m", "14.205 MHz / 410 kHz", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 186, 464, 28, "10 m", "28.510 MHz / 690 kHz", UI_STYL_NORMALNY);
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), DOK_T("Pasma","Bands","Bänder","Диапазоны"), DOK_T("Pomiar","Measure","Messen","Измерить"));
}

static void DOK_RysujSzukajF(uint8_t wariant)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Szukaj F", "Find F", "F suchen", "Поиск F"), true, false, 0);

    if (wariant == 0U)
    {
        UI_RysujPoleStatusu(12, 46, 456, 46,
                            DOK_T("Zakres wyszukiwania", "Search range", "Suchbereich", "Диапазон поиска"),
                            "1.0 - 480.0 MHz", UI_STYL_NORMALNY);
        UI_RysujPoleWartosci(12, 104, 218, 58,
                             DOK_T("Kryterium", "Criterion", "Kriterium", "Критерий"), "min SWR");
        UI_RysujPoleWartosci(250, 104, 218, 58,
                             DOK_T("Próg", "Threshold", "Schwelle", "Порог"), "SWR < 2.0");
        UI_RysujPoleStatusu(12, 174, 456, 38,
                            DOK_T("Stan", "State", "Status", "Состояние"),
                            DOK_T("Gotowe do skanowania", "Ready to scan", "Bereit zum Scannen", "Готово к сканированию"),
                            UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleLiczboweGlowne(12, 46, 218, 88, "F", "147.650", "MHz");
        UI_RysujPoleStatusu(250, 46, 218, 88, "SWR", "1.08", UI_STYL_AKTYWNY);
        UI_RysujPoleWartosci(12, 146, 218, 58, "Z", "48.9 + j2.1 Ohm");
        UI_RysujPoleWartosci(250, 146, 218, 58,
                             DOK_T("Ocena", "Assessment", "Bewertung", "Оценка"),
                             DOK_T("rezonans", "resonance", "Resonanz", "резонанс"));
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    DOK_T("Zakres","Range","Bereich","Диапазон"),
                    wariant == 0U ? DOK_T("Szukaj","Find","Suchen","Искать")
                                  : DOK_T("Pokaż","Show","Anzeigen","Показать"));
}

static void DOK_RysujGenerator(uint8_t wariant)
{
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("Generator RF", "RF generator", "HF-Generator", "ВЧ-генератор"), true, false, 0);

    if (wariant == 0U)
    {
        UI_RysujPoleLiczboweGlowne(12, 46, 270, 86, "F", "145.500000", "MHz");
        UI_RysujPoleStatusu(296, 46, 172, 86,
                            DOK_T("Wyjście", "Output", "Ausgang", "Выход"),
                            "CLK0 ON", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(12, 148, 456, 52,
                            DOK_T("Tryb", "Mode", "Modus", "Режим"),
                            DOK_T("Nośna ciągła", "Continuous carrier", "Dauerträger", "Непрерывная несущая"),
                            UI_STYL_NORMALNY);
    }
    else if (wariant == 1U)
    {
        UI_RysujPoleWartosci(12, 46, 218, 58,
                             DOK_T("Poziom", "Level", "Pegel", "Уровень"), "2 mA");
        UI_RysujPoleWartosci(250, 46, 218, 58,
                             DOK_T("Harmoniczna", "Harmonic", "Harmonische", "Гармоника"), "H1");
        UI_RysujPoleWartosci(12, 118, 218, 58, "CLK0", "145.500 MHz");
        UI_RysujPoleWartosci(250, 118, 218, 58, "CLK2", "OFF");
        UI_RysujPoleStatusu(12, 188, 456, 28,
                            DOK_T("Stan generatora", "Generator state", "Generatorstatus", "Состояние генератора"),
                            "PLL locked", UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleWartosci(12, 46, 218, 58,
                             DOK_T("Start", "Start", "Start", "Старт"), "140.000 MHz");
        UI_RysujPoleWartosci(250, 46, 218, 58,
                             DOK_T("Stop", "Stop", "Stopp", "Стоп"), "150.000 MHz");
        UI_RysujPoleWartosci(12, 118, 218, 58,
                             DOK_T("Krok", "Step", "Schritt", "Шаг"), "25 kHz");
        UI_RysujPoleWartosci(250, 118, 218, 58,
                             DOK_T("Czas", "Time", "Zeit", "Время"), "100 ms");
        UI_RysujPoleStatusu(12, 188, 456, 28,
                            DOK_T("Przemiatanie", "Sweep", "Sweep", "Сканирование"),
                            DOK_T("gotowe", "ready", "bereit", "готово"), UI_STYL_AKCENT);
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    DOK_T("Ustaw", "Set", "Setzen", "Задать"),
                    DOK_T("Start", "Start", "Start", "Старт"));
}

static void DOK_RysujCyfrowe(uint8_t wariant)
{
    const char *tryb = wariant == 2U ? "FT8" : "WSPR";
    UI_WyczyscEkran();
    UI_RysujPasekGorny(DOK_T("WSPR / FT8", "WSPR / FT8", "WSPR / FT8", "WSPR / FT8"), true, false, 0);

    if (wariant == 0U)
    {
        UI_RysujPoleStatusu(12, 44, 218, 54, DOK_T("Tryb", "Mode", "Modus", "Режим"), tryb, UI_STYL_AKTYWNY);
        UI_RysujPoleWartosci(250, 44, 218, 54, DOK_T("Pasmo", "Band", "Band", "Диапазон"), "20 m");
        UI_RysujPoleWartosci(12, 108, 218, 54, DOK_T("Znak", "Callsign", "Rufzeichen", "Позывной"), "SQ2KRR");
        UI_RysujPoleWartosci(250, 108, 218, 54, DOK_T("Lokator", "Locator", "Locator", "Локатор"), "JO84");
        UI_RysujPoleStatusu(12, 172, 456, 40, DOK_T("Stan", "State", "Status", "Состояние"),
                            DOK_T("Gotowe - oczekiwanie na ramkę czasu", "Ready - waiting for time slot", "Bereit - wartet auf Zeitfenster", "Готово - ожидание временного окна"),
                            UI_STYL_AKTYWNY);
    }
    else if (wariant == 1U)
    {
        UI_RysujPoleLiczboweGlowne(12, 44, 270, 86, "F", "14.095600", "MHz");
        UI_RysujPoleStatusu(296, 44, 172, 86, DOK_T("Tryb", "Mode", "Modus", "Режим"), "WSPR", UI_STYL_AKTYWNY);
        UI_RysujPoleWartosci(12, 144, 218, 58, DOK_T("Moc", "Power", "Leistung", "Мощность"), "23 dBm");
        UI_RysujPoleWartosci(250, 144, 218, 58, DOK_T("Czas", "Time", "Zeit", "Время"), "13:32:00");
    }
    else if (wariant == 2U)
    {
        UI_RysujPoleLiczboweGlowne(12, 44, 270, 86, "F", "14.074000", "MHz");
        UI_RysujPoleStatusu(296, 44, 172, 86, DOK_T("Tryb", "Mode", "Modus", "Режим"), "FT8", UI_STYL_AKTYWNY);
        UI_RysujPoleStatusu(12, 144, 456, 58, DOK_T("Wiadomość", "Message", "Nachricht", "Сообщение"), "CQ SQ2KRR JO84", UI_STYL_NORMALNY);
    }
    else
    {
        UI_RysujPoleStatusu(12, 44, 456, 58, DOK_T("Nadawanie", "Transmitting", "Senden", "Передача"), "WSPR TX", UI_STYL_OSTRZEZENIE);
        UI_RysujPoleLiczboweGlowne(12, 114, 218, 78, DOK_T("Symbol", "Symbol", "Symbol", "Символ"), "87 / 162", "");
        UI_RysujPoleStatusu(250, 114, 218, 78, DOK_T("Pozostało", "Remaining", "Restzeit", "Осталось"), "51 s", UI_STYL_AKCENT);
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    tryb,
                    DOK_T("Start", "Start", "Start", "Старт"));
}

static void DOK_RysujSkanerRF(uint8_t wariant)
{
    uint16_t x;
    UI_WyczyscEkran();

    if (wariant == 0U)
    {
        UI_RysujPasekGorny(DOK_T("Skaner RF / znajdź sygnał", "RF scanner / find signal",
                                 "HF-Scanner / Signal finden", "ВЧ-сканер / поиск сигнала"),
                           true, false, 0);
        UI_RysujPoleLiczboweGlowne(8, 42, 300, 78,
                                   DOK_T("Częstotliwość sygnału", "Signal frequency",
                                         "Signalfrequenz", "Частота сигнала"),
                                   "98.400000", "MHz");
        UI_RysujPoleWartosci(316, 42, 156, 78, DOK_T("Szczyt / tło", "Peak / floor",
                                                      "Peak / Grund", "Пик / фон"), "24.8 dB");
        UI_RysujPoleStatusu(8, 130, 464, 40, DOK_T("Zakres", "Range", "Bereich", "Диапазон"),
                            "88.000 - 108.000 MHz", UI_STYL_NORMALNY);
        UI_RysujPoleStatusu(8, 176, 464, 38, DOK_T("Interpretacja", "Interpretation",
                                                   "Interpretation", "Интерпретация"),
                            DOK_T("najsilniejszy wykryty sygnał", "strongest detected signal",
                                  "stärkstes erkanntes Signal", "самый сильный обнаруженный сигнал"),
                            UI_STYL_AKTYWNY);
        DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                        DOK_T("Od-Do","From-To","Von-Bis","От-До"),
                        DOK_T("Wodospad","Waterfall","Wasserfall","Водопад"));
        return;
    }

    UI_RysujPasekGorny(DOK_T("Skaner RF / wodospad", "RF scanner / waterfall",
                             "HF-Scanner / Wasserfall", "ВЧ-сканер / водопад"),
                       true, false, 0);
    UI_RysujPoleWartosci(8, 38, 300, 58,
                         DOK_T("Częstotliwość sygnału", "Signal frequency",
                               "Signalfrequenz", "Частота сигнала"),
                         "98.400000 MHz");
    UI_RysujPoleWartosci(316, 38, 156, 58, DOK_T("Szczyt / tło", "Peak / floor",
                                                 "Peak / Grund", "Пик / фон"), "24.8 dB");
    /* Przykład nowego RFSCAN4: spokojne tło i wiele pionowych śladów stacji.
     * Skala częstotliwości ma pięć znaczników jak rzeczywisty wodospad. */
    LCD_FillRect(LCD_MakePoint(0U, 104U), LCD_MakePoint(479U, 121U), UI_KolorTlaEkranu());
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 2, 104, "88");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 111, 104, "93");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 230, 104, "98");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 342, 104, "103");
    FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 430, 104, 478, "108");
    UI_WodospadWyczysc(0U, 122U, 479U, 218U);
    for (x = 0U; x < 480U; ++x)
    {
        uint16_t poziom = 0U;
        if ((x >= 34U && x <= 38U) || (x >= 77U && x <= 82U) ||
            (x >= 120U && x <= 126U) || (x >= 166U && x <= 171U) ||
            (x >= 211U && x <= 217U) || (x >= 252U && x <= 258U) ||
            (x >= 301U && x <= 306U) || (x >= 344U && x <= 350U))
            poziom = 560U;
        if ((x >= 92U && x <= 98U) || (x >= 392U && x <= 400U))
            poziom = 980U;
        LCD_VLine(LCD_MakePoint(x, 122U), 96U, UI_WodospadKolor(poziom));
    }
    DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),
                    DOK_T("Pauza","Pause","Pause","Пауза"),
                    DOK_T("Pełny ekran","Full screen","Vollbild","Полный экран"));
}

void DOK_PRZYKLADY_RysujStrone(uint32_t strona)
{
    /* 0..7: pojedynczy pomiar. */
    if (strona < 8U)
    {
        switch (strona)
        {
        case 0U: DOK_RysujPojedynczy(50.3f, 0.8f, DOK_T("Dopasowanie","Match","Anpassung","Согласование"), 1U, 0U); break;
        case 1U: DOK_RysujPojedynczy(43.2f, 35.6f, DOK_T("Charakter indukcyjny","Inductive","Induktiv","Индуктивный"), 1U, 0U); break;
        case 2U: DOK_RysujPojedynczy(46.1f, -28.4f, DOK_T("Charakter pojemnościowy","Capacitive","Kapazitiv","Емкостный"), 1U, 0U); break;
        case 3U: DOK_RysujPojedynczy(17.0f, 63.0f, DOK_T("Duże niedopasowanie","High mismatch","Große Fehlanpassung","Сильное рассогласование"), 1U, 0U); break;
        case 4U: DOK_RysujPojedynczy(50.0f, 0.0f, DOK_T("Wynik niewiarygodny","Unreliable result","Unzuverlässiges Ergebnis","Недостоверный результат"), 0U, 0U); break;
        case 5U: DOK_RysujPojedynczy(620.0f, 210.0f, DOK_T("Model równoległy","Parallel model","Parallelmodell","Параллельная модель"), 1U, 1U); break;
        case 6U: DOK_RysujSmith(DOK_T("Pojedynczy pomiar - Smith","Single measurement - Smith","Einzelmessung - Smith","Одиночный - Смит"), 0U); break;
        default:
            UI_WyczyscEkran();
            UI_RysujPasekGorny(DOK_T("Dane metrologiczne pomiaru","Measurement metrology data","Metrologische Messdaten","Метрологические данные"), true, false, 0);
            UI_RysujPoleWartosci(8, 44, 224, 54, "R", "50.32 Ohm");
            UI_RysujPoleWartosci(248, 44, 224, 54, "X", "+0.81 Ohm");
            UI_RysujPoleWartosci(8, 106, 224, 54, "|Gamma|", "0.0091");
            UI_RysujPoleWartosci(248, 106, 224, 54, "Phase", "+0.9 deg");
            UI_RysujPoleStatusu(8, 168, 464, 54, DOK_T("Korekcje","Corrections","Korrekturen","Коррекции"), "HW + OSL + port: ON", UI_STYL_AKTYWNY);
            DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"), "CSV", DOK_T("Weryfikacja","Verification","Prüfung","Проверка"));
            break;
        }
        return;
    }

    /* 8..19: SWR/Smith. */
    if (strona < 20U)
    {
        switch (strona - 8U)
        {
        case 0U: DOK_RysujWykres(DOK_T("SWR - wąski rezonans","SWR - narrow resonance","SWR - schmale Resonanz","КСВ - узкий резонанс"), "7.100 MHz / span 500 kHz", DOK_SwrDobryWaski, 1.0f, 7.0f, "SWR", "MHz"); break;
        case 1U: DOK_RysujWykres(DOK_T("SWR - szeroki rezonans","SWR - wide resonance","SWR - breite Resonanz","КСВ - широкий резонанс"), "14.200 MHz / span 2 MHz", DOK_SwrDobrySzeroki, 1.0f, 5.0f, "SWR", "MHz"); break;
        case 2U: DOK_RysujWykres(DOK_T("SWR - dwa rezonanse","SWR - dual resonance","SWR - Doppelresonanz","КСВ - два резонанса"), "multiband example", DOK_SwrDwa, 1.0f, 6.5f, "SWR", "MHz"); break;
        case 3U: DOK_RysujWykres(DOK_T("SWR - niedopasowanie","SWR - mismatch","SWR - Fehlanpassung","КСВ - рассогласование"), "no resonance in range", DOK_SwrZly, 1.0f, 6.0f, "SWR", "MHz"); break;
        case 4U:
            UI_WyczyscEkran(); UI_RysujPasekGorny("R / X", true, false, 0); DOK_RamkaWykresu("Ohm","MHz"); DOK_Linia(DOK_R, 0.0f, 100.0f, UI_KolorRamki(UI_STYL_AKCENT)); DOK_Linia(DOK_X, -100.0f, 100.0f, UI_KolorRamki(UI_STYL_OSTRZEZENIE)); DOK_PasekDolny3(DOK_T("Wstecz","Back","Zurück","Назад"),"R / X",DOK_T("Pomiar","Measure","Messen","Измерить")); break;
        case 5U: DOK_RysujWykres("S11 |Gamma| / dB", "return loss example", DOK_S11Mag, -40.0f, 0.0f, "dB", "MHz"); break;
        case 6U: DOK_RysujWykres(DOK_T("S11 - faza","S11 - phase","S11 - Phase","S11 - фаза"), "phase crosses 0 deg near resonance", DOK_Faza, -180.0f, 180.0f, "deg", "MHz"); break;
        case 7U: DOK_RysujSmith(DOK_T("Smith - rezonans","Smith - resonance","Smith - Resonanz","Смит - резонанс"), 0U); break;
        case 8U: DOK_RysujSmith(DOK_T("Smith - indukcyjny","Smith - inductive","Smith - induktiv","Смит - индуктивный"), 1U); break;
        case 9U: DOK_RysujSmith(DOK_T("Smith - pojemnościowy","Smith - capacitive","Smith - kapazitiv","Смит - емкостный"), 2U); break;
        case 10U: DOK_RysujSmith("Smith - O / S / L", 3U); break;
        default: DOK_RysujSmith(DOK_T("Smith - szeroki skan","Smith - wide sweep","Smith - breiter Sweep","Смит - широкий обзор"), 4U); break;
        }
        return;
    }

    /* 20..29: wpływ ustawień. Każda para pokazuje ten sam obiekt przy innej opcji. */
    if (strona < 30U)
    {
        switch (strona - 20U)
        {
        case 0U: DOK_RysujPorownanie("Z0 = 50 Ohm", "Z0", "50 Ohm", "Z", "50.3 + j0.8 Ohm", "SWR 1.02", 1U, 1U); break;
        case 1U: DOK_RysujPorownanie("Z0 = 75 Ohm", "Z0", "75 Ohm", "Z", "50.3 + j0.8 Ohm", "SWR 1.49", 1U, 1U); break;
        case 2U: DOK_RysujPorownanie(DOK_T("OSL wyłączone","OSL off","OSL aus","OSL выкл."), "RAW", "48.6 + j5.7 Ohm", "SWR", "1.14", DOK_T("Błąd toru pozostaje w wyniku","Fixture error remains","Messpfadfehler bleibt","Ошибка тракта остаётся"), 0U, 1U); break;
        case 3U: DOK_RysujPorownanie(DOK_T("OSL włączone","OSL on","OSL ein","OSL вкл."), "OSL", "50.1 + j0.4 Ohm", "SWR", "1.01", DOK_T("Korekcja przenosi płaszczyznę odniesienia","Correction moves reference plane","Korrektur verschiebt Referenzebene","Коррекция переносит плоскость"), 1U, 1U); break;
        case 4U: DOK_RysujPorownanie(DOK_T("Kabel bez kompensacji","Cable compensation off","Kabelkompensation aus","Компенсация кабеля выкл."), "Z@miernik", "34 + j31 Ohm", "SWR", "2.02", DOK_T("Widać transformację przez kabel","Cable transformation visible","Kabeltransformation sichtbar","Видно влияние кабеля"), 0U, 1U); break;
        case 5U: DOK_RysujPorownanie(DOK_T("Kabel skompensowany","Cable compensation on","Kabelkompensation ein","Компенсация кабеля вкл."), "Z@antena", "49 + j3 Ohm", "SWR", "1.07", DOK_T("Wynik odniesiony do końca kabla","Result at cable end","Ergebnis am Kabelende","Результат на конце кабеля"), 1U, 1U); break;
        case 6U: DOK_RysujPorownanie(DOK_T("Uśrednianie x1","Averaging x1","Mittelung x1","Усреднение x1"), "R", "49.2..52.8", "X", "-2.9..+3.5", DOK_T("Szybciej, większy rozrzut","Faster, more scatter","Schneller, mehr Streuung","Быстрее, больше разброс"), 0U, 0U); break;
        case 7U: DOK_RysujPorownanie(DOK_T("Uśrednianie x8","Averaging x8","Mittelung x8","Усреднение x8"), "R", "50.0..50.6", "X", "-0.4..+0.5", DOK_T("Wolniej, stabilniejszy odczyt","Slower, steadier reading","Langsamer, stabiler","Медленнее, стабильнее"), 1U, 1U); break;
        case 8U: DOK_RysujPorownanie(DOK_T("Kursor automatyczny","Automatic cursor","Automatischer Cursor","Автокурсор"), DOK_T("Pozycja","Position","Position","Позиция"), DOK_T("minimum SWR","SWR minimum","SWR-Minimum","минимум КСВ"), "f", "7.108 MHz", DOK_T("Kursor śledzi minimum","Cursor follows minimum","Cursor folgt Minimum","Курсор следует минимуму"), 1U, 1U); break;
        default: DOK_RysujPorownanie(DOK_T("Kursor ręczny","Manual cursor","Manueller Cursor","Ручной курсор"), DOK_T("Pozycja","Position","Position","Позиция"), DOK_T("wybrana przez użytkownika","user selected","vom Benutzer gewählt","выбрана пользователем"), "f", "7.160 MHz", DOK_T("Odczyt dotyczy wskazanego punktu","Readout is for selected point","Anzeige gilt für gewählten Punkt","Показание для выбранной точки"), 1U, 1U); break;
        }
        return;
    }

    /* 30..39: poprawna kalibracja / weryfikacja. */
    if (strona < 40U)
    {
        switch (strona - 30U)
        {
        case 0U: DOK_RysujKalibracje(DOK_T("Stan kalibracji","Calibration status","Kalibrierstatus","Состояние калибровки"), "OSL + HW", "A / 100 kHz-290 MHz", DOK_T("Kalibracja ważna, kompletna","Calibration valid and complete","Kalibrierung gültig und vollständig","Калибровка действительна"), 1U); break;
        case 1U: DOK_RysujKalibracje(DOK_T("Wzorce OSL","OSL standards","OSL-Standards","Эталоны OSL"), "S / L / O", "5.02 / 50.01 / 501.3 Ohm", DOK_T("Wartości dokładne aktywne","Exact values active","Exakte Werte aktiv","Точные значения активны"), 1U); break;
        case 2U: DOK_RysujKalibracje("OSL SHORT", "SHORT", "5.02 Ohm", "R 5.06 Ohm, X +0.12 Ohm", 1U); break;
        case 3U: DOK_RysujKalibracje("OSL LOAD", "LOAD", "50.01 Ohm", "R 50.08 Ohm, X -0.09 Ohm", 1U); break;
        case 4U: DOK_RysujKalibracje("OSL OPEN", "OPEN", "501.3 Ohm", "R 498.9 Ohm, X +2.1 Ohm", 1U); break;
        case 5U: DOK_RysujKalibracje(DOK_T("Weryfikacja wzorcem","Verification standard","Prüfstandard","Проверочный эталон"), "25 Ohm", "25.00 Ohm", "25.18 + j0.34 Ohm   błąd 0.8%", 1U); break;
        case 6U: DOK_RysujKalibracje(DOK_T("Weryfikacja wzorcem","Verification standard","Prüfstandard","Проверочный эталон"), "50 Ohm", "50.00 Ohm", "50.11 - j0.08 Ohm   SWR 1.00", 1U); break;
        case 7U: DOK_RysujKalibracje(DOK_T("Weryfikacja wzorcem","Verification standard","Prüfstandard","Проверочный эталон"), "75 Ohm", "75.00 Ohm", "74.62 + j0.41 Ohm   błąd 0.7%", 1U); break;
        case 8U: DOK_RysujKalibracje(DOK_T("Weryfikacja wzorcem","Verification standard","Prüfstandard","Проверочный эталон"), "100 Ohm", "100.00 Ohm", "99.24 - j0.77 Ohm   błąd 1.1%", 1U); break;
        default: DOK_RysujKalibracje("S21", "THRU + ATT", "0 / 10.00 dB", DOK_T("Kalibracja S21 gotowa","S21 calibration ready","S21-Kalibrierung fertig","Калибровка S21 готова"), 1U); break;
        }
        return;
    }

    if (strona < 48U) { DOK_RysujWidmoSyntetyczne((uint8_t)(strona - 40U)); return; }
    if (strona < 52U) { DOK_RysujStrojenie((uint8_t)(strona - 48U)); return; }
    if (strona < 56U) { DOK_RysujTDR((uint8_t)(strona - 52U)); return; }
    if (strona < 60U) { DOK_RysujS21((uint8_t)(strona - 56U)); return; }
    if (strona < 63U) { DOK_RysujLC((uint8_t)(strona - 60U)); return; }
    if (strona < 66U) { DOK_RysujKwarc((uint8_t)(strona - 63U)); return; }
    /* Diagnostyka nie ma syntetycznych ekranów. Do instrukcji trafiają tylko
     * zrzuty z rzeczywistego renderera i rzeczywistych stanów urządzenia. */
    if (strona < 69U) { DOK_RysujPliki((uint8_t)(strona - 66U)); return; }
    if (strona < 72U) { DOK_RysujWielePasm((uint8_t)(strona - 69U)); return; }
    if (strona < 74U) { DOK_RysujSzukajF((uint8_t)(strona - 72U)); return; }
    if (strona < 77U) { DOK_RysujGenerator((uint8_t)(strona - 74U)); return; }
    if (strona < 81U) { DOK_RysujCyfrowe((uint8_t)(strona - 77U)); return; }
    DOK_RysujSkanerRF((uint8_t)(strona - 81U));
}
