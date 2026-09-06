/*
  LCR Meater for EU1KY's AA

  by KD8CEC
  kd8cec@gmail.com
  -----------------------------------------------------------------------------
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/

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
#include "dsp.h"
#include "gen.h"
#include "stm32f746xx.h"
#include "oslfile.h"
#include "stm32746g_discovery_lcd.h"
#include "match.h"
#include "num_keypad.h"
#include "screenshot.h"
#include "smith.h"
#include "measurement.h"
#include "panfreq.h"
#include "panvswr2.h"
#include "bitmaps/bitmaps.h"
#include "sdram_heap.h"
#include "komunikaty.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "lc_metrologia.h"
#include "metody_eksperymentalne.h"
#include "pomiar_s11.h"
#include "rejestr_metod.h"
#include "element_rf.h"
#include "q_porownanie.h"
#include "porownanie_modeli_elementu.h"
#include "tryb_interfejsu.h"
#include "wejscia_uzytkownika.h"
#include "kalibracja_meta.h"

#define BACK_COLOR (UI_KolorTlaEkranu())
#define SELECT_FREQ_COLOR LCD_RGB(255, 127, 39)

extern void Sleep(uint32_t ms);
extern void TRACK_Beep(int duration);
//==============================================================================

static uint8_t isMatch = 0;
//static uint32_t meas_maxstep = 500000;





uint8_t LC_Mode = 0; //0 :L, 1 :C
static uint8_t lc_widok = 0U; /* 0: wynik, 1: L/C(f), 2: R/X(f) */
static uint8_t lc_metoda = METODA_ELEMENT_IDEALNA;
static uint8_t lc_porownaj_metody = 0U;
static uint16_t lc_ostatnia_liczba = 0U;
static LCD_BUFOR_KLATKI_t lc_bufor_klatki = {0U, 0U};

static void LC_OtworzPorownanieQ(uint32_t liczba);
static void LC_OtworzWyjasnienieModelu(uint32_t liczba);
static int LC_CzyAnulowanoPomiar(void);
static void LC_RysujEkranOczekiwania(void);

/*
#define LC_STEP1    100000
#define LC_STEP2    500000
#define LC_STEP3   1000000
#define LC_STEP4   5000000
#define LC_STEP5  10000000
#define LC_STEP6  15000000
#define LC_STEP7  30000000
*/

#define LC_STEP1 100000U
#define LC_STEP2 500000U
#define LC_STEP3 2000000U
#define LC_STEP4 7000000U
#define LC_STEP5 15000000U
#define LC_STEP6 30000000U

#define LC_LICZBA_GRANIC 6U
#define LC_LICZBA_ZAKRESOW 5U
#define LC_MAKS_PUNKTOW_ZAKRESU 305U
#define LC_USREDNIENIA_SKANU 3U
#define LC_USREDNIENIA_KONCOWE 7U

static const uint32_t LC_MEASURE_FREQS[LC_LICZBA_GRANIC] = {
    LC_STEP1, LC_STEP2, LC_STEP3, LC_STEP4, LC_STEP5, LC_STEP6};

/*
 * Zakres 100..500 kHz pozostaje dostępny ręcznie, ale automat metrologiczny
 * nie wybiera punktu reprezentatywnego poniżej 500 kHz. Gęstość skanu jest
 * dobrana tak, aby nie tracić kształtu charakterystyki i nie blokować UI.
 */
static const uint32_t LC_KROKI_ZAKRESOW_HZ[LC_LICZBA_ZAKRESOW] = {
    25000U, 50000U, 100000U, 200000U, 250000U};

//int GetLCMeasureFreq1(uint32_t targetIndex)
int GetLCStepFreq(uint32_t targetIndex)
{
    if (targetIndex >= LC_LICZBA_GRANIC)
        return 0;
    return (int)LC_MEASURE_FREQS[targetIndex];
}

//This code from measurement.c
//DrawMeasureLC(rx, findedIndex, MeasureFreq[findedIndex]);
static void LC_FormatujWartosc(float wartosc, char *bufor, size_t rozmiar)
{
    if (!isfinite(wartosc) || wartosc <= 0.0f)
    {
        snprintf(bufor, rozmiar, "---");
        return;
    }

    if (wartosc >= 1000.0f)
        snprintf(bufor, rozmiar, "%.1f", wartosc);
    else if (wartosc >= 100.0f)
        snprintf(bufor, rozmiar, "%.2f", wartosc);
    else
        snprintf(bufor, rozmiar, "%.3f", wartosc);
}


static void DrawMeasureLC(DSP_RX rx, uint32_t czestotliwosc_hz, int znaleziony,
                          float zmiennosc_proc, int pomiar_poprawny,
                          const POMIAR_S11_t *szczegoly)
{
    char tekst[96];
    char liczba[24];
    char tytul[64];
    float wartosc = NAN;
    const float r = crealf(rx);
    const float x = cimagf(rx);
    const float q = (pomiar_poprawny && isfinite(r) && isfinite(x) && fabsf(r) > 0.01f)
                      ? fabsf(x / r) : NAN;
    int model_poprawny = 0;

    (void)szczegoly;

    if (pomiar_poprawny && znaleziony)
    {
        if (LC_Mode == 0)
            model_poprawny = LCM_ObliczIndukcyjnosc_uH(x, czestotliwosc_hz, &wartosc);
        else
            model_poprawny = LCM_ObliczPojemnosc_pF(x, czestotliwosc_hz, &wartosc);
    }

    snprintf(tytul, sizeof(tytul), "%s - %s",
             JEZYK_Tekst(TEKST_MENU_LC),
             JEZYK_Tekst(LC_Mode == 0 ? TEKST_LC_INDUKCYJNOSC : TEKST_LC_POJEMNOSC));
    UI_RysujPasekGorny(tytul, true, false, 0);

    LC_FormatujWartosc(model_poprawny ? wartosc : NAN, liczba, sizeof(liczba));
    UI_RysujPoleLiczboweGlowne(4, 38, 472, 80,
                               JEZYK_Tekst(LC_Mode == 0 ? TEKST_LC_INDUKCYJNOSC
                                                        : TEKST_LC_POJEMNOSC),
                               liczba, LC_Mode == 0 ? "uH" : "pF");

    UI_RysujPanel(4, 122, 472, 94,
                  JEZYK_Wybierz("Wynik pomiaru", "Measurement result",
                                "Messergebnis", "Результат измерения"),
                  UI_STYL_NORMALNY);

    if (czestotliwosc_hz != 0U)
        snprintf(tekst, sizeof(tekst), "%s: %.6f MHz",
                 JEZYK_Tekst(TEKST_CZESTOTLIWOSC),
                 (double)czestotliwosc_hz / 1000000.0);
    else
        snprintf(tekst, sizeof(tekst), "%s: --", JEZYK_Tekst(TEKST_CZESTOTLIWOSC));
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 143, tekst);

    if (pomiar_poprawny && isfinite(r) && isfinite(x))
        snprintf(tekst, sizeof(tekst), "Rs: %.2f Ohm    Xs: %+.2f Ohm", r, x);
    else
        snprintf(tekst, sizeof(tekst), "Rs: --          Xs: --");
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 161, tekst);

    if (isfinite(q))
        snprintf(tekst, sizeof(tekst), "|Xs/Rs|: %.1f", q);
    else
        snprintf(tekst, sizeof(tekst), "|Xs/Rs|: --");
    if (znaleziony && isfinite(zmiennosc_proc))
    {
        const size_t uzyto = strlen(tekst);
        snprintf(tekst + uzyto, sizeof(tekst) - uzyto,
                 JEZYK_Wybierz("    zmienność: %.2f %%", "    variation: %.2f %%",
                               "    Streuung: %.2f %%", "    разброс: %.2f %%"),
                 zmiennosc_proc);
    }
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 179, tekst);

    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Kalibracja: HW %s    OSL %s: %s",
                           "Calibration: HW %s    OSL %s: %s",
                           "Kalibrierung: HW %s    OSL %s: %s",
                           "Калибровка: HW %s    OSL %s: %s"),
             OSL_IsErrCorrLoaded() ? "OK" : "--",
             OSL_GetSelectedName(), OSL_IsSelectedValid() ? "OK" : "--");
    FONT_Write(FONT_FRAN, OSL_IsSelectedValid() ? TextColor : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
               BACK_COLOR, 14, 197, tekst);

    if (!model_poprawny && (pomiar_poprawny || znaleziony))
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BACK_COLOR, 270, 98,
                   JEZYK_Tekst(pomiar_poprawny ? TEKST_LC_BRAK_ZAKRESU_MODELU
                                               : TEKST_LC_BRAK_POMIARU_DSP));
    }
}

static UI_STYL_t LC_StanKalibracji(KAL_META_TYP_t typ, int32_t profil, int zaladowana,
                                      char *tekst, size_t rozmiar)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;

    if (!zaladowana)
    {
        snprintf(tekst, rozmiar, "%s", JEZYK_Tekst(TEKST_STATUS_BRAK));
        return UI_STYL_OSTRZEZENIE;
    }

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));
    if (!KAL_META_Pobierz(typ, profil, &meta))
    {
        snprintf(tekst, rozmiar, "%s", JEZYK_Wybierz("OK bez historii", "OK, no history", "OK, ohne Verlauf", "OK, без истории"));
        return UI_STYL_NORMALNY;
    }

    KAL_META_Ocen(typ, profil, &meta, &ocena);
    if (ocena.uwagi & KAL_META_UWAGA_HW_ZMIENIONE)
    {
        snprintf(tekst, rozmiar, "%s", JEZYK_Wybierz("POWTÓRZ PO HW", "REFRESH AFTER HW", "NACH HW ERNEUERN", "ОБНОВИТЬ ПОСЛЕ HW"));
        return UI_STYL_OSTRZEZENIE;
    }
    if (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                       KAL_META_UWAGA_BRAK_PLIKU |
                       KAL_META_UWAGA_PLIK_ZMIENIONY))
    {
        snprintf(tekst, rozmiar, "%s", JEZYK_Wybierz("DO SPRAWDZENIA", "CHECK", "PRÜFEN", "ПРОВЕРИТЬ"));
        return UI_STYL_OSTRZEZENIE;
    }

    snprintf(tekst, rozmiar, "%s", JEZYK_Tekst(TEKST_STATUS_OK));
    return UI_STYL_AKTYWNY;
}

static void DisplayCalInfo(void)
{
    char stan_hw[28];
    char stan_osl[32];
    char profil[24];
    const int32_t profil_osl = OSL_GetSelected();
    const UI_STYL_t styl_hw = LC_StanKalibracji(KAL_META_HW, -1, OSL_IsErrCorrLoaded(),
                                                 stan_hw, sizeof(stan_hw));
    const UI_STYL_t styl_osl = LC_StanKalibracji(KAL_META_OSL, profil_osl, OSL_IsSelectedValid(),
                                                  stan_osl, sizeof(stan_osl));

    UI_RysujPoleStatusu(278, 42, 194, 52, "HW", stan_hw, styl_hw);

    snprintf(profil, sizeof(profil), JEZYK_Wybierz("OSL %s", "OSL %s", "OSL %s", "OSL %s"),
             OSL_GetSelectedName());
    UI_RysujPoleStatusu(278, 98, 194, 52, profil, stan_osl, styl_osl);

    UI_RysujPoleStatusu(278, 154, 194, 62,
                        JEZYK_Wybierz("Tor pomiarowy", "Measurement path", "Messpfad", "Измерительный тракт"),
                        OSL_IsSelectedValid()
                            ? JEZYK_Wybierz("wspólna OSL S11", "common S11 OSL", "gemeinsame S11-OSL", "общая OSL S11")
                            : JEZYK_Wybierz("RAW po HW - brak OSL", "RAW after HW - no OSL", "RAW nach HW - keine OSL", "RAW после HW - нет OSL"),
                        OSL_IsSelectedValid() ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE);
}

//static uint32_t fx = 14000000ul; //Scan range start frequency, in Hz
//static uint32_t fxkHz;//Scan range start frequency, in kHz
//static BANDSPAN pBs1;

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

/*
 * V2.1-LCUIQ1: ekran główny ma tylko jeden standardowy dolny pasek.
 * Każda funkcja ma własne, stałe miejsce 70 x 45 px. Zakres i dane
 * diagnostyczne są osobnymi ekranami, dlatego wynik nie konkuruje z
 * przyciskami o miejsce na LCD 480 x 272.
 */
#define MENU_EXIT 0
#define MENU_POMIAR 1
#define MENU_LC 2
#define MENU_VIEW 3       /* używany w ekranie „Więcej” */
#define MENU_SNAP 4       /* używany w ekranie „Więcej” */
#define MENU_PDOWN 5      /* historyczne - niewidoczne */
#define MENU_PUP 6        /* historyczne - niewidoczne */
#define MENU_PNAME 7      /* historyczne - niewidoczne */
#define MENU_SHORT 8      /* historyczne - niewidoczne */
#define MENU_LOAD 9       /* historyczne - niewidoczne */
#define MENU_OPEN 10      /* historyczne - niewidoczne */
#define MENU_ZAKRES_POPRZEDNI 11
#define MENU_ZAKRES_DOBIERZ 12
#define MENU_ZAKRES_NASTEPNY 13
#define MENU_ZAKRES_POMOC 14
#define MENU_APPLY 15     /* historyczne - niewidoczne */
#define MENU_KWARC 16
#define MENU_ZAKRES 17
#define MENU_WIECEJ 18

#define LC_KONTROLKI_LICZBA 6U

