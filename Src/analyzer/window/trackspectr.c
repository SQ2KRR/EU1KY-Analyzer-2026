/*
  S21-Gain for EU1KY's AA

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
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <string.h>

#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "config.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "ff.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "spectr.h"
#include "stm32746g_discovery_lcd.h"
#include "screenshot.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "smith.h"
#include "textbox.h"
#include "generator.h"
#include "FreqCounter.h"
#include "bitmaps/bitmaps.h"
#include "si5351.h"
#include "si5351_hs.h"
#include "sdram_heap.h"
#include "s21_analiza.h"

#define X0 51
#define Y0 18
#define WWIDTH 400
#define WHEIGHT 190
#define WY(offset) ((WHEIGHT + Y0) - (offset))
#define WGRIDCOLOR LCD_COLOR_DARKGRAY
#define RED1 LCD_RGB(245, 0, 0)
#define RED2 LCD_RGB(235, 0, 0)

extern uint8_t AUDIO1;

static void save_snapshot(void);

static int redrawRequired, exitScan;

//static const char *modstr = "CEC v." AAVERSION_CECV " ";

//To have the same range to reduce memory usage
extern const char *BSSTR[];
extern const char *BSSTR_HALF[];
extern const uint32_t BSVALUES[];

#define BSSTR_TRACK BSSTR
#define BSSTR_TRACK_HALF BSSTR_HALF
#define BSVALUES_TRACK BSVALUES

static char autoScanFactor = 1;
static uint32_t f1 = 14000000; //Scan range start frequency, in Hz
static uint32_t fstart, freq1;
static BANDSPAN span21 = BS400;
static float fcur; // frequency at cursor position in kHz
//static char buf[64];
static LCDPoint pt0;
float *valuesmI;
static uint8_t *s21_punkt_poprawny;
static S21_ANALIZA_t g_s21_analiza;

#define displayRate 2.9f //Display Zoom
#define displayOffset 190
//static char DisplayType = 0;      //values or valuesmI
static int isMeasured = 0;
static uint32_t cursorPos = WWIDTH / 2;

static uint32_t cursorChangeCount = 0;
static uint32_t autofast = 0;
//static void Track_DrawRX();
static int trackbeep;

static uint32_t activeLayerS21;
static int cursorVisibleS21;
//static int firstRun;
static int FirstRunS21;

//=====================================================================
//Menu
//---------------------------------------------------------------------
#define S21_BOTTOM_MENU_TOP 242
#define trackMenu_Length 11
static void TRACK_WypelnijKontrolki(UI_KONTROLKA_t kontrolki[trackMenu_Length])
{
    const char *freq_txt = JEZYK_Wybierz("Częst.", "Freq.", "Freq.", "Част.");
    const char *zrzut_txt = JEZYK_Wybierz("Zrzut", "Snapshot", "Bild", "Снимок");

    kontrolki[0] = UI_UtworzKontrolke(0, 0, S21_BOTTOM_MENU_TOP, 68, 30,
        JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[1] = UI_UtworzKontrolke(1, 70, S21_BOTTOM_MENU_TOP, 90, 30,
        JEZYK_Tekst(TEKST_SKANUJ), UI_STYL_AKCENT, UI_ROLA_TEKSTU_PRZYCISK, !autofast, false);
    kontrolki[2] = UI_UtworzKontrolke(2, 162, S21_BOTTOM_MENU_TOP, 42, 30,
        "-", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[3] = UI_UtworzKontrolke(3, 206, S21_BOTTOM_MENU_TOP, 42, 30,
        "+", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
    kontrolki[4] = UI_UtworzKontrolke(4, 250, S21_BOTTOM_MENU_TOP, 82, 30,
        freq_txt, UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    /* Dawne niewidoczne pole 40..479 x 0..80 duplikowalo przycisk Czest.
     * Usuwamy jego aktywnosc: niewidoczny obszar nie moze zachowywac sie jak przycisk. */
    kontrolki[5] = UI_UtworzKontrolke(5, 0, 0, 0, 0, "", UI_STYL_NIEAKTYWNY,
        UI_ROLA_TEKSTU_PRZYCISK, false, false);

    kontrolki[6] = UI_UtworzKontrolke(6, 334, S21_BOTTOM_MENU_TOP, 72, 30,
        zrzut_txt, UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);

    /* Obszar wykresu jest interaktywny (ustawia kursor), ale celowo nie jest rysowany
     * jak przycisk. Jego natura wynika z samego wykresu i kursora. */
    kontrolki[7] = UI_UtworzKontrolke(7, 40, 210, 410, 28, "", UI_STYL_NORMALNY,
        UI_ROLA_TEKSTU_PRZYCISK, true, false);

    kontrolki[8] = UI_UtworzKontrolke(8, 408, S21_BOTTOM_MENU_TOP, 72, 30,
        "Auto", autofast ? UI_STYL_AKTYWNY : UI_STYL_NORMALNY,
        UI_ROLA_TEKSTU_PRZYCISK, true, autofast != 0U);
    kontrolki[9] = UI_UtworzKontrolke(9, 0, 90, 50, 80,
        "<", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_WARTOSC_GLOWNA, true, false);
    kontrolki[10] = UI_UtworzKontrolke(10, 430, 90, 50, 80,
        ">", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_WARTOSC_GLOWNA, true, false);
}


void TRACK_Beep(int duration)
{
    if (BeepOn1 == 0)
        return;

    if (trackbeep == 0)
    {
        trackbeep = 1;
        AUDIO1 = 1;
        UB_TIMER2_Init_FRQ(880);
        UB_TIMER2_Start();
        Sleep(100);
        AUDIO1 = 0;
        // UB_TIMER2_Stop();
    }

    if (duration == 1)
        trackbeep = 0;
}

static void DrawAutoText(void)
{
    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    TRACK_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolke(&kontrolki[8]);
}

static void TRACK_DrawFootText(void)
{
    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    static const uint8_t indeksy[] = { 0U, 1U, 2U, 3U, 4U, 6U, 8U };
    uint8_t i;

    TRACK_WypelnijKontrolki(kontrolki);
    for (i = 0U; i < sizeof(indeksy) / sizeof(indeksy[0]); ++i)
        UI_RysujKontrolke(&kontrolki[indeksy[i]]);
}

static void TRACK_RysujPrzyciskiKursora(void)
{
    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    TRACK_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolke(&kontrolki[9]);
    UI_RysujKontrolke(&kontrolki[10]);
}

