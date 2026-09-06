/*
  KD8CEC's AUDIO DSP for HAM
  kd8cec@gmail.com

  by KD8CEC
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
//#include "smith.h"
//#include "measurement.h"
#include <stdint.h>
#include "arm_math.h"

#include "ui_wspolny.h"
#include "jezyk.h"
#include "audioirq.h"

#define BACK_COLOR LCD_RGB(10, 52, 74)
#define SELECT_FREQ_COLOR LCD_RGB(255, 127, 39)
#define SELECT_PROTOCOL_COLOR LCD_RGB(255, 30, 30)

#define GRAPH_TOP 0
#define GRAPH_BOTTOM 70
#define METER_HEIGHT 15
#define GRAPH_WATERFALL_TOP GRAPH_BOTTOM + METER_HEIGHT
/* Wodospad nie może dochodzić do pola stanu filtra. Wcześniej kończył się
 * na y=185 i kolejne odświeżenia zasłaniały opis trybu oraz napis nad nim. */
#define GRAPH_WATERFALL_HEIGHT 74
#define GRAPH_WATERFALL_BOTTOM GRAPH_WATERFALL_TOP + GRAPH_WATERFALL_HEIGHT
#define DSP_INFO_Y 162U
#define DSP_INFO_DOL_Y 218U

/* Dotknięcie wodospadu przełącza widok roboczy <-> pełny ekran. */
static uint8_t dsp_wodospad_pelny = 0U;

//Default Filters
#include "flt_lpf.h"
#include "flt_hpf.h"
#include "flt_bpf_50.h"
#include "flt_bpf_100.h"
#include "flt_bpf_150.h"
#include "flt_stop_50.h"

#include "flt_windows.h"

extern void Sleep(uint32_t ms);
extern void TRACK_Beep(int duration);

//=============================================================================
//VARIABLE DEFINE FOR FFT, FIR, IIR
//By KD8CEC
//-----------------------------------------------------------------------------
#define AUDIO_BUFF_SIZE ((uint32_t)512) //HALF 256, COMPLETE 256
#define FFT_BIN_FREQ (float)7.8125      //FFT SPECTRUM (FFT BIN)
//#define AUDIO_BUFFER_RAM AUDIO_BUFFER_OUT

//Share SD-RAM for Audio IN / OUT
//Received Buffer (MEMS MIC on board or MIC JACK)
static int16_t __attribute__((section(".user_sdram"))) AUDIO_BUFFER_IN_FLT[AUDIO_BUFF_SIZE] = {0};
//int16_t *AUDIO_BUFFER_IN_FLT = &AUDIO_BUFFER_RAM[0];

//Output Buffer (Phone Jack)
static int16_t __attribute__((section(".user_sdram"))) AUDIO_BUFFER_OUT_FLT[AUDIO_BUFF_SIZE] = {0};
//int16_t *AUDIO_BUFFER_OUT_FLT = &AUDIO_BUFFER_RAM[AUDIO_BUFF_SIZE];

//for iir
static float __attribute__((section(".user_sdram"))) float_buffer_in[AUDIO_BUFF_SIZE * 2] = {0};
static float __attribute__((section(".user_sdram"))) float_buffer_out[AUDIO_BUFF_SIZE * 2] = {0};
//float *float_buffer_in  = (float *)&AUDIO_BUFFER_RAM[AUDIO_BUFF_SIZE * 2];
//float *float_buffer_out = (float *)&AUDIO_BUFFER_RAM[AUDIO_BUFF_SIZE * 6];

#define iirStageCount 6
#define iirTapsCount 5 * iirStageCount //2 order : 5, B0, b1, B2, A1, B2
//float iirStateBuff[2 * iirStageCount];
//float iirStateBuff_HPF[2 * iirStageCount];
static float __attribute__((section(".user_sdram"))) iirStateBuff[2 * iirStageCount];
static float __attribute__((section(".user_sdram"))) iirStateBuff_HPF[2 * iirStageCount];

#define iirLength AUDIO_BUFF_SIZE / 2 //

//BASIC FILTER
//float iirCoeffBuff[30] = {
static float __attribute__((section(".user_sdram"))) iirCoeffBuff[30];
//float iirCoeffBuff_HPF[30] = {
static float __attribute__((section(".user_sdram"))) iirCoeffBuff_HPF[30];

//Spectrum Buffer
#define FFT_SPECTRUM_STEP 16
#define FFT_AUDIO_SIZE 128 //512 => 256 => 128 (LEFT CHANNEL )
#define FFT_SPECTRUM_SIZE FFT_AUDIO_SIZE *FFT_SPECTRUM_STEP
static float __attribute__((section(".user_sdram"))) SpectrumBuff[FFT_SPECTRUM_SIZE * 2] = {0};
static float __attribute__((section(".user_sdram"))) testOutput[FFT_SPECTRUM_SIZE] = {0};
//static float __attribute__((section (".user_sdram"))) windowData[FFT_AUDIO_SIZE] = { 0 };

//EXIT, SAVECONFIG, BPF, LPF, HPF, SSB
void SetCenterFrequency(arm_biquad_cascade_df2T_instance_f32 instMain, arm_biquad_cascade_df2T_instance_f32 instSecond, uint32_t newCenterFreq, uint32_t isChangedFilter);

#define FILTER_MINIMUM_FREQ 300
#define FILTER_MAXIMUM_FREQ 3000

//Draw ImageButton
//BOTTOM
#define MENU_EXIT 0
#define MENU_SAVECONFIG 1
#define MENU_BPF 2
#define MENU_USERFILTER 3 //Probe Select Down
#define MENU_BYPASS 4     //Probe Select Up

//RIGHT
#define MENU_AFFREQDOWN 5 //-Hz Center Freq
#define MENU_AFFREQUP 6   //+Hz Center Freq

//FREQ SELECT MENU
#define MENU_TOP1 9
#define MENU_TOP2 10
#define MENU_TOP3 11
#define MENU_TOP4 12

#define dspMenus_Length 7

#define FREQ_MENU_TOP 95
#define TOP_MENU_TOP 0
#define FREQ_INFO_TOP 80


//uint8_t isOnAir = 1;

#define DSP_FILTER_NONE 0
#define DSP_FILTER_BPF 1
#define DSP_FILTER_USER 2

//for FILTER
#define DSP_FILTER_BYPASS 0
#define DSP_FILTER_BPF_50 1
#define DSP_FILTER_BPF_100 2
#define DSP_FILTER_BPF_150 3
#define DSP_FILTER_LPF 4
#define DSP_FILTER_HPF 5
#define DSP_FILTER_SSB 10 //DUAL LPF, HPF