static UI_KONTROLKA_t lc_kontrolki[LC_KONTROLKI_LICZBA];
static uint8_t lc_kontrolki_gotowe = 0U;

static void LC_FormatujCzestotliwoscKrotko(uint32_t hz, char *bufor, size_t rozmiar)
{
    if (hz < 1000000U)
        snprintf(bufor, rozmiar, "%lu kHz", (unsigned long)(hz / 1000U));
    else if ((hz % 1000000U) == 0U)
        snprintf(bufor, rozmiar, "%lu MHz", (unsigned long)(hz / 1000000U));
    else
        snprintf(bufor, rozmiar, "%.1f MHz", (double)hz / 1000000.0);
}

static void LC_InicjalizujKontrolki(void)
{
    uint8_t i;

    if (lc_kontrolki_gotowe)
        return;

    for (i = 0U; i < LC_KONTROLKI_LICZBA; ++i)
    {
        lc_kontrolki[i].obszar = UI_ObszarPrzyciskuDolnego(i);
        lc_kontrolki[i].rola_tekstu = UI_ROLA_TEKSTU_PRZYCISK;
        lc_kontrolki[i].aktywna = true;
        lc_kontrolki[i].zaznaczona = false;
        lc_kontrolki[i].styl = UI_STYL_NORMALNY;
    }

    lc_kontrolki[0].id = MENU_EXIT;
    lc_kontrolki[0].styl = UI_STYL_POWROT;
    lc_kontrolki[1].id = MENU_POMIAR;
    lc_kontrolki[1].styl = UI_STYL_AKTYWNY;
    lc_kontrolki[2].id = MENU_LC;
    lc_kontrolki[2].styl = UI_STYL_AKCENT;
    lc_kontrolki[3].id = MENU_KWARC;
    lc_kontrolki[3].styl = UI_STYL_AKCENT;
    lc_kontrolki[4].id = MENU_ZAKRES;
    lc_kontrolki[5].id = MENU_WIECEJ;

    lc_kontrolki_gotowe = 1U;
}

static void LC_FormatujZakres(char *bufor, size_t rozmiar)
{
    int indeks = (int)CFG_GetParam(CFG_PARAM_LC_INDEX);
    char od[20];
    char do_[20];
    if (indeks < 0 || indeks >= (int)LC_LICZBA_ZAKRESOW)
        indeks = 0;

    LC_FormatujCzestotliwoscKrotko(LC_MEASURE_FREQS[indeks], od, sizeof(od));
    LC_FormatujCzestotliwoscKrotko(LC_MEASURE_FREQS[indeks + 1U], do_, sizeof(do_));
    snprintf(bufor, rozmiar, "%s %s-%s",
             JEZYK_Wybierz("Zakres:", "Range:", "Bereich:", "Диапазон:"), od, do_);
}

static void LC_PomocZakres(void)
{
    KOMUNIKAT_PokazTekst(
        JEZYK_Wybierz("Jak wybrać zakres L/C", "How to choose the L/C range",
                      "L/C-Bereich wählen", "Как выбрать диапазон L/C"),
        JEZYK_Wybierz(
            "Zakres nie jest pojedynczą częstotliwością. Ręczny pomiar L/C jest dostępny od 100 kHz. „Auto” przeszukuje 500 kHz-30 MHz, ponieważ testy wzorców wykazały, że najniższy fragment pasma nie powinien sam wybierać punktu ani modelu. Automat ocenia stabilność L/C i czułość mostka, preferując |X| bliskie Z0 (zwykle 50 Ohm). Zmienność modelu nie jest niepewnością pomiaru.",
            "The L/C range is not a single frequency. Manual measurement is available from 100 kHz. Auto searches 500 kHz-30 MHz because standard tests showed that the lowest band should not select the point or model by itself. Auto scores model stability and bridge sensitivity, preferring |X| near Z0.",
            "Der L/C-Bereich ist keine einzelne Frequenz. Manuelle Messungen sind ab 100 kHz möglich. Auto durchsucht 500 kHz-30 MHz, weil Messungen an Normalen gezeigt haben, dass der unterste Bereich den Messpunkt oder das Modell nicht automatisch bestimmen soll. Bevorzugt wird |X| nahe Z0.",
            "Диапазон L/C — не одна частота. Ручное измерение доступно от 100 кГц. Auto использует 500 кГц-30 МГц, поскольку испытания на эталонах показали, что нижняя часть диапазона не должна сама выбирать точку или модель. Предпочитается |X| около Z0."));
}

static void lcMenuDraw(void)
{
    uint8_t i;

    LC_InicjalizujKontrolki();
    lc_kontrolki[0].tekst = JEZYK_Tekst(TEKST_WSTECZ);
    lc_kontrolki[1].tekst = JEZYK_Wybierz("Mierz", "Measure", "Messen", "Измерить");
    lc_kontrolki[2].tekst = "L / C";
    lc_kontrolki[3].tekst = JEZYK_Wybierz("Kwarc", "Quartz", "Quarz", "Кварц");
    lc_kontrolki[4].tekst = JEZYK_Wybierz("Zakres", "Range", "Bereich", "Диапазон");
    lc_kontrolki[5].tekst = JEZYK_Wybierz("Więcej", "More", "Mehr", "Ещё");

    for (i = 0U; i < LC_KONTROLKI_LICZBA; ++i)
        UI_RysujKontrolke(&lc_kontrolki[i]);
}

static void LC_OpcjeWindow(void)
{
    LCDPoint pt;
    uint8_t pole = 0U;
    uint8_t odswiez = 1U;
    const bool tryb_zaawansowany = TRYB_CzyZaawansowany();

    if (!tryb_zaawansowany)
    {
        lc_metoda = METODA_ELEMENT_IDEALNA;
        lc_porownaj_metody = 0U;
    }

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        UI_METODA_OPCJA_t metody[METODA_ELEMENT_LICZBA];
        UI_METODA_OPCJA_t widoki[3] = {
            { JEZYK_Wybierz("Wynik", "Result", "Ergebnis", "Результат"), 0, true },
            { LC_Mode == 0 ? "L(f)" : "C(f)", 0, true },
            { "R / X", 0, true },
        };
        uint8_t liczba_metod = 0U;
        uint8_t i;
        const METODA_ELEMENT_REJESTR_t *rejestr = METODA_RejestrElementow(&liczba_metod);
        WEJSCIE_ZDARZENIE_t zdarzenie;

        if (liczba_metod > METODA_ELEMENT_LICZBA)
            liczba_metod = METODA_ELEMENT_LICZBA;
        for (i = 0U; i < liczba_metod; ++i)
        {
            metody[i].nazwa = rejestr[i].opis.nazwa();
            metody[i].opis = rejestr[i].opis.opis();
            metody[i].dostepna = METODA_CzyWidoczna(&rejestr[i].opis, tryb_zaawansowany);
        }
        if (lc_metoda >= liczba_metod)
            lc_metoda = METODA_ELEMENT_IDEALNA;

        if (odswiez)
        {
            char porownaj[48];
            UI_WyczyscEkran();
            UI_RysujNaglowek(JEZYK_Wybierz("L/C - opcje analizy", "L/C - analysis options",
                                            "L/C - Analyseoptionen",
                                            "L/C - параметры анализа"));

            if (tryb_zaawansowany)
            {
                UI_RysujWyborMetody(20, 42, 440, 54,
                                    JEZYK_Wybierz("Metoda obliczeniowa", "Calculation method", "Rechenmethode", "Метод расчёта"),
                                    metody, liczba_metod, lc_metoda);
            }
            else
            {
                UI_RysujPoleInformacyjne(20, 42, 440, 54,
                                          JEZYK_Wybierz("Metoda", "Method", "Methode", "Метод"),
                                          rejestr[METODA_ELEMENT_IDEALNA].opis.nazwa());
            }

            UI_RysujWyborMetody(20, 102, 440, 48,
                                JEZYK_Wybierz("Widok", "View", "Ansicht", "Вид"),
                                widoki, 3U, lc_widok);

            snprintf(porownaj, sizeof(porownaj), "%s: %s",
                     JEZYK_Wybierz("Porównaj modele", "Compare models", "Modelle vergleichen", "Сравнить модели"),
                     lc_porownaj_metody
                         ? JEZYK_Wybierz("TAK", "ON", "EIN", "ДА")
                         : JEZYK_Wybierz("NIE", "OFF", "AUS", "НЕТ"));
            UI_RysujPrzycisk(20, 156, 440, 32, porownaj,
                             tryb_zaawansowany
                                 ? (lc_porownaj_metody ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY)
                                 : UI_STYL_NIEAKTYWNY,
                             FONT_FRAN);
            UI_RysujPrzycisk(20, 190, 214, 26,
                             JEZYK_Wybierz("Wyjaśnij model", "Explain model", "Modell erklären", "Объяснить модель"),
                             (tryb_zaawansowany && lc_ostatnia_liczba >= 3U)
                                 ? (pole == 3U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT)
                                 : UI_STYL_NIEAKTYWNY,
                             FONT_FRAN);
            UI_RysujPrzycisk(246, 190, 214, 26,
                             JEZYK_Wybierz("Porównaj Q", "Compare Q", "Q vergleichen", "Сравнить Q"),
                             (tryb_zaawansowany && lc_ostatnia_liczba >= 7U)
                                 ? (pole == 4U ? UI_STYL_AKTYWNY : UI_STYL_AKCENT)
                                 : UI_STYL_NIEAKTYWNY,
                             FONT_FRAN);
            UI_RysujWsteczDolny(false);
            odswiez = 0U;
        }

        zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            if (tryb_zaawansowany && pole == 2U)
            {
                lc_porownaj_metody ^= 1U;
                pole = 3U;
            }
            else if (tryb_zaawansowany && pole == 3U)
            {
                if (lc_ostatnia_liczba >= 3U)
                    LC_OtworzWyjasnienieModelu(lc_ostatnia_liczba);
                pole = 4U;
            }
            else if (tryb_zaawansowany && pole == 4U)
            {
                if (lc_ostatnia_liczba >= 7U)
                    LC_OtworzPorownanieQ(lc_ostatnia_liczba);
                pole = 0U;
            }
            else if (tryb_zaawansowany)
                pole = (uint8_t)(pole + 1U);
            odswiez = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO || zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            const int8_t kierunek = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO ? 1 : -1;
            if (!tryb_zaawansowany || pole == 1U)
                lc_widok = UI_NastepnaDostepnaMetoda(widoki, 3U, lc_widok, kierunek);
            else if (pole == 0U)
                lc_metoda = UI_NastepnaDostepnaMetoda(metody, liczba_metod, lc_metoda, kierunek);
            else if (pole == 2U)
                lc_porownaj_metody ^= 1U;
            odswiez = 1U;
        }

        if (TOUCH_Poll(&pt))
        {
            if (tryb_zaawansowany && pt.y >= 42U && pt.y < 96U)
                lc_metoda = UI_WyborMetodyPoDotyku(pt, 20, 42, 440, 54, metody, liczba_metod, lc_metoda);
            else if (pt.y >= 102U && pt.y < 150U)
                lc_widok = UI_WyborMetodyPoDotyku(pt, 20, 102, 440, 48, widoki, 3U, lc_widok);
            else if (tryb_zaawansowany && pt.y >= 156U && pt.y < 188U)
                lc_porownaj_metody ^= 1U;
            else if (tryb_zaawansowany && lc_ostatnia_liczba >= 3U &&
                     pt.y >= 190U && pt.y < 216U && pt.x < 240U)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
                LC_OtworzWyjasnienieModelu(lc_ostatnia_liczba);
                odswiez = 1U;
                continue;
            }
            else if (tryb_zaawansowany && lc_ostatnia_liczba >= 7U &&
                     pt.y >= 190U && pt.y < 216U && pt.x >= 240U)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
                LC_OtworzPorownanieQ(lc_ostatnia_liczba);
                odswiez = 1U;
                continue;
            }
            else if (UI_CzyDotknietoWstecz(pt))
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
                break;
            }
            TRACK_Beep(1);
            TOUCH_CzekajNaPuszczenie(20U);
            odswiez = 1U;
        }
        Sleep(10);
    }
}

extern int32_t OSL_ChangeCurrentOSL(int oslIndex);
uint32_t MeasureStartFreq = 0;
uint32_t MeasureEndFreq = 0;

float *MeasureIM;
static float *MeasureRE;
uint32_t *MeasureFreq;
static uint8_t *MeasureValid;

static void LC_UstawZakres(uint32_t indeks, int *indeks_pomiaru, uint32_t *czestotliwosc_hz)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);

    if (indeks >= LC_LICZBA_ZAKRESOW)
        indeks = 0U;

    /* Nie zatrzymujemy sie na zakresie lezacym w calosci ponizej fmin. */
    while (indeks + 1U < LC_LICZBA_ZAKRESOW && LC_MEASURE_FREQS[indeks + 1U] <= fmin)
        ++indeks;

    CFG_SetParam(CFG_PARAM_LC_INDEX, indeks);
    MeasureStartFreq = (uint32_t)GetLCStepFreq(indeks);
    MeasureEndFreq = (uint32_t)GetLCStepFreq(indeks + 1U);
    if (MeasureStartFreq < fmin)
        MeasureStartFreq = fmin;
    if (indeks_pomiaru != NULL)
        *indeks_pomiaru = 0;
    if (czestotliwosc_hz != NULL)
        *czestotliwosc_hz = MeasureStartFreq;
}