float RawVoltage2;

static void DrawMeasuredValues(void)
{
    uint32_t fCursor;
    char buf[128];

    fCursor = fstart + (uint32_t)((cursorPos * BSVALUES_TRACK[span21] * 1000.0f) / WWIDTH);
    if (fCursor > CFG_GetParam(CFG_PARAM_BAND_FMAX))
        fCursor = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    LCD_FillRect(LCD_MakePoint(0, 209), LCD_MakePoint(479, 240), BackGrColor);

    if (isMeasured && g_s21_analiza.poprawny)
    {
        switch (g_s21_analiza.typ)
        {
        case S21_TYP_LPF:
            if (g_s21_analiza.ma_prawy_3db)
                snprintf(buf, sizeof(buf),
                         "LPF  fc %.3f MHz  IL %.1f dB  stop %.1f dB  %.0f dB/oct",
                         (double)g_s21_analiza.f2_3db_hz / 1000000.0,
                         (double)g_s21_analiza.strata_min_db,
                         (double)g_s21_analiza.tlumienie_stop_db,
                         (double)g_s21_analiza.nachylenie_prawe_db_na_oktawe);
            else
                snprintf(buf, sizeof(buf), "LPF  fc --  IL %.1f dB  stop %.1f dB",
                         (double)g_s21_analiza.strata_min_db,
                         (double)g_s21_analiza.tlumienie_stop_db);
            break;

        case S21_TYP_HPF:
            if (g_s21_analiza.ma_lewy_3db)
                snprintf(buf, sizeof(buf),
                         "HPF  fc %.3f MHz  IL %.1f dB  stop %.1f dB  %.0f dB/oct",
                         (double)g_s21_analiza.f1_3db_hz / 1000000.0,
                         (double)g_s21_analiza.strata_min_db,
                         (double)g_s21_analiza.tlumienie_stop_db,
                         (double)g_s21_analiza.nachylenie_lewe_db_na_oktawe);
            else
                snprintf(buf, sizeof(buf), "HPF  fc --  IL %.1f dB  stop %.1f dB",
                         (double)g_s21_analiza.strata_min_db,
                         (double)g_s21_analiza.tlumienie_stop_db);
            break;

        case S21_TYP_BPF:
            if (g_s21_analiza.ma_lewy_3db && g_s21_analiza.ma_prawy_3db &&
                g_s21_analiza.pasmo_3db_hz > 0.0f)
                snprintf(buf, sizeof(buf),
                         "BPF  f0 %.3f MHz  BW3 %.1f kHz  Q %.1f  stop %.1f dB",
                         (double)g_s21_analiza.f_min_hz / 1000000.0,
                         (double)g_s21_analiza.pasmo_3db_hz / 1000.0,
                         (double)g_s21_analiza.q_3db,
                         (double)g_s21_analiza.tlumienie_stop_db);
            else
                snprintf(buf, sizeof(buf), "BPF  f0 %.3f MHz  BW3 --  IL %.1f dB",
                         (double)g_s21_analiza.f_min_hz / 1000000.0,
                         (double)g_s21_analiza.strata_min_db);
            break;

        case S21_TYP_NOTCH:
            if (g_s21_analiza.ma_lewy_3db && g_s21_analiza.ma_prawy_3db &&
                g_s21_analiza.pasmo_3db_hz > 0.0f)
                snprintf(buf, sizeof(buf),
                         "NOTCH  f0 %.3f MHz  depth %.1f dB  BW3 %.1f kHz",
                         (double)g_s21_analiza.f_max_hz / 1000000.0,
                         (double)g_s21_analiza.glebokosc_notch_db,
                         (double)g_s21_analiza.pasmo_3db_hz / 1000.0);
            else
                snprintf(buf, sizeof(buf), "NOTCH  f0 %.3f MHz  depth %.1f dB  BW3 --",
                         (double)g_s21_analiza.f_max_hz / 1000000.0,
                         (double)g_s21_analiza.glebokosc_notch_db);
            break;

        default:
            if (g_s21_analiza.ma_lewy_3db && g_s21_analiza.ma_prawy_3db &&
                g_s21_analiza.pasmo_3db_hz > 0.0f)
                snprintf(buf, sizeof(buf),
                         "S21  best %.2f dB @ %.3f MHz  BW3 %.1f kHz",
                         (double)(-g_s21_analiza.strata_min_db),
                         (double)g_s21_analiza.f_min_hz / 1000000.0,
                         (double)g_s21_analiza.pasmo_3db_hz / 1000.0);
            else
                snprintf(buf, sizeof(buf), "S21  best %.2f dB @ %.3f MHz",
                         (double)(-g_s21_analiza.strata_min_db),
                         (double)g_s21_analiza.f_min_hz / 1000000.0);
            break;
        }
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 4, 210, buf);
    }

    if (!isMeasured || s21_punkt_poprawny == 0 || !s21_punkt_poprawny[cursorPos] ||
        !isfinite(valuesmI[cursorPos]))
    {
        snprintf(buf, sizeof(buf), "F: %.3f MHz   |S21|: -- dB   %s",
                 (double)fCursor / 1000000.0,
                 JEZYK_Tekst(TEKST_POMIAR_DANE_NIEWIARYGODNE));
    }
    else
    {
        const float s21_db = -valuesmI[cursorPos];
        snprintf(buf, sizeof(buf), "F: %.3f MHz   |S21|: %.2f dB",
                 (double)fCursor / 1000000.0, (double)s21_db);
    }
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 4, 223, buf);
}

bool IsInverted;

static void DrawCursorS21(int offset)
{
    int i;
    int x = X0 + cursorPos + offset;
    if ((x < X0) || (x > X0 + WWIDTH))
        return;
    LCDPoint p;
    //Draw cursor line in TextColor
    p = LCD_MakePoint(x, Y0);

    i = 0;
    for (p.y = Y0; p.y < Y0 + WHEIGHT; p.y++)
    {
        if (i < 10)
            LCD_SetPixel(p, TextColor); //was: WK_InvertPixel(p.x,p.y);
        i++;
        if (i >= 20)
            i = 0;
    }
    if (offset == 0)
    {
        LCD_FillRect((LCDPoint){X0 + cursorPos - 3, Y0 + WHEIGHT + 2}, (LCDPoint){X0 + cursorPos + 3, Y0 + WHEIGHT + 4}, BackGrColor);
        LCD_FillRect((LCDPoint){X0 + cursorPos - 2, Y0 + WHEIGHT + 2}, (LCDPoint){X0 + cursorPos + 2, Y0 + WHEIGHT + 4}, TextColor);
    }
    Sleep(1);
}