//Filter Type
/*
uint8_t DSP_OUT_VOL       = 100;                //0
uint8_t isTmpMute         = 0;                  //1
uint8_t nowSelectedFilter = DSP_FILTER_NONE;    //2
uint8_t nowBPFFilterIndex = 0;  //              //3
uint8_t filterType        = DSP_FILTER_BYPASS;  //4
int filterApplyFreq1 	= 1000;                 //5~8
int filterApplyFreq2    = 200;  //HPF           //9~12
int userFilterHalfWidth = 100;                  //13~16
*/

/*
 * Ustawienia DSP są przechowywane w zwykłych, wyrównanych zmiennych.
 * Historyczna wersja trzymała liczby int pod adresami +5, +9 i +13 w tablicy
 * bajtów. Na Cortex-M7 takie niewyrównane wskaźniki są niepotrzebnym ryzykiem.
 * Format pliku 20 B pozostaje jednak zgodny wstecznie, więc użytkownik nie traci
 * dotychczasowego /aa/audiodsp.bin.
 */
static uint8_t dsp_out_vol = 100U;
static uint8_t dsp_tmp_mute = 0U;
static uint8_t dsp_wybrany_filtr = DSP_FILTER_NONE;
static uint8_t dsp_indeks_bpf = 0U;
static uint8_t dsp_typ_filtra = DSP_FILTER_BYPASS;
static int32_t dsp_czestotliwosc_1_hz = 1000;
static int32_t dsp_czestotliwosc_2_hz = 500;
static int32_t dsp_pol_szerokosci_hz = 500;

uint8_t *DSP_OUT_VOL = &dsp_out_vol;
uint8_t *isTmpMute = &dsp_tmp_mute;
uint8_t *nowSelectedFilter = &dsp_wybrany_filtr;
uint8_t *nowBPFFilterIndex = &dsp_indeks_bpf;
uint8_t *filterType = &dsp_typ_filtra;
int32_t *filterApplyFreq1 = &dsp_czestotliwosc_1_hz;
int32_t *filterApplyFreq2 = &dsp_czestotliwosc_2_hz;
int32_t *userFilterHalfWidth = &dsp_pol_szerokosci_hz;

#include "ff.h"
#include "crash.h"
static const char *g_dsp_fpath = "/aa/audiodsp.bin";
#define DSP_PLIK_ROZMIAR 20U

static int32_t DSP_CzytajInt32LE(const uint8_t *dane)
{
    uint32_t wartosc = (uint32_t)dane[0]
                     | ((uint32_t)dane[1] << 8)
                     | ((uint32_t)dane[2] << 16)
                     | ((uint32_t)dane[3] << 24);
    return (int32_t)wartosc;
}