static void LC_ZmienZakres(int kierunek, int *indeks_pomiaru, uint32_t *czestotliwosc_hz)
{
    int indeks = (int)CFG_GetParam(CFG_PARAM_LC_INDEX);
    uint32_t proby = 0U;

    do
    {
        indeks += kierunek;
        if (indeks < 0)
            indeks = (int)LC_LICZBA_ZAKRESOW - 1;
        if (indeks >= (int)LC_LICZBA_ZAKRESOW)
            indeks = 0;
        ++proby;
    } while (LC_MEASURE_FREQS[indeks + 1] <= CFG_GetParam(CFG_PARAM_BAND_FMIN) &&
             proby < LC_LICZBA_ZAKRESOW);

    LC_UstawZakres((uint32_t)indeks, indeks_pomiaru, czestotliwosc_hz);
    CFG_Flush();
}


static int LC_CzyHWGotowaDoKalibracji(void)
{
    KAL_META_DANE_t meta;
    KAL_META_OCENA_t ocena;

    if (!OSL_IsErrCorrLoaded())
    {
        KOMUNIKAT_PokazTekst(JEZYK_Wybierz("Najpierw kalibracja HW", "HW calibration first",
                                            "Zuerst HW-Kalibrierung",
                                            "Сначала калибровка HW"),
                              JEZYK_Wybierz("Pomiar L/C korzysta z korekcji HW. Otwórz Ustawienia > Kalibracja, wykonaj HW i wróć do WORK.",
                                            "L/C measurement uses HW correction. Run HW calibration first and return to WORK.",
                                            "Die L/C-Messung nutzt die HW-Korrektur. Zuerst HW ausführen und auf WORK zurückkehren.",
                                            "Измерение L/C использует коррекцию HW. Сначала выполните HW и верните WORK."));
        return 0;
    }

    memset(&meta, 0, sizeof(meta));
    memset(&ocena, 0, sizeof(ocena));
    if (KAL_META_Pobierz(KAL_META_HW, -1, &meta))
    {
        KAL_META_Ocen(KAL_META_HW, -1, &meta, &ocena);
        if (ocena.uwagi & (KAL_META_UWAGA_KONFIGURACJA |
                           KAL_META_UWAGA_BRAK_PLIKU |
                           KAL_META_UWAGA_PLIK_ZMIENIONY))
        {
            KOMUNIKAT_PokazTekst(JEZYK_Wybierz("HW wymaga sprawdzenia", "HW needs checking",
                                                "HW muss geprüft werden",
                                                "HW требует проверки"),
                                  JEZYK_Wybierz("Stan HW nie odpowiada bieżącej konfiguracji. Najpierw otwórz Ustawienia > Kalibracja, aby pomiar L/C nie korzystał z niespójnej podstawy.",
                                                "HW does not match the current configuration. Check the calibration center before L/C measurement.",
                                                "HW passt nicht zur aktuellen Konfiguration. Vor der L/C-Messung das Kalibrierzentrum prüfen.",
                                                "HW не соответствует текущей конфигурации. Проверьте центр калибровки перед измерением L/C."));
            return 0;
        }
    }
    return 1;
}


static void LC_RysujWykresModelu(uint32_t liczba, const LCM_WYNIK_WYBORU_t *wybor)
{
    const uint16_t x0 = 12U, x1 = 266U, y0 = 70U, y1 = 218U;
    float minimum = INFINITY;
    float maksimum = -INFINITY;
    uint32_t i;
    int poprzedni = -1;
    int y_poprzedni = 0;
    char opis[64];
    const LCM_TRYB_t tryb = LC_Mode == 0 ? LCM_TRYB_INDUKCYJNOSC : LCM_TRYB_POJEMNOSC;

    LCD_FillRect(LCD_MakePoint(4, 42), LCD_MakePoint(274, 233), BACK_COLOR);
    LCD_Rectangle(LCD_MakePoint(4, 42), LCD_MakePoint(274, 233), UI_KolorRamki(UI_STYL_NORMALNY));
    snprintf(opis, sizeof(opis), "%s - %s",
             LC_Mode == 0 ? "L(f)" : "C(f)",
             JEZYK_Wybierz("model szeregowy", "series model", "Serienmodell", "послед. модель"));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 12, 48, opis);

    for (i = 0U; i < liczba; ++i)
    {
        float wartosc;
        if (MeasureValid[i] &&
            (tryb == LCM_TRYB_INDUKCYJNOSC
                 ? LCM_ObliczIndukcyjnosc_uH(MeasureIM[i], MeasureFreq[i], &wartosc)
                 : LCM_ObliczPojemnosc_pF(MeasureIM[i], MeasureFreq[i], &wartosc)))
        {
            if (wartosc < minimum) minimum = wartosc;
            if (wartosc > maksimum) maksimum = wartosc;
        }
    }

    if (!isfinite(minimum) || !isfinite(maksimum))
    {
        UI_RysujPanel(20, 100, 238, 58,
                      JEZYK_Wybierz("Brak stabilnych danych dla wybranego modelu.",
                                    "No stable data for the selected model.",
                                    "Keine stabilen Daten für das Modell.",
                                    "Нет стабильных данных для модели."), UI_STYL_OSTRZEZENIE);
        return;
    }

    if (fabsf(maksimum - minimum) < 1.0e-9f)
    {
        const float margines = fabsf(maksimum) * 0.05f + 0.001f;
        minimum -= margines;
        maksimum += margines;
    }

    LCD_Line(LCD_MakePoint(x0, y0), LCD_MakePoint(x0, y1), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    LCD_Line(LCD_MakePoint(x0, y1), LCD_MakePoint(x1, y1), UI_KolorRamki(UI_STYL_NIEAKTYWNY));

    for (i = 0U; i < liczba; ++i)
    {
        float wartosc;
        if (MeasureValid[i] &&
            (tryb == LCM_TRYB_INDUKCYJNOSC
                 ? LCM_ObliczIndukcyjnosc_uH(MeasureIM[i], MeasureFreq[i], &wartosc)
                 : LCM_ObliczPojemnosc_pF(MeasureIM[i], MeasureFreq[i], &wartosc)))
        {
            const int x = (int)x0 + (liczba > 1U ? (int)((i * (uint32_t)(x1 - x0)) / (liczba - 1U)) : 0);
            int y = (int)y1 - (int)(((wartosc - minimum) * (float)(y1 - y0)) / (maksimum - minimum));
            if (y < (int)y0) y = y0;
            if (y > (int)y1) y = y1;
            if (poprzedni >= 0)
                LCD_Line(LCD_MakePoint((uint16_t)poprzedni, (uint16_t)y_poprzedni),
                         LCD_MakePoint((uint16_t)x, (uint16_t)y), UI_KolorRamki(UI_STYL_AKTYWNY));
            poprzedni = x;
            y_poprzedni = y;
        }
        else
        {
            poprzedni = -1;
        }
    }

    if (wybor != NULL && wybor->indeks < liczba)
    {
        const uint16_t x = (uint16_t)(x0 + (liczba > 1U ? (wybor->indeks * (uint32_t)(x1 - x0)) / (liczba - 1U) : 0U));
        LCD_Line(LCD_MakePoint(x, y0), LCD_MakePoint(x, y1), UI_KolorRamki(UI_STYL_AKCENT));
        snprintf(opis, sizeof(opis), "%s %.3f MHz   %s %.2f%%",
                 JEZYK_Wybierz("wybrano", "selected", "gewählt", "выбрано"),
                 (double)wybor->czestotliwosc_hz / 1000000.0,
                 JEZYK_Wybierz("zmienność", "variation", "Streuung", "разброс"),
                 wybor->zmiennosc_proc);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BACK_COLOR, 12, 221, opis);
    }
}

static void LC_RysujWykresRX(uint32_t liczba, const LCM_WYNIK_WYBORU_t *wybor)
{
    const uint16_t x0 = 12U, x1 = 266U, y_srodek = 145U;
    const int polowa = 70;
    float maksimum = 1.0f;
    uint32_t i;
    int poprzedni_r = -1, poprzedni_x = -1;
    int yr_poprzedni = 0, yx_poprzedni = 0;
    char opis[64];

    LCD_FillRect(LCD_MakePoint(4, 42), LCD_MakePoint(274, 233), BACK_COLOR);
    LCD_Rectangle(LCD_MakePoint(4, 42), LCD_MakePoint(274, 233), UI_KolorRamki(UI_STYL_NORMALNY));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 12, 48,
               JEZYK_Wybierz("R(f) i X(f) - zmierzona impedancja", "R(f) and X(f) - measured impedance",
                             "R(f) und X(f) - gemessene Impedanz",
                             "R(f) и X(f) - измеренный импеданс"));

    for (i = 0U; i < liczba; ++i)
    {
        if (!MeasureValid[i]) continue;
        if (isfinite(MeasureRE[i]) && fabsf(MeasureRE[i]) > maksimum) maksimum = fabsf(MeasureRE[i]);
        if (isfinite(MeasureIM[i]) && fabsf(MeasureIM[i]) > maksimum) maksimum = fabsf(MeasureIM[i]);
    }

    LCD_Line(LCD_MakePoint(x0, y_srodek), LCD_MakePoint(x1, y_srodek), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKTYWNY), BACK_COLOR, 12, 61, "R");
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), BACK_COLOR, 34, 61, "X");

    for (i = 0U; i < liczba; ++i)
    {
        const int x = (int)x0 + (liczba > 1U ? (int)((i * (uint32_t)(x1 - x0)) / (liczba - 1U)) : 0);
        if (MeasureValid[i] && isfinite(MeasureRE[i]))
        {
            int y = (int)y_srodek - (int)(MeasureRE[i] / maksimum * (float)polowa);
            if (y < (int)y_srodek - polowa) y = (int)y_srodek - polowa;
            if (y > (int)y_srodek + polowa) y = (int)y_srodek + polowa;
            if (poprzedni_r >= 0)
                LCD_Line(LCD_MakePoint((uint16_t)poprzedni_r, (uint16_t)yr_poprzedni),
                         LCD_MakePoint((uint16_t)x, (uint16_t)y), UI_KolorRamki(UI_STYL_AKTYWNY));
            poprzedni_r = x; yr_poprzedni = y;
        }
        else poprzedni_r = -1;

        if (MeasureValid[i] && isfinite(MeasureIM[i]))
        {
            int y = (int)y_srodek - (int)(MeasureIM[i] / maksimum * (float)polowa);
            if (y < (int)y_srodek - polowa) y = (int)y_srodek - polowa;
            if (y > (int)y_srodek + polowa) y = (int)y_srodek + polowa;
            if (poprzedni_x >= 0)
                LCD_Line(LCD_MakePoint((uint16_t)poprzedni_x, (uint16_t)yx_poprzedni),
                         LCD_MakePoint((uint16_t)x, (uint16_t)y), UI_KolorRamki(UI_STYL_AKCENT));
            poprzedni_x = x; yx_poprzedni = y;
        }
        else poprzedni_x = -1;
    }

    snprintf(opis, sizeof(opis), "%s ±%.1f Ohm",
             JEZYK_Wybierz("skala", "scale", "Skala", "шкала"), maksimum);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 12, 221, opis);

    if (wybor != NULL && wybor->indeks < liczba)
    {
        const uint16_t x = (uint16_t)(x0 + (liczba > 1U ? (wybor->indeks * (uint32_t)(x1 - x0)) / (liczba - 1U) : 0U));
        LCD_Line(LCD_MakePoint(x, 70), LCD_MakePoint(x, 215), UI_KolorTekstu(UI_STYL_OSTRZEZENIE));
    }
}

static POMIAR_S11_t *LC_ZbudujSerieS11(uint32_t liczba, SERIA_S11_t *seria)
{
    POMIAR_S11_t *punkty;
    uint32_t i;

    if (seria == NULL || liczba == 0U || liczba > LC_MAKS_PUNKTOW_ZAKRESU)
        return NULL;

    punkty = (POMIAR_S11_t *)SDRH_try_malloc(sizeof(POMIAR_S11_t) * liczba);
    if (punkty == NULL)
        return NULL;

    memset(punkty, 0, sizeof(POMIAR_S11_t) * liczba);
    for (i = 0U; i < liczba; ++i)
    {
        punkty[i].czestotliwosc_hz = MeasureFreq[i];
        punkty[i].z0_ohm = (float)CFG_GetParam(CFG_PARAM_R0);
        punkty[i].impedancja_ohm = MeasureRE[i] + MeasureIM[i] * I;
        punkty[i].gamma = POMIAR_S11_GammaZImpedancji(punkty[i].impedancja_ohm, punkty[i].z0_ohm);
        if (MeasureValid[i] && isfinite(MeasureRE[i]) && isfinite(MeasureIM[i]) &&
            isfinite(crealf(punkty[i].gamma)) && isfinite(cimagf(punkty[i].gamma)))
            punkty[i].flagi = POMIAR_S11_FLAGA_POPRAWNY | POMIAR_S11_FLAGA_KOREKCJA_HW |
                              POMIAR_S11_FLAGA_KOREKCJA_OSL | POMIAR_S11_FLAGA_TOR_LC;
    }

    seria->punkty = punkty;
    seria->liczba = (uint16_t)liczba;
    return punkty;
}