static LCDColor c, c31, c35;
static int r, k, x, pos;

static void StrictDelCursor1(int offset)
{
    x = X0 + cursorPos + offset;
    if ((x < X0) || (x > X0 + WWIDTH))
        return;
    c31 = LCD_ReadPixel(LCD_MakePoint(x, Y0 + 31)); // no horizontal line between 30..39
    c35 = LCD_ReadPixel(LCD_MakePoint(x, Y0 + 35));
    c = c31;
    if (c31 == CurvColor)
        c = c35;
    LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 2, c); //draw original
    // draw horiz. lines:
    r = 10 * Y0;
    for (k = 0; k < 14; k++)
    { // horizontal lines
        LCD_SetPixel(LCD_MakePoint(x, r / 10), WGRIDCOLOR);
        if (k % 2 == 0)
            LCD_SetPixel(LCD_MakePoint(x, r / 10 + 1), WGRIDCOLOR);
        r += 146;
    }
    if (s21_punkt_poprawny != 0 && s21_punkt_poprawny[cursorPos + offset] &&
        isfinite(valuesmI[cursorPos + offset]))
    {
        pos = Y0 + (int)(valuesmI[cursorPos + offset] / 65.0f * WHEIGHT);
        if (pos < Y0) pos = Y0;
        if (pos > Y0 + WHEIGHT) pos = Y0 + WHEIGHT;
        LCD_SetPixel(LCD_MakePoint(x, pos), CurvColor);
        if (FatLines)
        {
            if (pos > Y0) LCD_SetPixel(LCD_MakePoint(x, pos - 1), CurvColor);
            if (pos < Y0 + WHEIGHT) LCD_SetPixel(LCD_MakePoint(x, pos + 1), CurvColor);
        }
    }
}

static void StrictDelCursor(void)
{
    StrictDelCursor1(0);
    if (FatLines)
    {
        StrictDelCursor1(-1);
        StrictDelCursor1(1);
    }
}

static void StrictDrawCursor(void)
{
    DrawCursorS21(0);
    if (FatLines)
    {
        DrawCursorS21(-1);
        DrawCursorS21(1);
    }
}

static void MoveCursor(int moveDirection)
{
    int NewPosition = cursorPos + moveDirection;

    if (!isMeasured)
        return;

    if ((NewPosition > WWIDTH) || (NewPosition < 0))
        return;

    StrictDelCursor(); // delete the old cursor
    cursorPos = NewPosition;
    StrictDrawCursor(); // set the new cursor

    if (cursorChangeCount++ < 5)
    {
        Sleep(20); //Slow down at first steps
    }
    DrawMeasuredValues(); //DH1AKF  25.10.2020
    Sleep(2);
}

#define linediv 10

static void Track_DrawGrid(int justGraphDraw) //
{
    char buf[30];
    int i, r;
    int verticalPos = 125;
    const uint32_t zakres_start = fstart;
    //uint32_t pos = 130;

#define VerticalStep 146
#define VerticalX 10

    if (0 == CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
        snprintf(buf, sizeof(buf), "%.3f MHz +%s", (double)f1 / 1000000.0, BSSTR_TRACK[span21]);
    else
        snprintf(buf, sizeof(buf), "%.3f MHz +/- %s", (double)f1 / 1000000.0, BSSTR_TRACK_HALF[span21]);
    LCD_FillRect(LCD_MakePoint(40, 15), LCD_MakePoint(459, 225), BackGrColor);
    cursorVisibleS21 = 0;
    if (justGraphDraw % 1 == 0)
    {
        //Vertical Numbers
        FONT_Write(FONT_FRAN, CurvColor, BackGrColor, VerticalX, Y0, " 0dB");
        r = 10 * Y0;
        verticalPos += VerticalStep;
        for (i = 10; i <= 75; i += 5)
        {
            verticalPos += VerticalStep;
            LCD_HLine(LCD_MakePoint(X0 - 5, r / 10), WWIDTH + 5, WGRIDCOLOR);
            if (i % 10 == 0)
            {
                LCD_HLine(LCD_MakePoint(X0 - 5, r / 10 + 1), WWIDTH + 5, WGRIDCOLOR);
                if (i <= 60)
                    FONT_Print(FONT_FRAN, CurvColor, BackGrColor, VerticalX, verticalPos / 10, "%d", -i); // - xx
            }
            r += VerticalStep;
        }

        //FONT_Write(FONT_FRAN, LCD_BLACK, LCD_PURPLE, 50, 0, modstr);
        TRACK_RysujPrzyciskiKursora();

        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 52, 0, JEZYK_Tekst(TEKST_S21_TYTUL));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 260, 0, buf);
    }

    //Draw F grid and labels
    int lmod = 5;
    //Draw vertical line every linediv pixels

    //Mark ham bands with colored background DH1AKF
    for (i = 0; i <= WWIDTH; i++)
    {
        uint32_t f = zakres_start / 1000 + (i * BSVALUES[span21]) / WWIDTH;
        if (IsFinHamBands(f))
        {
            LCD_VLine(LCD_MakePoint(X0 + i, Y0), WHEIGHT + 1, LCD_COLOR_BLUE); // (0, 0, 64) darkblue << >> yellow
        }
    }

    for (i = 0; i <= WWIDTH / linediv; i++)
    {
        int x = X0 + i * linediv;
        if ((i % lmod) == 0 || i == WWIDTH / linediv)
        {
            //char fr[10];
            float flabel = ((float)(zakres_start / 1000. + i * BSVALUES_TRACK[span21] / (WWIDTH / linediv))) / 1000.f;
            if (flabel * 1000000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
                continue;
            if (flabel > 999.99)
                sprintf(buf, "%.1f", ((float)(zakres_start / 1000. + i * BSVALUES_TRACK[span21] / (WWIDTH / linediv))) / 1000.f);
            else if (flabel > 99.99)
                sprintf(buf, "%.2f", ((float)(zakres_start / 1000. + i * BSVALUES_TRACK[span21] / (WWIDTH / linediv))) / 1000.f);
            else
                sprintf(buf, "%.3f", ((float)(zakres_start / 1000. + i * BSVALUES_TRACK[span21] / (WWIDTH / linediv))) / 1000.f); // WK
            int w = FONT_GetStrPixelWidth(FONT_SDIGITS, buf);
            // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, x - w / 2, Y0 + WHEIGHT + 5, f);// WK
            FONT_Write(FONT_FRAN, TextColor, BackGrColor, x - 8 - w / 2, Y0 + WHEIGHT + 3, buf);
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 1, WGRIDCOLOR);
            LCD_VLine(LCD_MakePoint(x + 1, Y0), WHEIGHT + 1, WGRIDCOLOR); // WK
        }
        else
        {
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT + 1, WGRIDCOLOR);
        }
    }
    if (justGraphDraw == 2)
    {
        TRACK_DrawFootText();
    }
    //LCD_FillRect((LCDPoint){X0 ,Y0+WHEIGHT+1},(LCDPoint){X0 + WWIDTH+2,Y0+WHEIGHT+3},BackGrColor);
}