static void DSP_ZapiszInt32LE(uint8_t *dane, int32_t wartosc)
{
    const uint32_t v = (uint32_t)wartosc;
    dane[0] = (uint8_t)(v & 0xFFU);
    dane[1] = (uint8_t)((v >> 8) & 0xFFU);
    dane[2] = (uint8_t)((v >> 16) & 0xFFU);
    dane[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void DSP_UstawDomyslne(void)
{
    *DSP_OUT_VOL = 100U;
    *isTmpMute = 0U;
    *nowSelectedFilter = DSP_FILTER_NONE;
    *nowBPFFilterIndex = 0U;
    *filterType = DSP_FILTER_BYPASS;
    *filterApplyFreq1 = 1000;
    *filterApplyFreq2 = 500;
    *userFilterHalfWidth = 500;
}

static uint8_t DSP_CzyUstawieniaPoprawne(void)
{
    const uint8_t typ = *filterType;
    const uint8_t typ_poprawny = (typ == DSP_FILTER_BYPASS ||
                                  typ == DSP_FILTER_BPF_50 ||
                                  typ == DSP_FILTER_BPF_100 ||
                                  typ == DSP_FILTER_BPF_150 ||
                                  typ == DSP_FILTER_LPF ||
                                  typ == DSP_FILTER_HPF ||
                                  typ == DSP_FILTER_SSB);

    return (*DSP_OUT_VOL >= 10U && *DSP_OUT_VOL <= 100U &&
            *nowSelectedFilter <= DSP_FILTER_USER &&
            *nowBPFFilterIndex <= 2U && typ_poprawny &&
            *filterApplyFreq1 >= FILTER_MINIMUM_FREQ &&
            *filterApplyFreq1 <= FILTER_MAXIMUM_FREQ &&
            *filterApplyFreq2 >= FILTER_MINIMUM_FREQ &&
            *filterApplyFreq2 <= FILTER_MAXIMUM_FREQ &&
            *userFilterHalfWidth > 0 &&
            *userFilterHalfWidth <= FILTER_MAXIMUM_FREQ);
}



void DSP_StoredInformatoin(void)
{
    uint8_t dane[DSP_PLIK_ROZMIAR] = {0};
    FRESULT res;
    FIL fo = {0};
    UINT bw = 0U;

    dane[0] = *DSP_OUT_VOL;
    dane[1] = 0U; /* Stan chwilowego wyciszenia nie jest ustawieniem trwałym. */
    dane[2] = *nowSelectedFilter;
    dane[3] = *nowBPFFilterIndex;
    dane[4] = *filterType;
    DSP_ZapiszInt32LE(&dane[5], *filterApplyFreq1);
    DSP_ZapiszInt32LE(&dane[9], *filterApplyFreq2);
    DSP_ZapiszInt32LE(&dane[13], *userFilterHalfWidth);

    res = f_open(&fo, g_dsp_fpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK)
    {
        res = f_write(&fo, dane, sizeof(dane), &bw);
        (void)res;
        (void)f_close(&fo);
    }
}

void DSP_LoadInformation(void)
{
    uint8_t dane[DSP_PLIK_ROZMIAR] = {0};
    FIL fo = {0};
    FILINFO finfo;
    UINT br = 0U;
    FRESULT res;

    DSP_UstawDomyslne();
    res = f_stat(g_dsp_fpath, &finfo);
    if (res != FR_OK || finfo.fsize < 17U)
        return;

    res = f_open(&fo, g_dsp_fpath, FA_READ);
    if (res != FR_OK)
        return;

    res = f_read(&fo, dane, sizeof(dane), &br);
    (void)f_close(&fo);
    if (res != FR_OK || br < 17U)
        return;

    *DSP_OUT_VOL = dane[0];
    *nowSelectedFilter = dane[2];
    *nowBPFFilterIndex = dane[3];
    *filterType = dane[4];
    *filterApplyFreq1 = DSP_CzytajInt32LE(&dane[5]);
    *filterApplyFreq2 = DSP_CzytajInt32LE(&dane[9]);
    *userFilterHalfWidth = DSP_CzytajInt32LE(&dane[13]);
    *isTmpMute = 0U;

    if (!DSP_CzyUstawieniaPoprawne())
        DSP_UstawDomyslne();
}

//==============================================================================
//DISPLAY Protocol Information
//------------------------------------------------------------------------------
#define INFO_TOP 130
#define INFO_LINE2 183

//#define TITLE_COLOR LCD_RGB(0, 63, 119)
#define TITLE_COLOR LCD_RGB(2, 26, 39)

static char g_dsp_bpf_etykieta[24];

static void DSP_WypelnijKontrolki(UI_KONTROLKA_t kontrolki[dspMenus_Length])
{
    if (*nowBPFFilterIndex == 0U)
        snprintf(g_dsp_bpf_etykieta, sizeof(g_dsp_bpf_etykieta), "BPF (50 Hz)");
    else if (*nowBPFFilterIndex == 1U)
        snprintf(g_dsp_bpf_etykieta, sizeof(g_dsp_bpf_etykieta), "BPF (100 Hz)");
    else
        snprintf(g_dsp_bpf_etykieta, sizeof(g_dsp_bpf_etykieta), "BPF (150 Hz)");

    {
        UI_PROSTOKAT_t o;
        o = UI_ObszarPrzyciskuDolnego(0U);
        kontrolki[MENU_EXIT] = UI_UtworzKontrolke(MENU_EXIT, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Wstecz", "Back", "Zurück", "Назад"),
            UI_STYL_POWROT, UI_ROLA_TEKSTU_PRZYCISK, true, false);
        o = UI_ObszarPrzyciskuDolnego(1U);
        kontrolki[MENU_SAVECONFIG] = UI_UtworzKontrolke(MENU_SAVECONFIG, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Zapisz", "Save", "Speich.", "Сохр."),
            UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true, false);
        o = UI_ObszarPrzyciskuDolnego(2U);
        kontrolki[MENU_BPF] = UI_UtworzKontrolke(MENU_BPF, o.x, o.y, o.szerokosc, o.wysokosc,
            g_dsp_bpf_etykieta, UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true,
            *nowSelectedFilter == DSP_FILTER_BPF);
        o = UI_ObszarPrzyciskuDolnego(3U);
        kontrolki[MENU_USERFILTER] = UI_UtworzKontrolke(MENU_USERFILTER, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Własny", "User", "Eigener", "Свой"),
            UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true,
            *nowSelectedFilter == DSP_FILTER_USER);
        o = UI_ObszarPrzyciskuDolnego(4U);
        kontrolki[MENU_BYPASS] = UI_UtworzKontrolke(MENU_BYPASS, o.x, o.y, o.szerokosc, o.wysokosc,
            JEZYK_Wybierz("Bez filtra", "Bypass", "Ohne", "Выкл."),
            UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK, true,
            *nowSelectedFilter == DSP_FILTER_NONE);
    }

    /* Strojenie jest osobną parą nad paskiem. Nie wolno mu wchodzić w y>=220. */
    kontrolki[MENU_AFFREQDOWN] = UI_UtworzKontrolke(MENU_AFFREQDOWN, 286, 174, 90, 42,
        "-1 Hz", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK,
        *nowSelectedFilter != DSP_FILTER_NONE, false);
    kontrolki[MENU_AFFREQUP] = UI_UtworzKontrolke(MENU_AFFREQUP, 382, 174, 90, 42,
        "+1 Hz", UI_STYL_NORMALNY, UI_ROLA_TEKSTU_PRZYCISK,
        *nowSelectedFilter != DSP_FILTER_NONE, false);
}

void dspMenuDraw(void)
{
    UI_KONTROLKA_t kontrolki[dspMenus_Length];
    DSP_WypelnijKontrolki(kontrolki);
    UI_RysujKontrolki(kontrolki, dspMenus_Length);
}

void ApplyBiquad(int fltType, arm_biquad_cascade_df2T_instance_f32 instMain, arm_biquad_cascade_df2T_instance_f32 instSecond, q15_t *srcBuff, q15_t *destBuff)
{
    //SpectrumBuff[buffIndex + i * 2] = inBuff[i * 2] * (WINDOW_FUNC((double)(i-(double)FFT_AUDIO_SIZE/2) / (double)((double)FFT_AUDIO_SIZE/2)));    //1 Channel of streo => Good!!!

    //Just Main Filterring
    for (int i = 0; i < iirLength / 2; i++)
    {
        //APPLY WINDOW FUNCTION
        //srcBuff[i * 2 + 1] *= (WINDOW_FUNC((double)(i-(double)FFT_AUDIO_SIZE/2) / (double)((double)FFT_AUDIO_SIZE/2)));    //1 Channel of streo => Good!!!

        srcBuff[i * 2 + 1] = 0;
    }

    if (fltType == DSP_FILTER_BYPASS)
    {
        //bypass
        memcpy(destBuff, srcBuff, AUDIO_BUFF_SIZE); //512
    }
    else if (fltType == DSP_FILTER_SSB)
    {
        arm_q15_to_float(srcBuff, (float32_t *)float_buffer_in, iirLength);
        //Apply Main Filter
        arm_biquad_cascade_df2T_f32(&instMain, (float32_t *)float_buffer_in, (float32_t *)float_buffer_out, iirLength);

        //Apply Second Filter
        arm_biquad_cascade_df2T_f32(&instSecond, (float32_t *)float_buffer_out, (float32_t *)float_buffer_in, iirLength);
        arm_float_to_q15((float32_t *)float_buffer_in, destBuff, iirLength);
    }
    else
    {
        //Just Main Filterring
        //Apply Main Filter
        arm_q15_to_float(srcBuff, (float32_t *)float_buffer_in, iirLength);
        arm_biquad_cascade_df2T_f32(&instMain, (float32_t *)float_buffer_in, (float32_t *)float_buffer_out, iirLength);
        arm_float_to_q15((float32_t *)float_buffer_out, destBuff, iirLength);
    }
}

//FILTER INFORMATION
int FLT_COUNT = FLT_LPF_COUNT;
const int *FLT_INDEXS = FLT_LPF_INDEX;
//const float (*APPLY_FILTER)[30]     = FLT_LPF;
//uint32_t FLT_MINFREQ 		        = FLT_LPF_MINFREQ;
//uint32_t FLT_MAXFREQ 		        = FLT_LPF_MAXFREQ;
//const float (*APPLY_FILTER_HPF)[30]	= FLT_HPF;

int filterStartIndex = (1000 - 50) / FFT_BIN_FREQ;
int filterEndIndex = (1000 + 50) / FFT_BIN_FREQ;
int filterStartIndexMin = (1000 - 50) / FFT_BIN_FREQ;
int filterEndIndexMax = (1000 + 50) / FFT_BIN_FREQ;

int findCenterFreq(int targetFreq)
{
    if (targetFreq <= FLT_INDEXS[0])
    {
        return 0;
    }
    else if (targetFreq >= FLT_INDEXS[FLT_COUNT - 1])
    {
        return FLT_COUNT - 1;
    }

    for (int i = FLT_COUNT - 1; i >= 0; i--)
    {
        if (FLT_INDEXS[i] <= targetFreq)
        {
            return i;
        }
    }
    return 0;
}

float FloatInterpolation(float y1, float y2, float y3, //values for frequencies x1, x2, x3
                         float x1, float x2, float x3, //frequencies of respective y values
                         float x)                      //Frequency between x2 and x3 where we want to interpolate result
{
    float a = ((y3 - y2) / (x3 - x2) - (y2 - y1) / (x2 - x1)) / (x3 - x1);
    float b = ((y3 - y2) / (x3 - x2) * (x2 - x1) + (y2 - y1) / (x2 - x1) * (x3 - x2)) / (x3 - x1);
    float res = a * powf(x - x2, 2.0) + b * (x - x2) + y2;

    return res;
}

void ApplyCoeffs(uint32_t targetFreq)
{
    const float (*tabela)[30] = 0;
    int x1, x2, x3;
    int baseIndex;
    char buff[64];
    char buffInfo[64];

    if (*filterType == DSP_FILTER_BPF_50)
        tabela = FLT_BPF_50;
    else if (*filterType == DSP_FILTER_BPF_100)
        tabela = FLT_BPF_100;
    else if (*filterType == DSP_FILTER_BPF_150)
        tabela = FLT_BPF_150;
    else if (*filterType == DSP_FILTER_LPF || *filterType == DSP_FILTER_SSB)
        tabela = FLT_LPF;
    else if (*filterType == DSP_FILTER_HPF)
        tabela = FLT_HPF;

    /* Bypass nie używa IIR, więc współczynników nie trzeba liczyć. */
    if (tabela != 0)
    {
        baseIndex = findCenterFreq((int)targetFreq);
        if (baseIndex == 0)
            baseIndex = 1;
        else if (baseIndex >= FLT_COUNT - 1)
            baseIndex = FLT_COUNT - 2;

        x1 = FLT_INDEXS[baseIndex - 1];
        x2 = FLT_INDEXS[baseIndex];
        x3 = FLT_INDEXS[baseIndex + 1];

        for (int i = 0; i < 30; i++)
        {
            iirCoeffBuff[i] = FloatInterpolation(tabela[baseIndex - 1][i],
                                                 tabela[baseIndex][i],
                                                 tabela[baseIndex + 1][i],
                                                 x1, x2, x3, targetFreq);
        }
    }

    if (*filterType == DSP_FILTER_BYPASS)
    {
        snprintf(buff, sizeof(buff), "%s",
                 JEZYK_Wybierz("TRYB PRZELOTOWY", "PASS THROUGH MODE",
                               "DURCHSCHLEIFMODUS", "СКВОЗНОЙ РЕЖИМ"));
        snprintf(buffInfo, sizeof(buffInfo), "%s",
                 JEZYK_Wybierz("Filtr: bez filtra", "Filter: bypass",
                               "Filter: aus", "Фильтр: выключен"));
    }
    else if (*filterType == DSP_FILTER_SSB)
    {
        snprintf(buff, sizeof(buff),
                 JEZYK_Wybierz("PASMO: %d - %d Hz", "BAND: %d - %d Hz",
                               "BAND: %d - %d Hz", "ПОЛОСА: %d - %d Гц"),
                 (int)*filterApplyFreq2, (int)*filterApplyFreq1);
        snprintf(buffInfo, sizeof(buffInfo),
                 JEZYK_Wybierz("Filtr: BPF %d Hz", "Filter: BPF %d Hz",
                               "Filter: BPF %d Hz", "Фильтр: BPF %d Гц"),
                 *nowBPFFilterIndex == 0 ? 50 : (*nowBPFFilterIndex == 1 ? 100 : 150));
    }
    else
    {
        snprintf(buff, sizeof(buff),
                 JEZYK_Wybierz("SRODEK: %d Hz", "CENTER: %d Hz",
                               "MITTE: %d Hz", "ЦЕНТР: %d Гц"),
                 (int)*filterApplyFreq1);
        snprintf(buffInfo, sizeof(buffInfo), "%s",
                 JEZYK_Wybierz("Filtr: uzytkownika", "Filter: user",
                               "Filter: Benutzer", "Фильтр: свой"));
    }

    /* Informacja o filtrze ma własne pole po lewej stronie. W wcześniejszej weryfikacji jej
     * obramowanie przechodziło przez przyciski strojenia i dolny pasek. */
    LCD_FillRect(LCD_MakePoint(0, DSP_INFO_Y), LCD_MakePoint(275, DSP_INFO_DOL_Y), LCD_RGB(8, 34, 47));
    LCD_Rectangle(LCD_MakePoint(0, DSP_INFO_Y), LCD_MakePoint(275, DSP_INFO_DOL_Y), UI_KolorRamki(UI_STYL_NORMALNY));

    FONT_Write(FONT_FRAN, TextColor, LCD_RGB(8, 34, 47), 10, DSP_INFO_Y + 3U, buffInfo);
    FONT_Write(FONT_FRANBIG, TextColor, LCD_RGB(8, 34, 47), 10, DSP_INFO_Y + 23U, buff);
}

void ApplyCoeffs_HPF(uint32_t targetFreq)
{
    int x1, x2, x3;

    int baseIndex = findCenterFreq(targetFreq);

    if (baseIndex == 0)
    {
        baseIndex = 1;
    }
    else if (baseIndex >= FLT_COUNT - 1)
    {
        baseIndex = FLT_COUNT - 2;
    }

    x1 = FLT_INDEXS[baseIndex - 1];
    x2 = FLT_INDEXS[baseIndex];
    x3 = FLT_INDEXS[baseIndex + 1];
    for (int i = 0; i < 30; i++)
    {
        iirCoeffBuff_HPF[i] = FloatInterpolation(FLT_HPF[baseIndex - 1][i], FLT_HPF[baseIndex][i], FLT_HPF[baseIndex + 1][i],
                                                 x1, x2, x3, targetFreq);
    }
}

void SetFFTFilter(uint8_t newFilter, arm_biquad_cascade_df2T_instance_f32 instMain, arm_biquad_cascade_df2T_instance_f32 instSecond)
{
    if (newFilter == DSP_FILTER_BPF_50)
    {
        FLT_COUNT = FLT_BPF_50_COUNT;
        FLT_INDEXS = FLT_BPF_50_INDEX;
        //FLT_MINFREQ 		    = FLT_BPF_50_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_BPF_50_MAXFREQ;
    }
    else if (newFilter == DSP_FILTER_BPF_100)
    {
        FLT_COUNT = FLT_BPF_100_COUNT;
        FLT_INDEXS = FLT_BPF_100_INDEX;
        //FLT_MINFREQ 		    = FLT_BPF_100_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_BPF_100_MAXFREQ;
    }
    else if (newFilter == DSP_FILTER_BPF_150)
    {
        FLT_COUNT = FLT_BPF_150_COUNT;
        FLT_INDEXS = FLT_BPF_150_INDEX;
        //FLT_MINFREQ 		    = FLT_BPF_150_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_BPF_150_MAXFREQ;
    }
    else if (newFilter == DSP_FILTER_LPF)
    {
        FLT_COUNT = FLT_LPF_COUNT;
        FLT_INDEXS = FLT_LPF_INDEX;
        //FLT_MINFREQ 		    = FLT_LPF_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_LPF_MAXFREQ;
    }
    else if (newFilter == DSP_FILTER_HPF)
    {
        FLT_COUNT = FLT_HPF_COUNT;
        FLT_INDEXS = FLT_HPF_INDEX;
        //FLT_MINFREQ 		    = FLT_HPF_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_HPF_MAXFREQ;
    }
    else if (newFilter == DSP_FILTER_SSB)
    {
        FLT_COUNT = FLT_LPF_COUNT;
        FLT_INDEXS = FLT_LPF_INDEX;
        //FLT_MINFREQ 		    = FLT_LPF_MINFREQ;
        //FLT_MAXFREQ 		    = FLT_LPF_MAXFREQ;

        //APPLY_FILTER_HPF    	= FLT_HPF;
    }

    *filterType = newFilter;

    ApplyCoeffs(*filterApplyFreq1);
    if (newFilter == DSP_FILTER_SSB)
    {
        ApplyCoeffs_HPF(*filterApplyFreq1);
    }

    SetCenterFrequency(instMain, instSecond, *filterApplyFreq1, 1);
    dspMenuDraw();
}

extern void BSP_LCD_HLineShift(uint16_t startYY, uint16_t endYY, uint16_t startXX, uint16_t endXX);
extern void BSP_LCD_DrawColorLine(uint16_t posYY, uint16_t startXX, uint16_t endXX, uint32_t *lineColorInfoBuff);

uint32_t GetWaterfallColor(uint32_t fftLevel)
{
    if (fftLevel > 1023U)
        fftLevel = 1023U;
    return UI_WodospadKolor((uint16_t)fftLevel);
}

#include "arm_const_structs.h"

int spectrumStep = 0;

extern void BSP_LCD_Draw2ColorVLine(uint16_t Xpos, uint16_t posYY0, uint32_t RGB_Code0, uint16_t posYY1, uint32_t RGB_Code1, uint16_t posYY2);

void DrawSpectrum(int16_t *inBuff)
{
    uint32_t *lineColorInfoBuff = UI_WodospadBuforLinii();
    uint16_t *poziomy_wodospadu = UI_WodospadBuforPoziomow();
    const uint16_t wodospad_gora = dsp_wodospad_pelny ? 0U : GRAPH_WATERFALL_TOP;
    const uint16_t wodospad_dol = dsp_wodospad_pelny ? 271U : GRAPH_WATERFALL_BOTTOM;

    //512 / 256
    if (spectrumStep < FFT_SPECTRUM_STEP)
    {
        int buffIndex = spectrumStep * FFT_AUDIO_SIZE * 2;
        for (int i = 0; i < FFT_AUDIO_SIZE; i++) //HALF -> COMPLETE
        {
            SpectrumBuff[buffIndex + i * 2] = inBuff[i * 2]; // * windowData[i];   //1 Channel of streo => Good!!!
            SpectrumBuff[buffIndex + i * 2 + 1] = 0;
        }
    }

    spectrumStep++;

    if (spectrumStep == FFT_SPECTRUM_STEP + 1)
    {
#define LCD_WATER_RGB(r, g, b) ((LCDColor)(0xFF000000ul |                     \
                                           ((((uint32_t)(r)) & 0xFF) << 16) | \
                                           ((((uint32_t)(g)) & 0xFF) << 8) |  \
                                           (((uint32_t)(b)) & 0xFF)))

        arm_cfft_f32(&arm_cfft_sR_f32_len2048, SpectrumBuff, 0, 1);
        arm_cmplx_mag_f32(SpectrumBuff, testOutput, FFT_SPECTRUM_SIZE);

        for (int i = 0; i < 480; i++)
        {
            uint32_t colorLevel = testOutput[i] / 200;
            if (colorLevel > 1023U)
                colorLevel = 1023U;
            poziomy_wodospadu[i] = (uint16_t)colorLevel;
            lineColorInfoBuff[i] = GetWaterfallColor(colorLevel);

            int freqLength = testOutput[i] / 7000;

            if (freqLength > GRAPH_BOTTOM)
                freqLength = GRAPH_BOTTOM;

            testOutput[i] = freqLength;
        }
    }
    else if (spectrumStep >= FFT_SPECTRUM_STEP + 2)
    {
        //Display Graph
        int dispIndex = spectrumStep - (FFT_SPECTRUM_STEP + 2);
#define DISP_LINE_BY_STEP 48 //120
#define MAX_DISP_INDEX 10    //4

        if (dispIndex < MAX_DISP_INDEX)
        {
            if (!dsp_wodospad_pelny)
            {
                for (int i = 0; i < DISP_LINE_BY_STEP; i++)
                {
                    int pixelIndex = dispIndex * DISP_LINE_BY_STEP + i;

                    uint32_t FFT_BACK_COLOR = LCD_BLACK;
                uint32_t FFT_GRAPH_COLOR = LCD_WHITE;

                if (*filterType != DSP_FILTER_BYPASS)
                {
                    //Check Selected range
                    if (filterStartIndex <= pixelIndex && filterEndIndex >= pixelIndex)
                    {
                        //FFT_BACK_COLOR     = LCD_RGB(59, 59, 59);
                        //FFT_BACK_COLOR     = LCD_RGB(80, 90, 233);
                        FFT_BACK_COLOR = LCD_RGB(59, 59, 143);
                        //FFT_GRAPH_COLOR    = LCD_RGB(255, 127, 39);
                        FFT_GRAPH_COLOR = LCD_RGB(230, 70, 70);
                    }
                    else if (filterStartIndexMin <= pixelIndex && filterEndIndexMax >= pixelIndex)
                    {
                        FFT_BACK_COLOR = LCD_RGB(30, 30, 30);
                        FFT_GRAPH_COLOR = LCD_RGB(255, 127, 39);
                    }
                } //end of if

                    BSP_LCD_Draw2ColorVLine(pixelIndex, 0, FFT_BACK_COLOR, GRAPH_BOTTOM - testOutput[pixelIndex], FFT_GRAPH_COLOR, GRAPH_BOTTOM);
                }
            }

            BSP_LCD_HLineShift(wodospad_gora, wodospad_dol,
                               dispIndex * DISP_LINE_BY_STEP,
                               dispIndex * DISP_LINE_BY_STEP + DISP_LINE_BY_STEP - 1);
        }
        else
        {
            //BSP_LCD_DrawColorLine
            //LCD_RGB(100, 100, 200);

            //BSP_LCD_HLineShift(GRAPH_BOTTOM + 1, 150, 0, 479);
            BSP_LCD_DrawColorLine(wodospad_gora, 0, 479, lineColorInfoBuff);
            UI_WodospadHistoriaDodaj(poziomy_wodospadu);

            spectrumStep = 0;
        }
    }
}