static int LC_ObliczMetodeZRejestru(uint8_t metoda, uint32_t liczba,
                                      uint16_t indeks_reprezentatywny,
                                      METODA_ELEMENT_WYNIK_t *wynik)
{
    POMIAR_S11_t *punkty;
    SERIA_S11_t seria;
    METODA_ELEMENT_DANE_t dane;
    const METODA_ELEMENT_REJESTR_t *rejestr;
    uint8_t liczba_metod = 0U;
    int ok;

    if (wynik == NULL || liczba == 0U)
        return 0;

    rejestr = METODA_RejestrElementow(&liczba_metod);
    if (rejestr == NULL || metoda >= liczba_metod)
        return 0;

    punkty = LC_ZbudujSerieS11(liczba, &seria);
    if (punkty == NULL)
        return 0;

    dane.seria = &seria;
    dane.indeks_reprezentatywny = indeks_reprezentatywny < seria.liczba ? indeks_reprezentatywny : 0U;
    dane.typ = LC_Mode == 0 ? METODA_ELEMENT_CEWKA : METODA_ELEMENT_KONDENSATOR;
    ok = METODA_ObliczElement(metoda, &dane, wynik) ? 1 : 0;
    SDRH_free(punkty);
    return ok;
}

static void LC_RysujWierszQ(uint16_t y, const char *nazwa,
                            const METODA_Q_WYNIK_t *wynik, int dostepna,
                            int referencyjna)
{
    char qtekst[32];
    char ftekst[40];
    char opis[80];
    UI_STYL_t styl = referencyjna ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY;

    if (!dostepna || wynik == NULL || !wynik->poprawny)
    {
        UI_RysujPoleStatusu(12, y, 456, 38, nazwa,
                            JEZYK_Wybierz("brak wyniku", "no result", "kein Ergebnis", "нет результата"),
                            UI_STYL_NIEAKTYWNY);
        return;
    }

    snprintf(qtekst, sizeof(qtekst), "Q %.1f", (double)wynik->q);
    snprintf(ftekst, sizeof(ftekst), "f0 %.6f MHz", (double)wynik->f0_hz / 1000000.0);
    snprintf(opis, sizeof(opis), "%s   %s", qtekst, ftekst);
    if (wynik->jakosc != METODA_JAKOSC_OK)
        styl = UI_STYL_OSTRZEZENIE;
    UI_RysujPoleStatusu(12, y, 456, 38, nazwa, opis, styl);
}

static void LC_OtworzPorownanieQ(uint32_t liczba)
{
    POMIAR_S11_t *punkty;
    SERIA_S11_t seria;
    Q_POROWNANIE_WYNIK_t porownanie;
    const METODA_Q_REJESTR_t *rejestr;
    uint8_t liczba_metod = 0U;
    char tekst[112];
    LCDPoint punkt;

    punkty = LC_ZbudujSerieS11(liczba, &seria);
    if (punkty == NULL)
    {
        KOMUNIKAT_PokazTekst(JEZYK_Wybierz("Brak pamięci", "Out of memory", "Speichermangel", "Недостаточно памяти"),
                              JEZYK_Wybierz("Nie można przygotować danych do porównania Q.",
                                            "Cannot prepare data for Q comparison.",
                                            "Daten für Q-Vergleich können nicht vorbereitet werden.",
                                            "Не удалось подготовить данные для сравнения Q."));
        return;
    }

    rejestr = METODA_RejestrQ(&liczba_metod);
    memset(&porownanie, 0, sizeof(porownanie));
    (void)Q_POROWNANIE_AnalizujMaska(&seria, CFG_GetParam(CFG_PARAM_MASKA_METOD_Q), &porownanie);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Porównanie dobroci Q", "Q method comparison", "Vergleich der Q-Methoden", "Сравнение методов Q"));

    snprintf(tekst, sizeof(tekst), "%s: %s   |   %s: %u",
             JEZYK_Wybierz("Rezonans", "Resonance", "Resonanz", "Резонанс"),
             Q_POROWNANIE_TekstTopologii(porownanie.topologia),
             JEZYK_Wybierz("te same punkty", "same points", "gleiche Punkte", "те же точки"),
             (unsigned)seria.liczba);
    UI_RysujPolePasywne(12, 38, 456, 34, tekst, FONT_FRAN);

    if (rejestr != NULL && liczba_metod >= METODA_Q_LICZBA)
    {
        LC_RysujWierszQ(74, rejestr[METODA_Q_3DB].opis.nazwa(),
                        &porownanie.metody[METODA_Q_3DB], porownanie.dostepne[METODA_Q_3DB],
                        porownanie.metoda_referencyjna == METODA_Q_3DB);
        LC_RysujWierszQ(108, rejestr[METODA_Q_LORENTZ].opis.nazwa(),
                        &porownanie.metody[METODA_Q_LORENTZ], porownanie.dostepne[METODA_Q_LORENTZ],
                        porownanie.metoda_referencyjna == METODA_Q_LORENTZ);
        LC_RysujWierszQ(142, rejestr[METODA_Q_RLC].opis.nazwa(),
                        &porownanie.metody[METODA_Q_RLC], porownanie.dostepne[METODA_Q_RLC],
                        porownanie.metoda_referencyjna == METODA_Q_RLC);
        LC_RysujWierszQ(176, rejestr[METODA_Q_OKRAG].opis.nazwa(),
                        &porownanie.metody[METODA_Q_OKRAG], porownanie.dostepne[METODA_Q_OKRAG],
                        porownanie.metoda_referencyjna == METODA_Q_OKRAG);
    }

    if (porownanie.zgodnosc == Q_POROWNANIE_ZGODNOSC_SLABA)
        snprintf(tekst, sizeof(tekst), "%s",
                 JEZYK_Wybierz("Q niewiarygodne - metody nie są zgodne",
                               "Q unreliable - methods disagree",
                               "Q unzuverlässig - Methoden uneinig",
                               "Q ненадёжна - методы расходятся"));
    else if (porownanie.liczba_poprawnych >= 2U && isfinite(porownanie.rozrzut_proc))
        snprintf(tekst, sizeof(tekst), "%s: %.1f%%   %s",
                 JEZYK_Wybierz("Rozrzut Q", "Q spread", "Q-Streuung", "Разброс Q"),
                 (double)porownanie.rozrzut_proc,
                 Q_POROWNANIE_TekstZgodnosci(porownanie.zgodnosc));
    else
        snprintf(tekst, sizeof(tekst), "%s", Q_POROWNANIE_TekstZgodnosci(porownanie.zgodnosc));
    /* Podsumowanie pozostaje nad paskiem nawigacji; Wstecz ma systemowy rozmiar. */
    UI_RysujPolePasywne(74, 218, 394, 18, tekst, FONT_FRAN);
    UI_RysujWsteczDolny(false);

    while (TOUCH_IsPressed())
        Sleep(5);
    WEJSCIA_WyczyscZdarzenia();
    for (;;)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_OK)
            break;
        if (TOUCH_Poll(&punkt))
        {
            if (UI_CzyDotknietoWstecz(punkt))
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
                break;
            }
            TOUCH_CzekajNaPuszczenie(20U);
        }
        Sleep(5);
    }

    SDRH_free(punkty);
}

static void LC_RysujWynikModelRLC(uint32_t liczba)
{
    METODA_ELEMENT_WYNIK_t najlepszy;
    char tekst[80];

    UI_RysujPanel(4, 42, 270, 191,
                  JEZYK_Wybierz("Model RLC - eksperymentalny", "RLC model - experimental",
                                "RLC-Modell - experimentell",
                                "Модель RLC - эксперимент"),
                  UI_STYL_AKCENT);

    if (!LC_ObliczMetodeZRejestru(METODA_ELEMENT_RLC, liczba, 0U, &najlepszy))
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BACK_COLOR, 14, 92,
                   JEZYK_Wybierz("Dane nie pasują stabilnie do modelu RLC.",
                                 "Data do not fit a stable RLC model.",
                                 "Die Daten passen nicht stabil zu einem RLC-Modell.",
                                 "Данные не дают устойчивой модели RLC."));
        return;
    }

    snprintf(tekst, sizeof(tekst), "%s: %s",
             JEZYK_Wybierz("Topologia", "Topology", "Topologie", "Топология"),
             najlepszy.model_rlc == MET_MODEL_RLC_SZEREGOWY
                 ? JEZYK_Wybierz("szeregowa", "series", "seriell", "последовательная")
                 : JEZYK_Wybierz("równoległa", "parallel", "parallel", "параллельная"));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 70, tekst);
    snprintf(tekst, sizeof(tekst), "f0: %.6f MHz", (double)najlepszy.f0_hz / 1000000.0);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKCENT), BACK_COLOR, 14, 91, tekst);
    snprintf(tekst, sizeof(tekst), "Q: %.2f    R: %.3f Ohm", najlepszy.q, najlepszy.r_ohm);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 123, tekst);
    snprintf(tekst, sizeof(tekst), "L: %.4f uH", (double)najlepszy.l_h * 1000000.0);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 147, tekst);
    snprintf(tekst, sizeof(tekst), "C: %.4f pF", (double)najlepszy.c_f * 1000000000000.0);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 169, tekst);
    snprintf(tekst, sizeof(tekst), "RMS: %.3f Ohm", najlepszy.blad_rms_ohm);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 14, 194, tekst);
    snprintf(tekst, sizeof(tekst), "%s: %s",
             JEZYK_Wybierz("Ocena", "Quality", "Bewertung", "Оценка"),
             METODA_TekstJakosci(najlepszy.jakosc));
    FONT_Write(FONT_FRAN, najlepszy.jakosc == METODA_JAKOSC_OK
                              ? UI_KolorTekstu(UI_STYL_AKTYWNY)
                              : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
               BACK_COLOR, 14, 214, tekst);
}

static void LC_RysujWynikModelRF(uint32_t liczba, const LCM_WYNIK_WYBORU_t *wybor)
{
    METODA_ELEMENT_WYNIK_t rf;
    char tekst[96];
    const uint16_t indeks = (wybor != NULL && wybor->indeks < liczba) ? (uint16_t)wybor->indeks : 0U;

    UI_RysujPanel(4, 42, 270, 191,
                  JEZYK_Wybierz("Analizator elementu RF", "RF component analyzer",
                                "HF-Bauteilanalyse",
                                "Анализатор ВЧ-компонента"),
                  UI_STYL_AKCENT);

    if (!LC_ObliczMetodeZRejestru(METODA_ELEMENT_RF_PASOZYTNICZY, liczba, indeks, &rf))
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BACK_COLOR, 14, 84,
                   JEZYK_Wybierz("Brak stabilnego modelu pasożytniczego.",
                                 "No stable parasitic model.",
                                 "Kein stabiles Parasitärmodell.",
                                 "Нет устойчивой модели паразитов."));
        return;
    }

    if (LC_Mode == 0)
        snprintf(tekst, sizeof(tekst), "L: %.4f uH", (double)rf.l_h * 1000000.0);
    else
        snprintf(tekst, sizeof(tekst), "C: %.4f pF", (double)rf.c_f * 1000000000000.0);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKTYWNY), BACK_COLOR, 14, 67, tekst);

    if (LC_Mode == 0)
        snprintf(tekst, sizeof(tekst), "Rs: %.3f Ohm    Qmodel@%.2fMHz: %.1f",
                 rf.r_ohm, (double)wybor->czestotliwosc_hz / 1000000.0, rf.q);
    else
        snprintf(tekst, sizeof(tekst), "ESR: %.3f Ohm   Qmodel@%.2fMHz: %.1f",
                 rf.r_ohm, (double)wybor->czestotliwosc_hz / 1000000.0, rf.q);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 99, tekst);

    if (LC_Mode == 0)
        snprintf(tekst, sizeof(tekst), "Cp: %.3f pF", (double)rf.pasozyt_f_lub_h * 1000000000000.0);
    else
        snprintf(tekst, sizeof(tekst), "ESL: %.3f nH", (double)rf.pasozyt_f_lub_h * 1000000000.0);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 14, 121, tekst);

    if (isfinite(rf.f0_hz))
        snprintf(tekst, sizeof(tekst), "SRF model: %.6f MHz", (double)rf.f0_hz / 1000000.0);
    else
        snprintf(tekst, sizeof(tekst), "SRF model: --");
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BACK_COLOR, 14, 143, tekst);

    if (isfinite(rf.srf_obserwowane_hz))
        snprintf(tekst, sizeof(tekst), "%s: %.6f MHz",
                 JEZYK_Wybierz("SRF ze skanu", "SRF from sweep", "SRF aus Sweep", "SRF по скану"),
                 (double)rf.srf_obserwowane_hz / 1000000.0);
    else
        snprintf(tekst, sizeof(tekst), "%s",
                 JEZYK_Wybierz("SRF poza bieżącym zakresem", "SRF outside current sweep",
                               "SRF außerhalb des Sweeps",
                               "SRF вне текущего диапазона"));
    FONT_Write(FONT_FRAN, isfinite(rf.srf_obserwowane_hz)
                              ? UI_KolorTekstu(UI_STYL_NORMALNY)
                              : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
               BACK_COLOR, 14, 165, tekst);

    snprintf(tekst, sizeof(tekst), "RMS: %.3f Ohm", rf.blad_rms_ohm);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 14, 187, tekst);
    snprintf(tekst, sizeof(tekst), "%s: %s",
             JEZYK_Wybierz("Ocena", "Quality", "Bewertung", "Оценка"),
             METODA_TekstJakosci(rf.jakosc));
    FONT_Write(FONT_FRAN, rf.jakosc == METODA_JAKOSC_OK
                              ? UI_KolorTekstu(UI_STYL_AKTYWNY)
                              : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
               BACK_COLOR, 14, 209, tekst);
}