//static uint32_t Fs, Fp;// in Hz

static int S21_PomiarPunktu(uint32_t frequency, uint32_t nScanCount, float *wynik)
{
    float wartosc;

    if (wynik == 0)
        return 0;
    wartosc = CalcBin107(frequency, (int)nScanCount, &value1, &value2, &value3);
    if (!isfinite(wartosc))
        return 0;

    *wynik = wartosc;
    return 1;
}

static int S21_SkanDoBufora(uint32_t krok, int mozna_przerwac)
{
    float *nowe_wartosci;
    uint8_t *nowe_poprawne;
    uint32_t nScanCount;
    uint32_t deltaF;
    uint32_t poprzedni = 0U;
    uint32_t nastepny;
    LCDPoint pt;
    int przerwany = 0;

    if (krok < 1U)
        krok = 1U;
    if (krok > 20U)
        krok = 20U;

    nowe_wartosci = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    nowe_poprawne = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    if (nowe_wartosci == 0 || nowe_poprawne == 0)
    {
        if (nowe_wartosci != 0)
            SDRH_free(nowe_wartosci);
        if (nowe_poprawne != 0)
            SDRH_free(nowe_poprawne);
        GEN_SetTXFreq(0);
        return 0;
    }

    memset(nowe_wartosci, 0, sizeof(float) * (WWIDTH + 1U));
    memset(nowe_poprawne, 0, sizeof(uint8_t) * (WWIDTH + 1U));
    deltaF = (BSVALUES_TRACK[span21] * 1000U) / WWIDTH;
    nScanCount = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    if (nScanCount < 1U)
        nScanCount = 1U;
    if (nScanCount > 20U)
        nScanCount = 20U;

    CLK2_drive = 0;
    HS_SetPower(2, 0, 1);

    /* Pierwszy punkt jest zawsze rzeczywiście mierzony. */
    freq1 = fstart;
    nowe_poprawne[0] = (uint8_t)S21_PomiarPunktu(freq1, nScanCount, &nowe_wartosci[0]);

    while (poprzedni < WWIDTH)
    {
        uint32_t m;
        nastepny = poprzedni + krok;
        if (nastepny > WWIDTH)
            nastepny = WWIDTH;

        if (mozna_przerwac && TOUCH_Poll(&pt))
        {
            przerwany = 1;
            break;
        }

        freq1 = fstart + nastepny * deltaF;
        nowe_poprawne[nastepny] = (uint8_t)S21_PomiarPunktu(freq1, nScanCount, &nowe_wartosci[nastepny]);

        /*
         * Interpolujemy wyłącznie pomiędzy dwoma poprawnymi punktami.
         * Jeżeli któryś pomiar jest niewiarygodny, powstaje jawna przerwa
         * zamiast gładkiej linii sugerującej nieistniejący wynik.
         */
        for (m = poprzedni + 1U; m < nastepny; m++)
        {
            if (nowe_poprawne[poprzedni] && nowe_poprawne[nastepny])
            {
                const float udzial = (float)(m - poprzedni) / (float)(nastepny - poprzedni);
                nowe_wartosci[m] = nowe_wartosci[poprzedni] +
                                   (nowe_wartosci[nastepny] - nowe_wartosci[poprzedni]) * udzial;
                nowe_poprawne[m] = 1U;
            }
        }

        poprzedni = nastepny;
    }

    GEN_SetTXFreq(0);
    if (!przerwany)
    {
        memcpy(valuesmI, nowe_wartosci, sizeof(float) * (WWIDTH + 1U));
        memcpy(s21_punkt_poprawny, nowe_poprawne, sizeof(uint8_t) * (WWIDTH + 1U));
        (void)S21_AnalizujFiltr(valuesmI, s21_punkt_poprawny, (uint16_t)(WWIDTH + 1U),
                               fstart, (float)deltaF, &g_s21_analiza);
        isMeasured = 1;
    }

    SDRH_free(nowe_wartosci);
    SDRH_free(nowe_poprawne);
    return przerwany ? 0 : 1;
}

static void Scan21(int selector)
{
    (void)selector;
    (void)S21_SkanDoBufora(1U, 0);
}