void DrawFFTSpectrumLayout()
{
#define METER_H_LINE_COLOR LCD_RGB(255, 127, 39)
    char aBuff[32];
    LCD_FillRect(LCD_MakePoint(0, GRAPH_BOTTOM + 1), LCD_MakePoint(479, GRAPH_BOTTOM + METER_HEIGHT - 1), LCD_BLACK);
    LCD_Line(LCD_MakePoint(0, GRAPH_BOTTOM + 1), LCD_MakePoint(479, GRAPH_BOTTOM + 1), METER_H_LINE_COLOR);
    //LCD_Line(LCD_MakePoint(0, GRAPH_BOTTOM + METER_HEIGHT -1), LCD_MakePoint(479, GRAPH_BOTTOM + METER_HEIGHT -1), METER_H_LINE_COLOR);
    //LCD_FillRect(LCD_MakePoint(0, GRAPH_BOTTOM + 1), LCD_MakePoint(479, GRAPH_BOTTOM + 10), LCD_BLACK);
    for (int i = 0; i < 480; i += 8)
    {
        LCD_Line(LCD_MakePoint(i, GRAPH_BOTTOM + 1), LCD_MakePoint(i, GRAPH_BOTTOM + 2), METER_H_LINE_COLOR);

        if ((i % 128) == 0)
        {
            sprintf(aBuff, "%dKhz", i / 128);
            FONT_Write(FONT_FRAN, LCD_WHITE, 0, i + 2, GRAPH_BOTTOM + METER_HEIGHT - 14, aBuff);
            LCD_Line(LCD_MakePoint(i, GRAPH_BOTTOM + 1), LCD_MakePoint(i, GRAPH_BOTTOM + 10), METER_H_LINE_COLOR);
        }
        else if ((i % 64) == 0)
        {
            sprintf(aBuff, "%d", i / 64 * 512);
            FONT_Write(FONT_SDIGITS, LCD_WHITE, 0, i + 2, GRAPH_BOTTOM + METER_HEIGHT - 10, aBuff);
            LCD_Line(LCD_MakePoint(i, GRAPH_BOTTOM + 1), LCD_MakePoint(i, GRAPH_BOTTOM + 5), METER_H_LINE_COLOR);
        }
    }

    //LCD_MakePoint(0, GRAPH_WATERFALL_TOP)
    LCD_FillRect(LCD_MakePoint(0, GRAPH_WATERFALL_TOP), LCD_MakePoint(479, GRAPH_WATERFALL_BOTTOM), GetWaterfallColor(0));
    FONT_Write(FONT_FRANBIG, LCD_WHITE, 0, 50, GRAPH_WATERFALL_TOP + 3,
               JEZYK_Wybierz("DSP audio / CW i SSB", "Audio DSP / CW & SSB",
                             "Audio-DSP / CW und SSB", "Аудио DSP / CW и SSB"));
    FONT_Write(FONT_FRAN, LCD_WHITE, 0, 100, GRAPH_WATERFALL_TOP + 35,
               JEZYK_Wybierz("Pasmo audio 300-3000 Hz", "Audio band 300-3000 Hz",
                             "Audioband 300-3000 Hz", "Полоса аудио 300-3000 Гц"));
    FONT_Write(FONT_FRAN, LCD_WHITE, 0, 290, GRAPH_WATERFALL_TOP + 50,
               "KD8CEC / EU1KY-PL 2026");
}