static const char *LC_NazwaModeluPorownania(POR_MODELI_ID_t id)
{
    switch (id)
    {
    case POR_MODELI_IDEALNY:
        return JEZYK_Wybierz("Idealny", "Ideal", "Ideal", "Идеальный");
    case POR_MODELI_RLC:
        return "RLC";
    case POR_MODELI_RF_PASOZYTNICZY:
        return JEZYK_Wybierz("RF pasoż.", "RF parasitic", "HF-Parasit.", "ВЧ паразит.");
    case POR_MODELI_CAUER_1:
        return "Cauer 1";
    case POR_MODELI_CAUER_3:
        return "Cauer 3";
    default:
        return "---";
    }
}

static UI_STYL_t LC_StylModeluPorownania(const POR_MODELI_POROWNANIE_t *porownanie,
                                         const POR_MODELI_WYNIK_t *model)
{
    if (porownanie == NULL || model == NULL || !model->obliczony)
        return UI_STYL_NIEAKTYWNY;
    if (porownanie->ma_najlepszy && porownanie->najlepszy == model->id)
        return UI_STYL_AKTYWNY;
    if (!model->zaakceptowany || model->jakosc != POR_MODELI_JAKOSC_OK)
        return UI_STYL_OSTRZEZENIE;
    return UI_STYL_NORMALNY;
}

static void LC_RysujWierszModelu(uint16_t y,
                                 const POR_MODELI_POROWNANIE_t *porownanie,
                                 const POR_MODELI_WYNIK_t *model)
{
    const uint16_t x = 10U;
    const uint16_t szerokosc = 258U;
    const uint16_t wysokosc = 28U;
    const UI_STYL_t styl = LC_StylModeluPorownania(porownanie, model);
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor ramka = UI_KolorRamki(styl);
    const char *nazwa = LC_NazwaModeluPorownania(model != NULL ? model->id : POR_MODELI_IDEALNY);
    char wynik[80];

    LCD_FillRect(LCD_MakePoint(x, y),
                 LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), tlo);
    LCD_Rectangle(LCD_MakePoint(x, y),
                  LCD_MakePoint((uint16_t)(x + szerokosc - 1U), (uint16_t)(y + wysokosc - 1U)), ramka);
    LCD_FillRect(LCD_MakePoint((uint16_t)(x + 1U), (uint16_t)(y + 1U)),
                 LCD_MakePoint((uint16_t)(x + 4U), (uint16_t)(y + wysokosc - 2U)), ramka);

    if (model == NULL || !model->obliczony)
    {
        snprintf(wynik, sizeof(wynik), "%s",
                 (model != NULL && (model->jakosc & POR_MODELI_JAKOSC_NIEOBSLUGIWANY) != 0U)
                     ? JEZYK_Wybierz("nie dotyczy", "not applicable", "nicht anwendbar", "не применимо")
                     : JEZYK_Wybierz("brak wyniku", "no result", "kein Ergebnis", "нет результата"));
    }
    else if (isfinite(model->delta_aicc))
    {
        snprintf(wynik, sizeof(wynik), "e %.2f%%  dA %.1f",
                 model->blad_wzgledny * 100.0, model->delta_aicc);
    }
    else
    {
        snprintf(wynik, sizeof(wynik), "e %.2f%%",
                 model->blad_wzgledny * 100.0);
    }

    if (porownanie != NULL && porownanie->ma_najlepszy && model != NULL &&
        porownanie->najlepszy == model->id)
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKTYWNY), tlo, 16, (uint16_t)(y + 5U), ">");

    FONT_Write(FONT_FRAN, UI_KolorTekstu(styl), tlo, 30, (uint16_t)(y + 6U), nazwa);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(styl), tlo, 132, (uint16_t)(y + 6U), wynik);
}

static void LC_RysujPorownanieModeli(uint32_t liczba,
                                     const LCM_WYNIK_WYBORU_t *wybor,
                                     int pomiar_poprawny)
{
    POMIAR_S11_t *punkty = NULL;
    SERIA_S11_t seria;
    POR_MODELI_POROWNANIE_t porownanie;
    uint8_t i;
    const uint16_t y0 = 64U;
    const uint16_t krok = 30U;
    bool ok = false;

    memset(&seria, 0, sizeof(seria));
    memset(&porownanie, 0, sizeof(porownanie));

    UI_RysujPanel(4, 42, 270, 191,
                  JEZYK_Wybierz("Porównanie modeli", "Model comparison", "Modellvergleich", "Сравнение моделей"),
                  UI_STYL_AKCENT);

    if (pomiar_poprawny && wybor != NULL && wybor->indeks < liczba)
    {
        punkty = LC_ZbudujSerieS11(liczba, &seria);
        if (punkty != NULL)
        {
            ok = POR_MODELI_PorownajMaska(&seria,
                                          LC_Mode == 0 ? METODA_ELEMENT_CEWKA : METODA_ELEMENT_KONDENSATOR,
                                          (uint16_t)wybor->indeks,
                                          CFG_GetParam(CFG_PARAM_MASKA_MODELI_RF),
                                          &porownanie);
        }
    }

    if (punkty == NULL)
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BACK_COLOR, 14, 92,
                   JEZYK_Wybierz("Nie można przygotować porównania modeli.",
                                 "Cannot prepare model comparison.",
                                 "Modellvergleich nicht verfügbar.",
                                 "Невозможно сравнить модели."));
        return;
    }

    for (i = 0U; i < POR_MODELI_LICZBA; ++i)
        LC_RysujWierszModelu((uint16_t)(y0 + (uint16_t)i * krok), &porownanie, &porownanie.model[i]);

    FONT_Write(FONT_FRAN, UI_KolorTekstu(ok ? UI_STYL_NIEAKTYWNY : UI_STYL_OSTRZEZENIE),
               BACK_COLOR, 12, 216,
               ok ? JEZYK_Wybierz("AICc: ranking modeli, nie dokładność pomiaru",
                                   "AICc: model ranking, not measurement accuracy",
                                   "AICc: Modellrang, nicht Messgenauigkeit",
                                   "AICc: рейтинг моделей, не точность")
                  : JEZYK_Wybierz("Brak modelu spełniającego kryteria jakości",
                                   "No model meets the quality criteria",
                                   "Kein Modell erfüllt die Qualitätskriterien",
                                   "Нет модели, прошедшей критерии качества"));

    SDRH_free(punkty);
}

static const char *LC_OpisModelu(POR_MODELI_ID_t id)
{
    switch (id)
    {
    case POR_MODELI_IDEALNY:
        return JEZYK_Wybierz(
            "Najprostszy model R+L lub R+C. Dobry z dala od rezonansu; nie opisuje pasożytów ani wielu rezonansów.",
            "Simplest R+L or R+C model. Good away from resonance; it does not describe parasitics or multiple resonances.",
            "Einfachstes R+L- bzw. R+C-Modell. Gut fern der Resonanz; Parasiten und mehrere Resonanzen fehlen.",
            "Простейшая модель R+L или R+C. Хороша вдали от резонанса; не учитывает паразитные элементы и несколько резонансов.");
    case POR_MODELI_RLC:
        return JEZYK_Wybierz(
            "Model jednego rezonansu z R, L i C. Przydatny, gdy charakterystyka ma jeden dominujący rezonans.",
            "Single-resonance RLC model. Useful when one resonance dominates the measured response.",
            "RLC-Modell einer einzelnen Resonanz. Sinnvoll bei einer dominierenden Resonanz.",
            "RLC-модель одного резонанса. Полезна, когда в измерении доминирует один резонанс.");
    case POR_MODELI_RF_PASOZYTNICZY:
        return JEZYK_Wybierz(
            "Model RF uwzględnia straty i pasożyty: dla cewki Cp, dla kondensatora ESL. Najbardziej fizyczny dla typowego elementu RF.",
            "RF model includes losses and parasitics: Cp for an inductor, ESL for a capacitor. Often the most physical model of a real RF part.",
            "HF-Modell mit Verlusten und Parasiten: Cp bei Spulen, ESL bei Kondensatoren. Oft das physikalischste Modell realer HF-Bauteile.",
            "ВЧ-модель учитывает потери и паразитные параметры: Cp катушки и ESL конденсатора. Часто наиболее физична для реального ВЧ-элемента.");
    case POR_MODELI_CAUER_1:
        return JEZYK_Wybierz(
            "Bezstratna postać Cauer I z jednym elementem. Jest modelem reaktancji; nie opisuje rezystancji strat.",
            "Lossless one-element Cauer I form. It models reactance and does not describe resistive losses.",
            "Verlustlose Cauer-I-Form mit einem Element. Sie modelliert die Reaktanz, nicht die Verlustwiderstände.",
            "Без потерь, форма Cauer I с одним элементом. Описывает реактивность, но не резистивные потери.");
    case POR_MODELI_CAUER_3:
        return JEZYK_Wybierz(
            "Bezstratna drabinka L-C-L. Może opisać bardziej złożoną reaktancję, lecz większa złożoność ma sens tylko przy wyraźnie lepszym dopasowaniu.",
            "Lossless L-C-L ladder. It can describe more complex reactance, but the extra complexity is justified only by clearly better fit.",
            "Verlustlose L-C-L-Leiter. Sie beschreibt komplexere Reaktanz; die Mehrkomplexität ist nur bei deutlich besserer Anpassung sinnvoll.",
            "Без потерь, лестница L-C-L. Может описывать более сложную реактивность, но усложнение оправдано только заметно лучшим совпадением.");
    default:
        return JEZYK_Wybierz("Brak opisu modelu.", "No model description.",
                             "Keine Modellbeschreibung.", "Нет описания модели.");
    }
}

static void LC_RysujWyjasnienieModelu(const POR_MODELI_POROWNANIE_t *porownanie)
{
    const POR_MODELI_WYNIK_t *model;
    UI_STYL_t styl;
    char blad[72];
    char ranking[72];

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Wybierz("Dlaczego ten model?", "Why this model?",
                                    "Warum dieses Modell?", "Почему эта модель?"));

    if (porownanie == NULL || !porownanie->ma_najlepszy)
    {
        UI_RysujPoleStatusu(16, 54, 448, 110,
                            JEZYK_Wybierz("Rekomendacja", "Recommendation", "Empfehlung", "Рекомендация"),
                            JEZYK_Wybierz("Brak modelu spełniającego kryteria jakości.",
                                          "No model meets the quality criteria.",
                                          "Kein Modell erfüllt die Qualitätskriterien.",
                                          "Нет модели, прошедшей критерии качества."),
                            UI_STYL_OSTRZEZENIE);
        UI_RysujWsteczDolny(false);
        return;
    }

    model = &porownanie->model[porownanie->najlepszy];
    styl = model->jakosc == POR_MODELI_JAKOSC_OK ? UI_STYL_AKTYWNY : UI_STYL_OSTRZEZENIE;

    UI_RysujPoleStatusu(16, 42, 448, 46,
                        JEZYK_Wybierz("Rekomendacja", "Recommendation", "Empfehlung", "Рекомендация"),
                        LC_NazwaModeluPorownania(model->id), styl);

    snprintf(blad, sizeof(blad), "RMS %.3f Ohm   e %.2f%%",
             model->blad_rms_ohm, model->blad_wzgledny * 100.0);
    if (isfinite(model->delta_aicc))
        snprintf(ranking, sizeof(ranking), "dA %.2f   k=%u",
                 model->delta_aicc, (unsigned)model->liczba_parametrow);
    else
        snprintf(ranking, sizeof(ranking), "dA ---   k=%u", (unsigned)model->liczba_parametrow);

    UI_RysujPoleStatusu(16, 94, 214, 44,
                        JEZYK_Wybierz("Błąd dopasowania", "Fit error", "Anpassungsfehler", "Ошибка модели"),
                        blad, styl);
    UI_RysujPoleStatusu(250, 94, 214, 44,
                        JEZYK_Wybierz("Ranking AICc", "AICc ranking", "AICc-Rang", "Рейтинг AICc"),
                        ranking, UI_STYL_NORMALNY);
    UI_RysujPoleStatusu(16, 146, 448, 68,
                        JEZYK_Wybierz("Znaczenie i ograniczenia", "Meaning and limits",
                                      "Bedeutung und Grenzen", "Смысл и ограничения"),
                        LC_OpisModelu(model->id), styl);
    UI_RysujWsteczDolny(false);
}

static void LC_OtworzWyjasnienieModelu(uint32_t liczba)
{
    LCM_WYNIK_WYBORU_t wybor;
    const LCM_TRYB_t tryb = LC_Mode == 0 ? LCM_TRYB_INDUKCYJNOSC : LCM_TRYB_POJEMNOSC;
    POMIAR_S11_t *punkty = NULL;
    SERIA_S11_t seria;
    POR_MODELI_POROWNANIE_t porownanie;
    LCDPoint pt;

    memset(&seria, 0, sizeof(seria));
    memset(&porownanie, 0, sizeof(porownanie));

    if (liczba >= 3U && LCM_WybierzPunkt(MeasureIM, MeasureFreq, MeasureValid,
                                        liczba, tryb, &wybor))
    {
        punkty = LC_ZbudujSerieS11(liczba, &seria);
        if (punkty != NULL)
            (void)POR_MODELI_PorownajMaska(&seria,
                                           LC_Mode == 0 ? METODA_ELEMENT_CEWKA : METODA_ELEMENT_KONDENSATOR,
                                           (uint16_t)wybor.indeks,
                                           CFG_GetParam(CFG_PARAM_MASKA_MODELI_RF),
                                           &porownanie);
    }

    LC_RysujWyjasnienieModelu(punkty != NULL ? &porownanie : NULL);
    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_OK)
            break;
        if (TOUCH_Poll(&pt) && UI_CzyDotknietoWstecz(pt))
        {
            TRACK_Beep(1);
            TOUCH_CzekajNaPuszczenie(20U);
            break;
        }
        Sleep(10);
    }

    if (punkty != NULL)
        SDRH_free(punkty);
}