//=========================================================================
//                        NOT REMOVE BELOW LINES
//=========================================================================
////For Version 0.5
////Check memory interference and open
//int offsetIndex = 0;
//static void Scan21Fast_v05()
//{
//    float inputMI;
//    uint32_t i, nScanCount;
//    uint32_t fstart, freq1, deltaF;
//
//    f1=CFG_GetParam(CFG_PARAM_S21_F1);
//    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
//        fstart = f1;
//    else
//        fstart = f1 - 500*BSVALUES_TRACK[span]; // 2;
//
//    freq1 = fstart - 150000;
//    if (freq1 < 100000)
//        freq1 = 100000;
//
//    DSP_MeasureTrack(freq1, 1, 1, 1); //Fake initial run to let the circuit stabilize
//    inputMI = DSP_MeasuredTrackValue();
//    //Sleep(20);
//
//    DSP_MeasureTrack(freq1, 1, 1, 1);
//    inputMI = DSP_MeasuredTrackValue();
//
//    deltaF=(BSVALUES_TRACK[span] * 1000) / WWIDTH;
//    nScanCount = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
//
///*
//#define MEASURE_INTERVAL_FAST 32
//#define USEHIST_INTERVAL_FAST 16
//#define MEASURE_MAX_INDEX 1
//*/
//#define MEASURE_INTERVAL_FAST 32
//#define USEHIST_INTERVAL_FAST 8
//#define MEASURE_MAX_INDEX 3
//
//    const int FAST_SCAN_OFFSET[] = {0, 16, 8, 24};
//
//    int fastOffset = FAST_SCAN_OFFSET[offsetIndex++];
//
//    if (offsetIndex > MEASURE_MAX_INDEX)
//        offsetIndex = 0;
//
//    for(i = 0; i <= WWIDTH; i++)
//    {
//        freq1 = fstart + (i + fastOffset) * deltaF;
//        if (freq1 == 0) //To overcome special case in DSP_Measure, where 0 is valid value
//            freq1 = 1;
//
//        int drawX = i + fastOffset;
//
//
//        if (i % MEASURE_INTERVAL_FAST != 0)
//        {
//            continue;
//        }
//#ifdef _DEBUG_UART
////    DBG_Printf("PROCESS: OFST:%d, IDX:%d, drawX:%d, Freq:%u", fastOffset, i, drawX, freq1);
//#endif
//
//        //DSP_MeasureTrack(freq1, 1, 1, nScanCount);
//        DSP_MeasureTrack(freq1, 1, 1, 1);
//        valuesdBI[drawX] = OSL_TXTodB(freq1, DSP_MeasuredTrackValue());
//
//            /*
//        //Break by Touch
//        if (autofast)
//        {
//            if (TOUCH_Poll(&pt))
//            {
//                if (pt.y > 230 && pt.x > 442)
//                {
//                    autofast = 0;
//                    TRACK_Beep(0);
//                    TRACK_DrawFootText();
//                    return;
//                }
//            }
//        }
//            */
//    }
//
//    //FONT_Write(FONT_FRAN, LCD_RED, LCD_BLACK, 420, 0, "     ");
//    GEN_SetMeasurementFreq(0);
//    isMeasured = 1;
//
///*
//    //fastOffset
//    //First Interpolate
//    for(i = fastOffset; i < WWIDTH - MEASURE_INTERVAL_FAST; i += USEHIST_INTERVAL_FAST)
//    {
//        uint32_t fr = i % MEASURE_INTERVAL_FAST;
//
//        if (fastOffset == fr)
//            continue;
//
//        int fi0, fi2;
//
//        //17 : 16, 48
//
//        fi0 = i / MEASURE_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= MEASURE_INTERVAL_FAST;
//        fi2 *= MEASURE_INTERVAL_FAST;
//
//        fi0 += fastOffset;
//        fi2 += fastOffset;
//
//        if (fi2 > WWIDTH)
//            continue;
//
//#ifdef _DEBUG_UART
//    //DBG_Printf("PROCESS: OFST:%d, IDX:%d, f0:%d, f2:%u", fastOffset, i, fi0, fi2);
//#endif
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//    }
//*/
//
//
//    //Interpolate intermediate values
//    for(i = 1; i <= WWIDTH; i++)
//    {
//        uint32_t fr = i % USEHIST_INTERVAL_FAST;
//
//        if (0 == fr)
//            continue;
//
//        int fi0, fi1, fi2;
//
//        fi0 = i / USEHIST_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= USEHIST_INTERVAL_FAST;
//        fi2 *= USEHIST_INTERVAL_FAST;
//
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        valuesdBI[i] = Yb;
//    }
//
///*
//    //Full Interpolate by Latest Measurement data
//    for(i = fastOffset; i <= WWIDTH; i++)
//    {
//        uint32_t fr = i % MEASURE_INTERVAL_FAST;
//
//        if (fastOffset == fr)
//            continue;
//
//        int fi0, fi2;
//        fi0 = i / MEASURE_INTERVAL_FAST;
//        fi2 = fi0 + 1;
//
//        fi0 *= MEASURE_INTERVAL_FAST;
//        fi2 *= MEASURE_INTERVAL_FAST;
//
//        fi0 += fastOffset;
//        fi2 += fastOffset;
//
//
//        //if (fi2 > WWIDTH)
//        //    continue;
//
//#ifdef _DEBUG_UART
//    //DBG_Printf("PROCESS: OFST:%d, IDX:%d, f0:%d, f2:%u", fastOffset, i, fi0, fi2);
//#endif
//
//        float Yi = valuesdBI[fi0];
//        float Xi = fi0;
//        float Yi1 = valuesdBI[fi2];
//        float Xi1 = fi2;
//        float Xb = i;
//
//        float Yb = Yi + (Yi1 - Yi) * (Xb - Xi) / (Xi1 - Xi);
//        //valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//
//        if (fabs(valuesdBI[i] - Yb) > 20)
//        {
//            valuesdBI[i] = Yb ;
//        }
//        else
//        {
//            valuesdBI[i] = Yb * 0.3 + valuesdBI[i] * 0.7;
//        }
//        //valuesdBI[i] = Yb;
//    }
//*/
//
//}

//Version 0.35

static void MakeFstart(void)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    uint64_t span_hz;
    uint64_t start64;

    f1 = CFG_GetParam(CFG_PARAM_S21_F1);
    span21 = (BANDSPAN)CFG_GetParam(CFG_PARAM_S21_SPAN);
    if ((uint32_t)span21 > (uint32_t)BS100M)
        span21 = BS400;

    span_hz = (uint64_t)BSVALUES_TRACK[span21] * 1000ULL;
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0)
        start64 = f1;
    else
    {
        const uint64_t polowa = span_hz / 2ULL;
        start64 = (uint64_t)f1 > polowa ? (uint64_t)f1 - polowa : (uint64_t)fmin;
    }

    if (start64 < fmin)
        start64 = fmin;
    if (fmax > fmin && span_hz <= (uint64_t)(fmax - fmin) && start64 + span_hz > fmax)
        start64 = (uint64_t)fmax - span_hz;
    if (start64 > UINT32_MAX)
        start64 = UINT32_MAX;

    fstart = (uint32_t)start64;
    freq1 = fstart > 150000U ? fstart - 150000U : fmin;
    if (freq1 < fmin)
        freq1 = fmin;
}

static void RedrawWindowS21(int justGraphDraw);
//static LCDPoint    pt1;
static int touchIndex;