void SetCenterFrequency(arm_biquad_cascade_df2T_instance_f32 instMain, arm_biquad_cascade_df2T_instance_f32 instSecond, uint32_t newCenterFreq, uint32_t isChangedFilter)
{
    if ((newCenterFreq != *filterApplyFreq1) || isChangedFilter)
    {
        *filterApplyFreq1 = newCenterFreq;
        uint32_t newCenterFreqStart = newCenterFreq;
        uint32_t newCenterFreqEnd = newCenterFreq;
        int secondInterval = 50;

        if (*filterType == DSP_FILTER_BPF_50)
        {
            newCenterFreqStart = (*filterApplyFreq1 - 25);
            newCenterFreqEnd = (*filterApplyFreq1 + 25);
            secondInterval = 15;
        }
        else if (*filterType == DSP_FILTER_BPF_100)
        {
            newCenterFreqStart = (*filterApplyFreq1 - 50);
            newCenterFreqEnd = (*filterApplyFreq1 + 50);
            secondInterval = 15;
        }
        else if (*filterType == DSP_FILTER_BPF_150)
        {
            newCenterFreqStart = (*filterApplyFreq1 - 80);
            newCenterFreqEnd = (*filterApplyFreq1 + 80);
            secondInterval = 20;
        }
        else if (*filterType == DSP_FILTER_LPF || *filterType == DSP_FILTER_SSB)
        {
            newCenterFreqStart = 0;
            newCenterFreqEnd = (*filterApplyFreq1);
            secondInterval = 35;

            if (*filterType == DSP_FILTER_SSB)
            {
                newCenterFreqStart = (*filterApplyFreq2);
            }
        }
        else if (*filterType == DSP_FILTER_HPF)
        {
            secondInterval = 35;
            newCenterFreqStart = (*filterApplyFreq1);
            newCenterFreqEnd = 8000;
        }

        filterStartIndex = (float)newCenterFreqStart / FFT_BIN_FREQ;
        filterEndIndex = (float)newCenterFreqEnd / FFT_BIN_FREQ;

        filterStartIndexMin = filterStartIndex - secondInterval;
        filterEndIndexMax = filterEndIndex + secondInterval;

        *isTmpMute = 5;
        BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_ON);
        ApplyCoeffs(*filterApplyFreq1);
        arm_biquad_cascade_df2T_init_f32(&instMain, iirStageCount, iirCoeffBuff, iirStateBuff);

        if (*filterType == DSP_FILTER_SSB)
        {
            ApplyCoeffs_HPF(*filterApplyFreq2);
            arm_biquad_cascade_df2T_init_f32(&instSecond, iirStageCount, iirCoeffBuff_HPF, iirStateBuff_HPF);
        }
    }
}