static void LC_RysujWynikLubWykres(DSP_RX rx, uint32_t liczba,
                                    const LCM_WYNIK_WYBORU_t *wybor,
                                    int znaleziony, int pomiar_poprawny,
                                    const POMIAR_S11_t *szczegoly)
{
    if (!znaleziony || wybor == NULL)
    {
        DrawMeasureLC(NAN + NAN * I, 0U, 0, NAN, 0, NULL);
        return;
    }

    if (lc_widok == 1U)
        LC_RysujWykresModelu(liczba, wybor);
    else if (lc_widok == 2U)
        LC_RysujWykresRX(liczba, wybor);
    else if (lc_porownaj_metody)
        LC_RysujPorownanieModeli(liczba, wybor, pomiar_poprawny);
    else if (lc_metoda == METODA_ELEMENT_RF_PASOZYTNICZY)
        LC_RysujWynikModelRF(liczba, wybor);
    else if (lc_metoda == METODA_ELEMENT_RLC)
        LC_RysujWynikModelRLC(liczba);
    else
        DrawMeasureLC(rx, wybor->czestotliwosc_hz, 1, wybor->zmiennosc_proc, pomiar_poprawny, szczegoly);
}

static void LC_RysujPostepDoboru(uint32_t procent)
{
    char tekst[64];
    const uint16_t szerokosc = (uint16_t)((360U * (procent > 100U ? 100U : procent)) / 100U);

    UI_RysujPanel(42, 86, 396, 92,
                  JEZYK_Wybierz("Automatyczny dobór zakresu 500 kHz-30 MHz", "Automatic range selection 500 kHz-30 MHz",
                                "Automatische Bereichswahl 500 kHz-30 MHz",
                                "Автовыбор диапазона 500 кГц-30 МГц"), UI_STYL_AKCENT);
    LCD_FillRect(LCD_MakePoint(60, 137), LCD_MakePoint(420, 157), UI_KolorTlaPola());
    if (szerokosc > 0U)
        LCD_FillRect(LCD_MakePoint(60, 137), LCD_MakePoint((uint16_t)(60U + szerokosc), 157), UI_KolorRamki(UI_STYL_AKCENT));
    snprintf(tekst, sizeof(tekst), "%lu%%", (unsigned long)procent);
    FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NORMALNY), BACK_COLOR, 216, 111, tekst);
}

static int LC_CzyPodstawaPomiaruGotowa(void)
{
    if (!LC_CzyHWGotowaDoKalibracji())
        return 0;

    if (!OSL_IsSelectedValid())
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Brak wspólnej OSL", "Common OSL missing",
                          "Gemeinsame OSL fehlt", "Нет общей OSL"),
            JEZYK_Wybierz(
                "L/C korzysta teraz z tej samej kalibracji OSL co pomiar S11. Wykonaj zwykłą OSL w płaszczyźnie, w której podłączasz element. W tej konstrukcji użyj tych samych trzech znanych rezystancji co w OSL (typowo około 5 / 50 / 500 Ohm, najlepiej z wpisanymi wartościami z omomierza), podłączanych między zacisk sygnałowy i masę uchwytu BNC.",
                "L/C now uses the same OSL as S11. Run the normal OSL at the DUT reference plane, using the same three known resistance standards configured for OSL (typically about 5 / 50 / 500 ohm, preferably with measured values entered), connected across the BNC fixture pads.",
                "L/C verwendet jetzt dieselbe OSL wie S11. Die normale OSL an der DUT-Ebene mit denselben drei bekannten Widerständen ausführen (typisch etwa 5 / 50 / 500 Ohm, vorzugsweise mit gemessenen Werten).",
                "L/C теперь использует ту же OSL, что и S11. Выполните обычную OSL в плоскости DUT с теми же тремя известными сопротивлениями (обычно около 5 / 50 / 500 Ом, лучше с введёнными измеренными значениями)."));
        return 0;
    }
    return 1;
}

static uint32_t LC_KrokAutomatuHz(uint32_t czestotliwosc_hz)
{
    uint32_t zakres;

    for (zakres = 0U; zakres < LC_LICZBA_ZAKRESOW; ++zakres)
    {
        if (czestotliwosc_hz < LC_MEASURE_FREQS[zakres + 1U])
            return LC_KROKI_ZAKRESOW_HZ[zakres];
    }
    return LC_KROKI_ZAKRESOW_HZ[LC_LICZBA_ZAKRESOW - 1U];
}

static uint32_t LC_ZakresDlaCzestotliwosci(uint32_t czestotliwosc_hz)
{
    uint32_t zakres;

    for (zakres = 0U; zakres < LC_LICZBA_ZAKRESOW; ++zakres)
    {
        if (czestotliwosc_hz < LC_MEASURE_FREQS[zakres + 1U])
            return zakres;
    }
    return LC_LICZBA_ZAKRESOW - 1U;
}

static uint32_t LC_PoliczPunktyAutomatu(uint32_t fmin_hz, uint32_t fmax_hz)
{
    uint32_t f = fmin_hz;
    uint32_t liczba = 0U;

    while (f < fmax_hz && liczba < LC_MAKS_PUNKTOW_ZAKRESU)
    {
        const uint32_t krok = LC_KrokAutomatuHz(f);
        ++liczba;
        if (UINT32_MAX - f < krok)
            break;
        f += krok;
    }
    return liczba;
}

static int LC_DobierzZakresAutomatycznie(int *indeks_pomiaru, uint32_t *czestotliwosc_hz)
{
    uint32_t f;
    uint32_t liczba = 0U;
    uint32_t numer = 0U;
    uint32_t wybrany_zakres;
    uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    uint32_t razem;
    LCM_WYNIK_WYBORU_t wynik;
    const LCM_TRYB_t tryb = LC_Mode == 0 ? LCM_TRYB_INDUKCYJNOSC : LCM_TRYB_POJEMNOSC;
    char komunikat[64];
    char zakres_od[20];
    char zakres_do[20];

    if (!LC_CzyPodstawaPomiaruGotowa())
        return 0;

    /* Automat nie traci czasu na obszar 100..500 kHz, który pozostaje
     * dostępny do ręcznego pomiaru, lecz nie jest źródłem decyzji
     * metrologicznych po wynikach kampanii V2.1. */
    if (fmin < 500000U)
        fmin = 500000U;
    if (fmax > LC_STEP6)
        fmax = LC_STEP6;
    if (fmax <= fmin)
        return 0;

    razem = LC_PoliczPunktyAutomatu(fmin, fmax);
    if (razem < 3U)
        return 0;

    memset(MeasureValid, 0, sizeof(uint8_t) * LC_MAKS_PUNKTOW_ZAKRESU);

    for (f = fmin; f < fmax && liczba < LC_MAKS_PUNKTOW_ZAKRESU; )
    {
        POMIAR_S11_t pomiar;
        const uint32_t krok = LC_KrokAutomatuHz(f);
        const POMIAR_S11_USTAWIENIA_t ustawienia = {
            .tor = POMIAR_S11_TOR_LC,
            .liczba_usrednien = LC_USREDNIENIA_SKANU,
            .korekcja_hw = true,
            .korekcja_osl = true,
            .kompensacja_portu = false};
        const bool poprawny = POMIAR_S11_PobierzPunkt(f, &ustawienia, &pomiar);

        MeasureFreq[liczba] = f;
        if (poprawny)
        {
            MeasureRE[liczba] = crealf(pomiar.impedancja_ohm);
            MeasureIM[liczba] = cimagf(pomiar.impedancja_ohm);
            MeasureValid[liczba] = 1U;
        }
        else
        {
            MeasureRE[liczba] = NAN;
            MeasureIM[liczba] = NAN;
            MeasureValid[liczba] = 0U;
        }

        ++liczba;
        ++numer;
        if (numer == 1U || numer == razem || (numer % 4U) == 0U)
            LC_RysujPostepDoboru((100U * numer) / razem);

        if (LC_CzyAnulowanoPomiar())
        {
            GEN_SetMeasurementFreq(0U);
            LC_RysujEkranOczekiwania();
            return 0;
        }

        Sleep(0U);
        if (UINT32_MAX - f < krok)
            break;
        f += krok;
    }

    GEN_SetMeasurementFreq(0U);
    LC_RysujPostepDoboru(100U);

    if (!LCM_WybierzPunktAutomatyczny(MeasureIM, MeasureFreq, MeasureValid, liczba,
                                       tryb, (float)CFG_GetParam(CFG_PARAM_R0), &wynik))
    {
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Nie znaleziono dobrego zakresu", "No suitable range found",
                          "Kein geeigneter Bereich gefunden", "Подходящий диапазон не найден"),
            JEZYK_Wybierz(
                "Automat nie znalazł co najmniej trzech kolejnych wiarygodnych punktów zgodnych z wybranym modelem L/C. Sprawdź typ elementu, OSL i połączenia. Automat nie używa do doboru punktów poniżej 500 kHz.",
                "Auto range found no run of at least three reliable points matching the selected L/C model. Check the part type, OSL and connections. Auto selection does not use points below 500 kHz.",
                "Die Automatik fand keine mindestens drei zuverlässigen Punkte des gewählten L/C-Modells. Bauteiltyp, OSL und Anschlüsse prüfen. Die Automatik verwendet keine Punkte unter 500 kHz.",
                "Автовыбор не нашёл минимум трёх надёжных точек выбранной модели L/C. Проверьте тип элемента, OSL и соединения. Автовыбор не использует точки ниже 500 кГц."));
        return 0;
    }

    wybrany_zakres = LC_ZakresDlaCzestotliwosci(wynik.czestotliwosc_hz);
    LC_UstawZakres(wybrany_zakres, indeks_pomiaru, czestotliwosc_hz);
    CFG_Flush();

    LC_FormatujCzestotliwoscKrotko(LC_MEASURE_FREQS[wybrany_zakres], zakres_od, sizeof(zakres_od));
    LC_FormatujCzestotliwoscKrotko(LC_MEASURE_FREQS[wybrany_zakres + 1U], zakres_do, sizeof(zakres_do));
    snprintf(komunikat, sizeof(komunikat), "%s-%s", zakres_od, zakres_do);

    {
        char opis[256];
        snprintf(opis, sizeof(opis),
                 JEZYK_Wybierz(
                     "Wybrano %s. Najlepszy punkt zgrubnego skanu: %.3f MHz, X=%+.1f Ohm, zmienność modelu %.2f%%. Automat preferuje stabilny model i |X| bliskie Z0, a nie po prostu zakres z największą liczbą próbek.",
                     "Selected %s. Best coarse-scan point: %.3f MHz, X=%+.1f Ohm, model variation %.2f%%. Auto range prefers a stable model and |X| near Z0 rather than the range with the most samples.",
                     "%s gewählt. Bester Grobscan-Punkt: %.3f MHz, X=%+.1f Ohm, Modellstreuung %.2f%%. Die Automatik bevorzugt ein stabiles Modell und |X| nahe Z0.",
                     "Выбран %s. Лучшая точка грубого скана: %.3f МГц, X=%+.1f Ом, разброс модели %.2f%%. Автоматика предпочитает устойчивую модель и |X| около Z0."),
                 komunikat,
                 (double)wynik.czestotliwosc_hz / 1000000.0,
                 wynik.reaktancja_ohm,
                 wynik.zmiennosc_proc);
        KOMUNIKAT_PokazTekst(
            JEZYK_Wybierz("Zakres dobrany", "Range selected", "Bereich gewählt", "Диапазон выбран"),
            opis);
    }
    return 1;
}


typedef enum
{
    LC_WIECEJ_AKCJA_SZCZEGOLY = 30,
    LC_WIECEJ_AKCJA_OPCJE,
    LC_WIECEJ_AKCJA_ZRZUT
} LC_WIECEJ_AKCJA_t;

static void LC_RysujZakresEkran(void)
{
    const uint32_t indeks = CFG_GetParam(CFG_PARAM_LC_INDEX) < LC_LICZBA_ZAKRESOW
                               ? CFG_GetParam(CFG_PARAM_LC_INDEX) : 0U;
    const UI_AKCJA_t akcje[5] =
    {
        { MENU_ZAKRES_POPRZEDNI, "<", UI_STYL_NORMALNY, true, false },
        { MENU_ZAKRES_DOBIERZ, "Auto", UI_STYL_AKCENT, true, false },
        { MENU_ZAKRES_NASTEPNY, ">", UI_STYL_NORMALNY, true, false },
        { MENU_ZAKRES_POMOC, JEZYK_Wybierz("Pomoc", "Help", "Hilfe", "Помощь"),
          UI_STYL_NORMALNY, true, false },
        { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
    };
    char zakres[64];
    char tekst[96];
    char krok[24];

    UI_WyczyscEkran();
    UI_RysujPasekGorny(
        JEZYK_Wybierz("Elementy RF - zakres", "RF components - range",
                      "HF-Bauteile - Bereich", "ВЧ-элементы - диапазон"),
        true, false, 0);

    LC_FormatujZakres(zakres, sizeof(zakres));
    UI_RysujPanel(24, 52, 432, 146,
                  JEZYK_Wybierz("Zakres pomiaru L/C", "L/C measurement range",
                                "L/C-Messbereich", "Диапазон измерения L/C"),
                  UI_STYL_NORMALNY);
    FONT_Write(FONT_FRANBIG, TextColor, BACK_COLOR, 54, 82, zakres);

    LC_FormatujCzestotliwoscKrotko(LC_KROKI_ZAKRESOW_HZ[indeks], krok, sizeof(krok));
    snprintf(tekst, sizeof(tekst),
             JEZYK_Wybierz("Krok skanu: %s", "Scan step: %s",
                           "Scan-Schritt: %s", "Шаг сканирования: %s"), krok);
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 54, 116, tekst);

    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 54, 143,
               JEZYK_Wybierz("Auto wybiera pasmo z najlepszą stabilnością i |X| bliskim Z0.",
                             "Auto chooses the band with best stability and |X| near Z0.",
                             "Auto wählt den stabilsten Bereich mit |X| nahe Z0.",
                             "Auto выбирает устойчивый диапазон с |X| около Z0."));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 54, 166,
               JEZYK_Wybierz("Enkoder: poprzedni / następny zakres.",
                             "Encoder: previous / next range.",
                             "Encoder: vorheriger / nächster Bereich.",
                             "Энкодер: предыдущий / следующий диапазон."));

    UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 5U);
}