void TrackTouchExecute(LCDPoint pt)
{

    UI_KONTROLKA_t kontrolki[trackMenu_Length];
    TRACK_WypelnijKontrolki(kontrolki);
    touchIndex = UI_ZnajdzKontrolke(pt, kontrolki, trackMenu_Length);
    if (touchIndex < 9)
    { // Beep
        TRACK_Beep(1);
        while (TOUCH_IsPressed())
            ;
        Sleep(100);
    }

    switch (touchIndex)
    { // Exit
    case 0:
    {
        exitScan = 1;
        break;
    }
    case 9:
    { // < Button
        if (FirstRunS21 == 1)
        {
            touchIndex = 8;
            FirstRunS21 = 0;
            break;
        }
        //if(autofast==0)
        MoveCursor(-1);
        break;
    }
    case 10:
    { // > Button
        if (FirstRunS21 == 1)
        {
            touchIndex = 8;
            FirstRunS21 = 0;
            break;
        }
        //if(autofast==0)
        MoveCursor(1);
        break;
    }
    case 1:
    { //SCAN
        if (autofast)
            break;

        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 170, 100, JEZYK_Tekst(TEKST_TRWA_POMIAR));
        redrawRequired = 1;
        break;
    }
    case 2:
    { //ZOOM OUT
        //zoomMinus();
        if (fcur != 0)
            f1 = fcur * 1000.;
        if (span21 > 0)
            span21--;
        redrawRequired = 1;
        break;
    }
    case 3:
    { //ZOOM IN
        //zoomPlus();
        if (fcur != 0)
            f1 = fcur * 1000.;
        if (span21 < BS100M)
            span21++;
        redrawRequired = 1;
        break;
    }
    case 4: //Input Freq
    case 5:
    {
        touchIndex = 4;
        break;
    }
    case 6:
    { //Capture
        LCD_FillRect((LCDPoint){0, 248}, (LCDPoint){50, 271}, BackGrColor);
        save_snapshot();
        //Redraw
        //redrawRequired = 1;
        RedrawWindowS21(0);
        //DrawCursorS
        TRACK_DrawFootText();
        DrawAutoText();
        break;
    }
    case 7:
    { // SetCursor
        //if(autofast!=0) break;
        uint16_t cursorNew = pt.x;
        if (pt.x < X0)
            cursorNew = X0;
        if (pt.x > X0 + WWIDTH)
            cursorNew = WWIDTH;
        MoveCursor(cursorNew - cursorPos - X0);
        break;
    } //end of case
    }
    if (touchIndex == 8)
    {
        // Button Auto
        if (autofast == 0)
        {
            autofast = 1;
            Track_DrawGrid(2);
            cursorChangeCount = 0;
            LCD_FillRect(LCD_MakePoint(40, 15), LCD_MakePoint(459, 225), BackGrColor);
            Track_DrawGrid(1);
        }
        else
        {
            autofast = 0;
            StrictDrawCursor();
            TRACK_DrawFootText();
        }
        return;
    }
}

static int S21_PobierzKalibracjeDb(uint32_t frequency, float *log0, float *log_att)
{
    int idx;
    uint32_t fr1, fr2;
    float prop = 0.0f;
    float v0a, v0b, vatta, vattb;
    float l0a, l0b, latta, lattb;

    if (log0 == 0 || log_att == 0)
        return 0;

    idx = GetIndexForFreq(frequency);
    if (idx < 0)
        return 0;

    fr1 = OSL_GetCalFreqByIdx(idx);
    if (fr1 == 0U)
        return 0;

    osl_txCorr = (OSL_ERRCORR *)WORK_Ptr;
    v0a = osl_txCorr[idx].val0;
    vatta = osl_txCorr[idx].valAtt;
    if (!isfinite(v0a) || !isfinite(vatta) || v0a <= 0.0f || vatta <= 0.0f || vatta >= v0a)
        return 0;

    l0a = 10.0f * log10f(v0a);
    latta = 10.0f * log10f(vatta);
    if (!isfinite(l0a) || !isfinite(latta))
        return 0;

    fr2 = OSL_GetCalFreqByIdx(idx + 1);
    if (fr2 != 0U && frequency > fr1 && frequency < fr2)
    {
        v0b = osl_txCorr[idx + 1].val0;
        vattb = osl_txCorr[idx + 1].valAtt;
        if (!isfinite(v0b) || !isfinite(vattb) || v0b <= 0.0f || vattb <= 0.0f || vattb >= v0b)
            return 0;
        l0b = 10.0f * log10f(v0b);
        lattb = 10.0f * log10f(vattb);
        if (!isfinite(l0b) || !isfinite(lattb))
            return 0;

        /*
         * Charakterystyka transmisji jest wielkością multiplikatywną,
         * dlatego pomiędzy punktami kalibracji interpolujemy poziom w
         * dziedzinie logarytmicznej (dB), a nie surową amplitudę ADC.
         */
        prop = (float)(frequency - fr1) / (float)(fr2 - fr1);
        l0a += (l0b - l0a) * prop;
        latta += (lattb - latta) * prop;
    }

    if (latta >= l0a)
        return 0;
    *log0 = l0a;
    *log_att = latta;
    return 1;
}

float CalcBin107(uint32_t frequency, int iterations, float *val1, float *val2, float *val3)
{
    float sygnal;
    float log_sygnal;
    float log0;
    float log_att;
    float mianownik;
    const float tlumik_db = (float)CFG_GetParam(CFG_PARAM_ATTENUATOR);

    if (val1 != 0)
        *val1 = NAN;
    if (val2 != 0)
        *val2 = NAN;
    if (val3 != 0)
        *val3 = NAN;

    if (frequency < CFG_GetParam(CFG_PARAM_BAND_FMIN) ||
        frequency > CFG_GetParam(CFG_PARAM_BAND_FMAX) ||
        !S21_PobierzKalibracjeDb(frequency, &log0, &log_att))
        return NAN;

    sygnal = DSP_GetValue(frequency, iterations);
    if (!DSP_CzyOstatniPomiarTrackPoprawny() || !isfinite(sygnal) || sygnal <= 0.0f)
        return NAN;

    /*
     * Operujemy w dziedzinie logarytmicznej, a dwa zmierzone poziomy
     * kalibracyjne przypisujemy do 0 dB i wartości znanego tłumika.
     * Dzięki temu nie zakładamy liniowej charakterystyki amplitudowej ADC.
     */
    log_sygnal = 10.0f * log10f(sygnal);
    mianownik = log_att - log0;

    if (!isfinite(log_sygnal) || !isfinite(log0) || !isfinite(log_att) ||
        fabsf(mianownik) < 0.05f || tlumik_db <= 0.0f)
        return NAN;

    if (val1 != 0)
        *val1 = log0;
    if (val2 != 0)
        *val2 = log_att;

    return tlumik_db * (log_sygnal - log0) / mianownik;
}