void SetNewFreqByTouch(int newFreq, int isCenter, arm_biquad_cascade_df2T_instance_f32 instMain, arm_biquad_cascade_df2T_instance_f32 instSecond)
{
    if (*filterType == DSP_FILTER_SSB)
    {
        int basicFreq = (*filterApplyFreq1 - *filterApplyFreq2) / 2 + *filterApplyFreq2;

        if (isCenter)
        {
            //diff value
            int diffFreq = newFreq - basicFreq;
            //HPF
            *filterApplyFreq2 += diffFreq;
            *filterApplyFreq1 += diffFreq;

            if (*filterApplyFreq2 < FILTER_MINIMUM_FREQ)
            {
                *filterApplyFreq2 = FILTER_MINIMUM_FREQ;
            }
            if (*filterApplyFreq1 < FILTER_MINIMUM_FREQ + 100)
            {
                *filterApplyFreq1 = FILTER_MINIMUM_FREQ + 100;
            }

            if (*filterApplyFreq1 > FILTER_MAXIMUM_FREQ)
            {
                *filterApplyFreq1 = FILTER_MAXIMUM_FREQ;
            }

            SetCenterFrequency(instMain, instSecond, *filterApplyFreq1, 1);
        }
        else if (newFreq < basicFreq)
        {
            //HPF
            if (newFreq >= FILTER_MINIMUM_FREQ && *filterApplyFreq2 != newFreq)
            {
                *filterApplyFreq2 = newFreq;

                SetCenterFrequency(instMain, instSecond, *filterApplyFreq1, 1);
            }
        }
        else
        {
            //LPF
            if (newFreq <= FILTER_MAXIMUM_FREQ && *filterApplyFreq1 != newFreq)
            {
                //LPF
                if (newFreq < FILTER_MINIMUM_FREQ + 100)
                {
                    newFreq = FILTER_MINIMUM_FREQ + 100;
                }
                SetCenterFrequency(instMain, instSecond, newFreq, 0);
            }
        }

        *userFilterHalfWidth = (*filterApplyFreq1 - *filterApplyFreq2) / 2;
    }
    else
    {
        SetCenterFrequency(instMain, instSecond, newFreq, 0);
    }
}