static int LC_ZakresWindow(int *indeks_pomiaru, uint32_t *czestotliwosc_hz)
{
    int zmieniono = 0;

    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();
    LC_RysujZakresEkran();

    for (;;)
    {
        LCDPoint punkt;
        int16_t akcja = -1;
        const UI_AKCJA_t akcje[5] =
        {
            { MENU_ZAKRES_POPRZEDNI, "<", UI_STYL_NORMALNY, true, false },
            { MENU_ZAKRES_DOBIERZ, "Auto", UI_STYL_AKCENT, true, false },
            { MENU_ZAKRES_NASTEPNY, ">", UI_STYL_NORMALNY, true, false },
            { MENU_ZAKRES_POMOC, JEZYK_Wybierz("Pomoc", "Help", "Hilfe", "Помощь"),
              UI_STYL_NORMALNY, true, false },
            { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return zmieniono;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
            akcja = MENU_ZAKRES_POPRZEDNI;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
            akcja = MENU_ZAKRES_NASTEPNY;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
                 zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            akcja = MENU_ZAKRES_DOBIERZ;

        if (TOUCH_Poll(&punkt))
        {
            akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                         UI_DOLNY_PRZYCISK_WYSOKOSC,
                                         akcje, 5U);
            if (akcja != -1)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
            }
        }

        if (akcja == MENU_EXIT)
            return zmieniono;
        if (akcja == MENU_ZAKRES_POPRZEDNI)
        {
            LC_ZmienZakres(-1, indeks_pomiaru, czestotliwosc_hz);
            zmieniono = 1;
            LC_RysujZakresEkran();
        }
        else if (akcja == MENU_ZAKRES_NASTEPNY)
        {
            LC_ZmienZakres(1, indeks_pomiaru, czestotliwosc_hz);
            zmieniono = 1;
            LC_RysujZakresEkran();
        }
        else if (akcja == MENU_ZAKRES_DOBIERZ)
        {
            GEN_SetMeasurementFreq(0U);
            if (LC_DobierzZakresAutomatycznie(indeks_pomiaru, czestotliwosc_hz))
                zmieniono = 1;
            LC_RysujZakresEkran();
        }
        else if (akcja == MENU_ZAKRES_POMOC)
        {
            LC_PomocZakres();
            LC_RysujZakresEkran();
        }

        Sleep(10U);
    }
}

static void LC_RysujSzczegolyEkran(DSP_RX rx,
                                   const LCM_WYNIK_WYBORU_t *wybor,
                                   const POMIAR_S11_t *pomiar,
                                   int ma_wynik)
{
    const UI_AKCJA_t akcje[2] =
    {
        { LC_WIECEJ_AKCJA_ZRZUT, JEZYK_Tekst(TEKST_ZAPISZ_ZRZUT),
          UI_STYL_NORMALNY, true, false },
        { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
    };
    char tekst[96];
    float r = NAN;
    float x = NAN;
    float q = NAN;

    if (ma_wynik)
    {
        r = crealf(rx);
        x = cimagf(rx);
        if (isfinite(r) && isfinite(x) && fabsf(r) > 0.01f)
            q = fabsf(x / r);
    }

    UI_WyczyscEkran();
    UI_RysujPasekGorny(
        JEZYK_Wybierz("Elementy RF - szczegóły", "RF components - details",
                      "HF-Bauteile - Details", "ВЧ-элементы - подробно"),
        true, false, 0);
    UI_RysujPanel(4, 42, 270, 174,
                  JEZYK_Wybierz("Dane pomiaru", "Measurement data",
                                "Messdaten", "Данные измерения"),
                  UI_STYL_NORMALNY);

    if (!ma_wynik || wybor == NULL || pomiar == NULL)
    {
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 26, 90,
                   JEZYK_Wybierz("Brak wykonanego pomiaru", "No measurement yet",
                                 "Noch keine Messung", "Измерение ещё не выполнено"));
    }
    else
    {
        snprintf(tekst, sizeof(tekst), "f: %.6f MHz",
                 (double)wybor->czestotliwosc_hz / 1000000.0);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 66, tekst);
        snprintf(tekst, sizeof(tekst), "Rs: %.3f Ohm   Xs: %+.3f Ohm", r, x);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 87, tekst);
        snprintf(tekst, sizeof(tekst), "|Xs/Rs|: %s", isfinite(q) ? "" : "--");
        if (isfinite(q))
            snprintf(tekst, sizeof(tekst), "|Xs/Rs|: %.2f", q);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 108, tekst);
        snprintf(tekst, sizeof(tekst), JEZYK_Tekst(TEKST_LC_ZMIENNOSC_MODELU_FMT),
                 wybor->zmiennosc_proc);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 129, tekst);
        snprintf(tekst, sizeof(tekst), "V: %.4f mV   I: %.4f mV",
                 pomiar->napiecie_v_mv, pomiar->napiecie_i_mv);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 150, tekst);
        snprintf(tekst, sizeof(tekst), JEZYK_Tekst(TEKST_LC_ROZNICA_AMPLITUD_FMT),
                 pomiar->stosunek_db);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 171, tekst);
        snprintf(tekst, sizeof(tekst),
                 JEZYK_Wybierz("Faza: %.2f°   spójność: %.2f",
                               "Phase: %.2f°   coherence: %.2f",
                               "Phase: %.2f°   Kohärenz: %.2f",
                               "Фаза: %.2f°   когерентность: %.2f"),
                 pomiar->faza_stopnie, pomiar->spojnosc_fazy);
        FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 14, 192, tekst);
    }

    DisplayCalInfo();
    UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 2U);
}

static void LC_SzczegolyWindow(DSP_RX rx,
                               const LCM_WYNIK_WYBORU_t *wybor,
                               const POMIAR_S11_t *pomiar,
                               int ma_wynik)
{
    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();
    LC_RysujSzczegolyEkran(rx, wybor, pomiar, ma_wynik);

    for (;;)
    {
        LCDPoint punkt;
        int16_t akcja = -1;
        const UI_AKCJA_t akcje[2] =
        {
            { LC_WIECEJ_AKCJA_ZRZUT, JEZYK_Tekst(TEKST_ZAPISZ_ZRZUT),
              UI_STYL_NORMALNY, true, false },
            { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return;
        if (TOUCH_Poll(&punkt))
        {
            akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                         UI_DOLNY_PRZYCISK_WYSOKOSC,
                                         akcje, 2U);
            if (akcja != -1)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
            }
        }
        if (akcja == MENU_EXIT)
            return;
        if (akcja == LC_WIECEJ_AKCJA_ZRZUT)
        {
            MEASUREMENT_Screenshot();
            LC_RysujSzczegolyEkran(rx, wybor, pomiar, ma_wynik);
        }
        Sleep(10U);
    }
}

static void LC_RysujWiecejEkran(void)
{
    const UI_AKCJA_t akcje[4] =
    {
        { LC_WIECEJ_AKCJA_SZCZEGOLY,
          JEZYK_Wybierz("Szczeg.", "Details", "Details", "Детали"),
          UI_STYL_AKCENT, true, false },
        { LC_WIECEJ_AKCJA_OPCJE,
          JEZYK_Wybierz("Opcje", "Options", "Optionen", "Опции"),
          UI_STYL_NORMALNY, true, false },
        { LC_WIECEJ_AKCJA_ZRZUT,
          JEZYK_Tekst(TEKST_ZAPISZ_ZRZUT), UI_STYL_NORMALNY, true, false },
        { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
    };
    char zakres[64];

    UI_WyczyscEkran();
    UI_RysujPasekGorny(
        JEZYK_Wybierz("Elementy RF - więcej", "RF components - more",
                      "HF-Bauteile - mehr", "ВЧ-элементы - ещё"),
        true, false, 0);
    LC_FormatujZakres(zakres, sizeof(zakres));
    UI_RysujPanel(34, 62, 412, 126,
                  JEZYK_Wybierz("Dodatkowe funkcje", "Additional functions",
                                "Zusatzfunktionen", "Дополнительные функции"),
                  UI_STYL_NORMALNY);
    FONT_Write(FONT_FRANBIG, TextColor, BACK_COLOR, 60, 92,
               LC_Mode == 0
                   ? JEZYK_Wybierz("Tryb: cewka", "Mode: inductor", "Modus: Spule", "Режим: катушка")
                   : JEZYK_Wybierz("Tryb: kondensator", "Mode: capacitor", "Modus: Kondensator", "Режим: конденсатор"));
    FONT_Write(FONT_FRAN, TextColor, BACK_COLOR, 60, 126, zakres);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BACK_COLOR, 60, 151,
               JEZYK_Wybierz("Szczegóły zawierają V/I, fazę oraz stan HW i OSL.",
                             "Details contain V/I, phase, and HW/OSL status.",
                             "Details enthalten V/I, Phase sowie HW/OSL-Status.",
                             "В деталях: V/I, фаза и состояние HW/OSL."));
    UI_RysujPasekAkcji(UI_DOLNY_PASEK_Y, UI_DOLNY_PRZYCISK_WYSOKOSC, akcje, 4U);
}