/*
void SetMeasPointS21B(uint32_t i, int pos, LCDColor col){
int lmod = 5;
int k, length;
    if(pos<Y0) pos=Y0;
    if(pos>Y0+WHEIGHT) pos=Y0+WHEIGHT;

    LCD_SetPixel(LCD_MakePoint(i+X0, pos), col);// set the new pixel(s)
    if(FatLines){
        LCD_SetPixel(LCD_MakePoint(i+X0, pos-1), col);// WK
    }
}
*/

void SetMeasPointS21(uint32_t i, int pos)
{
    int r;
    int k; //, length;
    if (pos < Y0)
        pos = Y0;
    if (pos > Y0 + WHEIGHT)
        pos = Y0 + WHEIGHT;

    if (i == cursorPos)
        StrictDelCursor();

    c31 = LCD_ReadPixel(LCD_MakePoint(i + X0, Y0 + 31)); // no horizontal line between 30..39
    c35 = LCD_ReadPixel(LCD_MakePoint(i + X0, Y0 + 35));
    c = c31;
    if (c31 == CurvColor)
        c = c35;
    LCD_VLine(LCD_MakePoint(i + X0, Y0), WHEIGHT + 2, c); //draw original
    r = 10 * Y0;
    for (k = 0; k < 14; k++)
    { // horizontal lines
        LCD_SetPixel(LCD_MakePoint(i + X0, r / 10), WGRIDCOLOR);
        if (k % 2 == 0)
            LCD_SetPixel(LCD_MakePoint(i + X0, r / 10 + 1), WGRIDCOLOR);
        r += 146;
    }
    if (i == cursorPos)
        StrictDrawCursor();
    LCD_SetPixel(LCD_MakePoint(i + X0, pos), CurvColor); // set the new pixel(s) over cursor line
    if (FatLines)
    {
        LCD_SetPixel(LCD_MakePoint(i + X0, pos - 1), CurvColor);
        LCD_SetPixel(LCD_MakePoint(i + X0, pos + 1), CurvColor);
    }
}

static void Track_DrawCurve(void);

static void Scan21Fast(void)
{
    if (S21_SkanDoBufora((uint32_t)autoScanFactor, 1))
        Track_DrawCurve();
}

int lastoffset;

static void Track_DrawCurve(void) // SelQu=1, if quartz measurement  SelEqu=1, if equal scales
{
#define LimitR 1999.f
    int i; //, imax;
    int x, yofs;

    if (!isMeasured)
        return;
    Track_DrawGrid(1);

    for (i = 0; i <= WWIDTH; i++)
    {
        if (s21_punkt_poprawny == 0 || !s21_punkt_poprawny[i] || !isfinite(valuesmI[i]))
            continue;
        x = X0 + i;
        yofs = (int)(valuesmI[i] / 65.0f * WHEIGHT) + Y0;

        if (yofs > Y0 + WHEIGHT)
            yofs = Y0 + WHEIGHT;
        else if (yofs < Y0)
            yofs = Y0;

        LCD_SetPixel(LCD_MakePoint(x, yofs), CurvColor);
        if (FatLines)
        {
            if (yofs < Y0 + WHEIGHT)
                LCD_SetPixel(LCD_MakePoint(x, yofs + 1), CurvColor);
            if (yofs > Y0)
                LCD_SetPixel(LCD_MakePoint(x, yofs - 1), CurvColor);
        }
    }
    StrictDrawCursor();
}

static void RedrawWindowS21(int justGraphDraw)
{
    Track_DrawGrid(justGraphDraw);
    Track_DrawCurve(); // 1

    if (!justGraphDraw)
    {
        TRACK_DrawFootText();
    }
    DrawMeasuredValues();
    DrawAutoText();
}

#define XX0 190
#define YY0 42

static void save_snapshot(void)
{
    //static const TCHAR *sndir = "/aa/snapshot";
    //char path[64];
    //char wbuf[256];
    char *fname = 0;
    //uint32_t i = 0;
    //FRESULT fr = FR_OK;

    if (!isMeasured)
        return;

    Date_Time_Stamp();

    fname = SCREENSHOT_SelectFileName();

    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);

    //DrawSavedText();
    //static const char* txt = "  Snapshot saved  ";
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKTYWNY), BackGrColor, 165, Y0 + WHEIGHT + 16 + 16,
               JEZYK_Wybierz("Zrzut zapisany", "Snapshot saved", "Bild gespeichert", "Снимок сохранён"));
    TRACK_DrawFootText();
    DrawAutoText();

    return;
}

//=====================================================================
//TRACK PROC
//---------------------------------------------------------------------