void AudioDSP_Proc(void)
{
    LCDPoint pt;

    SetColours();
    BSP_LCD_SelectLayer(0);
    LCD_FillAll(BACK_COLOR);
    BSP_LCD_SelectLayer(1);
    LCD_FillAll(BACK_COLOR);
    LCD_ShowActiveLayerOnly();

    //Load Configuration
    DSP_LoadInformation();
    dsp_wodospad_pelny = 0U;
    UI_WodospadHistoriaResetuj();

    DrawFFTSpectrumLayout();
    dspMenuDraw();

    while (TOUCH_IsPressed())
        ;

    if (BSP_AUDIO_IN_OUT_Init(INPUT_DEVICE_DIGITAL_MICROPHONE_2, OUTPUT_DEVICE_HEADPHONE, 100, I2S_AUDIOFREQ_16K) == AUDIO_OK)

    {
#ifdef _DEBUG_UART
        DBG_Printf("Complete Audio Init...");
#endif
    }
    else
    {
#ifdef _DEBUG_UART
        DBG_Printf("Failed Init...");
#endif
    }

    memset((uint16_t *)AUDIO_BUFFER_IN_FLT, 0, AUDIO_BUFF_SIZE * 2);  //Half  (512) -> Complete (512)
    memset((uint16_t *)AUDIO_BUFFER_OUT_FLT, 0, AUDIO_BUFF_SIZE * 2); //Half  (512) -> Complete (512)
    Audio_Status = AUDIO_TRANSFER_NONE;

    BSP_AUDIO_IN_Record((uint16_t *)AUDIO_BUFFER_IN_FLT, AUDIO_BUFF_SIZE);

    BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
    BSP_AUDIO_OUT_Play((uint16_t *)AUDIO_BUFFER_OUT_FLT, AUDIO_BUFF_SIZE * 2);

    BSP_AUDIO_IN_SetVolume(100);
    BSP_AUDIO_OUT_SetVolume(*DSP_OUT_VOL);
    BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_OFF);

    arm_biquad_cascade_df2T_instance_f32 instMain;
    arm_biquad_cascade_df2T_instance_f32 instSecond;

    //uint8_t *filterType = DSP_FILTER_BYPASS;
    SetFFTFilter(*filterType, instMain, instSecond);
    arm_biquad_cascade_df2T_init_f32(&instMain, iirStageCount, iirCoeffBuff, iirStateBuff);

    ApplyCoeffs_HPF(*filterApplyFreq2);
    arm_biquad_cascade_df2T_init_f32(&instSecond, iirStageCount, iirCoeffBuff_HPF, iirStateBuff_HPF);

    int skipCount = 0; //for while button process, processing DSP

    for (;;)
    {
        while (Audio_Status != AUDIO_TRANSFER_HALF)
        {
            HAL_Delay(1);
            __NOP();
        }
        Audio_Status = AUDIO_TRANSFER_NONE; //Receive Start
        ApplyBiquad(*filterType, instMain, instSecond, AUDIO_BUFFER_IN_FLT, AUDIO_BUFFER_OUT_FLT);
        DrawSpectrum(AUDIO_BUFFER_IN_FLT);

        if (*isTmpMute > 0)
        {
            if (--(*isTmpMute) <= 0)
            {
                //BSP_AUDIO_IN_SetVolume(*DSP_OUT_VOL);
                //BSP_AUDIO_OUT_SetVolume(*DSP_OUT_VOL);
                BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_OFF);
            }
        }

        while (Audio_Status != AUDIO_TRANSFER_COMPLETE)
        {
            HAL_Delay(1);
            __NOP();
        }

        Audio_Status = AUDIO_TRANSFER_NONE;

        DrawSpectrum(&AUDIO_BUFFER_IN_FLT[256]);
        ApplyBiquad(*filterType, instMain, instSecond, &AUDIO_BUFFER_IN_FLT[256], &AUDIO_BUFFER_OUT_FLT[256]);

        //==========================================================================================
        //CHECK INPUT USER BUTTONS
        //------------------------------------------------------------------------------------------
        if (TOUCH_Poll(&pt))
        {
            /*
             * Pełny wodospad jest celowo bez przycisków. Jedno dotknięcie wraca
             * do widoku roboczego; w widoku roboczym dotknięcie samego wodospadu
             * powiększa go. Strojenie dotykiem pozostaje na wykresie FFT u góry.
             */
            if (dsp_wodospad_pelny)
            {
                while (TOUCH_IsPressed())
                    ;
                dsp_wodospad_pelny = 0U;
                spectrumStep = 0;
                LCD_FillAll(BACK_COLOR);
                DrawFFTSpectrumLayout();
                SetFFTFilter(*filterType, instMain, instSecond);
                UI_WodospadHistoriaRysuj(0U, GRAPH_WATERFALL_TOP, 479U, GRAPH_WATERFALL_BOTTOM);
                dspMenuDraw();
                continue;
            }
            if (pt.y >= GRAPH_WATERFALL_TOP && pt.y <= GRAPH_WATERFALL_BOTTOM)
            {
                while (TOUCH_IsPressed())
                    ;
                dsp_wodospad_pelny = 1U;
                spectrumStep = 0;
                LCD_FillAll(UI_WodospadKolor(0U));
                UI_WodospadHistoriaRysuj(0U, 0U, 479U, 271U);
                continue;
            }

            UI_KONTROLKA_t kontrolki[dspMenus_Length];
            DSP_WypelnijKontrolki(kontrolki);
            int touchIndex = UI_ZnajdzKontrolke(pt, kontrolki, dspMenus_Length);

            //Play Beep
            if (touchIndex != -1)
            {
                //TRACK_Beep(1);
                //Chbeck AF Frequency Up And Down / for Continues change
                if ((touchIndex == MENU_AFFREQDOWN || touchIndex == MENU_AFFREQUP) && (*nowSelectedFilter != DSP_FILTER_NONE)) //+- 10Hz
                {
                    if (skipCount < 1)
                    {
                        int newFreq = *filterApplyFreq1;

                        if (*filterType == DSP_FILTER_SSB)
                        {
                            //newFreq = (*filterApplyFreq1 - *filterApplyFreq2) / 2 + *filterApplyFreq2 + (touchIndex == MENU_AFFREQDOWN ? -1 : 1);
                            newFreq = (*filterApplyFreq1 - *filterApplyFreq2) / 2 + *filterApplyFreq2;
                        }

                        newFreq += (touchIndex == MENU_AFFREQDOWN ? -1 : 1);

                        if (newFreq < FILTER_MINIMUM_FREQ)
                        {
                            newFreq = FILTER_MINIMUM_FREQ;
                        }
                        else if (newFreq > FILTER_MAXIMUM_FREQ)
                        {
                            newFreq = FILTER_MAXIMUM_FREQ;
                        }

                        SetNewFreqByTouch(newFreq, 1, instMain, instSecond);

                        skipCount = 5;
                    }
                    else
                    {
                        skipCount--;
                    }

                    //Sleep(70);
                    continue;
                }
                else
                {
                    *isTmpMute = 1;
                    BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_ON);

                    TRACK_Beep(1);
                    while (TOUCH_IsPressed())
                        ;
                }

                //while(TOUCH_IsPressed());

                if (touchIndex == MENU_EXIT) //EXIT
                {
                    break;
                }
                else if (touchIndex == MENU_SAVECONFIG)
                {
                    DSP_StoredInformatoin();
                }
                else if (touchIndex == MENU_BPF)
                {
                    //Check before BPF
                    if (*nowSelectedFilter == DSP_FILTER_BPF)
                    {
                        (*nowBPFFilterIndex)++;

                        if (*nowBPFFilterIndex > 2)
                        {
                            *nowBPFFilterIndex = 0;
                        }
                    }
                    else if (*nowSelectedFilter == DSP_FILTER_USER)
                    {
                        *filterApplyFreq1 = *filterApplyFreq1 - *userFilterHalfWidth;
                    }

                    if (*nowBPFFilterIndex == 0)
                        *filterType = DSP_FILTER_BPF_50;
                    else if (*nowBPFFilterIndex == 1)
                        *filterType = DSP_FILTER_BPF_100;
                    else if (*nowBPFFilterIndex == 2)
                        *filterType = DSP_FILTER_BPF_150;

                    *nowSelectedFilter = DSP_FILTER_BPF;
                    //*filterType = DSP_FILTER_BPF_50;
                }
                else if (touchIndex == MENU_USERFILTER)
                {
                    if (*nowSelectedFilter != DSP_FILTER_USER)
                    {
                        //Adjust
                        if (*nowSelectedFilter == DSP_FILTER_BPF)
                        {
                            *filterApplyFreq2 = *filterApplyFreq1 - *userFilterHalfWidth; //HPF
                            *filterApplyFreq1 = *filterApplyFreq1 + *userFilterHalfWidth; //LPF
                        }

                        if (*filterApplyFreq2 < FILTER_MINIMUM_FREQ)
                        {
                            *filterApplyFreq2 = FILTER_MINIMUM_FREQ; //HPF
                        }

                        if (*filterApplyFreq1 > FILTER_MAXIMUM_FREQ)
                        {
                            *filterApplyFreq1 = FILTER_MAXIMUM_FREQ; //HPF
                        }

                        *nowSelectedFilter = DSP_FILTER_USER;
                        *filterType = DSP_FILTER_SSB;
                    }
                }
                else if (touchIndex == MENU_BYPASS)
                {
                    *nowSelectedFilter = DSP_FILTER_NONE;
                    *filterType = DSP_FILTER_BYPASS;
                }

                SetFFTFilter(*filterType, instMain, instSecond);
            }
            else if (pt.y <= GRAPH_BOTTOM)
            {
                int nowPos = pt.x;
                int newFreq = nowPos * FFT_BIN_FREQ;

                if (newFreq < FILTER_MINIMUM_FREQ)
                {
                    newFreq = FILTER_MINIMUM_FREQ;
                }
                else if (newFreq > FILTER_MAXIMUM_FREQ)
                {
                    newFreq = FILTER_MAXIMUM_FREQ;
                }

                if (*filterType == DSP_FILTER_SSB)
                {
                    if (newFreq != (*filterApplyFreq1 - *filterApplyFreq2) / 2 + *filterApplyFreq2)
                        SetNewFreqByTouch(newFreq, pt.y > GRAPH_BOTTOM, instMain, instSecond);
                }
                else
                {
                    if (newFreq != *filterApplyFreq1)
                        SetNewFreqByTouch(newFreq, pt.y > GRAPH_BOTTOM, instMain, instSecond);
                }
            }
        } //end of if (check for touch screen)

    } //end of for

    //SET_PTT(0);
    //GEN_SetMeasurementFreq(0);
    DSP_Init();
    return;
}