static void LC_WiecejWindow(DSP_RX rx,
                            const LCM_WYNIK_WYBORU_t *wybor,
                            const POMIAR_S11_t *pomiar,
                            int ma_wynik)
{
    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();
    LC_RysujWiecejEkran();

    for (;;)
    {
        LCDPoint punkt;
        int16_t akcja = -1;
        const UI_AKCJA_t akcje[4] =
        {
            { LC_WIECEJ_AKCJA_SZCZEGOLY,
              JEZYK_Wybierz("Szczeg.", "Details", "Details", "Детали"),
              UI_STYL_AKCENT, true, false },
            { LC_WIECEJ_AKCJA_OPCJE,
              JEZYK_Wybierz("Opcje", "Options", "Optionen", "Опции"),
              UI_STYL_NORMALNY, true, false },
            { LC_WIECEJ_AKCJA_ZRZUT,
              JEZYK_Tekst(TEKST_ZAPISZ_ZRZUT), UI_STYL_NORMALNY, true, false },
            { MENU_EXIT, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return;
        if (TOUCH_Poll(&punkt))
        {
            akcja = UI_ZnajdzAkcjePaska(punkt, UI_DOLNY_PASEK_Y,
                                         UI_DOLNY_PRZYCISK_WYSOKOSC,
                                         akcje, 4U);
            if (akcja != -1)
            {
                TRACK_Beep(1);
                TOUCH_CzekajNaPuszczenie(20U);
            }
        }

        if (akcja == MENU_EXIT)
            return;
        if (akcja == LC_WIECEJ_AKCJA_SZCZEGOLY)
        {
            LC_SzczegolyWindow(rx, wybor, pomiar, ma_wynik);
            LC_RysujWiecejEkran();
        }
        else if (akcja == LC_WIECEJ_AKCJA_OPCJE)
        {
            LC_OpcjeWindow();
            LC_RysujWiecejEkran();
        }
        else if (akcja == LC_WIECEJ_AKCJA_ZRZUT)
        {
            MEASUREMENT_Screenshot();
            LC_RysujWiecejEkran();
        }
        Sleep(10U);
    }
}

static void LC_RozpocznijKlatke(void)
{
    LCD_BuforKlatkiRozpocznij(&lc_bufor_klatki);

    /*
     * Nie kopiujemy wizualnie poprzedniej klatki jako tła nowego ekranu.
     * Podwójny bufor nadal chroni przed miganiem, ale każda ukryta warstwa
     * zaczyna od czystego tła. Dzięki temu w kilkupikselowych szczelinach
     * między panelem wyniku a dolnym panelem nie zostają fragmenty tekstu
     * z poprzedniego widoku („duchy” widoczne na fizycznym LCD).
     */
    UI_WyczyscEkran();
}

static void LC_PokazKlatke(void)
{
    LCD_BuforKlatkiPokaz(&lc_bufor_klatki);
}

static void LC_RysujEkranOczekiwania(void)
{
    /*
     * Elementy RF są ekranem ciągłego pomiaru. Dawniej wynik, status
     * kalibracji i menu były rysowane kolejno bezpośrednio na widocznej
     * warstwie, co na fizycznym LCD dawało krótkie błyski między etapami.
     * Całą klatkę składamy teraz poza ekranem i pokazujemy jednym przełączeniem.
     */
    LC_RozpocznijKlatke();
    DrawMeasureLC(NAN + NAN * I, 0U, 0, NAN, 0, NULL);
    lcMenuDraw();
    LC_PokazKlatke();
}

static void LC_RysujPostepPomiaru(uint32_t numer, uint32_t razem, uint32_t f_hz)
{
    char tekst[72];
    const uint8_t procent = razem > 0U
        ? (uint8_t)((100UL * (numer > razem ? razem : numer)) / razem)
        : 0U;

    LC_RozpocznijKlatke();
    DrawMeasureLC(NAN + NAN * I, 0U, 0, NAN, 0, NULL);
    lcMenuDraw();

    UI_RysujPanel(10, 124, 254, 96,
                  JEZYK_Wybierz("Pomiar zakresu", "Range measurement",
                                "Bereichsmessung", "Измерение диапазона"),
                  UI_STYL_AKCENT);
    snprintf(tekst, sizeof(tekst), "%lu/%lu   %.3f MHz",
             (unsigned long)numer, (unsigned long)razem,
             (double)f_hz / 1000000.0);
    UI_RysujWskaznikPostepu(22, 170, 230, 22, procent, tekst);
    LC_PokazKlatke();
}

static int LC_CzyAnulowanoPomiar(void)
{
    LCDPoint punkt;
    const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

    if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ ||
        zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        return 1;

    if (TOUCH_Poll(&punkt) && UI_CzyDotknietoWstecz(punkt))
    {
        TOUCH_CzekajNaPuszczenie(20U);
        return 1;
    }
    return 0;
}

static void LC_RysujZatrzymanyWynik(DSP_RX rx, uint32_t liczba,
                                     const LCM_WYNIK_WYBORU_t *wybor,
                                     int znaleziony, int pomiar_poprawny,
                                     const POMIAR_S11_t *pomiar)
{
    LC_RozpocznijKlatke();
    LC_RysujWynikLubWykres(rx, liczba, wybor, znaleziony, pomiar_poprawny, pomiar);
    lcMenuDraw();
    LC_PokazKlatke();
}

static int LC_WykonajPomiarZakresu(LCM_WYNIK_WYBORU_t *wybor,
                                    POMIAR_S11_t *pomiar_koncowy,
                                    DSP_RX *rx_koncowe,
                                    uint32_t *liczba_punktow)
{
    const POMIAR_S11_USTAWIENIA_t ustawienia_skanu = {
        .tor = POMIAR_S11_TOR_LC, .liczba_usrednien = LC_USREDNIENIA_SKANU,
        .korekcja_hw = true, .korekcja_osl = true, .kompensacja_portu = false};
    const POMIAR_S11_USTAWIENIA_t ustawienia_koncowe = {
        .tor = POMIAR_S11_TOR_LC, .liczba_usrednien = LC_USREDNIENIA_KONCOWE,
        .korekcja_hw = true, .korekcja_osl = true, .kompensacja_portu = false};
    const LCM_TRYB_t tryb = LC_Mode == 0 ? LCM_TRYB_INDUKCYJNOSC : LCM_TRYB_POJEMNOSC;
    uint32_t indeks_zakresu = CFG_GetParam(CFG_PARAM_LC_INDEX);
    uint32_t krok_skanu;
    uint32_t f_hz;
    uint32_t liczba = 0U;
    uint32_t razem;
    int znaleziony;

    if (wybor == NULL || pomiar_koncowy == NULL || rx_koncowe == NULL || liczba_punktow == NULL)
        return 0;

    memset(wybor, 0, sizeof(*wybor));
    memset(pomiar_koncowy, 0, sizeof(*pomiar_koncowy));
    *rx_koncowe = NAN + NAN * I;
    *liczba_punktow = 0U;

    if (!LC_CzyPodstawaPomiaruGotowa())
        return 0;

    if (MeasureEndFreq <= MeasureStartFreq)
        return 0;

    if (indeks_zakresu >= LC_LICZBA_ZAKRESOW)
        indeks_zakresu = 0U;
    krok_skanu = LC_KROKI_ZAKRESOW_HZ[indeks_zakresu];

    razem = (MeasureEndFreq - MeasureStartFreq + krok_skanu - 1U) / krok_skanu;
    if (razem > LC_MAKS_PUNKTOW_ZAKRESU)
        razem = LC_MAKS_PUNKTOW_ZAKRESU;

    memset(MeasureValid, 0, sizeof(uint8_t) * LC_MAKS_PUNKTOW_ZAKRESU);
    for (f_hz = MeasureStartFreq;
         f_hz < MeasureEndFreq && liczba < LC_MAKS_PUNKTOW_ZAKRESU;
         f_hz += krok_skanu)
    {
        POMIAR_S11_t pomiar;
        const bool poprawny = POMIAR_S11_PobierzPunkt(f_hz, &ustawienia_skanu, &pomiar);

        MeasureFreq[liczba] = f_hz;
        if (poprawny)
        {
            MeasureRE[liczba] = crealf(pomiar.impedancja_ohm);
            MeasureIM[liczba] = cimagf(pomiar.impedancja_ohm);
            MeasureValid[liczba] = 1U;
        }
        else
        {
            MeasureRE[liczba] = NAN;
            MeasureIM[liczba] = NAN;
            MeasureValid[liczba] = 0U;
        }

        ++liczba;
        if (liczba == 1U || liczba == razem || (liczba % 4U) == 0U)
            LC_RysujPostepPomiaru(liczba, razem, f_hz);

        if (LC_CzyAnulowanoPomiar())
        {
            GEN_SetMeasurementFreq(0U);
            LC_RysujEkranOczekiwania();
            return 0;
        }
    }

    GEN_SetMeasurementFreq(0U);
    lc_ostatnia_liczba = (uint16_t)liczba;
    *liczba_punktow = liczba;

    znaleziony = LCM_WybierzPunktAutomatyczny(
        MeasureIM, MeasureFreq, MeasureValid, liczba, tryb,
        (float)CFG_GetParam(CFG_PARAM_R0), wybor);
    if (!znaleziony)
    {
        LC_RysujZatrzymanyWynik(NAN + NAN * I, liczba, NULL, 0, 0, NULL);
        return 0;
    }

    if (!POMIAR_S11_PobierzPunkt(wybor->czestotliwosc_hz,
                                 &ustawienia_koncowe, pomiar_koncowy))
    {
        GEN_SetMeasurementFreq(0U);
        LC_RysujZatrzymanyWynik(NAN + NAN * I, liczba, wybor, 1, 0, pomiar_koncowy);
        return 0;
    }

    GEN_SetMeasurementFreq(0U);
    *rx_koncowe = pomiar_koncowy->impedancja_ohm;
    LC_RysujZatrzymanyWynik(*rx_koncowe, liczba, wybor, 1, 1, pomiar_koncowy);
    return 1;
}

void Measure_LCR_Proc(void)
{
    LCDPoint pt;
    LCM_WYNIK_WYBORU_t ostatni_wybor;
    POMIAR_S11_t ostatni_pomiar;
    DSP_RX ostatnie_rx = NAN + NAN * I;
    uint32_t ostatnia_liczba = 0U;
    int ma_wynik = 0;
    int zakres_dummy = 0;
    uint32_t f_dummy = 0U;

    MeasureIM = (float *)SDRH_malloc(sizeof(float) * LC_MAKS_PUNKTOW_ZAKRESU);
    MeasureRE = (float *)SDRH_malloc(sizeof(float) * LC_MAKS_PUNKTOW_ZAKRESU);
    MeasureFreq = (uint32_t *)SDRH_malloc(sizeof(uint32_t) * LC_MAKS_PUNKTOW_ZAKRESU);
    MeasureValid = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * LC_MAKS_PUNKTOW_ZAKRESU);
    if (MeasureIM == NULL || MeasureRE == NULL || MeasureFreq == NULL || MeasureValid == NULL)
    {
        if (MeasureIM != NULL) SDRH_free(MeasureIM);
        if (MeasureRE != NULL) SDRH_free(MeasureRE);
        if (MeasureFreq != NULL) SDRH_free(MeasureFreq);
        if (MeasureValid != NULL) SDRH_free(MeasureValid);
        GEN_SetMeasurementFreq(0U);
        return;
    }

    if (CFG_GetParam(CFG_PARAM_LC_INDEX) >= LC_LICZBA_ZAKRESOW)
    {
        CFG_SetParam(CFG_PARAM_LC_INDEX, 0U);
        CFG_Flush();
    }

    /* V2.1-LCUX1: miernik L/C nie posiada już własnego profilu .lc. Ten sam
     * DUT i ta sama płaszczyzna odniesienia mają jedną kalibrację OSL S11.
     * Na wejściu czyścimy wynik i czekamy na jawne polecenie Mierz. */
    isMatch = 0;
    memset(&ostatni_wybor, 0, sizeof(ostatni_wybor));
    memset(&ostatni_pomiar, 0, sizeof(ostatni_pomiar));
    SetColours();
    BSP_LCD_SelectLayer(0);
    LCD_FillAll(BACK_COLOR);
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BACK_COLOR);
    LCD_BuforKlatkiInicjalizuj(&lc_bufor_klatki, 1U);

    LC_UstawZakres(CFG_GetParam(CFG_PARAM_LC_INDEX), &zakres_dummy, &f_dummy);
    while (TOUCH_IsPressed())
        ;
    WEJSCIA_WyczyscZdarzenia();
    LC_RysujEkranOczekiwania();

    for (;;)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        int rozpocznij_pomiar = 0;
        int wyczysc_wynik = 0;

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            LC_ZmienZakres(-1, &zakres_dummy, &f_dummy);
            wyczysc_wynik = 1;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            LC_ZmienZakres(1, &zakres_dummy, &f_dummy);
            wyczysc_wynik = 1;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK ||
                 zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        {
            rozpocznij_pomiar = 1;
        }

        if (TOUCH_Poll(&pt))
        {
            int touchIndex;
            LC_InicjalizujKontrolki();
            touchIndex = (int)UI_ZnajdzKontrolke(pt, lc_kontrolki, LC_KONTROLKI_LICZBA);

            if (touchIndex != -1)
            {
                TRACK_Beep(1);
                while (TOUCH_IsPressed())
                    ;
            }

            if (touchIndex == MENU_EXIT)
                break;
            else if (touchIndex == MENU_POMIAR)
                rozpocznij_pomiar = 1;
            else if (touchIndex == MENU_LC)
            {
                LC_Mode = LC_Mode == 0 ? 1 : 0;
                wyczysc_wynik = 1;
            }
            else if (touchIndex == MENU_KWARC)
            {
                /* Kwarc jest funkcją równorzędną L/C, dlatego ma bezpośredni
                 * przycisk. Po powrocie odtwarzamy ekran L/C i zachowujemy
                 * jego ostatni wynik; użytkownik nie musi wracać przez menu. */
                GEN_SetMeasurementFreq(0U);
                Quartz_proc();
                SetColours();
                BSP_LCD_SelectLayer(1);
                while (TOUCH_IsPressed())
                    ;
                WEJSCIA_WyczyscZdarzenia();
                if (ma_wynik)
                    LC_RysujZatrzymanyWynik(ostatnie_rx, ostatnia_liczba,
                                             &ostatni_wybor, 1, 1, &ostatni_pomiar);
                else
                    LC_RysujEkranOczekiwania();
            }
            else if (touchIndex == MENU_ZAKRES)
            {
                GEN_SetMeasurementFreq(0U);
                if (LC_ZakresWindow(&zakres_dummy, &f_dummy))
                    wyczysc_wynik = 1;
                else if (ma_wynik)
                    LC_RysujZatrzymanyWynik(ostatnie_rx, ostatnia_liczba,
                                             &ostatni_wybor, 1, 1, &ostatni_pomiar);
                else
                    LC_RysujEkranOczekiwania();
            }
            else if (touchIndex == MENU_WIECEJ)
            {
                GEN_SetMeasurementFreq(0U);
                LC_WiecejWindow(ostatnie_rx,
                                ma_wynik ? &ostatni_wybor : NULL,
                                ma_wynik ? &ostatni_pomiar : NULL,
                                ma_wynik);
                if (ma_wynik)
                    LC_RysujZatrzymanyWynik(ostatnie_rx, ostatnia_liczba,
                                             &ostatni_wybor, 1, 1, &ostatni_pomiar);
                else
                    LC_RysujEkranOczekiwania();
            }
        }

        if (wyczysc_wynik)
        {
            ma_wynik = 0;
            memset(&ostatni_wybor, 0, sizeof(ostatni_wybor));
            memset(&ostatni_pomiar, 0, sizeof(ostatni_pomiar));
            ostatnie_rx = NAN + NAN * I;
            ostatnia_liczba = 0U;
            LC_RysujEkranOczekiwania();
        }

        if (rozpocznij_pomiar)
        {
            ma_wynik = LC_WykonajPomiarZakresu(&ostatni_wybor, &ostatni_pomiar,
                                                &ostatnie_rx, &ostatnia_liczba);
            if (!ma_wynik)
            {
                memset(&ostatni_wybor, 0, sizeof(ostatni_wybor));
                memset(&ostatni_pomiar, 0, sizeof(ostatni_pomiar));
                ostatnie_rx = NAN + NAN * I;
            }
        }

        Sleep(10U);
    }

    SDRH_free(MeasureIM);
    SDRH_free(MeasureRE);
    SDRH_free(MeasureFreq);
    SDRH_free(MeasureValid);
    MeasureIM = NULL;
    MeasureRE = NULL;
    MeasureFreq = NULL;
    MeasureValid = NULL;
    GEN_SetMeasurementFreq(0U);


}