void Track_Proc(void) // ==================== S21 screen ====================================
{
    uint32_t fxkHzs; //Scan range start frequency, in kHz

    /* S21 potrzebuje niezaleznego CLK2 jako zrodla transmisyjnego. W wariancie
     * 2x ADF ten sam CLK2 jest referencja 27 MHz i nie wolno go przestrajac. */
    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
    {
        UI_RysujEkranPrzejsciowy(
            JEZYK_Tekst(TEKST_S21_TYTUL),
            JEZYK_Wybierz("Niedostępne: CLK2 pracuje jako wzorzec ADF.",
                          "Unavailable: CLK2 is used as the ADF reference.",
                          "Nicht verfügbar: CLK2 ist ADF-Referenz.",
                          "Недоступно: CLK2 используется как опора ADF."));
        Sleep(1800U);
        return;
    }
    uint32_t fxs;
    //extern int OSL_ENTRIES;

    redrawRequired = exitScan = 0;
    IsInverted = false;
    // Bufor wyniku i osobna maska wiarygodności każdego punktu.
    valuesmI = (float *)SDRH_malloc(sizeof(float) * (WWIDTH + 1U));
    s21_punkt_poprawny = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    if (valuesmI == 0 || s21_punkt_poprawny == 0)
    {
        if (valuesmI != 0)
            SDRH_free(valuesmI);
        if (s21_punkt_poprawny != 0)
            SDRH_free(s21_punkt_poprawny);
        valuesmI = 0;
        s21_punkt_poprawny = 0;
        GEN_SetTXFreq(0);
        return;
    }
    memset(valuesmI, 0, sizeof(float) * (WWIDTH + 1U));
    memset(s21_punkt_poprawny, 0, sizeof(uint8_t) * (WWIDTH + 1U));
    memset(&g_s21_analiza, 0, sizeof(g_s21_analiza));
    activeLayerS21 = 1;
    BSP_LCD_SelectLayer(activeLayerS21);
    LCD_ShowActiveLayerOnly();
    cursorVisibleS21 = 0; // in the beginning not visible
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 1)
        cursorPos = WWIDTH / 2;
    else
        cursorPos = 0;
    /* SetColours ustala paletę samego wykresu. Nie nadpisujemy jej
     * paletą menu, bo wtedy przełącznik jasne/ciemne nie miał żadnego efektu. */
    SetColours();
    LCD_FillAll(BackGrColor);
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_S21_TYTUL));
    FONT_Write(FONT_FRAN, TextColor, BackGrColor,
               66, 36, JEZYK_Tekst(TEKST_S21_SKALARNE_INFO));
    while (TOUCH_IsPressed())
        ;
    //Load Calibration
    if (!OSL_IsTXCorrLoaded())
    {
        OSL_LoadTXCorr();
#ifdef _DEBUG_UART
        DBG_Str("Load VNA Calibration File \r\n");
#endif
    }
    if (!OSL_IsTXCorrLoaded())
    {
        // Brak pliku kalibracji S21 nie jest awaria calego przyrzadu.
        // Informujemy uzytkownika i wracamy do menu bez uruchamiania pomiaru.
        char pomoc_s21[128];
        UI_RysujPoleStatusu(65, 82, 350, 78, JEZYK_Tekst(TEKST_S21_TYTUL),
                            JEZYK_Tekst(TEKST_S21_BRAK_KALIBRACJI), UI_STYL_OSTRZEZENIE);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor,
                   38, 172, JEZYK_Wybierz("Kalibracja -> S21: krok 1 S2 -> S1 bez tłumika.",
                                          "Calibration -> S21: step 1 S2 -> S1, no attenuator.",
                                          "Kalibrierung -> S21: Schritt 1 S2 -> S1 ohne Dämpfer.",
                                          "Калибровка -> S21: шаг 1 S2 -> S1 без аттенюатора."));
        snprintf(pomoc_s21, sizeof(pomoc_s21), "%s %lu dB",
                 JEZYK_Wybierz("Krok 2: wstaw tłumik 50 om:", "Step 2: insert 50-ohm attenuator:",
                               "Schritt 2: 50-Ohm-Dämpfer einsetzen:", "Шаг 2: установите аттенюатор 50 Ом:"),
                 (unsigned long)CFG_GetParam(CFG_PARAM_ATTENUATOR));
        FONT_Write(FONT_FRAN, TextColor, BackGrColor,
                   38, 194, pomoc_s21);
        Sleep(4200);
        SDRH_free(valuesmI);
        SDRH_free(s21_punkt_poprawny);
        valuesmI = 0;
        s21_punkt_poprawny = 0;
        return;
    }
    osl_txCorr = (OSL_ERRCORR *)WORK_Ptr;

    //Load saved frequency and span values from config file
    uint32_t fbkup = CFG_GetParam(CFG_PARAM_S21_F1);

    //2019.03.26
    //if (fbkup != 0 && fbkup >= BAND_FMIN && fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    if (fbkup != 0 && fbkup >= CFG_GetParam(CFG_PARAM_BAND_FMIN) &&
        fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    {
        f1 = fbkup;
    }
    else
        f1 = 14000000;
    CFG_SetParam(CFG_PARAM_S21_F1, f1);
    CFG_Flush();

    span21 = (BANDSPAN)CFG_GetParam(CFG_PARAM_S21_SPAN);
    if ((uint32_t)span21 > (uint32_t)BS100M)
        span21 = BS400;
    autoScanFactor = (char)CFG_GetParam(CFG_PARAM_S21_AUTOSPEED);
    if (autoScanFactor < 1 || autoScanFactor > 20)
        autoScanFactor = 8;
    MakeFstart();

    Track_DrawGrid(0);
    TRACK_DrawFootText();
    DrawAutoText();
    BSP_LCD_SelectLayer(activeLayerS21);
    LCD_FillAll(BackGrColor);
    BSP_LCD_SelectLayer((1 + activeLayerS21) % 2);
    LCD_FillAll(BackGrColor);
    Track_DrawGrid(0);
    BSP_LCD_SelectLayer(activeLayerS21);
    exitScan = 0;
    FirstRunS21 = 1;
    redrawRequired = 0;
    autofast = 0;

    for (;;)
    {
        Sleep(0);
        if (TOUCH_Poll(&pt0))
        { // touch check
            TrackTouchExecute(pt0);
            if (exitScan == 1)
                break;
        }
        else
        {
            cursorChangeCount = 0;
            trackbeep = 0;
        }                    //end of
        if (touchIndex == 4) // new frequency
        {
            fxs = CFG_GetParam(CFG_PARAM_S21_F1);
            fxkHzs = fxs / 1000;
            if (PanFreqWindow(&fxkHzs, (BANDSPAN *)&span21))
            {
                f1 = fxkHzs * 1000;
                CFG_SetParam(CFG_PARAM_S21_F1, f1);
                CFG_SetParam(CFG_PARAM_S21_SPAN, span21);
                CFG_Flush();
                MakeFstart(); // and span
                isMeasured = 0;
                Track_DrawGrid(2);
                /* BSP_LCD_SelectLayer((1+activeLayerS21)%2);
                Track_DrawGrid(2);
                BSP_LCD_SelectLayer(activeLayerS21);*/
                redrawRequired = 0;
                IsInverted = false;
            }
            touchIndex = 255;
        }

        if (autofast) //Auto Scan
        {

            /* activeLayerS21=(1+activeLayerS21)%2;
            BSP_LCD_SelectLayer(activeLayerS21);
            BSP_LCD_SetLayerVisible_NoReload(activeLayerS21, 1);//  ?
            BSP_LCD_SetLayerVisible((1+activeLayerS21)%2, 0);*/
            Scan21Fast();
            DrawMeasuredValues();
            /* BSP_LCD_SetLayerVisible_NoReload(activeosl_txCorrLayerS21, 1);//  ?
            BSP_LCD_SetLayerVisible((1+activeLayerS21)%2, 0);*/
            redrawRequired = 0;
        }
        if (redrawRequired)
        {
            Scan21(0);
            RedrawWindowS21(0);
            redrawRequired = 0;
        }
        LCD_ShowActiveLayerOnly();
    }                   //end of for(;;)
    GEN_SetClk2Freq(0); // CLK2 off
    //Release Memory
    SDRH_free(valuesmI);
    SDRH_free(s21_punkt_poprawny);
    valuesmI = 0;
    s21_punkt_poprawny = 0;
    isMeasured = 0;
}
