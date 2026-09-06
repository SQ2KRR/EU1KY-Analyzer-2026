/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <string.h>

#include "LCD.h"
#include "main.h"

#include "touch.h"
#include "font.h"
#include "config.h"
#include "ff.h"
#include "crash.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "stm32746g_discovery_lcd.h"
#include "screenshot.h"
#include "panvswr2.h"
#include "panfreq.h"
#include "smith.h"
#include "textbox.h"
#include "generator.h"
#include "BeepTimer.h"
#include "bitmaps/bitmaps.h"
#include "sdram_heap.h"
#include "komunikaty.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "analiza_przebiegu.h"
#include "kwarc_metrologia.h"
#include "kwarc_seria.h"
#include "kalibracja_meta.h"
#include "pomiar_s11.h"
#include "wejscia_uzytkownika.h"

#define X0 51
#define Y0 50
#define WWIDTH 400
#define WHEIGHT 138

/*
 * Układ ekranu SWR 2026. Górne 32 px należą do wspólnego paska stanu,
 * dolne 38 px do wspólnego paska akcji. Wykres i dane nie mogą wchodzić
 * w te obszary. Dzięki temu ten ekran zachowuje się tak samo jak nowe
 * moduły anten, TDR i ustawień.
 */
#define PAN_SWR_INFO_Y 34U
#define PAN_SWR_PASEK_Y 232U
#define PAN_SWR_PASEK_H 38U
#define WY(offset) ((WHEIGHT + Y0) - (offset))
//#define WGRIDCOLOR LCD_RGB(80,80,80)
#define RED1 LCD_RGB(245, 0, 0)
#define RED2 LCD_RGB(235, 0, 0)
#define SMITH_CIRCLE_BG LCD_BLACK
#define SMITH_LINE_FG LCD_GREEN

#define BUTTON_BACKCOLOR LCD_RGB(87, 87, 87)


/*
 * Paleta wykresu Retro.
 *
 * Wykres ma wyglądać jak karta pomiarowa z przyrządu laboratoryjnego:
 * ciepły papier, delikatna siatka i ciemna stalowa kreska. Nie używamy
 * bitmapy tła, bo wykres jest dynamiczny i musi pozostać szybki w rysowaniu.
 */
static bool PANVSWR_CzyWykresRetro(void)
{
    return CFG_GetParam(CFG_PARAM_ZESTAW_IKON) == 0U;
}

static LCDColor PANVSWR_KolorTlaWykresu(void)
{
    /*
     * ZMIANA PO ZDJĘCIACH: jasny "papier" wyglądał dobrze osobno, ale kłócił
     * się z resztą interfejsu, która została ciemna (menu główne, pasek akcji).
     * Wykres dostaje więc ciemne tło w tym samym tonie co reszta aplikacji,
     * a siatka/krzywa/tekst są jasne zamiast ciemnych - inwersja tamtej
     * palety, nie nowy zestaw kolorów od zera.
     */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(10, 8, 5) : BackGrColor;
}

static LCDColor PANVSWR_KolorSiatkiDrobnej(void)
{
    /*
     * Siatka drobna ma być tłem dla krzywej, a nie drugą krzywą - stąd tylko
     * lekko jaśniejsza od tła, tak samo jak wcześniej była tylko lekko
     * ciemniejsza od papieru.
     */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(45, 38, 28) : LCD_COLOR_DARKGRAY;
}

static LCDColor PANVSWR_KolorSiatkiGlownej(void)
{
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(90, 74, 50) : LCD_RGB(160, 160, 96);
}

static LCDColor PANVSWR_KolorTekstuWykresu(void)
{
    /* Ciepła kremowa biel zamiast ciemnego tuszu - ten sam odcień co tekst gdzie indziej w aplikacji. */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(230, 210, 175) : TextColor;
}

static LCDColor PANVSWR_KolorKrzywej(void)
{
    /*
     * Kolor wybierany w Ustawieniach (CFG_PARAM_KOLOR_KRZYWEJ_SWR). Domyślny
     * "Stalowy" jest najjaśniejszym elementem wykresu z założenia - krzywa ma
     * być pierwsza rzecz, jaką widać. Pozostałe warianty są dobrane tak, by
     * mieć podobny, wysoki kontrast wobec ciemnego tła wykresu.
     */
    uint32_t kolor;

    if (!PANVSWR_CzyWykresRetro())
        return CurvColor;

    kolor = CFG_GetParam(CFG_PARAM_KOLOR_KRZYWEJ_SWR);
    switch (kolor)
    {
    case 1U: return LCD_RGB(80, 235, 90);   /* Zielony */
    case 2U: return LCD_RGB(235, 175, 60);  /* Bursztynowy */
    case 3U: return LCD_RGB(235, 80, 70);   /* Czerwony */
    default: return LCD_RGB(240, 235, 220); /* Stalowy */
    }
}

static LCDColor PANVSWR_KolorKrzywejDrugiej(void)
{
    /* Jasna miedź dla drugiej krzywej R/X - wyraźnie odróżnialna od głównej krzywej, ale nie tak jasna. */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(224, 130, 60) : LCD_RED;
}

static LCDColor PANVSWR_KolorPasma(void)
{
    /* Delikatny podkład pod pasmem - ciemniejszy niż siatka drobna, żeby nie konkurował z nią o uwagę. */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(35, 29, 20) : Color3;
}

static LCDColor PANVSWR_KolorKursora(void)
{
    /* Ten sam mosiądz co ramki kafli - spójny akcent w całej aplikacji. */
    return PANVSWR_CzyWykresRetro() ? LCD_RGB(216, 168, 92) : TextColor;
}

#define WGRIDCOLOR   (PANVSWR_KolorSiatkiDrobnej())
#define WGRIDCOLORBR (PANVSWR_KolorSiatkiGlownej())

/*
 * Pelnoekranowy wykres Smitha.
 *
 * Zwykly widok pozostaje bez zmian, aby nie naruszac sprawdzonej obslugi
 * panoramy. Tryb pelnoekranowy jest modalnym widokiem tej samej tablicy
 * pomiarowej `values[]`: nie wykonuje wlasnych obliczen metrologicznych i nie
 * zmienia danych po OSL. Dzieki temu nowy ekran jest tylko lepsza prezentacja
 * juz policzonego pomiaru.
 */
/*
 * Geometria pelnego Smitha.
 *
 * Kolo ma byc flagowym elementem ekranu, ale nie moze nachodzic na naglowek,
 * boczne panele ani dolny pasek akcji. Promien 106 px i srodek y=127 daja:
 *   gora  = 21 px,
 *   dol   = 233 px,
 *   lewo  = 134 px,
 *   prawo = 346 px.
 *
 * Boczne panele koncza sie na x=118 i zaczynaja od x=362, a pasek akcji
 * zaczyna sie na y=238. Zostaje rzeczywisty margines bez zmniejszania Smitha
 * do 198 px srednicy.
 */
#define SMITH_PELNY_CX 240
#define SMITH_PELNY_CY 127
#define SMITH_PELNY_PROMIEN 106

static void SMITH_RysujZnacznikStartu(float r0f);
static void SMITH_RysujStatusKalibracji(void);
#define SMITH_PELNY_PRZYCISK_Y 238
#define SMITH_PELNY_PRZYCISK_H 33

static uint8_t smith_pelny_ekran = 0U;

/* Model korekcji uzyty w ostatnim skanie — patrz PANVSWR_ZmierzPunkt(). */
static POMIAR_S11_KOREKCJA_t pan_model_korekcji = POMIAR_S11_KOREKCJA_BRAK;

#define RX_FILE_HEADER_LEN 100

//#define MAX(a,b) (((a)>(b))?(a):(b))
//#define MIN(a,b) (((a)<(b))?(a):(b))

// Please read the article why smoothing looks beautiful but actually
// decreases precision, and averaging increases precision though looks ugly:
// http://www.microwaves101.com/encyclopedias/smoothing-is-cheating
// This analyzer draws both smoothed (bright) and averaged (dark) measurement
// results, you see them both.
#define SMOOTHWINDOW 3 //Must be odd!
#define SMOOTHOFS (SMOOTHWINDOW / 2)
#define SMOOTHWINDOW_HI 7 //Must be odd!
#define SMOOTHOFS_HI (SMOOTHWINDOW_HI / 2)
#define SM_INTENSITY 64
extern uint8_t rqDel;
extern void ShowF(void);
extern void TRACK_Beep(int duration);
extern uint8_t AUDIO1;
extern uint8_t loudness;

void DrawFootText(void);
extern float DSP_CalcX(void);

typedef enum
{
    GRAPH_VSWR,
    GRAPH_VSWR_Z,
    GRAPH_VSWR_RX,
    GRAPH_RX,
    GRAPH_S11,
    GRAPH_SMITH,
    GRAPH_Smooth
} GRAPHTYPE;

// Highligtht IARU regions   19.09.2020 DH1AKF // DG2DRF

// Region 1            10.04.2020 DG2DRF

static const HAM_BANDS hamBands1[] =
    {
        {135ul, 138ul},
        {472ul, 479ul},
        {1810ul, 2000ul},
        {3500ul, 3800ul},
        {5351ul, 5366ul},
        {7000ul, 7200ul},
        {10100ul, 10150ul},
        {14000ul, 14350ul},
        {18068ul, 18168ul},
        {21000ul, 21450ul},
        {24890ul, 24990ul},
        {28000ul, 29700ul},
        {50000ul, 52000ul},
        {70000ul, 70500ul},
        {144000ul, 146000ul},
        {222000ul, 222000ul}, // not allowed
        {430000ul, 440000ul},
        {902000ul, 902000ul}, // not allowed
        {1240000ul, 1300000ul},

};

// Region 2    10.04.2020 DG2DRF
static const HAM_BANDS hamBands2[] =
    {
        {135ul, 138ul},
        {472ul, 479ul},
        {1800ul, 2000ul},
        {3500ul, 4000ul},
        {5351ul, 5366ul},
        {7000ul, 7300ul},
        {10100ul, 10150ul},
        {14000ul, 14350ul},
        {18068ul, 18168ul},
        {21000ul, 21450ul},
        {24890ul, 24990ul},
        {28000ul, 29700ul},
        {50000ul, 54000ul},
        {70000ul, 70000ul}, // not allowed
        {144000ul, 148000ul},
        {222000ul, 225000ul},
        {430000ul, 450000ul},
        {902000ul, 928000ul},
        {1240000ul, 1300000ul},
};

// Region 3          10.04.2020 DG2DRF
static const HAM_BANDS hamBands3[] =
    {
        {135ul, 138ul},
        {472ul, 479ul},
        {1800ul, 2000ul},
        {3500ul, 3900ul},
        {5351ul, 5366ul},
        {7000ul, 7300ul},
        {10100ul, 10150ul},
        {14000ul, 14350ul},
        {18068ul, 18168ul},
        {21000ul, 21450ul},
        {24890ul, 24990ul},
        {28000ul, 29700ul},
        {50000ul, 54000ul},
        {70000ul, 70000ul}, // not allowed
        {144000ul, 148000ul},
        {222000ul, 222000ul}, // not allowed
        {430000ul, 450000ul},
        {902000ul, 902000ul}, // not allowed
        {1240000ul, 1300000ul},
};

// LPD B�nder ; not veryfied "low power devices"
static const HAM_BANDS lpd[] =
    {
        {50000ul, 54000ul},    //lpd
        {144000ul, 148000ul},  //lpd
        {220000ul, 225000ul},  //lpd
        {430000ul, 450000ul},  //lpd
        {902000ul, 928000ul},  //lpd
        {1240000ul, 1300000ul} //lpd
};

// DG2DRF Regionsauswahl
static const uint32_t hamBandsNum1 = sizeof(hamBands1) / sizeof(*hamBands1);
static const uint32_t hamBandsNum2 = sizeof(hamBands2) / sizeof(*hamBands2);
static const uint32_t hamBandsNum3 = sizeof(hamBands3) / sizeof(*hamBands3);
static const uint32_t lpdNum = sizeof(lpd) / sizeof(*lpd);
// DG2DRF Regionsauswahl

static int holdScale; // hold scale for "Auto" mode
static float MIN_S11;
static uint32_t activeLayerX;

//static const uint32_t hamBandsNum = sizeof(hamBands) / sizeof(*hamBands);

//Smith chart center:
#define cx0 240
#define cy0 128

static const char *modstr = "";

static uint32_t modstrw = 0;
// ** WK ** / DL8MBY:

const char *BSSTR[] = {"2 kHz", "4 kHz", "10 kHz", "20 kHz", "40 kHz", "100 kHz", "150 kHz",
                       "200 kHz", "250 kHz", "300 kHz", "400 kHz", "500 kHz", "1000 kHz", "2 MHz", "4 MHz", "10 MHz", "20 MHz",
                       "30 MHz", "40 MHz", "60 MHz", "100 MHz", "200 MHz", "250 Mhz", "300 MHz",
                       "350 MHz", "400 MHz", "450 MHz", "500 MHz", "700 MHz", "1.0 Ghz"};
const char *BSSTR_HALF[] = {"1 kHz", "2 kHz", "5 kHz", "10 kHz", "20 kHz",
                            "50 kHz", "75 kHz", "100 kHz", "125 kHz", "150 kHz", "200 kHz", "250 kHz", "500 kHz", "1 MHz", "2 MHz", "5 MHz",
                            "10 MHz", "15 MHz", "20 MHz", "30 MHz", "50 MHz", "100 MHz", "125 MHz", "150 MHz",
                            "175 MHz", "200 MHz", "225 MHz", "250 MHz", "350 MHz", " 0.5 GHz "};
const uint32_t BSVALUES[] = {2, 4, 10, 20, 40, 100, 150, 200, 250, 300, 400, 500, 1000, 2000,
                             4000, 10000, 20000, 30000, 40000, 60000, 100000, 200000, 250000, 300000,
                             350000, 400000, 450000, 500000, 700000, 1000000};

static char autoScanFactor = 0;
static uint32_t f1 = 14000000; //Scan range start frequency, in Hz
static BANDSPAN span = BS400;
static float fcur; // frequency at cursor position in kHz
static char buf[64];
static LCDPoint pt;
static float complex values[WWIDTH + 1];
static uint8_t pan_maska_poprawnosci[WWIDTH + 1];
static uint16_t pan_punkty_poprawne = 0U;
static uint16_t pan_punkty_odrzucone = 0U;
static uint8_t pan_skan_przerwany = 0U;
static uint8_t pan_wykres_aktywny = 0U;
static uint32_t freqChg;

//modified by KD8CEC for reduce memory usage rate
/*
static float complex SavedValues1[WWIDTH+1];
static float complex SavedValues2[WWIDTH+1];
static float complex SavedValues3[WWIDTH+1];
*/
uint32_t RXPHeader[5][5] = {};

static float complex *SavedValues1;
static float complex *SavedValues2;
static float complex *SavedValues3;

static int isStored;

static int isMeasured = 0;
static uint32_t cursorPos;
static int ManualCursor;
static GRAPHTYPE grType = GRAPH_VSWR;
static uint32_t isSaved = 0;
static uint32_t cursorChangeCount = 0;
static uint32_t autofast = 0;
static int loglog = 0; // scale for SWR
extern volatile uint32_t autosleep_timer;

static void DrawRX();
static void DrawSmith();
static void SMITH_PelnyEkran(void);
static void SMITH_PelnyAnalizaEkran(void);
static uint8_t PANVSWR_CzyPunktPoprawny(uint32_t indeks);
static uint8_t PANVSWR_WygladzPunkt(int idx, int useHighSmooth, float complex *wynik);
static uint8_t PANVSWR_ZnajdzNajblizszyPoprawny(uint32_t indeks, uint32_t *wynik);
static uint8_t PANVSWR_ZnajdzPoprawnyWKierunku(uint32_t indeks, int kierunek, uint32_t *wynik);
static void PANVSWR_OdtworzMaskeZValues(void);

void SWR_Exit(void);

void SWR_Mute(void);

void SWR_SetFrequency(void);
void SWR_SetFrequencyMuted(void);

static bool QuMeasure(void);
static bool QuCalibrate(void);
static void KWARC_PrzygotujKolejnyPomiar(void);
static void KWARC_RysujKrokSterowania(void);
static const char *KWARC_T(const char *pl, const char *en, const char *de, const char *ru);
void DrawX_Scale(float maxRXi, float minRXi);
int QuStep, sCalib;
extern uint32_t fxs;
extern uint32_t fxkHzs;
extern int QuartzSel;
static void ScanRXFast(void);
static uint32_t PANVSWR_StartSkanuHz(void);
static void PANVSWR_RysujNaglowekStrony(const char *tytul);
static void PANVSWR_RysujPomocStartowa(void);
static void PANVSWR_RysujStroneWynik(void);
static void PANVSWR_RysujStroneNarzedzia(void);

#define M_BGCOLOR LCD_RGB(0, 0, 64)    //Menu item background color
#define M_FGCOLOR LCD_RGB(255, 255, 0) //Menu item foreground color

//ianlee
static int redrawRequired;

typedef enum
{
    PAN_SWR_STRONA_WYKRES = 0,
    PAN_SWR_STRONA_WYNIK,
    PAN_SWR_STRONA_NARZEDZIA
} PAN_SWR_STRONA_t;

static PAN_SWR_STRONA_t pan_swr_strona = PAN_SWR_STRONA_WYKRES;
static int isM1Loaded, isM2Loaded, isM3Loaded;

static uint32_t Saving;
static uint32_t FreqkHz;

static const char *SMITH_T(const char *pl, const char *en, const char *de, const char *ru)
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

#define MEMMODE_LOAD 0
#define MEMMODE_STORE 1
uint8_t memMode = 0; //0 :Load Mode, 1 :Store Mode    //Memory Status

uint32_t last_TuneFreq = 0;
uint32_t AutoCursor;

static bool rqExitSWR, rqExitSWR1;

void SWR_Exit(void)
{
    rqExitSWR = true;
}

void SWR_Exit1(void)
{
    rqExitSWR1 = true;
}

void QuNextStep(void);

float freqMHzf;

void ShowFr(int digits) //digits: 0 ...3
{
    char str[20];
    freqMHzf = (float)(CFG_GetParam(CFG_PARAM_MEAS_F) / 1000000.);

    if (digits == 0)
        sprintf(str, "F: %4.0f MHz ", freqMHzf);
    else if (digits == 1)
        sprintf(str, "F: %4.1f MHz ", freqMHzf);
    else if (digits == 2)
        sprintf(str, "F: %4.2f MHz ", freqMHzf);
    else
        sprintf(str, "F: %4.3f MHz ", freqMHzf);

    LCD_FillRect(LCD_MakePoint(0, 70), LCD_MakePoint(200, 125), BackGrColor);
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 0, 70, str); // WK
    freqChg = 1;
}

uint32_t multi_fr[5] = {1850, 21200, 27800, 3670, 7150};  //Multi SWR frequencies in kHz
static uint32_t multi_bw[5] = {200, 1000, 200, 400, 100}; //Multi SWR bandwidth in kHz
static BANDSPAN multi_bwNo[5] = {6, 8, 6, 5, 4};          //Multi SWR bandwidth number
static int beep;

void Beep(int duration)
{
    if (BeepOn1 == 0)
        return;
    if (beep == 0)
    {
        beep = 1;
        AUDIO1 = 1;
        UB_TIMER2_Init_FRQ(880);
        UB_TIMER2_Start();
        Sleep(100);
        AUDIO1 = 0;
        // UB_TIMER2_Stop();
    }
    if (duration == 1)
        beep = 0;
}
// DG2DRF Regionsauswahl
unsigned long GetUpper(int i)
{
    switch (CFG_GetParam(CFG_PARAM_REGION))
    {
    case 0:
        if ((i >= 0) && (i < (int)hamBandsNum1))
            return 1000 * hamBands1[i].fhi;
        return 0;
    case 1:
        if ((i >= 0) && (i < (int)hamBandsNum2))
            return 1000 * hamBands2[i].fhi;
        return 0;
    case 2:
        if ((i >= 0) && (i < (int)hamBandsNum3))
            return 1000 * hamBands3[i].fhi;
        return 0;
    case 3:
        if ((i >= 0) && (i < (int)lpdNum))
            return 1000 * lpd[i].fhi;
        return 0;
    }
    return 0;
}

unsigned long GetLower(int i)
{
    switch (CFG_GetParam(CFG_PARAM_REGION))
    {
    case 0:
        if ((i >= 0) && (i < (int)hamBandsNum1))
            return 1000 * hamBands1[i].flo;
        return 0;
    case 1:
        if ((i >= 0) && (i < (int)hamBandsNum2))
            return 1000 * hamBands2[i].flo;
        return 0;
    case 2:
        if ((i >= 0) && (i < (int)hamBandsNum3))
            return 1000 * hamBands3[i].flo;
        return 0;
    case 3:
        if ((i >= 0) && (i < (int)lpdNum))
            return 1000 * lpd[i].flo;
        return 0;
    }
    return 0;
}
// DG2DRF Regionsauswahl

int GetBandNr(unsigned long freq)
{
    int i, regnu = 0, found = 0;

    switch (CFG_GetParam(CFG_PARAM_REGION))
    {
    case 0:
        regnu = hamBandsNum1;
        return regnu;
    case 1:
        regnu = hamBandsNum2;
        return regnu;
    case 2:
        regnu = hamBandsNum3;
        return regnu;
    case 3:
        regnu = lpdNum;
        return regnu;
    }

    for (i = 0; i <= regnu; i++)
    {
        if (GetLower(i) >= freq)
        {
            found = 1;
            i--;
            break;
        }
    }
    if (found == 1)
    {
        if (GetUpper(i) >= freq)
            return i;
    }
    if ((GetLower(regnu) <= freq) && (GetUpper(regnu) >= freq))
        return regnu;
    return -1; // not in a Ham band
}

void WK_InvertPixel(uint16_t x, uint16_t y)
{
    LCDColor c;
    LCDPoint p = LCD_MakePoint(x, y);
    c = LCD_ReadPixel(p);
    switch (c)
    {
    case LCD_COLOR_YELLOW:
    {
        LCD_SetPixel(p, LCD_COLOR_RED);
        return;
    }
    case LCD_COLOR_WHITE:
    {
        LCD_SetPixel(p, RED1);
        return;
    }
    case LCD_COLOR_DARKGRAY:
    {
        LCD_SetPixel(p, RED2);
        return;
    }
    case LCD_COLOR_RED:
    {
        LCD_SetPixel(p, LCD_COLOR_YELLOW);
        return;
    }
    case RED1:
    {
        LCD_SetPixel(p, LCD_COLOR_WHITE);
        return;
    }
    case RED2:
    {
        LCD_SetPixel(p, LCD_COLOR_DARKGRAY);
        return;
    }
    default:
        LCD_InvertPixel(p);
    }
}

static int swroffset(float swr)
{
    int offs = (int)roundf(150. * log10f(swr));
    if (offs >= WHEIGHT)
        offs = WHEIGHT - 1;
    else if (offs < 0)
        offs = 0;
    return offs;
}

static float S11Calc(float swr)
{
    float offs = 20 * log10f((swr - 1) / (swr + 1));
    return offs;
}

int IsFinHamBands(uint32_t f_kHz)
{
    uint32_t i;
    switch (CFG_GetParam(CFG_PARAM_REGION))
    {
    case 0:
    {
        for (i = 0; i < hamBandsNum1; i++)
        {
            if ((f_kHz >= hamBands1[i].flo) && (f_kHz <= hamBands1[i].fhi))
                return 1;
        }
        return 0;
    } // Region 1       10.04.2020 DG2DRF
    case 1:
    {
        for (i = 0; i < hamBandsNum2; i++)
        {
            if ((f_kHz >= hamBands2[i].flo) && (f_kHz <= hamBands2[i].fhi))
                return 1;
        }
        return 0;
    } // Region 2       10.04.2020 DG2DRF
    case 2:
    {
        for (i = 0; i < hamBandsNum3; i++)
        {
            if ((f_kHz >= hamBands3[i].flo) && (f_kHz <= hamBands3[i].fhi))
                return 1;
        }
        return 0;
    } // Region 3       10.04.2020 DG2DRF
    case 3:
    {
        for (i = 0; i < lpdNum; i++)
        {
            if ((f_kHz >= lpd[i].flo) && (f_kHz <= lpd[i].fhi))
                return 1;
        }
        return 0;
    } // LPD       10.04.2020 DG2DRF
    }
    return 0;
}

int cursorVisible;

static void DrawCursor1()
{
    int8_t i;
    LCDPoint p;

    if (isMeasured && !PANVSWR_CzyPunktPoprawny(cursorPos))
    {
        cursorVisible = 0;
        return;
    }

    if (cursorVisible == 0)
        cursorVisible = 1;
    else
        cursorVisible = 0;
    if (grType == GRAPH_SMITH)
    {
        float complex rx = values[cursorPos]; //SmoothRX(cursorPos, f1 > (CFG_GetParam(CFG_PARAM_BAND_FMAX) / 1000) ? 1 : 0);
        float complex g = OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0));
        uint32_t x = (uint32_t)roundf(cx0 + crealf(g) * 100.);
        uint32_t y = (uint32_t)roundf(cy0 - cimagf(g) * 100.);
        p = LCD_MakePoint(x, y);
        for (i = -4; i < 4; i++)
        {
            p.x += i;
            LCD_InvertPixel(p);
            p.x -= i;
        }
        for (i = -4; i < 4; i++)
        {
            p.y += i;
            LCD_InvertPixel(p);
            p.y -= i;
        }
    }
    else
    {
        //Draw cursor line as inverted image
        p = LCD_MakePoint(X0 + cursorPos, Y0);
        if (ColourSelection == 1) // Daylightcolours
        {
            while (p.y < Y0 + WHEIGHT)
            {
                if ((p.y % 20) < 10)
                    WK_InvertPixel(p.x, p.y);
                else
                    LCD_InvertPixel(p);
                p.y++;
            }
        }
        else
        {
            while (p.y < Y0 + WHEIGHT)
            {
                if ((p.y % 20) < 10)
                    LCD_InvertPixel(p);
                p.y++;
            }
        }
        if (FatLines)
        {
            p.x--;
            while (p.y >= Y0)
            {
                LCD_InvertPixel(p);
                p.y--;
            }
            p.x += 2;
            while (p.y < Y0 + WHEIGHT)
            {
                LCD_InvertPixel(p);
                p.y++;
            }
            p.x--;
        }

        LCD_FillRect((LCDPoint){
                         X0 + cursorPos - 3, Y0 + WHEIGHT + 1},
                     (LCDPoint){X0 + cursorPos + 3, Y0 + WHEIGHT + 3}, PANVSWR_KolorTlaWykresu());
        LCD_FillRect((LCDPoint){
                         X0 + cursorPos - 2, Y0 + WHEIGHT + 1},
                     (LCDPoint){X0 + cursorPos + 2, Y0 + WHEIGHT + 3}, PANVSWR_KolorKursora());
    }
}

static void DrawCursor()
{
    DrawCursor1();
    if (BSP_LCD_GetActiveLayer() != activeLayerX)
    {
        BSP_LCD_SelectLayer(activeLayerX);
        DrawCursor1();
        BSP_LCD_SelectLayer(!activeLayerX);
    }
    else
    {
        BSP_LCD_SelectLayer(!activeLayerX);
        DrawCursor1();
        BSP_LCD_SelectLayer(activeLayerX);
    }
}

static void DrawCursorText()
{
    uint32_t fstart = PANVSWR_StartSkanuHz();

    fcur = ((float)(fstart / 1000. + (float)cursorPos * BSVALUES[span] / WWIDTH)); ///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;

    LCD_FillRect(LCD_MakePoint(20, Y0 + WHEIGHT + 16), LCD_MakePoint(409, Y0 + WHEIGHT + 30), BackGrColor);

    if (!PANVSWR_CzyPunktPoprawny(cursorPos))
    {
        FONT_Print(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BackGrColor,
                   20, Y0 + WHEIGHT + 16,
                   "F: %.2f MHz    %s",
                   fcur / 1000,
                   SMITH_T("Punkt odrzucony - brak wyniku", "Rejected point - no result",
                           "Punkt verworfen - kein Ergebnis", "Точка отброшена - нет результата"));
        return;
    }

    float complex rx = values[cursorPos];
    float ga = cabsf(OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0))); //G magnitude

    float Q = 0.f;
    if ((crealf(rx) > 0.1f) && (fabs(cimagf(rx)) > crealf(rx)))
        Q = fabs(cimagf(rx) / crealf(rx));
    if (Q > 2000.f)
        Q = 2000.f;
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, 20, Y0 + WHEIGHT + 16, "F: %.2f MHz    Z: %.1f%+.1fj     SWR: %.1f    MCL: %.2f dB    Q: %.1f  ",
               fcur / 1000,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               (ga > 0.01f) ? (-10. * log10f(ga)) : 99.f, // Matched cable loss
               Q);
}

static void DrawCursorTextWithS11()
{
    uint32_t fstart = PANVSWR_StartSkanuHz();

    fcur = ((float)(fstart / 1000. + (float)cursorPos * BSVALUES[span] / WWIDTH)); ///1000.;
    if (fcur * 1000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
        fcur = 0.f;
    LCD_FillRect(LCD_MakePoint(X0 - 30, Y0 + WHEIGHT + 16), LCD_MakePoint(409, Y0 + WHEIGHT + 30), BackGrColor);

    if (!PANVSWR_CzyPunktPoprawny(cursorPos))
    {
        FONT_Print(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BackGrColor,
                   X0 - 30, Y0 + WHEIGHT + 16,
                   "F: %.2f MHz    %s",
                   fcur / 1000,
                   SMITH_T("Punkt odrzucony - brak S11", "Rejected point - no S11",
                           "Punkt verworfen - kein S11", "Точка отброшена - нет S11"));
        return;
    }

    float complex rx = values[cursorPos];
    FONT_Print(FONT_FRAN, TextColor, BackGrColor, X0 - 30, Y0 + WHEIGHT + 16, "F: %.2f MHZ    Z: %.1f%+.1fj     SWR: %.1f     S11: %.2f dB  ",
               fcur / 1000,
               crealf(rx),
               cimagf(rx),
               DSP_CalcVSWR(rx),
               S11Calc(DSP_CalcVSWR(rx)));
}

static void DrawSavedText(void)
{
    if (smith_pelny_ekran)
    {
        UI_RysujPoleStatusu(150, 108, 180, 52, 0,
                            SMITH_T("Zapisano", "Saved", "Gespeichert", "Сохранено"),
                            UI_STYL_AKTYWNY);
        Sleep(900);
        return;
    }

    static const char *txt = "  Snapshot saved  ";
    FONT_Write(FONT_FRAN, LCD_WHITE, LCD_RGB(0, 60, 0), 165,
               Y0 + WHEIGHT + 16 + 16, txt);
    Sleep(2000);
    DrawFootText();
    //DrawAutoText();
}
static void DecrCursor()
{
    uint32_t nowy;

    if (!PANVSWR_ZnajdzPoprawnyWKierunku(cursorPos, -1, &nowy))
        return;
    ManualCursor = 1;
    //AutoCursor=0;
    if (cursorVisible == 1)
        DrawCursor(); // delete the old cursor
    cursorPos = nowy;
    DrawCursor();
    cursorVisible = 1;
    if ((grType == GRAPH_S11) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
    {
        DrawCursorTextWithS11();
    }

    else
    {
        DrawCursorText();
    }
    if (cursorChangeCount++ < 5)
        Sleep(100); //Slow down at first steps
    Sleep(5);
}

static void IncrCursor()
{
    uint32_t nowy;

    if (!PANVSWR_ZnajdzPoprawnyWKierunku(cursorPos, 1, &nowy))
        return;
    ManualCursor = 1;
    //AutoCursor=0;
    if (cursorVisible == 1)
        DrawCursor(); // delete the old cursor
    cursorPos = nowy;
    DrawCursor();
    cursorVisible = 1;
    if ((grType == GRAPH_S11) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
    {
        DrawCursorTextWithS11();
    }
    else
    {
        DrawCursorText();
    }
    if (cursorChangeCount++ < 5)
        Sleep(100); //Slow down at first steps

    Sleep(5);
}

static int ClearScreen = 1;

static void DrawGrid(GRAPHTYPE grType) //
{
    int i;
    const LCDColor tlo_wykresu = PANVSWR_KolorTlaWykresu();
    const LCDColor tekst_wykresu = PANVSWR_KolorTekstuWykresu();

    LCD_FillRect((LCDPoint){X0 - 30, Y0},
                 (LCDPoint){X0 + WWIDTH + 2, Y0 + WHEIGHT + 3}, tlo_wykresu);

    /*
     * W trybie Retro najpierw rysujemy równą siatkę 10 px, jak na starej
     * karcie milimetrowej. Późniejsze linie skali nakładają się na nią i są
     * odrobinę ciemniejsze, więc podziałka nadal odpowiada wielkości fizycznej.
     */
    if (PANVSWR_CzyWykresRetro())
    {
        int gx;
        int gy;
        for (gx = X0; gx <= X0 + WWIDTH; gx += 10)
            LCD_VLine(LCD_MakePoint((uint16_t)gx, Y0), WHEIGHT, PANVSWR_KolorSiatkiDrobnej());
        for (gy = Y0; gy <= Y0 + WHEIGHT; gy += 10)
            LCD_HLine(LCD_MakePoint(X0, (uint16_t)gy), WWIDTH, PANVSWR_KolorSiatkiDrobnej());
        LCD_Rectangle(LCD_MakePoint(X0, Y0),
                      LCD_MakePoint(X0 + WWIDTH, Y0 + WHEIGHT),
                      PANVSWR_KolorSiatkiGlownej());
    }
    //cursorVisible=0;
    uint32_t fstart;

    if (0 == CFG_GetParam(CFG_PARAM_PAN_CENTER_F))
    {
        fstart = f1;
        snprintf(buf, sizeof(buf), "%.3f MHz   +%s   Z0=%d Ohm",
                 (double)f1 / 1000000.0, BSSTR[span], (int)CFG_GetParam(CFG_PARAM_R0));
    }
    else
    {
        fstart = PANVSWR_StartSkanuHz();
        snprintf(buf, sizeof(buf), "%.3f MHz   +/- %s   Z0=%d Ohm",
                 (double)f1 / 1000000.0, BSSTR_HALF[span], (int)CFG_GetParam(CFG_PARAM_R0));
    }

    /*
     * Parametry skanu mają własny spokojny wiersz pod nagłówkiem. Nie są już
     * wciskane w ten sam pas co nazwa wykresu i status urządzenia.
     */
    LCD_FillRect(LCD_MakePoint(48, PAN_SWR_INFO_Y), LCD_MakePoint(451, PAN_SWR_INFO_Y + 13U), BackGrColor);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 54, PAN_SWR_INFO_Y, buf);

    /*
     * Z jakiej kalibracji pochodzi ten wykres. Male, po prawej, przygaszone —
     * nie zasmieca wykresu, ale usuwa jedyna informacje, ktorej dotad nie dalo
     * sie odczytac z ekranu pomiarowego: ktory profil OSL jest aktywny.
     *
     * Profil niewazny pokazujemy w kolorze ostrzezenia. Nie blokujemy pomiaru —
     * bez kalibracji tez wolno mierzyc, byle wiedziec, ze sie to robi.
     */
    {
        const bool wazny = (pan_model_korekcji != POMIAR_S11_KOREKCJA_BRAK);
        char etykieta[24];

        /* Model faktycznie uzyty, nie wybrany — jak na ekranie Smitha. */
        if (pan_model_korekcji == POMIAR_S11_KOREKCJA_OSL_70CM)
            snprintf(etykieta, sizeof(etykieta), "OSL 70cm");
        else if (pan_model_korekcji == POMIAR_S11_KOREKCJA_OSL_LC)
            snprintf(etykieta, sizeof(etykieta), "OSL L/C");
        else if (wazny)
            snprintf(etykieta, sizeof(etykieta), "OSL %s", OSL_GetSelectedName());
        else
            snprintf(etykieta, sizeof(etykieta), "bez OSL");

        FONT_Write(FONT_FRAN,
                   wazny ? UI_KolorTekstu(UI_STYL_NIEAKTYWNY)
                         : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
                   BackGrColor,
                   (uint16_t)(451 - (uint16_t)(strlen(etykieta) * 7U)),
                   PAN_SWR_INFO_Y, etykieta);
    }

    /*
     * Nie ukrywamy braków pomiarowych. Sama luka na krzywej jest uczciwa,
     * ale licznik pozwala od razu odróżnić pojedynczy odrzucony punkt od
     * problemu obejmującego większą część skanu.
     */
    if (isMeasured && pan_punkty_odrzucone > 0U)
    {
        char jakosc[28];
        snprintf(jakosc, sizeof(jakosc), "! %s %u/%u",
                 SMITH_T("Pkt", "Pts", "Pkt", "Тчк"),
                 (unsigned)pan_punkty_poprawne,
                 (unsigned)(WWIDTH + 1U));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BackGrColor,
                   286, PAN_SWR_INFO_Y, jakosc);
    }

    //Mark ham bands with colored background
    for (i = 0; i <= WWIDTH; i++)
    {
        uint32_t f = fstart / 1000 + (i * BSVALUES[span]) / WWIDTH;
        if (IsFinHamBands(f))
        {
            LCD_VLine(LCD_MakePoint(X0 + i, Y0), WHEIGHT, PANVSWR_KolorPasma());
        }
    }

    //Draw F grid and labels
    int lmod = 5;
    int linediv = 10; //Draw vertical line every linediv pixels

    for (i = 0; i <= WWIDTH / linediv; i++)
    {
        int x = X0 + i * linediv;
        if ((i % lmod) == 0 || i == WWIDTH / linediv)
        {
            char fr[10];
            float flabel = ((float)(fstart / 1000. + i * BSVALUES[span] / (WWIDTH / linediv))) / 1000.f;
            if (flabel * 1000000.f > (float)(CFG_GetParam(CFG_PARAM_BAND_FMAX) + 1))
                continue;
            if (flabel > 999.99)
                sprintf(fr, "%.1f", ((float)(fstart / 1000. + i * BSVALUES[span] / (WWIDTH / linediv))) / 1000.f);
            else if (flabel > 99.99)
                sprintf(fr, "%.2f", ((float)(fstart / 1000. + i * BSVALUES[span] / (WWIDTH / linediv))) / 1000.f);
            else
                sprintf(fr, "%.3f", ((float)(fstart / 1000. + i * BSVALUES[span] / (WWIDTH / linediv))) / 1000.f); // WK
            int w = FONT_GetStrPixelWidth(FONT_SDIGITS, fr);
            // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, x - w / 2, Y0 + WHEIGHT + 5, f);// WK
            FONT_Write(FONT_FRAN, tekst_wykresu, tlo_wykresu, x - 8 - w / 2, Y0 + WHEIGHT + 3, fr);
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT, WGRIDCOLORBR);
            LCD_VLine(LCD_MakePoint(x + 1, Y0), WHEIGHT, WGRIDCOLORBR); // linia główna
        }
        else
        {
            LCD_VLine(LCD_MakePoint(x, Y0), WHEIGHT, WGRIDCOLOR);
        }
    }

    if ((grType == GRAPH_VSWR) || (grType == GRAPH_VSWR_Z) || (grType == GRAPH_VSWR_RX) || (grType == GRAPH_Smooth))
    {
        if (loglog == 0)
        {
            //Draw SWR grid and labels
            static const float swrs[] = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10., 13., 16., 20.};
            static const char labels[] = {1, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1};
            static const int nswrs = sizeof(swrs) / sizeof(float);
            for (i = 0; i < nswrs; i++)
            {
                int yofs = swroffset(swrs[i]);
                if (labels[i])
                {
                    char s[10];
                    if ((int)(10 * swrs[i]) % 10 == 0) // WK
                    {
                        if (swrs[i] > 9.0)
                            sprintf(s, "%d", (int)swrs[i]);
                        else
                            sprintf(s, " % d", (int)swrs[i]);
                    }
                    else
                        sprintf(s, "%.1f", swrs[i]);
                    // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, X0 - 15, WY(yofs) - 2, s);
                    FONT_Write(FONT_FRAN, tekst_wykresu, tlo_wykresu, X0 - 21, WY(yofs) - 12, s);
                }
                LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLORBR);
            }
        }
        else
        {
            static const float swrsl[] = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10., 13., 16., 20.};
            static const char labelsl[] = {1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1};
            static const int nswrsl = sizeof(swrsl) / sizeof(float);
            for (i = 0; i < nswrsl; i++)
            {
                int yofs = swroffset(14 * log10f(swrsl[i]) + 1);
                if (labelsl[i])
                {
                    char s[10];
                    if ((int)(10 * swrsl[i]) % 10 == 0) // WK
                    {
                        if (swrsl[i] > 9.0)
                            sprintf(s, "%d", (int)swrsl[i]);
                        else
                            sprintf(s, " % d", (int)swrsl[i]);
                    }
                    else
                        sprintf(s, "%.1f", swrsl[i]);
                    // FONT_Write(FONT_SDIGITS, LCD_WHITE, LCD_BLACK, X0 - 15, WY(yofs) - 2, s);
                    FONT_Write(FONT_FRAN, tekst_wykresu, tlo_wykresu, X0 - 21, WY(yofs) - 12, s);
                }
                LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLORBR);
            }
        }
    }
}

#define RXFAST_DIVIDE_FREQ autoScanFactor
//#define FAST_DIVIDE_FREQ autoScanFactor

static uint32_t PANVSWR_StartSkanuHz(void)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint64_t zakres_hz = (uint64_t)BSVALUES[span] * 1000ULL;
    int64_t start_hz = (int64_t)f1;

    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) != 0U)
        start_hz -= (int64_t)(zakres_hz / 2ULL);

    if (start_hz < (int64_t)fmin)
        start_hz = (int64_t)fmin;
    if (start_hz > (int64_t)fmax)
        start_hz = (int64_t)fmax;

    /*
     * W trybie częstotliwości środkowej zachowujemy pełny zadany zakres,
     * jeżeli mieści się on w paśmie pomiarowym. Chroni to jednocześnie przed
     * historycznym przepełnieniem uint32_t przy dużym span i niskim środku.
     */
    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) != 0U &&
        zakres_hz <= (uint64_t)fmax - (uint64_t)fmin &&
        (uint64_t)start_hz + zakres_hz > (uint64_t)fmax)
    {
        start_hz = (int64_t)((uint64_t)fmax - zakres_hz);
    }
    return (uint32_t)start_hz;
}

static uint8_t PANVSWR_PunktPomiarowyPoprawny(DSP_RX z)
{
    return (isfinite(crealf(z)) && isfinite(cimagf(z))) ? 1U : 0U;
}

static uint8_t PANVSWR_PobierzS11(uint32_t freq, int n, uint8_t port_extension, DSP_RX *z)
{
    const uint32_t zakres_od_hz = PANVSWR_StartSkanuHz();
    const uint64_t zakres_do64 = (uint64_t)zakres_od_hz + (uint64_t)BSVALUES[span] * 1000ULL;
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint32_t zakres_do_hz = zakres_do64 > fmax ? fmax : (uint32_t)zakres_do64;
    POMIAR_S11_t pomiar;
    POMIAR_S11_USTAWIENIA_t ustawienia = {
        .tor = POMIAR_S11_TOR_STANDARD,
        .liczba_usrednien = (uint8_t)(n > 0 ? n : 1),
        .korekcja_hw = true,
        .korekcja_osl = true,
        .kompensacja_portu = port_extension != 0U,
        .kompensacja_kabla = CFG_GetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY) != 0U,
        .automatyczna_osl_pasmowa = true,
        .zakres_pomiaru_od_hz = zakres_od_hz,
        .zakres_pomiaru_do_hz = zakres_do_hz};

    if (z == NULL)
        return 0U;

    if (!POMIAR_S11_PobierzPunkt(freq, &ustawienia, &pomiar))
    {
        *z = NAN + NAN * I;
        return 0U;
    }

    *z = pomiar.impedancja_ohm;

    /*
     * Zapamietujemy model korekcji faktycznie uzyty przez warstwe rf_korekcja.
     * Wybrany profil i profil zastosowany to dwie rozne rzeczy: przy kalibracji
     * pasmowej 70 cm caly zakres mieszczacy sie w jej granicach zostanie
     * skorygowany lokalnie, niezaleznie od tego, ktory profil szerokopasmowy
     * jest wybrany. Bez tego pola ekran moglby podawac nazwe kalibracji, ktora
     * w tym pomiarze nie brala udzialu.
     *
     * Jedno pole na caly przebieg wystarcza, bo OSL70_CzyUzycDlaZakresu()
     * wymaga, zeby CALY zakres miescil sie w kalibracji pasmowej — model nie
     * zmienia sie wiec w srodku jednego wykresu.
     */
    pan_model_korekcji = pomiar.model_korekcji;

    return PANVSWR_PunktPomiarowyPoprawny(*z);
}

static void PANVSWR_ZliczJakosc(const uint8_t *maska)
{
    uint32_t i;
    pan_punkty_poprawne = 0U;
    pan_punkty_odrzucone = 0U;
    for (i = 0U; i <= WWIDTH; ++i)
    {
        if (maska[i])
            pan_punkty_poprawne++;
        else
            pan_punkty_odrzucone++;
    }
}

/*
 * Stary format pamięci RX nie zapisuje osobnej maski. W nowych plikach luka
 * pozostaje jako NaN, więc po wczytaniu możemy uczciwie odtworzyć maskę z
 * danych. Starszego pliku, który już zawiera skopiowaną poprzednią wartość,
 * nie da się naprawić bez dodatkowej informacji, której w nim nie ma.
 */
static void PANVSWR_OdtworzMaskeZValues(void)
{
    uint32_t i;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        pan_maska_poprawnosci[i] =
            (uint8_t)(isfinite(crealf(values[i])) && isfinite(cimagf(values[i])));
    }

    PANVSWR_ZliczJakosc(pan_maska_poprawnosci);
}

static uint8_t PANVSWR_ZmierzPunkt(uint32_t freq, int n, DSP_RX *z)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    if (freq < fmin || freq > fmax)
        return 0U;
    if (freq == 0U)
        freq = 1U;
    return PANVSWR_PobierzS11(freq, n, pan_wykres_aktywny ? 1U : 0U, z);
}

static void ScanRXFast(void)
{
    uint32_t i;
    uint32_t krok_indeksu = (uint32_t)RXFAST_DIVIDE_FREQ;
    uint32_t fstart = PANVSWR_StartSkanuHz();
    const uint32_t delta_hz = (BSVALUES[span] * 1000U) / WWIDTH;
    const float z0 = (float)CFG_GetParam(CFG_PARAM_R0);
    float complex *robocze = NULL;
    uint8_t *maska = NULL;
    uint8_t przerwano = 0U;

    /* Nowy skan nie moze dziedziczyc etykiety OSL po poprzednim pomiarze. */
    pan_model_korekcji = POMIAR_S11_KOREKCJA_BRAK;

    if (krok_indeksu == 0U || krok_indeksu > WWIDTH)
        krok_indeksu = 1U;

    robocze = (float complex *)SDRH_malloc(sizeof(float complex) * (WWIDTH + 1U));
    maska = (uint8_t *)SDRH_malloc(sizeof(uint8_t) * (WWIDTH + 1U));
    if (robocze == NULL || maska == NULL)
    {
        if (robocze != NULL)
            SDRH_free(robocze);
        if (maska != NULL)
            SDRH_free(maska);
        GEN_SetMeasurementFreq(0);
        return;
    }
    memset(robocze, 0, sizeof(float complex) * (WWIDTH + 1U));
    memset(maska, 0, sizeof(uint8_t) * (WWIDTH + 1U));

    /* Pomiar ustalający stan toru przed właściwym skanem. */
    {
        DSP_RX tmp;
        (void)PANVSWR_ZmierzPunkt(fstart, 1, &tmp);
    }

    for (i = 0U; i <= WWIDTH; i += krok_indeksu)
    {
        uint32_t freq = fstart + i * delta_hz;
        DSP_RX rx = 0.0f + 0.0f * I;
        maska[i] = PANVSWR_ZmierzPunkt(freq, (int)CFG_GetParam(CFG_PARAM_PAN_NSCANS), &rx);
        robocze[i] = rx;
        if (!maska[i])
            robocze[i] = NAN + NAN * I;

        if ((i % 32U) == 0U && TOUCH_IsPressed())
        {
            przerwano = 1U;
            break;
        }
        if (i > WWIDTH - krok_indeksu)
            break;
    }

    /*
     * Gdy 400 nie jest wielokrotnością przyspieszenia (np. krok 12 lub 14),
     * mierzymy punkt końcowy jawnie. Stary kod później próbował używać punktu
     * 408/406 i czytał pamięć poza tablicą values[].
     */
    if (!przerwano && (WWIDTH % krok_indeksu) != 0U)
    {
        DSP_RX rx = 0.0f + 0.0f * I;
        const uint32_t freq = fstart + WWIDTH * delta_hz;
        maska[WWIDTH] = PANVSWR_ZmierzPunkt(freq, (int)CFG_GetParam(CFG_PARAM_PAN_NSCANS), &rx);
        robocze[WWIDTH] = maska[WWIDTH] ? rx : (NAN + NAN * I);
    }

    GEN_SetMeasurementFreq(0);
    if (przerwano)
    {
        pan_skan_przerwany = 1U;
        autofast = 0;
        SDRH_free(maska);
        SDRH_free(robocze);
        return;
    }

    /* Interpolacja tylko pomiędzy istniejącymi, sprawdzonymi punktami. */
    for (i = 0U; i <= WWIDTH; ++i)
    {
        uint32_t dolny;
        uint32_t gorny;
        uint32_t p0, p1, p2;
        if (maska[i] || (i % krok_indeksu) == 0U || i == WWIDTH)
            continue;

        dolny = (i / krok_indeksu) * krok_indeksu;
        gorny = dolny + krok_indeksu;
        if (gorny > WWIDTH)
            gorny = WWIDTH;

        if (dolny == 0U)
        {
            p0 = 0U;
            p1 = gorny;
            p2 = gorny + krok_indeksu;
            if (p2 > WWIDTH)
                p2 = WWIDTH;
        }
        else if (gorny < WWIDTH)
        {
            p0 = dolny - krok_indeksu;
            p1 = dolny;
            p2 = gorny;
        }
        else
        {
            p2 = WWIDTH;
            p1 = dolny;
            p0 = dolny >= krok_indeksu ? dolny - krok_indeksu : 0U;
        }

        if (p0 != p1 && p1 != p2 && maska[p0] && maska[p1] && maska[p2])
        {
            float complex G0 = OSL_GFromZ(robocze[p0], z0);
            float complex G1 = OSL_GFromZ(robocze[p1], z0);
            float complex G2 = OSL_GFromZ(robocze[p2], z0);
            float complex Gi = OSL_ParabolicInterpolation(G0, G1, G2,
                                                           (float)p0, (float)p1, (float)p2, (float)i);
            robocze[i] = OSL_ZFromG(Gi, z0);
            maska[i] = isfinite(crealf(robocze[i])) && isfinite(cimagf(robocze[i]));
        }
        else
        {
            /* Brak podstaw do interpolacji = jawna luka, bez wartości zastępczej. */
            robocze[i] = NAN + NAN * I;
        }
    }

    memcpy(values, robocze, sizeof(values));
    memcpy(pan_maska_poprawnosci, maska, sizeof(pan_maska_poprawnosci));
    PANVSWR_ZliczJakosc(pan_maska_poprawnosci);
    pan_skan_przerwany = 0U;
    isMeasured = pan_punkty_poprawne > 0U;

    SDRH_free(maska);
    SDRH_free(robocze);
    LCD_FillRect((LCDPoint){X0, Y0}, (LCDPoint){X0 + WWIDTH + 2, Y0 + WHEIGHT + 3}, PANVSWR_KolorTlaWykresu());
}

static void ScanRX(void)
{
    float complex rx;
    uint32_t i;
    uint32_t k;
    uint32_t fstart;
    uint32_t deltaF;

    f1 = CFG_GetParam(CFG_PARAM_PAN_F1);
    fstart = PANVSWR_StartSkanuHz();
    deltaF = (BSVALUES[span] * 1000U) / WWIDTH;
    k = CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    if (k == 0U)
        k = 1U;

    /* Model zostanie ustawiony dopiero przez poprawnie zmierzony punkt. */
    pan_model_korekcji = POMIAR_S11_KOREKCJA_BRAK;
    memset(pan_maska_poprawnosci, 0, sizeof(pan_maska_poprawnosci));
    pan_skan_przerwany = 0U;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        uint32_t freq1 = fstart + i * deltaF;
        const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
        const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

        if (freq1 == 0U)
            freq1 = 1U;

        if (freq1 >= fmin && freq1 <= fmax)
        {
            pan_maska_poprawnosci[i] = PANVSWR_PobierzS11(
                freq1, (int)k, pan_wykres_aktywny ? 1U : 0U, &rx);
        }
        else
        {
            rx = NAN + NAN * I;
        }

        /* MASKSAFE1: brak pomiaru pozostaje NaN-em. Renderer i analiza
           respektują maskę, więc nie ma już potrzeby produkować schodów przez
           kopiowanie ostatniego poprawnego punktu. */
        if (!pan_maska_poprawnosci[i])
            rx = NAN + NAN * I;

        values[i] = rx;
        LCD_SetPixel(LCD_MakePoint(X0 + i, 135), LCD_BLUE);
        LCD_SetPixel(LCD_MakePoint(X0 + i, 136), LCD_BLUE);
    }

    GEN_SetMeasurementFreq(0);
    PANVSWR_ZliczJakosc(pan_maska_poprawnosci);
    isMeasured = pan_punkty_poprawne > 0U;
    LCD_FillRect((LCDPoint){X0, Y0 + WHEIGHT + 1},
                 (LCDPoint){X0 + WWIDTH + 2, Y0 + WHEIGHT + 3}, BackGrColor);
}

/*
 * MASKSAFE1: jedynym źródłem prawdy o jakości punktu jest maska.
 * W tej wersji odrzucony punkt pozostaje dodatkowo jako NaN, ale nie wolno
 * opierać logiki wyłącznie na tej reprezentacji. Maska opisuje ważność
 * pomiaru niezależnie od sposobu przechowywania values[].
 */
static uint8_t PANVSWR_CzyPunktPoprawny(uint32_t indeks)
{
    if (indeks > WWIDTH || !pan_maska_poprawnosci[indeks])
        return 0U;

    return (uint8_t)(isfinite(crealf(values[indeks])) &&
                     isfinite(cimagf(values[indeks])));
}

/*
 * Wygładzanie nie może przechodzić przez lukę pomiarową. Jeżeli środkowy
 * punkt jest odrzucony, wynik wygładzania także jest odrzucony. Dla punktu
 * poprawnego bierzemy tylko sąsiadów z TEGO SAMEGO ciągłego fragmentu.
 * Pierwsza luka po lewej lub prawej zatrzymuje zbieranie próbek w tym
 * kierunku. Nie „przerzucamy” więc informacji nad obszarem bez pomiaru.
 */
static uint8_t PANVSWR_WygladzPunkt(int idx, int useHighSmooth, float complex *wynik)
{
    int odsuniecie;
    int smoothofs;
    uint8_t lewa_aktywna = 1U;
    uint8_t prawa_aktywna = 1U;
    uint32_t liczba = 0U;
    float suma_r = 0.0f;
    float suma_x = 0.0f;

    if (wynik == NULL || idx < 0 || idx > WWIDTH ||
        !PANVSWR_CzyPunktPoprawny((uint32_t)idx))
        return 0U;

    smoothofs = useHighSmooth ? SMOOTHOFS_HI : SMOOTHOFS;

    suma_r = crealf(values[idx]);
    suma_x = cimagf(values[idx]);
    liczba = 1U;

    for (odsuniecie = 1; odsuniecie <= smoothofs; ++odsuniecie)
    {
        const int lewy = idx - odsuniecie;
        const int prawy = idx + odsuniecie;

        if (lewa_aktywna)
        {
            if (lewy >= 0 && PANVSWR_CzyPunktPoprawny((uint32_t)lewy))
            {
                suma_r += crealf(values[lewy]);
                suma_x += cimagf(values[lewy]);
                ++liczba;
            }
            else
            {
                lewa_aktywna = 0U;
            }
        }

        if (prawa_aktywna)
        {
            if (prawy <= WWIDTH && PANVSWR_CzyPunktPoprawny((uint32_t)prawy))
            {
                suma_r += crealf(values[prawy]);
                suma_x += cimagf(values[prawy]);
                ++liczba;
            }
            else
            {
                prawa_aktywna = 0U;
            }
        }

        if (!lewa_aktywna && !prawa_aktywna)
            break;
    }

    if (liczba == 0U)
        return 0U;

    *wynik = (suma_r / (float)liczba) + (suma_x / (float)liczba) * I;
    return (uint8_t)(isfinite(crealf(*wynik)) && isfinite(cimagf(*wynik)));
}

/* Znajduje najbliższy poprawny punkt bez preferowania lewej lub prawej strony. */
static uint8_t PANVSWR_ZnajdzNajblizszyPoprawny(uint32_t indeks, uint32_t *wynik)
{
    uint32_t odleglosc;

    if (wynik == NULL || pan_punkty_poprawne == 0U)
        return 0U;

    if (indeks > WWIDTH)
        indeks = WWIDTH;

    if (PANVSWR_CzyPunktPoprawny(indeks))
    {
        *wynik = indeks;
        return 1U;
    }

    for (odleglosc = 1U; odleglosc <= WWIDTH; ++odleglosc)
    {
        if (indeks >= odleglosc && PANVSWR_CzyPunktPoprawny(indeks - odleglosc))
        {
            *wynik = indeks - odleglosc;
            return 1U;
        }
        if (indeks + odleglosc <= WWIDTH && PANVSWR_CzyPunktPoprawny(indeks + odleglosc))
        {
            *wynik = indeks + odleglosc;
            return 1U;
        }
    }

    return 0U;
}

/* kierunek < 0: poprzedni poprawny punkt, kierunek > 0: następny. */
static uint8_t PANVSWR_ZnajdzPoprawnyWKierunku(uint32_t indeks, int kierunek, uint32_t *wynik)
{
    int32_t i;

    if (wynik == NULL || pan_punkty_poprawne == 0U || kierunek == 0)
        return 0U;

    i = (int32_t)indeks + (kierunek < 0 ? -1 : 1);
    while (i >= 0 && i <= WWIDTH)
    {
        if (PANVSWR_CzyPunktPoprawny((uint32_t)i))
        {
            *wynik = (uint32_t)i;
            return 1U;
        }
        i += kierunek < 0 ? -1 : 1;
    }

    return 0U;
}
static uint32_t MinIndex;

static void DrawVSWR(void)
{
    /* Tytuł ekranu jednoznacznie mówi „Wykres SWR”. Dawny pionowy napis
     * S/W/R po lewej stronie nie wnosił informacji i kolidował z przyciskami
     * przesuwania kursora. Lewy margines zostawiamy wyłącznie dla skali i
     * dwóch przycisków kursora. */
    if (grType == GRAPH_VSWR_Z)
        FONT_Write(FONT_FRANBIG, PANVSWR_KolorKrzywejDrugiej(), PANVSWR_KolorTlaWykresu(), X0 + 405, Y0 + 40, "|Z|");
    if (!isMeasured)
        return;
    //BSP_LCD_SelectLayer(1);
    //BSP_LCD_SetTransparency(1,0);

    //BSP_LCD_FillRect(X0,Y0,WWIDTH,WHEIGHT);

    MinIndex = WWIDTH + 1U;
    float MaxZ, MinZ, factorA, factorB;
    int lastoffset = 0;
    int i, x;
    float najlepszy_swr = INFINITY;
    int lastoffset_sm = 0;
    uint8_t poprzedni_wazny = 0U;
    for (i = 0; i <= WWIDTH; i++)
    {
        float complex wygladzony;
        float swr_float_sm;
        int offset_sm;

        if (!PANVSWR_WygladzPunkt(i, f1 > CFG_GetParam(CFG_PARAM_BAND_FMAX), &wygladzony))
        {
            poprzedni_wazny = 0U;
            continue;
        }

        swr_float_sm = DSP_CalcVSWR(wygladzony);
        if (!isfinite(swr_float_sm) || swr_float_sm < 1.0f)
        {
            poprzedni_wazny = 0U;
            continue;
        }

        if (loglog == 1)
        {
            offset_sm = swroffset(14 * log10f(swr_float_sm) + 1);
        }
        else
        {
            offset_sm = swroffset(swr_float_sm);
        }

        x = X0 + i;
        if (swr_float_sm < najlepszy_swr)
        {
            najlepszy_swr = swr_float_sm;
            MinIndex = (uint32_t)i;
        }

        if (!poprzedni_wazny)
        {
            LCD_SetPixel(LCD_MakePoint(x, WY(offset_sm)), PANVSWR_KolorKrzywej());
        }
        else
        {
            LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(offset_sm)), PANVSWR_KolorKrzywej());
            if (FatLines)
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm) - 1), LCD_MakePoint(x, WY(offset_sm) - 1), PANVSWR_KolorKrzywej());
                LCD_Line(LCD_MakePoint(x - 2, WY(lastoffset_sm) - 1), LCD_MakePoint(x - 1, WY(offset_sm) - 1), PANVSWR_KolorKrzywej());
                LCD_Line(LCD_MakePoint(x, WY(lastoffset_sm) - 1), LCD_MakePoint(x + 1, WY(offset_sm) + 1), PANVSWR_KolorKrzywej());
            }
        }

        lastoffset_sm = offset_sm;
        poprzedni_wazny = 1U;
    }

    if (MinIndex <= WWIDTH &&
        ((AutoCursor == 2) || ((AutoCursor == 1) && (ManualCursor == 0))))
    {
        cursorPos = MinIndex;
    }

    if (grType == GRAPH_VSWR_Z)
    {
        float impedance;
        int yofs;
        int lastoffset_wazny = 0;
        lastoffset = 0;
        lastoffset_sm = 0;
        MaxZ = 0;
        MinZ = 999999.;
        for (i = 0; i <= WWIDTH; i++)
        {
            impedance = cimagf(values[i]) * cimagf(values[i]) + crealf(values[i]) * crealf(values[i]);
            impedance = sqrtf(impedance);

            /*
             * Punkty niepoprawne pomijamy w skalowaniu. Porownanie z NaN jest
             * zawsze falszywe, wiec dotad nie wplywaly na Min/Max — ale nizej
             * trafialy do rzutowania na int, ktore dla NaN jest zachowaniem
             * nieokreslonym. Stad prostokatne artefakty na krzywej |Z|:
             * przypadkowa wartosc calkowita, obcinana potem do krawedzi wykresu
             * i laczona linia z sasiadem.
             */
            if (!pan_maska_poprawnosci[i] || !isfinite(impedance))
                continue;

            if (impedance < MinZ)
                MinZ = impedance;
            if (impedance > MaxZ)
                MaxZ = impedance;
        }

        /* Caly przebieg niepoprawny — nie ma czego skalowac ani rysowac. */
        if (MaxZ <= 0.0f || MinZ > MaxZ)
        {
            MinZ = 2.0f;
            MaxZ = 100.0f;
        }
        if (MinZ < 2)
            MinZ = 2;
        if (MaxZ / MinZ < 2)
        {
            MaxZ = 1.5f * MinZ;
            MinZ = 0.5f * MinZ;
        }
        factorA = (float)WHEIGHT / (MaxZ - MinZ);
        factorB = -(float)WHEIGHT * MinZ / (MaxZ - MinZ);
        for (i = 0; i <= WWIDTH; i++)
        {
            impedance = cimagf(values[i]) * cimagf(values[i]) + crealf(values[i]) * crealf(values[i]);
            impedance = sqrtf(impedance);
            x = X0 + i;

            /*
             * Przerwa w krzywej zamiast falszywego punktu. Rysowanie linii do
             * miejsca, ktorego nie zmierzono, jest gorsze niz luka: luka mowi
             * prawde, linia udaje pomiar.
             */
            /*
             * Maska jest nadrzędna wobec reprezentacji values[]. W tej wersji
             * punkt odrzucony jest także NaN-em, lecz renderer nadal sprawdza
             * oba warunki: ważność pomiaru i skończoność wyniku. Dzięki temu
             * późniejsza zmiana reprezentacji nie przywróci sztucznych schodów.
             */
            if (!pan_maska_poprawnosci[i] || !isfinite(impedance))
            {
                lastoffset_wazny = 0;
                continue;
            }

            yofs = factorA * impedance + factorB;

            if (yofs > WHEIGHT)
                yofs = WHEIGHT; // DH1AKF 05.10.2020
            if (yofs < 0)
                yofs = 0;
            if (i == 0 || !lastoffset_wazny)
            {
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), PANVSWR_KolorKrzywejDrugiej());
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs) + 1), PANVSWR_KolorKrzywejDrugiej());
            }
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), PANVSWR_KolorKrzywejDrugiej());
                if (FatLines)
                {
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset) + 1), LCD_MakePoint(x, WY(yofs) + 1), PANVSWR_KolorKrzywejDrugiej());
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset) + 2), LCD_MakePoint(x, WY(yofs) + 2), PANVSWR_KolorKrzywejDrugiej());
                }
            }
            /* yofs_sm nie jest w tej petli wyliczany — dawne obcinanie
               i przepisywanie go bylo odczytem zmiennej niezainicjowanej. */
            lastoffset = yofs;
            lastoffset_wazny = 1;
        }
        DrawX_Scale(MaxZ, MinZ);
    }
    else if (grType == GRAPH_VSWR_RX)
    {
        DrawRX(0, 1);
    }

    //    DrawCursor();
    //   cursorVisible=1;
}

static void LoadBkups()
{
    //Load saved frequency and span values from config file
    uint32_t fbkup = CFG_GetParam(CFG_PARAM_PAN_F1);
    if (fbkup != 0 && fbkup >= BAND_FMIN && fbkup <= CFG_GetParam(CFG_PARAM_BAND_FMAX) && (fbkup % 100) == 0)
    {
        f1 = fbkup;
    }
    else
    {
        f1 = 14000000;
        CFG_SetParam(CFG_PARAM_PAN_F1, f1);
        CFG_SetParam(CFG_PARAM_PAN_SPAN, BS400);
        CFG_Flush();
    }

    int spbkup = CFG_GetParam(CFG_PARAM_PAN_SPAN);
    if (spbkup <= BS1000M) // DL8MBY
    {
        span = (BANDSPAN)spbkup;
    }
    else
    {
        span = BS400;
        CFG_SetParam(CFG_PARAM_PAN_SPAN, span);
        CFG_Flush();
    }

    autoScanFactor = CFG_GetParam(CFG_PARAM_PAN_AUTOSPEED);
}

/*
   This function is based on:
   "Nice Numbers for Graph Labels" article by Paul Heckbert
   from "Graphics Gems", Academic Press, 1990
   nicenum: find a "nice" number approximately equal to x.
   Round the number if round=1, take ceiling if round=0
 */
static float nicenum(float x, int round)
{
    int expv; /* exponent of x */
    float f;  /* fractional part of x */
    float nf; /* nice, rounded fraction */

    expv = floorf(log10f(x));
    f = x / powf(10., expv); /* between 1 and 10 */
    if (round)
    {
        if (f < 1.5)
            nf = 1.;
        else if (f < 3.)
            nf = 2.;
        else if (f < 7.)
            nf = 5.;
        else
            nf = 10.;
    }
    else
    {
        if (f <= 1.)
            nf = 1.;
        else if (f <= 2.)
            nf = 2.;
        else if (f <= 5.)
            nf = 5.;
        else
            nf = 10.;
    }
    return nf * powf(10., expv);
}

static void DrawS11()
{
    FONT_Write(FONT_FRANBIG, PANVSWR_KolorTekstuWykresu(), PANVSWR_KolorTlaWykresu(), 0, Y0 - 18, "S ");
    FONT_Write(FONT_FRANBIG, PANVSWR_KolorTekstuWykresu(), PANVSWR_KolorTlaWykresu(), 0, Y0 + 8, "1 ");
    FONT_Write(FONT_FRANBIG, PANVSWR_KolorTekstuWykresu(), PANVSWR_KolorTlaWykresu(), 0, Y0 + 34, "1 ");
    int i;
    int j;
    float minS11;
    uint8_t znaleziono_skale = 0U;
    if (!isMeasured)
        return;
    if (holdScale == 0)
    {
        /* Skala powstaje wyłącznie z rzeczywistych, poprawnych punktów. */
        minS11 = 0.f;
        for (i = 0; i <= WWIDTH; i++)
        {
            float swr;
            float s11;

            if (!PANVSWR_CzyPunktPoprawny((uint32_t)i))
                continue;

            swr = DSP_CalcVSWR(values[i]);
            if (!isfinite(swr) || swr < 1.0f)
                continue;

            s11 = S11Calc(swr);
            if (!isfinite(s11))
                continue;

            znaleziono_skale = 1U;
            if (s11 < minS11)
                minS11 = s11;
        }

        if (!znaleziono_skale || minS11 >= -0.01f)
            minS11 = -30.0f;
        if (minS11 < -60.f)
            minS11 = -60.f;
        MIN_S11 = minS11;
    }
    else
        minS11 = MIN_S11;

    if (!isfinite(minS11) || minS11 >= -0.01f)
        minS11 = -30.0f;

    int nticks = 14; //Max number of intermediate ticks of labels
    float range = nicenum(-minS11, 0);
    float d = nicenum(range / (nticks - 1), 1);
    float graphmin = floorf(minS11 / d) * d;
    float graphmax = 0.f;
    float grange = graphmax - graphmin;
    float nfrac = MAX(-floorf(log10f(d)), 0); // # of fractional digits to show
    char str[30];
    if (nfrac > 4)
        nfrac = 4;
    sprintf(str, "%%.%df", (int)nfrac); // simplest axis labels

    //Draw horizontal lines and labels
    int yofs = 0;
    //int yofs_sm = 0;
    float labelValue;

#define S11OFFS(s11) ((int)roundf(((s11 - graphmin) * WHEIGHT) / grange) + 1)

    for (labelValue = graphmin; labelValue < graphmax + (.5 * d); labelValue += d)
    {
        sprintf(buf, str, labelValue); //Get label string in buf
        yofs = S11OFFS(labelValue);
        FONT_Write(FONT_FRAN, PANVSWR_KolorTekstuWykresu(), PANVSWR_KolorTlaWykresu(), X0 - 21, WY(yofs) - 12, buf); // FONT_SDIGITS WK
                                                                                    /* if (roundf(labelValue) == 0)
            LCD_HLine(LCD_MakePoint(X0, WY(S11OFFS(0.f))), WWIDTH, WGRIDCOLOR);
        else*/
        LCD_HLine(LCD_MakePoint(X0, WY(yofs)), WWIDTH, WGRIDCOLOR);
    }

    uint16_t lasty = 0;
    int MaxJ = -1, maxY = -1;
    uint8_t poprzedni_wazny = 0U;
    for (j = 0; j <= WWIDTH; j++)
    {
        float swr;
        float s11;
        int offset;
        uint16_t y;
        int x;

        if (!PANVSWR_CzyPunktPoprawny((uint32_t)j))
        {
            poprzedni_wazny = 0U;
            continue;
        }

        swr = DSP_CalcVSWR(values[j]);
        s11 = S11Calc(swr);
        if (!isfinite(swr) || swr < 1.0f || !isfinite(s11))
        {
            poprzedni_wazny = 0U;
            continue;
        }

        offset = roundf((WHEIGHT / (-graphmin)) * s11);

        y = WY(offset + WHEIGHT);
        if (y > (WHEIGHT + Y0))
            y = WHEIGHT + Y0;
        x = X0 + j;
        if (maxY < y)
        {
            maxY = y;
            MaxJ = j;
        }
        if (!poprzedni_wazny)
        {
            LCD_SetPixel(LCD_MakePoint(x, y), PANVSWR_KolorKrzywej());
        }
        else
        {
            if (FatLines)
            {
                LCD_Line(LCD_MakePoint(x - 1, lasty), LCD_MakePoint(x, y), PANVSWR_KolorKrzywej()); // LCD_GREEN WK
                LCD_Line(LCD_MakePoint(x - 1, lasty + 1), LCD_MakePoint(x, y + 1), PANVSWR_KolorKrzywej());
            }
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, lasty), LCD_MakePoint(x, y), PANVSWR_KolorKrzywej());
            }
        }
        lasty = y;
        poprzedni_wazny = 1U;
    }
    if (MaxJ >= 0 &&
        ((AutoCursor == 2) || ((AutoCursor == 1) && (ManualCursor == 0))))
    {
        cursorPos = (uint32_t)MaxJ;
    }
    //DrawCursor();
}

void DrawX_Scale(float MaxZ, float MinZ)
{
    float labelValue, d, factorA, factorB;
    int yofs;
    char str[20];
    int nticks = 7; //Max number of intermediate ticks of labels 8
    float range_i = nicenum(MaxZ - MinZ, 0);
    d = nicenum(range_i / (nticks - 1), 1);
    float graphmin_i = floorf(MinZ / d) * d;
    float graphmax_i = MaxZ * 0.95; //ceilf(MaxZ / d) * d;
    //float grange_i = graphmax_i - graphmin_i;
    float nfrac_i = MAX(-floorf(log10f(d)), 0); // # of fractional digits to show

    if (nfrac_i > 3)
        nfrac_i = 3;
    sprintf(str, "%%.%df", (int)nfrac_i); // simplest axis labels

    //Draw  labels
    yofs = 0;

    factorA = (float)WHEIGHT / (MaxZ - MinZ);
    factorB = -(float)WHEIGHT * MinZ / (MaxZ - MinZ);
    for (labelValue = graphmin_i; labelValue < graphmax_i + (.5 * d); labelValue += d)
    {
        if (graphmax_i >= 10000)
            sprintf(buf, "%.0f k", labelValue / 1000);
        else
            sprintf(buf, str, labelValue); //Get label string in buf
        yofs = factorA * labelValue + factorB;
        if (yofs < WHEIGHT - 12)
            FONT_Write(FONT_FRAN, PANVSWR_KolorKrzywejDrugiej(), PANVSWR_KolorTlaWykresu(), 425, WY(yofs) - 12, buf);
    }
}

static void DrawRX(int SelQu, int SelEqu) // SelQu=1, if quartz measurement  SelEqu=1, if equal scales
{
#define LimitR 1999.f
    float LimitX;
    int i; //, imax;
    int x, RXX0;
    /* Obie gałęzie SelEqu dawały ten sam kolor - rozgałęzienie było martwe. */
    const int32_t RCurvColor = (int32_t)PANVSWR_KolorKrzywej();
    if (SelQu == 0)
    {
        LimitX = LimitR;
        RXX0 = X0;
    }
    else
    {
        LimitX = 99999.f;
        RXX0 = 21;
    }
    if (SelQu == 0)
    {
        FONT_Write(FONT_FRANBIG, RCurvColor, PANVSWR_KolorTlaWykresu(), RXX0 + 412, Y0 - 18, "R");
        FONT_Write(FONT_FRANBIG, PANVSWR_KolorKrzywejDrugiej(), PANVSWR_KolorTlaWykresu(), RXX0 + 412, Y0 + 28, "X");
    }
    if (!isMeasured)
        return;
    //Find min and max values among scanned R and X to set up scale

    float minRXr = 1000000.f, minRXi = 1000000.f;
    float maxRXr = -1000000.f, maxRXi = -1000000.f;
    uint8_t znaleziono_punkt = 0U;
    for (i = 0; i <= WWIDTH; i++)
    {
        if (!PANVSWR_CzyPunktPoprawny((uint32_t)i))
            continue;

        znaleziono_punkt = 1U;
        if (crealf(values[i]) < minRXr)
            minRXr = crealf(values[i]);
        if (cimagf(values[i]) < minRXi)
            minRXi = cimagf(values[i]);
        if (crealf(values[i]) > maxRXr)
            maxRXr = crealf(values[i]);
        if (cimagf(values[i]) > maxRXi)
        {
            maxRXi = cimagf(values[i]);
            //if(cimagf(values[i+1])<=maxRXi)
            //    imax=i;
        }
    }

    if (!znaleziono_punkt)
    {
        minRXr = 0.0f;
        maxRXr = 100.0f;
        minRXi = -50.0f;
        maxRXi = 50.0f;
    }

    if (minRXr < -LimitR)
        minRXr = -LimitR;
    if (maxRXr > LimitR)
        maxRXr = LimitR;

    if (minRXi < -LimitX) // 1999.f or 49999.f
        minRXi = -LimitX;
    if (maxRXi > LimitX)
        maxRXi = LimitX;

    if (SelEqu == 1)
    {
        if (maxRXr < maxRXi)
            maxRXr = maxRXi;
        else
            maxRXi = maxRXr;
        if (minRXr > minRXi)
            minRXr = minRXi;
        else
            minRXi = minRXr;
    }
    if (maxRXr - minRXr < 40)
    {
        maxRXr += 20;
        minRXr -= 10;
    }
    if (maxRXi - minRXi < 40)
    {
        maxRXi += 20;
        minRXi -= 10;
    }

    int nticks = 8; //Max number of intermediate ticks of labels
    float range_r = nicenum(maxRXr - minRXr, 0);

    float d = nicenum(range_r / (nticks - 1), 1);
    float graphmin_r = floorf(minRXr / d) * d;
    float graphmax_r = ceilf(maxRXr / d) * d;
    float grange_r = graphmax_r - graphmin_r;
    float nfrac_r = MAX(-floorf(log10f(d)), 0); // # of fractional digits to show
    char str[20];
    if (nfrac_r > 4)
        nfrac_r = 4;
    sprintf(str, "%%.%df", (int)nfrac_r); // simplest axis labels

    //Draw horizontal lines and labels
    int yofs = 0;
    int yofs_sm = 0;
    float labelValue;

#define RXOFFS(rx) ((int)roundf(((rx - graphmin_r) * WHEIGHT) / grange_r) + 1)

    for (labelValue = graphmin_r; labelValue < graphmax_r + (.5 * d); labelValue += d)
    {
        yofs = RXOFFS(labelValue);
        sprintf(buf, str, labelValue);                                              //Get label string in buf
        if (SelEqu == 0)
        {
            const int y_etykiety = WY(yofs) - 12;

            /*
             * W podglądzie kwarcu pierwszy (najwyższy) opis skali R wpadał
             * w wiersz z częstotliwością i zakresem skanu. Zwykły wykres RX
             * zachowuje historyczne etykiety, natomiast dla kwarcu pomijamy
             * tylko etykietę, która wychodzi ponad właściwy obszar wykresu.
             */
            if (SelQu == 0 || y_etykiety >= Y0)
                FONT_Write(FONT_FRAN, RCurvColor, PANVSWR_KolorTlaWykresu(), 27, (uint16_t)y_etykiety, buf);
        }
        if (roundf(labelValue) == 0)
            LCD_HLine(LCD_MakePoint(RXX0, WY(RXOFFS(0.f))), WWIDTH, WGRIDCOLORBR);
        else
            LCD_HLine(LCD_MakePoint(RXX0, WY(yofs)), WWIDTH, WGRIDCOLOR);
    }

    //Now draw R graph
    int lastoffset = 0;
    int lastoffset_sm = 0;
    uint8_t poprzedni_r_wazny = 0U;
    uint8_t poprzedni_r_sm_wazny = 0U;

    for (i = 0; i <= WWIDTH; i++)
    {
        float complex wygladzony;
        uint8_t r_wazny = PANVSWR_CzyPunktPoprawny((uint32_t)i);
        uint8_t r_sm_wazny = PANVSWR_WygladzPunkt(i,
            f1 > CFG_GetParam(CFG_PARAM_BAND_FMAX), &wygladzony);
        x = RXX0 + i;

        if (r_wazny)
        {
            float r = crealf(values[i]);
            if (r < -LimitR)
                r = -LimitR;
            else if (r > LimitR)
                r = LimitR;
            yofs = RXOFFS(r);
            if (yofs > WHEIGHT)
                yofs = WHEIGHT;
            if (yofs < 0)
                yofs = 0;

            if (!poprzedni_r_wazny)
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), RCurvColor);
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), RCurvColor);
                if (FatLines)
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset) + 1), LCD_MakePoint(x, WY(yofs) + 1), RCurvColor);
            }
            lastoffset = yofs;
        }
        poprzedni_r_wazny = r_wazny;

        if (r_sm_wazny)
        {
            float r = crealf(wygladzony);
            if (r < -LimitR)
                r = -LimitR;
            else if (r > LimitR)
                r = LimitR;
            yofs_sm = RXOFFS(r);
            if (yofs_sm > WHEIGHT)
                yofs_sm = WHEIGHT;
            if (yofs_sm < 0)
                yofs_sm = 0;

            if (!poprzedni_r_sm_wazny)
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs_sm)), RCurvColor);
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(yofs_sm)), RCurvColor);
                if (FatLines)
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm) + 1), LCD_MakePoint(x, WY(yofs_sm) + 1), RCurvColor);
            }
            lastoffset_sm = yofs_sm;
        }
        poprzedni_r_sm_wazny = r_sm_wazny;
    }
    float range_i = nicenum(maxRXi - minRXi, 0);
    d = nicenum(range_i / (nticks - 1), 1);
    float graphmin_i = floorf(minRXi / d) * d;
    float graphmax_i = ceilf(maxRXi / d) * d;
    float grange_i = graphmax_i - graphmin_i;
    float nfrac_i = MAX(-floorf(log10f(d)), 0); // # of fractional digits to show

    if (nfrac_i > 4)
        nfrac_i = 4;
    sprintf(str, "%%.%df", (int)nfrac_i); // simplest axis labels

    //Draw  labels
    yofs = 0;
    yofs_sm = 0;
    // draw right scale:
    for (labelValue = graphmin_i; labelValue < graphmax_i + (.5 * d); labelValue += d)
    {
        if (graphmax_i >= 10000)
            sprintf(buf, "%.0f k", labelValue / 1000);
        else
            sprintf(buf, str, labelValue); //Get label string in buf

        yofs = ((int)roundf(((labelValue - graphmin_i) * WHEIGHT) / grange_i) + 1);
        if (yofs < WHEIGHT - 12)
            FONT_Write(FONT_FRAN, PANVSWR_KolorKrzywejDrugiej(), PANVSWR_KolorTlaWykresu(), 425, WY(yofs) - 12, buf); // WK
    }

    //Now draw X graph
    lastoffset = 0;
    lastoffset_sm = 0;
    uint8_t poprzedni_x_wazny = 0U;
    uint8_t poprzedni_x_sm_wazny = 0U;
    for (i = 0; i <= WWIDTH; i++)
    {
        float complex wygladzony;
        uint8_t x_wazny = PANVSWR_CzyPunktPoprawny((uint32_t)i);
        uint8_t x_sm_wazny = PANVSWR_WygladzPunkt(i,
            f1 > CFG_GetParam(CFG_PARAM_BAND_FMAX), &wygladzony);
        x = RXX0 + i;

        if (x_wazny)
        {
            float ix = cimagf(values[i]);
            if (ix < -LimitX)
                ix = -LimitX;
            else if (ix > LimitX)
                ix = LimitX;
            yofs = ((int)roundf(((ix - graphmin_i) * WHEIGHT) / grange_i) + 1);
            if (yofs > WHEIGHT)
                yofs = WHEIGHT;
            if (yofs < 0)
                yofs = 0;

            if (!poprzedni_x_wazny)
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs)), PANVSWR_KolorKrzywejDrugiej());
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset)), LCD_MakePoint(x, WY(yofs)), PANVSWR_KolorKrzywejDrugiej());
                if (FatLines)
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset) + 1), LCD_MakePoint(x, WY(yofs) + 1), PANVSWR_KolorKrzywejDrugiej());
            }
            lastoffset = yofs;
        }
        poprzedni_x_wazny = x_wazny;

        if (x_sm_wazny)
        {
            float ix = cimagf(wygladzony);
            if (ix < -LimitX)
                ix = -LimitX;
            else if (ix > LimitX)
                ix = LimitX;
            yofs_sm = ((int)roundf(((ix - graphmin_i) * WHEIGHT) / grange_i) + 1);
            if (yofs_sm > WHEIGHT)
                yofs_sm = WHEIGHT;
            if (yofs_sm < 0)
                yofs_sm = 0;

            if (!poprzedni_x_sm_wazny)
                LCD_SetPixel(LCD_MakePoint(x, WY(yofs_sm)), PANVSWR_KolorKrzywejDrugiej());
            else
            {
                LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm)), LCD_MakePoint(x, WY(yofs_sm)), PANVSWR_KolorKrzywejDrugiej());
                if (FatLines)
                    LCD_Line(LCD_MakePoint(x - 1, WY(lastoffset_sm) + 1), LCD_MakePoint(x, WY(yofs_sm) + 1), PANVSWR_KolorKrzywejDrugiej());
            }
            lastoffset_sm = yofs_sm;
        }
        poprzedni_x_sm_wazny = x_sm_wazny;
    }
    //if (grType != GRAPH_VSWR_RX)
    //    DrawCursor();
}

static void DrawSmith(void)
{
    int i;
    const uint32_t flagi_siatki = SMITH_R50 | SMITH_R25 | SMITH_R10 |
                                  SMITH_R100 | SMITH_R200 | SMITH_R500 |
                                  SMITH_J50 | SMITH_J100 | SMITH_J200 |
                                  SMITH_J25 | SMITH_J10 | SMITH_J500 |
                                  SMITH_SWR2 | SMITH_Y50;
    const uint32_t flagi_opisow = SMITH_R25 | SMITH_R50 | SMITH_R100 |
                                  SMITH_R200 | SMITH_J25 | SMITH_J50 |
                                  SMITH_J100 | SMITH_J200;
    const float r0f = (float)CFG_GetParam(CFG_PARAM_R0);

    /*
     * wcześniejszej weryfikacji: Smith nie ma już wariantu miniaturowego. Funkcja jest
     * rendererem wyłącznie pełnego widoku uruchamianego przez
     * SMITH_PelnyEkran(). Gdy ktoś omyłkowo wywoła ją z panoramy, niczego
     * nie rysujemy zamiast przywracać dawny, nieczytelny wykres 180 px.
     */
    if (!smith_pelny_ekran)
        return;

    LCD_FillAll(BackGrColor);
    SMITH_DrawGrid(SMITH_PELNY_CX, SMITH_PELNY_CY, SMITH_PELNY_PROMIEN,
                   WGRIDCOLOR, BackGrColor, flagi_siatki);
    SMITH_DrawLabels(TextColor, BackGrColor, flagi_opisow);

    if (!isMeasured)
        return;

    SMITH_ResetStartPoint();
    for (i = 0; i <= WWIDTH; i++)
    {
        if (!PANVSWR_CzyPunktPoprawny((uint32_t)i))
        {
            /* Luka w pomiarze ma być także luką na wykresie Smitha. */
            SMITH_ResetStartPoint();
            continue;
        }

        const float complex g = OSL_GFromZ(values[i], r0f);
        if (!isfinite(crealf(g)) || !isfinite(cimagf(g)))
        {
            SMITH_ResetStartPoint();
            continue;
        }
        SMITH_DrawG(i, g, CurvColor);
    }
    SMITH_DrawGEndMark(LCD_RED);

    SMITH_RysujZnacznikStartu(r0f);
    SMITH_RysujStatusKalibracji();
}

/*
 * Bez znacznika poczatku kierunek narastania czestotliwosci da sie odczytac
 * tylko wtedy, gdy zna sie ksztalt krzywej. Koniec byl juz zaznaczony na
 * czerwono; poczatek dostaje pusty okrag, zeby nie mylil sie z markerem.
 */
static void SMITH_RysujZnacznikStartu(float r0f)
{
    uint32_t pierwszy = 0U;
    float complex g;
    int32_t x;
    int32_t y;

    if (!PANVSWR_ZnajdzNajblizszyPoprawny(0U, &pierwszy))
        return;

    g = OSL_GFromZ(values[pierwszy], r0f);

    if (!isfinite(crealf(g)) || !isfinite(cimagf(g)) || cabsf(g) > 1.0f)
        return;

    x = SMITH_PELNY_CX + (int32_t)roundf(crealf(g) * SMITH_PELNY_PROMIEN);
    y = SMITH_PELNY_CY - (int32_t)roundf(cimagf(g) * SMITH_PELNY_PROMIEN);

    LCD_Circle(LCD_MakePoint(x, y), 4, UI_KolorTekstu(UI_STYL_NIEAKTYWNY));
}

/*
 * Z jakiej kalibracji pochodzi ten wykres. Przy nieważnym profilu mowimy o tym
 * wprost zamiast rysowac ladny wykres udajacy wiarygodny pomiar.
 */
static void SMITH_RysujStatusKalibracji(void)
{
    const bool wazny = (pan_model_korekcji != POMIAR_S11_KOREKCJA_BRAK);
    char linia[28];

    /*
     * Status trafia pod lewy panel, a nie na dolny pasek akcji. W SMITH2 byl
     * rysowany na y=246 i natychmiast przykrywany przez pasek od y=238.
     * Pozycja x=10, y=176 jest poza kolem (lewa krawedz x=134) i pod panelem
     * danych, ktory konczy sie na y=162.
     *
     * Etykieta opisuje model FAKTYCZNIE zwrocony przez POMIAR_S11, a nie tylko
     * profil zaznaczony w menu.
     */
    switch (pan_model_korekcji)
    {
    case POMIAR_S11_KOREKCJA_OSL_70CM:
        snprintf(linia, sizeof(linia), "OSL: 70cm");
        break;
    case POMIAR_S11_KOREKCJA_OSL_LC:
        snprintf(linia, sizeof(linia), "OSL: L/C");
        break;
    case POMIAR_S11_KOREKCJA_OSL_KLASYCZNY:
        snprintf(linia, sizeof(linia), "OSL: %s", OSL_GetSelectedName());
        break;
    default:
        snprintf(linia, sizeof(linia), "OSL: BRAK");
        break;
    }

    FONT_Write(FONT_FRAN,
               wazny ? UI_KolorTekstu(UI_STYL_NIEAKTYWNY)
                     : UI_KolorTekstu(UI_STYL_OSTRZEZENIE),
               BackGrColor, 10, 176, linia);
}

static void SMITH_PelnyRysujKursor(void)
{
    float complex g;
    int32_t x;
    int32_t y;
    LCDColor kolor;

    if (!isMeasured || cursorPos > WWIDTH || !PANVSWR_CzyPunktPoprawny(cursorPos))
        return;

    g = OSL_GFromZ(values[cursorPos], (float)CFG_GetParam(CFG_PARAM_R0));
    if (!isfinite(crealf(g)) || !isfinite(cimagf(g)) || cabsf(g) > 1.0f)
        return;

    x = SMITH_PELNY_CX + (int32_t)roundf(crealf(g) * SMITH_PELNY_PROMIEN);
    y = SMITH_PELNY_CY - (int32_t)roundf(cimagf(g) * SMITH_PELNY_PROMIEN);
    kolor = UI_KolorRamki(UI_STYL_OSTRZEZENIE);

    /*
     * Krzyz z przerwa w srodku zamiast pelnego kola. Kropka zasłaniala punkt,
     * ktory ma wskazywac — a przy krzywej gestej jak w panoramie 500 punktow
     * to wlasnie polozenie jest cala informacja. Przerwa zostawia widoczny
     * fragment krzywej pod markerem.
     */
    LCD_Circle(LCD_MakePoint(x, y), 6, kolor);
    LCD_HLine(LCD_MakePoint(x - 12, y), 7, kolor);
    LCD_HLine(LCD_MakePoint(x + 6, y), 7, kolor);
    LCD_VLine(LCD_MakePoint(x, y - 12), 7, kolor);
    LCD_VLine(LCD_MakePoint(x, y + 6), 7, kolor);

    /* Etykieta obok, nigdy na markerze. */
    /*
     * Prawy panel zaczyna sie przy x=362. Dla markera w prawej czesci kola
     * etykieta musi trafiac na lewo od punktu, inaczej moze wejsc na panel
     * danych mimo ze sam marker nadal miesci sie w kole.
     */
    FONT_Write(FONT_FRAN, kolor, BackGrColor,
               (uint16_t)((x <= 320) ? (x + 10) : (x - 26)),
               (uint16_t)((y > 20) ? (y - 16) : (y + 10)), "M1");
}

typedef struct
{
    uint8_t poprawny;
    uint8_t minimum_na_krawedzi;
    uint8_t ma_pasmo_swr2;
    uint32_t indeks_min_swr;
    uint32_t indeks_min_abs_x;
    uint32_t indeks_swr2_od;
    uint32_t indeks_swr2_do;
    float min_swr;
    float min_abs_x;
} SMITH_ANALIZA_SKAN_t;

static uint32_t SMITH_PelnyCzestotliwoscHz(uint32_t indeks)
{
    const uint64_t start_hz = PANVSWR_StartSkanuHz();
    const uint64_t zakres_hz = (uint64_t)BSVALUES[span] * 1000ULL;

    if (indeks > WWIDTH)
        indeks = WWIDTH;

    return (uint32_t)(start_hz +
                      ((uint64_t)indeks * zakres_hz + (uint64_t)WWIDTH / 2ULL) /
                          (uint64_t)WWIDTH);
}

static uint8_t SMITH_PelnyPobierzSWR(uint32_t indeks, float *swr)
{
    float wartosc;
    DSP_RX z;

    if (swr == 0 || indeks > WWIDTH || !PANVSWR_CzyPunktPoprawny(indeks))
        return 0U;

    z = values[indeks];
    if (!isfinite(crealf(z)) || !isfinite(cimagf(z)))
        return 0U;

    wartosc = DSP_CalcVSWR(z);
    if (!isfinite(wartosc) || wartosc < 1.0f)
        return 0U;

    *swr = wartosc;
    return 1U;
}

static void SMITH_PelnyAnalizujSkan(SMITH_ANALIZA_SKAN_t *wynik)
{
    uint32_t i;
    float z0;

    if (wynik == 0)
        return;

    memset(wynik, 0, sizeof(*wynik));
    wynik->min_swr = INFINITY;
    wynik->min_abs_x = INFINITY;
    z0 = (float)CFG_GetParam(CFG_PARAM_R0);

    if (!isMeasured || z0 <= 0.0f)
        return;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        float swr;
        const DSP_RX z = values[i];
        const float abs_x = fabsf(cimagf(z));

        if (!PANVSWR_CzyPunktPoprawny(i) ||
            !isfinite(crealf(z)) || !isfinite(cimagf(z)))
            continue;

        if (SMITH_PelnyPobierzSWR(i, &swr) && swr < wynik->min_swr)
        {
            wynik->min_swr = swr;
            wynik->indeks_min_swr = i;
            wynik->poprawny = 1U;
        }

        if (isfinite(abs_x) && abs_x < wynik->min_abs_x)
        {
            wynik->min_abs_x = abs_x;
            wynik->indeks_min_abs_x = i;
        }
    }

    if (!wynik->poprawny)
        return;

    /*
     * Minimum przy samym skraju nie dowodzi, że znaleźliśmy rzeczywiste
     * minimum anteny. Sygnalizujemy to użytkownikowi zamiast zgadywać.
     */
    wynik->minimum_na_krawedzi =
        (uint8_t)(wynik->indeks_min_swr <= 4U || wynik->indeks_min_swr >= (WWIDTH - 4U));

    if (wynik->min_swr <= 2.0f)
    {
        uint32_t lewy = wynik->indeks_min_swr;
        uint32_t prawy = wynik->indeks_min_swr;
        float swr;

        while (lewy > 0U && SMITH_PelnyPobierzSWR(lewy - 1U, &swr) && swr <= 2.0f)
            --lewy;
        while (prawy < WWIDTH && SMITH_PelnyPobierzSWR(prawy + 1U, &swr) && swr <= 2.0f)
            ++prawy;

        wynik->ma_pasmo_swr2 = 1U;
        wynik->indeks_swr2_od = lewy;
        wynik->indeks_swr2_do = prawy;
    }
}

static const char *SMITH_PelnyOpisDopasowania(float swr)
{
    if (swr <= 1.20f)
        return SMITH_T("bardzo dobre", "very good", "sehr gut", "очень хорошее");
    if (swr <= 1.50f)
        return SMITH_T("dobre", "good", "gut", "хорошее");
    if (swr <= 2.00f)
        return SMITH_T("umiarkowane", "moderate", "mittel", "среднее");
    if (swr <= 3.00f)
        return SMITH_T("słabe", "poor", "schwach", "плохое");
    return SMITH_T("bardzo słabe", "very poor", "sehr schwach", "очень плохое");
}

static const char *SMITH_PelnyOpisR(float r, float z0)
{
    const float tolerancja = 0.10f * z0;

    if (r < 0.0f)
        return SMITH_T("R ujemna - sprawdź pomiar", "negative R - check measurement", "R negativ - Messung prüfen", "R отриц. - проверьте измерение");
    if (fabsf(r - z0) <= tolerancja)
        return SMITH_T("R blisko Z0", "R near Z0", "R nahe Z0", "R близко к Z0");
    if (r < z0)
        return SMITH_T("R poniżej Z0", "R below Z0", "R unter Z0", "R ниже Z0");
    return SMITH_T("R powyżej Z0", "R above Z0", "R über Z0", "R выше Z0");
}

static const char *SMITH_PelnyOpisX(float x, float z0)
{
    float prog = 0.05f * z0;
    if (prog < 2.0f)
        prog = 2.0f;

    if (fabsf(x) <= prog)
        return SMITH_T("X blisko 0 - blisko rezonansu", "X near 0 - near resonance", "X nahe 0 - resonanznah", "X близко к 0 - близко к резонансу");
    if (x > 0.0f)
        return SMITH_T("charakter indukcyjny (X > 0)", "inductive (X > 0)", "induktiv (X > 0)", "индуктивный (X > 0)");
    return SMITH_T("charakter pojemnościowy (X < 0)", "capacitive (X < 0)", "kapazitiv (X < 0)", "ёмкостный (X < 0)");
}

static void SMITH_PelnyRysujDane(void)
{
    char tekst[48];
    float complex rx;
    float complex g;
    float swr;
    float modul_g;
    float faza_deg;
    float q = 0.0f;
    float f_mhz;
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor tekst_kolor = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor akcent = UI_KolorRamki(UI_STYL_AKCENT);

    UI_RysujPanel(4, 24, 114, 138, 0, UI_STYL_NORMALNY);
    UI_RysujPanel(362, 24, 114, 138, 0, UI_STYL_NORMALNY);

    FONT_Write(FONT_FRANBIG, akcent, tlo, 25, 32, "SMITH");

    if (!isMeasured || cursorPos > WWIDTH || !PANVSWR_CzyPunktPoprawny(cursorPos))
    {
        FONT_Write(FONT_FRAN, tekst_kolor, tlo, 12, 66,
                   isMeasured
                       ? SMITH_T("Punkt odrzucony", "Rejected point", "Punkt verworfen", "Точка отброшена")
                       : SMITH_T("Brak pomiaru", "No scan", "Keine Messung", "Нет измерения"));
        FONT_Write(FONT_FRAN, tekst_kolor, tlo, 370, 44, "Z0");
        snprintf(tekst, sizeof(tekst), "%lu Ohm", (unsigned long)CFG_GetParam(CFG_PARAM_R0));
        FONT_Write(FONT_FRAN, tekst_kolor, tlo, 370, 64, tekst);
        return;
    }

    rx = values[cursorPos];
    g = OSL_GFromZ(rx, (float)CFG_GetParam(CFG_PARAM_R0));
    swr = DSP_CalcVSWR(rx);
    modul_g = cabsf(g);
    faza_deg = atan2f(cimagf(g), crealf(g)) * (180.0f / 3.14159265358979323846f);
    if (crealf(rx) > 0.1f)
        q = fabsf(cimagf(rx) / crealf(rx));
    if (q > 2000.0f)
        q = 2000.0f;

    f_mhz = (float)SMITH_PelnyCzestotliwoscHz(cursorPos) / 1000000.0f;

    snprintf(tekst, sizeof(tekst), "F %.3f MHz", (double)f_mhz);
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 10, 62, tekst);
    snprintf(tekst, sizeof(tekst), "SWR %.2f", (double)swr);
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 10, 84, tekst);
    snprintf(tekst, sizeof(tekst), "|G| %.3f", (double)modul_g);
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 10, 106, tekst);
    snprintf(tekst, sizeof(tekst), "phi %.1f deg", (double)faza_deg);
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 10, 128, tekst);

    snprintf(tekst, sizeof(tekst), "Z0 %lu Ohm", (unsigned long)CFG_GetParam(CFG_PARAM_R0));
    FONT_Write(FONT_FRAN, akcent, tlo, 370, 34, tekst);
    snprintf(tekst, sizeof(tekst), "R %.1f Ohm", (double)crealf(rx));
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 370, 62, tekst);
    snprintf(tekst, sizeof(tekst), "X %+.1f Ohm", (double)cimagf(rx));
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 370, 84, tekst);
    snprintf(tekst, sizeof(tekst), "Q %.2f", (double)q);
    FONT_Write(FONT_FRAN, tekst_kolor, tlo, 370, 106, tekst);
    snprintf(tekst, sizeof(tekst), "%lu/%lu", (unsigned long)(cursorPos + 1U), (unsigned long)(WWIDTH + 1U));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 370, 128, tekst);
}

enum
{
    SMITH_AKCJA_WSTECZ = 1,
    SMITH_AKCJA_SKANUJ,
    SMITH_AKCJA_ANALIZA,
    SMITH_AKCJA_ZRZUT
};

static void SMITH_PelnyPobierzAkcje(UI_AKCJA_t akcje[4])
{
    akcje[0] = (UI_AKCJA_t){SMITH_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false};
    akcje[1] = (UI_AKCJA_t){SMITH_AKCJA_SKANUJ, JEZYK_Tekst(TEKST_SKANUJ), UI_STYL_AKCENT, true, false};
    akcje[2] = (UI_AKCJA_t){SMITH_AKCJA_ANALIZA, JEZYK_Tekst(TEKST_ANALIZA), UI_STYL_AKCENT, isMeasured != 0, false};
    akcje[3] = (UI_AKCJA_t){SMITH_AKCJA_ZRZUT,
                            SMITH_T("Zrzut", "Snapshot", "Bild", "Снимок"),
                            UI_STYL_NORMALNY, isMeasured != 0, false};
}

static void SMITH_PelnyRysuj(void)
{
    DrawSmith();
    SMITH_PelnyRysujKursor();
    SMITH_PelnyRysujDane();

    {
        UI_AKCJA_t akcje[4];
        SMITH_PelnyPobierzAkcje(akcje);
        UI_RysujPasekAkcji(SMITH_PELNY_PRZYCISK_Y, SMITH_PELNY_PRZYCISK_H, akcje, 4U);
    }
}

static void SMITH_PelnyAnalizaRysuj(void)
{
    char linia[112];
    char f1[24];
    char f2[24];
    char f3[24];
    DSP_RX z;
    float swr;
    float z0;
    SMITH_ANALIZA_SKAN_t skan;
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor normalny = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor akcent = UI_KolorRamki(UI_STYL_AKCENT);

    UI_WyczyscEkran();
    UI_RysujNaglowek(SMITH_T("Analiza wykresu Smitha", "Smith chart analysis", "Smith-Diagramm Analyse", "Анализ диаграммы Смита"));

    if (!isMeasured || cursorPos > WWIDTH ||
        !SMITH_PelnyPobierzSWR(cursorPos, &swr))
    {
        UI_RysujPanel(16, 58, 448, 116, 0, UI_STYL_OSTRZEZENIE);
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), tlo, 34, 86,
                   SMITH_T("Brak poprawnego skanu do analizy.", "No valid scan to analyse.", "Keine gültige Messung zur Analyse.", "Нет корректного скана для анализа."));
        UI_RysujWsteczDolny(false);
        return;
    }

    z = values[cursorPos];
    z0 = (float)CFG_GetParam(CFG_PARAM_R0);
    SMITH_PelnyAnalizujSkan(&skan);

    UI_RysujPanel(10, 38, 460, 94,
                  SMITH_T("Punkt kursora", "Cursor point", "Cursorpunkt", "Точка курсора"), UI_STYL_NORMALNY);
    snprintf(f1, sizeof(f1), "%.3f MHz", (double)SMITH_PelnyCzestotliwoscHz(cursorPos) / 1000000.0);
    snprintf(linia, sizeof(linia), "%s   Z = %.1f %+.1fj Ohm   SWR %.2f",
             f1, (double)crealf(z), (double)cimagf(z), (double)swr);
    FONT_Write(FONT_FRAN, akcent, tlo, 22, 58, linia);
    snprintf(linia, sizeof(linia), "%s: %s",
             SMITH_T("Dopasowanie", "Match", "Anpassung", "Согласование"),
             SMITH_PelnyOpisDopasowania(swr));
    FONT_Write(FONT_FRAN, normalny, tlo, 22, 80, linia);
    FONT_Write(FONT_FRAN, normalny, tlo, 22, 101, SMITH_PelnyOpisR(crealf(z), z0));
    FONT_Write(FONT_FRAN, normalny, tlo, 220, 101, SMITH_PelnyOpisX(cimagf(z), z0));

    UI_RysujPanel(10, 140, 460, 78,
                  SMITH_T("Cały skan", "Whole scan", "Gesamter Scan", "Весь скан"), UI_STYL_NORMALNY);

    if (skan.poprawny)
    {
        snprintf(f1, sizeof(f1), "%.3f", (double)SMITH_PelnyCzestotliwoscHz(skan.indeks_min_swr) / 1000000.0);
        snprintf(linia, sizeof(linia), "%s: SWR %.2f @ %s MHz%s",
                 SMITH_T("Najlepsze", "Best", "Bestes", "Лучшее"),
                 (double)skan.min_swr, f1,
                 skan.minimum_na_krawedzi ? "  !" : "");
        FONT_Write(FONT_FRAN, skan.minimum_na_krawedzi ? UI_KolorRamki(UI_STYL_OSTRZEZENIE) : akcent,
                   tlo, 22, 158, linia);

        snprintf(f2, sizeof(f2), "%.3f", (double)SMITH_PelnyCzestotliwoscHz(skan.indeks_min_abs_x) / 1000000.0);
        snprintf(linia, sizeof(linia), "min |X| %.1f Ohm @ %s MHz", (double)skan.min_abs_x, f2);
        FONT_Write(FONT_FRAN, normalny, tlo, 22, 178, linia);

        if (skan.ma_pasmo_swr2)
        {
            snprintf(f1, sizeof(f1), "%.3f", (double)SMITH_PelnyCzestotliwoscHz(skan.indeks_swr2_od) / 1000000.0);
            snprintf(f3, sizeof(f3), "%.3f", (double)SMITH_PelnyCzestotliwoscHz(skan.indeks_swr2_do) / 1000000.0);
            snprintf(linia, sizeof(linia), "SWR <= 2: %s ... %s MHz", f1, f3);
        }
        else
        {
            snprintf(linia, sizeof(linia), "%s",
                     SMITH_T("W tym zakresie SWR nie spada do 2.", "SWR does not reach 2 in this span.", "SWR erreicht in diesem Bereich nicht 2.", "В этом диапазоне SWR не снижается до 2."));
        }
        FONT_Write(FONT_FRAN, normalny, tlo, 22, 198, linia);
    }

    if (skan.minimum_na_krawedzi)
    {
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_OSTRZEZENIE), UI_KolorTlaEkranu(), 14, 222,
                   SMITH_T("! Minimum na brzegu skanu - warto rozszerzyć zakres.", "! Minimum at scan edge - consider widening the span.", "! Minimum am Scanrand - Bereich erweitern.", "! Минимум на краю - расширьте диапазон."));
    }
    else
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaEkranu(), 14, 222,
                   SMITH_T("Opis dotyczy impedancji; nie zgaduje konstrukcji anteny.", "Interpretation describes impedance, not antenna construction.", "Die Auswertung beschreibt die Impedanz, nicht den Antennenaufbau.", "Оценка описывает импеданс, а не конструкцию антенны."));
    }

    UI_RysujWsteczDolny(false);
}

static void SMITH_PelnyAnalizaEkran(void)
{
    uint8_t koniec = 0U;

    while (TOUCH_IsPressed())
        Sleep(10);

    SMITH_PelnyAnalizaRysuj();
    LCD_ShowActiveLayerOnly();

    while (!koniec)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ || zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            koniec = 1U;
        }
        else if (TOUCH_Poll(&pt))
        {
            if (UI_CzyDotknietoWstecz(pt))
            {
                TOUCH_CzekajNaPuszczenie(20U);
                koniec = 1U;
            }
        }
        Sleep(10);
    }

    while (TOUCH_IsPressed())
        Sleep(10);
}

static void SMITH_PelnyUstawKursorDotykiem(const LCDPoint *punkt)
{
    uint32_t i;
    uint32_t najlepszy = cursorPos;
    uint32_t najlepsza_odleglosc = 0xFFFFFFFFUL;

    if (!isMeasured || punkt == 0)
        return;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        float complex g;
        int32_t x;
        int32_t y;
        int32_t dx;
        int32_t dy;
        uint32_t odleglosc;

        if (!PANVSWR_CzyPunktPoprawny(i))
            continue;

        g = OSL_GFromZ(values[i], (float)CFG_GetParam(CFG_PARAM_R0));
        if (!isfinite(crealf(g)) || !isfinite(cimagf(g)) || cabsf(g) > 1.0f)
            continue;

        x = SMITH_PELNY_CX + (int32_t)roundf(crealf(g) * SMITH_PELNY_PROMIEN);
        y = SMITH_PELNY_CY - (int32_t)roundf(cimagf(g) * SMITH_PELNY_PROMIEN);
        dx = (int32_t)punkt->x - x;
        dy = (int32_t)punkt->y - y;
        odleglosc = (uint32_t)(dx * dx + dy * dy);

        if (odleglosc < najlepsza_odleglosc)
        {
            najlepsza_odleglosc = odleglosc;
            najlepszy = i;
        }
    }

    cursorPos = najlepszy;
    ManualCursor = 1;
}

static void RedrawWindow()
{
    isSaved = 0;

    if (pan_swr_strona == PAN_SWR_STRONA_WYNIK)
    {
        PANVSWR_RysujStroneWynik();
        ClearScreen = 0;
        return;
    }

    if (pan_swr_strona == PAN_SWR_STRONA_NARZEDZIA)
    {
        PANVSWR_RysujStroneNarzedzia();
        ClearScreen = 0;
        return;
    }

    if (ClearScreen)
    {
        LCD_FillAll(BackGrColor);
        ClearScreen = 0;
    }

    PANVSWR_RysujNaglowekStrony(JEZYK_Tekst(TEKST_MENU_WYKRES_SWR));

    if ((grType == GRAPH_VSWR) || (grType == GRAPH_VSWR_Z) || (grType == GRAPH_VSWR_RX))
    {
        DrawGrid(GRAPH_VSWR);
        if (isMeasured)
            DrawVSWR();
    }
    else if (grType == GRAPH_RX)
    {
        DrawGrid(GRAPH_RX);
        if (isMeasured)
            DrawRX(0, 0);
    }
    else if (grType == GRAPH_S11)
    {
        DrawGrid(GRAPH_S11);
        if (isMeasured)
            DrawS11();
    }
    else
    {
        /* Stan GRAPH_SMITH może pochodzić ze starej konfiguracji lub pamięci.
         * Nie rysujemy już miniatury: wracamy do podstawowego wykresu. */
        grType = GRAPH_VSWR;
        DrawGrid(GRAPH_VSWR);
        if (isMeasured)
            DrawVSWR();
    }

    if (!isMeasured)
    {
        PANVSWR_RysujPomocStartowa();
        cursorVisible = 0;
    }
    else
    {
        uint32_t poprawny;

        if (!PANVSWR_CzyPunktPoprawny(cursorPos) &&
            PANVSWR_ZnajdzNajblizszyPoprawny(cursorPos, &poprawny))
        {
            cursorPos = poprawny;
        }

        DrawCursor();
        cursorVisible = PANVSWR_CzyPunktPoprawny(cursorPos) ? 1 : 0;
        if (grType != GRAPH_S11)
            DrawCursorText();
        else if (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1)
            DrawCursorTextWithS11();
    }

    DrawFootText();
}

static void save_snapshot(void)
{
    static const TCHAR *sndir = "/aa/snapshot";
    char path[64];
    char wbuf[256];
    char *fname = 0;
    uint32_t i = 0;
    FRESULT fr = FR_OK;

    if (!isMeasured || isSaved)
        return;

    if (!CFG_CzyKartaSDDostepna())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    Date_Time_Stamp();

    fname = SCREENSHOT_SelectFileName();

    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);

    //Now write measured data to S1P file
    sprintf(path, "%s/%s.s1p", sndir, fname);
    FIL fo = {0};
    UINT bw = 0;
    uint8_t plik_otwarty = 0;
    fr = f_open(&fo, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr)
        goto BLAD_S1P;
    plik_otwarty = 1;
    {
        const unsigned int z0_ohm = (unsigned int)CFG_GetParam(CFG_PARAM_R0);
        if (CFG_S1P_TYPE_S_RI == CFG_GetParam(CFG_PARAM_S1P_TYPE))
        {
            snprintf(wbuf, sizeof(wbuf), "! Touchstone file by EU1KY antenna analyzer\r\n"
                     "# MHz S RI R %u\r\n"
                     "! Format: Frequency S-real S-imaginary, reference %u Ohm\r\n"
                     "! Invalid measurement points are omitted\r\n",
                     z0_ohm, z0_ohm);
        }
        else if (CFG_S1P_TYPE_S_MA == CFG_GetParam(CFG_PARAM_S1P_TYPE))
        {
            snprintf(wbuf, sizeof(wbuf), "! Touchstone file by EU1KY antenna analyzer\r\n"
                     "# MHz S MA R %u\r\n"
                     "! Format: Frequency S-magnitude S-angle, reference %u Ohm, angle in degrees\r\n"
                     "! Invalid measurement points are omitted\r\n",
                     z0_ohm, z0_ohm);
        }
        else // CFG_S1P_TYPE_Z_RI
        {
            /* Touchstone 1.x zapisuje Z jako wartości znormalizowane do R z linii opcji. */
            snprintf(wbuf, sizeof(wbuf), "! Touchstone file by EU1KY antenna analyzer\r\n"
                     "# MHz Z RI R %u\r\n"
                     "! Format: Frequency normalized-Z-real normalized-Z-imaginary, reference %u Ohm\r\n"
                     "! Invalid measurement points are omitted\r\n",
                     z0_ohm, z0_ohm);
        }
    }
    fr = f_write(&fo, wbuf, strlen(wbuf), &bw);
    if (FR_OK != fr || bw != strlen(wbuf))
        goto BLAD_S1P;

    uint32_t fstart = PANVSWR_StartSkanuHz();

    for (i = 0; i <= WWIDTH; i++)
    {
        if (!PANVSWR_CzyPunktPoprawny(i))
            continue;

        float complex g = OSL_GFromZ(values[i], (float)CFG_GetParam(CFG_PARAM_R0));
        float fmhz = ((float)fstart / 1000.f + (float)i * BSVALUES[span] / WWIDTH) / 1000.0f;
        if (CFG_S1P_TYPE_S_RI == CFG_GetParam(CFG_PARAM_S1P_TYPE))
        {
            sprintf(wbuf, "%.6f %.6f %.6f\r\n", fmhz, crealf(g), cimagf(g));
        }
        else if (CFG_S1P_TYPE_S_MA == CFG_GetParam(CFG_PARAM_S1P_TYPE))
        {
            g = OSL_GtoMA(g); //Convert G to magnitude and angle in degrees
            sprintf(wbuf, "%.6f %.6f %.6f\r\n", fmhz, crealf(g), cimagf(g));
        }
        else // CFG_S1P_TYPE_Z_RI
        {
            sprintf(wbuf, "%.6f %.6f %.6f\r\n", fmhz, crealf(values[i]) / (float)CFG_GetParam(CFG_PARAM_R0), cimagf(values[i]) / (float)CFG_GetParam(CFG_PARAM_R0));
        }
        bw = 0;
        fr = f_write(&fo, wbuf, strlen(wbuf), &bw);
        if (FR_OK != fr || bw != strlen(wbuf))
            goto BLAD_S1P;
    }
    f_close(&fo);
    plik_otwarty = 0;

    //Stored RawData for RXP Converter by KD8CEC
    //250 : Finxed FilePaht
    //sprintf(path, "%s/%s.yhw", sndir, fname);
    //StoreRXP(250, values, path);

    isSaved = 1;
    //    BSP_LCD_SelectLayer(0);
    //    DrawSavedText();
    //    BSP_LCD_SelectLayer(1);
    DrawSavedText();
    return;

BLAD_S1P:
    if (plik_otwarty)
        f_close(&fo);
    f_unlink(path);
    KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
}

uint32_t GetFrequency(uint32_t f0)
{
    // fo in kHz
    uint32_t fkhz = f0;

    (void)PanFreqWindow(&fkhz, &span);
    return fkhz;
}

uint32_t fxs = 3500000ul; //Scan range start frequency, in Hz
uint32_t fxkHzs;          //Scan range start frequency, in kHz
BANDSPAN *pBss;

void SWR_SetFrequency(void)
{
    //    while(TOUCH_IsPressed()); WK
    fxs = CFG_GetParam(CFG_PARAM_MEAS_F);

    fxkHzs = fxs / 1000;
    span = BS300;

    if (PanFreqWindow(&fxkHzs, (BANDSPAN *)&span))
    {
        //Span or frequency has been changed
        CFG_SetParam(CFG_PARAM_MEAS_F, fxkHzs * 1000);

        f1 = fxkHzs * 1000;
        // span=(BANDSPAN)pBss;
    }
    CFG_Flush();
    //  redrawWindow = 1;
    Sleep(200);
    ShowFr(1);
}

//================================================================================================
//BEGIN OF Multi SWR
//------------------------------------------------------------------------------------------------
#define MULTISWR_LINEHEIGHT 41
#define MULTISWR_XX0 230
#define MULTISWR_YY0 42
#define MULTISWR_WIERSZE 5
#define MULTISWR_WIERSZE_NA_STRONE 4U
#define MULTISWR_LICZBA_STRON 2U

#include "bitmaps/bitmaps.h"

#define msMenus_Length 6
#define MENU_EXIT 0
#define MENU_SNAP 1
#define MENU_ANT0 2
#define MENU_PDOWN 3
#define MENU_PUP 4
#define MENU_STRONA 5

static uint8_t multi_wybrany_wiersz = 0U;
static uint8_t multi_strona_wierszy = 0U;
static uint8_t multi_pomiar_poprawny[MULTISWR_WIERSZE] = {0U};
static uint8_t multi_z200_poprawne[21] = {0U};

#define MULTISWR_LICZBA_SPANOW ((uint32_t)(sizeof(BSVALUES) / sizeof(BSVALUES[0])))

/*
 * W starych konfiguracjach spotyka sie wartosc 100000. Historyczny kod
 * zapisywal ja jako domyslna szerokosc, a pozniej uzywal jak indeksu tablicy,
 * co moglo prowadzic do odczytu poza BSVALUES[]. Traktujemy ja jako dawny zapis
 * 100 kHz i zamieniamy na bezpieczny indeks BS100.
 */
static BANDSPAN MultiSWR_NormalizujSpan(uint32_t zapis)
{
    if (zapis < MULTISWR_LICZBA_SPANOW)
        return (BANDSPAN)zapis;

    if (zapis == 100000U)
        return BS100;

    return BS200;
}

static bool MultiSWR_CzestotliwoscPoprawna(int64_t f_hz)
{
    const int64_t fmin = (int64_t)CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const int64_t fmax = (int64_t)CFG_GetParam(CFG_PARAM_BAND_FMAX);
    return f_hz >= fmin && f_hz <= fmax;
}

static bool MultiSWR_WynikPoprawny(float complex z)
{
    return isfinite(crealf(z)) && isfinite(cimagf(z));
}

static void MultiSWR_WylaczRF(void)
{
    GEN_SetMeasurementFreq(0U);
}

/*
 * Nazwa pasma jest tylko podpowiedzia orientacyjna. Nie sluzy do ograniczania
 * zakresu pomiaru i nie zmienia ustawien regionu IARU. Dzięki temu ekran jest
 * czytelniejszy, ale nadal pozwala wpisac dowolna czestotliwosc obslugiwana
 * przez analizator.
 */
static const char *MultiSWR_NazwaPasma(uint32_t f_khz)
{
    if (f_khz >= 130U && f_khz <= 150U) return "2200 m";
    if (f_khz >= 450U && f_khz <= 500U) return "630 m";
    if (f_khz >= 1800U && f_khz <= 2000U) return "160 m";
    if (f_khz >= 3500U && f_khz <= 4000U) return "80 m";
    if (f_khz >= 5250U && f_khz <= 5450U) return "60 m";
    if (f_khz >= 7000U && f_khz <= 7300U) return "40 m";
    if (f_khz >= 10000U && f_khz <= 10200U) return "30 m";
    if (f_khz >= 14000U && f_khz <= 14350U) return "20 m";
    if (f_khz >= 18000U && f_khz <= 18200U) return "17 m";
    if (f_khz >= 21000U && f_khz <= 21500U) return "15 m";
    if (f_khz >= 24800U && f_khz <= 25100U) return "12 m";
    if (f_khz >= 28000U && f_khz <= 30000U) return "10 m";
    if (f_khz >= 50000U && f_khz <= 54000U) return "6 m";
    if (f_khz >= 69000U && f_khz <= 71000U) return "4 m";
    if (f_khz >= 144000U && f_khz <= 148000U) return "2 m";
    if (f_khz >= 220000U && f_khz <= 225000U) return "1.25 m";
    if (f_khz >= 430000U && f_khz <= 450000U) return "70 cm";
    if (f_khz >= 900000U && f_khz <= 930000U) return "33 cm";
    if (f_khz >= 1240000U && f_khz <= 1300000U) return "23 cm";
    return "";
}

static uint8_t MultiSWR_PierwszyWierszStrony(void)
{
    return (uint8_t)(multi_strona_wierszy * MULTISWR_WIERSZE_NA_STRONE);
}

static uint8_t MultiSWR_LiczbaWidocznychWierszy(void)
{
    const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
    const uint8_t pozostalo = (uint8_t)(MULTISWR_WIERSZE - pierwszy);
    return pozostalo > MULTISWR_WIERSZE_NA_STRONE ? MULTISWR_WIERSZE_NA_STRONE : pozostalo;
}

static bool MultiSWR_CzyWierszWidoczny(uint8_t indeks)
{
    const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
    return indeks >= pierwszy && indeks < (uint8_t)(pierwszy + MultiSWR_LiczbaWidocznychWierszy());
}

static uint8_t MultiSWR_LokalnyWiersz(uint8_t indeks)
{
    return (uint8_t)(indeks - MultiSWR_PierwszyWierszStrony());
}

static uint16_t MultiSWR_GoraWiersza(uint8_t indeks)
{
    return (uint16_t)(MULTISWR_YY0 - 9 + MULTISWR_LINEHEIGHT * MultiSWR_LokalnyWiersz(indeks));
}

static uint16_t MultiSWR_DolWiersza(uint8_t indeks)
{
    return (uint16_t)(MULTISWR_YY0 + 29 + MULTISWR_LINEHEIGHT * MultiSWR_LokalnyWiersz(indeks));
}

static void MultiSWR_RysujRamkeWiersza(uint8_t indeks)
{
    UI_STYL_t styl;
    uint16_t y1;
    uint16_t y2;

    if (indeks >= MULTISWR_WIERSZE || !MultiSWR_CzyWierszWidoczny(indeks))
        return;

    y1 = MultiSWR_GoraWiersza(indeks);
    y2 = MultiSWR_DolWiersza(indeks);
    if (indeks == multi_wybrany_wiersz)
        styl = UI_STYL_AKTYWNY;
    else if (multi_pomiar_poprawny[indeks] == 2U)
        styl = UI_STYL_OSTRZEZENIE;
    else
        styl = UI_STYL_NORMALNY;

    LCD_Rectangle(LCD_MakePoint(2, y1), LCD_MakePoint(478, y2), UI_KolorRamki(styl));
    if (indeks == multi_wybrany_wiersz)
        LCD_Rectangle(LCD_MakePoint(3, (uint16_t)(y1 + 1U)), LCD_MakePoint(477, (uint16_t)(y2 - 1U)), UI_KolorRamki(styl));
}

static void MultiSWR_RysujNaglowek(uint32_t ant_index)
{
    char tytul[72];

    snprintf(tytul, sizeof(tytul), "%s | %s %lu/5 | %lu/%lu",
             JEZYK_Tekst(TEKST_MENU_WIELE_PASM),
             JEZYK_Wybierz("pamięć", "memory", "Speicher", "память"),
             (unsigned long)(ant_index + 1U),
             (unsigned long)(multi_strona_wierszy + 1U),
             (unsigned long)MULTISWR_LICZBA_STRON);

    UI_RysujNaglowek(tytul);
}

int TouchTest()
{
    if (TOUCH_Poll(&pt))
    {
        uint8_t lokalny;
        const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
        const uint8_t liczba = MultiSWR_LiczbaWidocznychWierszy();

        /* Dolny pasek ma własny hit-test. Nie interpretujemy go jako wiersza. */
        if (pt.y >= UI_DOLNY_PASEK_Y)
            return -1;

        if (pt.x < (MULTISWR_XX0 - 8))
        {
            for (lokalny = 0U; lokalny < liczba; ++lokalny)
            {
                const uint8_t indeks = (uint8_t)(pierwszy + lokalny);
                if (pt.y >= MultiSWR_GoraWiersza(indeks) &&
                    pt.y <= MultiSWR_DolWiersza(indeks))
                {
                    Beep(1);
                    return (int)indeks;
                }
            }
        }
    }
    return -1;
}

// Skan R-50 / X w zakresie wokol wybranej czestotliwosci.
static int rMax;
static int xMax;
static float complex z200[21] = {0};

/*
 * Wynik SWR zmienia się znacznie wolniej niż wewnętrzna pętla pomiarowa.
 * Zapamiętujemy dokładnie to, co było narysowane, aby nie czyścić pola LCD
 * przy każdym obiegu. To usuwa widoczne miganie bez zmiany samego pomiaru.
 */
static char multi_poprzedni_wynik[MULTISWR_WIERSZE][10];
static UI_STYL_t multi_poprzedni_styl[MULTISWR_WIERSZE];
static uint8_t multi_wynik_narysowany[MULTISWR_WIERSZE];
static int8_t multi_mini_r[MULTISWR_WIERSZE][21];
static int8_t multi_mini_x[MULTISWR_WIERSZE][21];
static uint8_t multi_mini_poprawne[MULTISWR_WIERSZE][21];
static uint8_t multi_mini_narysowany[MULTISWR_WIERSZE];
static char multi_poprzedni_r[MULTISWR_WIERSZE][16];
static char multi_poprzedni_x[MULTISWR_WIERSZE][16];
static uint8_t multi_rx_narysowany[MULTISWR_WIERSZE];

static void MultiSWR_WyczyscPamiecWynikow(void)
{
    memset(multi_poprzedni_wynik, 0, sizeof(multi_poprzedni_wynik));
    memset(multi_poprzedni_styl, 0, sizeof(multi_poprzedni_styl));
    memset(multi_wynik_narysowany, 0, sizeof(multi_wynik_narysowany));
    memset(multi_mini_r, 0, sizeof(multi_mini_r));
    memset(multi_mini_x, 0, sizeof(multi_mini_x));
    memset(multi_mini_poprawne, 0, sizeof(multi_mini_poprawne));
    memset(multi_mini_narysowany, 0, sizeof(multi_mini_narysowany));
    memset(multi_poprzedni_r, 0, sizeof(multi_poprzedni_r));
    memset(multi_poprzedni_x, 0, sizeof(multi_poprzedni_x));
    memset(multi_rx_narysowany, 0, sizeof(multi_rx_narysowany));
}

static void MultiSWR_PrzeliczPunktMini(int idx, float z_odniesienia,
                                      int r_max, int x_max, int8_t *r, int8_t *x)
{
    int r_tmp;
    int x_tmp;

    r_tmp = (int)((crealf(z200[idx]) - z_odniesienia) * 20.0f / (float)r_max);
    if (r_tmp > 16) r_tmp = 16;
    if (r_tmp < -16) r_tmp = -16;

    x_tmp = (int)(cimagf(z200[idx]) * 16.0f / (float)x_max);
    if (x_tmp > 16) x_tmp = 16;
    if (x_tmp < -16) x_tmp = -16;

    *r = (int8_t)r_tmp;
    *x = (int8_t)x_tmp;
}

static uint8_t MultiSWR_CzyMiniWykresZmieniony(uint8_t line, float z_odniesienia,
                                                int r_max, int x_max)
{
    int idx;

    if (!multi_mini_narysowany[line])
        return 1U;

    for (idx = 0; idx < 21; ++idx)
    {
        int8_t r = 0;
        int8_t x = 0;
        const uint8_t poprawny = multi_z200_poprawne[idx] ? 1U : 0U;

        if (multi_mini_poprawne[line][idx] != poprawny)
            return 1U;
        if (!poprawny)
            continue;

        MultiSWR_PrzeliczPunktMini(idx, z_odniesienia, r_max, x_max, &r, &x);

        /* Jednopikselowe drganie wykresu jest niewidoczne, za to powoduje miganie. */
        if (abs((int)multi_mini_r[line][idx] - (int)r) >= 2 ||
            abs((int)multi_mini_x[line][idx] - (int)x) >= 2)
            return 1U;
    }
    return 0U;
}

static void MultiSWR_ZapamietajMiniWykres(uint8_t line, float z_odniesienia,
                                          int r_max, int x_max)
{
    int idx;

    for (idx = 0; idx < 21; ++idx)
    {
        int8_t r = 0;
        int8_t x = 0;

        multi_mini_poprawne[line][idx] = multi_z200_poprawne[idx] ? 1U : 0U;
        if (multi_z200_poprawne[idx])
            MultiSWR_PrzeliczPunktMini(idx, z_odniesienia, r_max, x_max, &r, &x);
        multi_mini_r[line][idx] = r;
        multi_mini_x[line][idx] = x;
    }
    multi_mini_narysowany[line] = 1U;
}

static void MultiSWR_RysujPelnyMiniWykres(uint8_t line, float z_odniesienia,
                                           int r_max, int x_max)
{
    int idx;
    int poprzedni_r = 0;
    int poprzedni_x = 0;
    int poprzedni_indeks = -1;
    const uint32_t lineOffset = MULTISWR_LINEHEIGHT * MultiSWR_LokalnyWiersz(line);
    const LCDColor kolor_r = UI_KolorRamki(UI_STYL_AKCENT);
    const LCDColor kolor_x = UI_KolorRamki(UI_STYL_AKTYWNY);

    /*
     * Całe 21 punktów jest już zebrane w z200[]. Dawniej rysowaliśmy po jednym
     * odcinku w kolejnych obiegach pętli, co na fizycznym LCD wyglądało jak
     * miganie prawego pola. Teraz budujemy kompletny miniwykres jednym ciągiem.
     */
    for (idx = 0; idx < 21; ++idx)
    {
        int r;
        int x;

        if (!multi_z200_poprawne[idx])
        {
            poprzedni_indeks = -1;
            continue;
        }

        {
            int8_t r8;
            int8_t x8;
            MultiSWR_PrzeliczPunktMini(idx, z_odniesienia, r_max, x_max, &r8, &x8);
            r = (int)r8;
            x = (int)x8;
        }

        if (poprzedni_indeks >= 0)
        {
            const int x1 = MULTISWR_XX0 + poprzedni_indeks * 6;
            const int x2 = MULTISWR_XX0 + idx * 6;
            const int y0 = MULTISWR_YY0 + 10 + (int)lineOffset;

            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_r), LCD_MakePoint(x2, y0 - r), kolor_r);
            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_r - 1), LCD_MakePoint(x2, y0 - r - 1), kolor_r);
            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_r + 1), LCD_MakePoint(x2, y0 - r + 1), kolor_r);

            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_x), LCD_MakePoint(x2, y0 - x), kolor_x);
            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_x - 1), LCD_MakePoint(x2, y0 - x - 1), kolor_x);
            LCD_Line(LCD_MakePoint(x1, y0 - poprzedni_x + 1), LCD_MakePoint(x2, y0 - x + 1), kolor_x);
        }

        poprzedni_r = r;
        poprzedni_x = x;
        poprzedni_indeks = idx;
    }
}

int Scan200(uint8_t line, int index1)
{
    int touch;
    int32_t r;
    int32_t x;
    int8_t idx;
    char tmpBuff[24];
    uint32_t lineOffset;
    uint16_t y1;
    uint16_t y2;
    const float z_odniesienia = (float)CFG_GetParam(CFG_PARAM_R0);
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor kolor_r = UI_KolorRamki(UI_STYL_AKCENT);
    const LCDColor kolor_x = UI_KolorRamki(UI_STYL_AKTYWNY);

    if (line >= MULTISWR_WIERSZE || !MultiSWR_CzyWierszWidoczny(line) || multi_fr[line] == 0U)
        return -1;

    lineOffset = MULTISWR_LINEHEIGHT * MultiSWR_LokalnyWiersz(line);
    y1 = MultiSWR_GoraWiersza(line);
    y2 = MultiSWR_DolWiersza(line);

    if (index1 == 0)
    {
        rMax = 0;
        xMax = 0;
        memset(multi_z200_poprawne, 0, sizeof(multi_z200_poprawne));

        for (idx = 0; idx < 21; idx++)
        {
            const int64_t fq = (int64_t)multi_fr[line] * 1000LL +
                               (int64_t)(idx - 10) * (int64_t)multi_bw[line] * 50LL;
            touch = TouchTest();
            if (touch != -1)
                return touch;

            if (!MultiSWR_CzestotliwoscPoprawna(fq))
                continue;

            GEN_SetMeasurementFreq((uint32_t)fq);
            Sleep(2);
            if (!PANVSWR_PobierzS11((uint32_t)fq, (int)CFG_GetParam(CFG_PARAM_MEAS_NSCANS),
                                    1U, &z200[idx]) ||
                !MultiSWR_WynikPoprawny(z200[idx]))
                continue;

            multi_z200_poprawne[idx] = 1U;

            /* Skala R jest liczona wzgledem rzeczywistego Z0, nie stalego 50 ohm. */
            r = (int32_t)fabsf(crealf(z200[idx]) - z_odniesienia);
            if (rMax < r)
                rMax = r;

            x = (int32_t)fabsf(cimagf(z200[idx]));
            if (x > 1000)
                x = 1000;
            if (xMax < x)
                xMax = x;
        }

        if (rMax < 100)
            rMax = 100;
        if (xMax < 100)
            xMax = 100;

        /* R/X przygotowujemy jako tekst. LCD zmieniamy dopiero, gdy tekst się zmieni. */
        if (multi_z200_poprawne[10])
        {
            r = (int32_t)crealf(z200[10]);
            if (r > 999) r = 999;
            if (r < -999) r = -999;
            x = (int32_t)cimagf(z200[10]);
            if (x > 999) x = 999;
            if (x < -999) x = -999;

            snprintf(tmpBuff, sizeof(tmpBuff), "%ld ohm", (long)r);
            if (!multi_rx_narysowany[line] || strcmp(multi_poprzedni_r[line], tmpBuff) != 0)
            {
                LCD_FillRect(LCD_MakePoint(MULTISWR_XX0 + 158, y1 + 2U),
                             LCD_MakePoint(471, y1 + 18U), tlo);
                FONT_Write_RightAlign(FONT_FRAN, kolor_r, tlo, MULTISWR_XX0 + 158, y1 + 3U, 470, tmpBuff);
                strncpy(multi_poprzedni_r[line], tmpBuff, sizeof(multi_poprzedni_r[line]) - 1U);
                multi_poprzedni_r[line][sizeof(multi_poprzedni_r[line]) - 1U] = '\0';
            }

            snprintf(tmpBuff, sizeof(tmpBuff), "%+ld ohm", (long)x);
            if (!multi_rx_narysowany[line] || strcmp(multi_poprzedni_x[line], tmpBuff) != 0)
            {
                LCD_FillRect(LCD_MakePoint(MULTISWR_XX0 + 158, y1 + 20U),
                             LCD_MakePoint(471, y1 + 36U), tlo);
                FONT_Write_RightAlign(FONT_FRAN, kolor_x, tlo, MULTISWR_XX0 + 158, y1 + 21U, 470, tmpBuff);
                strncpy(multi_poprzedni_x[line], tmpBuff, sizeof(multi_poprzedni_x[line]) - 1U);
                multi_poprzedni_x[line][sizeof(multi_poprzedni_x[line]) - 1U] = '\0';
            }
        }
        else
        {
            multi_pomiar_poprawny[line] = 2U;
            if (!multi_rx_narysowany[line] || strcmp(multi_poprzedni_r[line], "--") != 0)
            {
                LCD_FillRect(LCD_MakePoint(MULTISWR_XX0 + 158, y1 + 2U),
                             LCD_MakePoint(471, y1 + 18U), tlo);
                FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
                                      MULTISWR_XX0 + 158, y1 + 3U, 470, "--");
                strcpy(multi_poprzedni_r[line], "--");
            }
            if (!multi_rx_narysowany[line] || strcmp(multi_poprzedni_x[line], "--") != 0)
            {
                LCD_FillRect(LCD_MakePoint(MULTISWR_XX0 + 158, y1 + 20U),
                             LCD_MakePoint(471, y1 + 36U), tlo);
                FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
                                      MULTISWR_XX0 + 158, y1 + 21U, 470, "--");
                strcpy(multi_poprzedni_x[line], "--");
            }
        }

        if (!multi_rx_narysowany[line])
        {
            /* Statyczne etykiety R/X powstają raz po wejściu lub zmianie ustawień wiersza. */
            FONT_Write(FONT_FRAN, kolor_r, tlo, MULTISWR_XX0 + 143, y1 + 3U, "R");
            FONT_Write(FONT_FRAN, kolor_x, tlo, MULTISWR_XX0 + 143, y1 + 21U, "X");
            multi_rx_narysowany[line] = 1U;
        }

        if (MultiSWR_CzyMiniWykresZmieniony(line, z_odniesienia, rMax, xMax))
        {
            /* Czyścimy wyłącznie pole miniwykresu i od razu rysujemy komplet 21 punktów. */
            LCD_FillRect(LCD_MakePoint(MULTISWR_XX0 - 5, y1 + 1U),
                         LCD_MakePoint(MULTISWR_XX0 + 135, y2 - 1U), tlo);
            LCD_Rectangle(LCD_MakePoint(MULTISWR_XX0 - 5, y1 + 1U),
                          LCD_MakePoint(MULTISWR_XX0 + 135, y2 - 1U),
                          UI_KolorRamki(UI_STYL_NORMALNY));
            LCD_HLine(LCD_MakePoint(MULTISWR_XX0 - 4, MULTISWR_YY0 + 10 + lineOffset),
                      138, UI_KolorRamki(UI_STYL_NIEAKTYWNY));
            LCD_VLine(LCD_MakePoint(MULTISWR_XX0 + 10 * 6, y1 + 2U),
                      (uint16_t)(y2 - y1 - 3U), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
            MultiSWR_RysujPelnyMiniWykres(line, z_odniesienia, rMax, xMax);
            MultiSWR_ZapamietajMiniWykres(line, z_odniesienia, rMax, xMax);
        }
        MultiSWR_RysujRamkeWiersza(line);
    }
    else
    {
        /* Punkty wykresu są rysowane od razu po zebraniu całego skanu. */
        touch = TouchTest();
        if (touch != -1)
            return touch;
    }
    return -1;
}

static uint32_t freqx; // kHz

int ShowFreq(int indx)
{
    uint32_t dp;
    uint32_t mhz;
    uint32_t bw1;
    uint16_t y1;
    uint16_t y2;
    char tmpBuff[24];
    char freqBuff[20];
    const char *pasmo;
    const char separator = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    const LCDColor tlo = UI_KolorTlaPola();
    const LCDColor tekst = UI_KolorTekstu(UI_STYL_NORMALNY);
    const LCDColor opis = UI_KolorRamki(UI_STYL_NORMALNY);

    if (indx < 0 || indx >= MULTISWR_WIERSZE || !MultiSWR_CzyWierszWidoczny((uint8_t)indx))
        return -1;

    freqx = multi_fr[indx];
    bw1 = multi_bw[indx];
    y1 = MultiSWR_GoraWiersza((uint8_t)indx);
    y2 = MultiSWR_DolWiersza((uint8_t)indx);

    LCD_FillRect(LCD_MakePoint(2, y1), LCD_MakePoint(478, y2), tlo);
    multi_mini_narysowany[indx] = 0U;
    multi_rx_narysowany[indx] = 0U;
    multi_poprzedni_r[indx][0] = '\0';
    multi_poprzedni_x[indx][0] = '\0';

    if (freqx == 0)
    {
        const char *pusty = JEZYK_Wybierz("Puste - dotknij, aby ustawić", "Empty - tap to set", "Leer - antippen zum Setzen", "Пусто - коснитесь для установки");
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 10, y1 + 11U, pusty);
        MultiSWR_RysujRamkeWiersza((uint8_t)indx);
        return -1;
    }

    dp = freqx % 1000U;
    mhz = freqx / 1000U;
    pasmo = MultiSWR_NazwaPasma(freqx);

    if (pasmo[0] != '\0')
        FONT_Write(FONT_FRAN, opis, tlo, 8, y1 + 3U, pasmo);

    snprintf(freqBuff, sizeof(freqBuff), "%lu%c%03lu", (unsigned long)mhz, separator, (unsigned long)dp);
    /* Dwie linie po 16 px mieszczą się w wierszu 39 px bez obcinania. */
    FONT_Write_RightAlign(FONT_FRAN, tekst, tlo, 44, y1 + 3U, 137, freqBuff);

    snprintf(tmpBuff, sizeof(tmpBuff), "+/-%lu kHz", (unsigned long)(bw1 / 2U));
    FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 44, y1 + 21U, 137, tmpBuff);

    LCD_VLine(LCD_MakePoint(142, y1 + 2U), (uint16_t)(y2 - y1 - 3U), UI_KolorRamki(UI_STYL_NIEAKTYWNY));
    FONT_Write(FONT_FRAN, opis, tlo, 151, y1 + 3U, "SWR");
    LCD_VLine(LCD_MakePoint(221, y1 + 2U), (uint16_t)(y2 - y1 - 3U), UI_KolorRamki(UI_STYL_NIEAKTYWNY));

    MultiSWR_RysujRamkeWiersza((uint8_t)indx);
    return indx;
}

void ShowResult(int indx)
{
    float VSWR;
    float complex z0;
    char wynik[10];
    uint16_t y1;
    UI_STYL_t styl;
    const LCDColor tlo = UI_KolorTlaPola();
    const uint32_t f_hz = (indx >= 0 && indx < MULTISWR_WIERSZE) ? multi_fr[indx] * 1000U : 0U;

    if (indx < 0 || indx >= MULTISWR_WIERSZE ||
        !MultiSWR_CzyWierszWidoczny((uint8_t)indx) || multi_fr[indx] == 0U)
        return;

    multi_pomiar_poprawny[indx] = 0U;

    if (!MultiSWR_CzestotliwoscPoprawna((int64_t)f_hz))
    {
        snprintf(wynik, sizeof(wynik), "--");
        styl = UI_STYL_NIEAKTYWNY;
        multi_pomiar_poprawny[indx] = 2U;
    }
    else
    {
        GEN_SetMeasurementFreq(f_hz);
        Sleep(10);
        if (!PANVSWR_PobierzS11(f_hz, (int)CFG_GetParam(CFG_PARAM_MEAS_NSCANS), 1U, &z0))
            z0 = NAN + NAN * I;

        if (!MultiSWR_WynikPoprawny(z0))
        {
            snprintf(wynik, sizeof(wynik), "--");
            styl = UI_STYL_OSTRZEZENIE;
            multi_pomiar_poprawny[indx] = 2U;
        }
        else
        {
            VSWR = DSP_CalcVSWR(z0);
            if (!isfinite(VSWR) || VSWR < 1.0f)
            {
                snprintf(wynik, sizeof(wynik), "--");
                styl = UI_STYL_OSTRZEZENIE;
                multi_pomiar_poprawny[indx] = 2U;
            }
            else
            {
                multi_pomiar_poprawny[indx] = 1U;
                if (VSWR > 99.0f)
                    snprintf(wynik, sizeof(wynik), ">99");
                else
                    snprintf(wynik, sizeof(wynik), "%.1f", (double)VSWR);

                if (VSWR <= 1.5f)
                    styl = UI_STYL_AKTYWNY;
                else if (VSWR <= 2.0f)
                    styl = UI_STYL_AKCENT;
                else if (VSWR <= 3.0f)
                    styl = UI_STYL_OSTRZEZENIE;
                else
                    styl = UI_STYL_POWROT;
            }
        }
    }

    if (multi_wynik_narysowany[indx] &&
        multi_poprzedni_styl[indx] == styl &&
        strcmp(multi_poprzedni_wynik[indx], wynik) == 0)
    {
        return;
    }

    y1 = MultiSWR_GoraWiersza((uint8_t)indx);
    LCD_FillRect(LCD_MakePoint(146, y1 + 19U), LCD_MakePoint(216, y1 + 36U), tlo);
    FONT_Write_RightAlign(FONT_FRAN, UI_KolorRamki(styl), tlo, 147, y1 + 20U, 216, wynik);
    MultiSWR_RysujRamkeWiersza((uint8_t)indx);

    strncpy(multi_poprzedni_wynik[indx], wynik, sizeof(multi_poprzedni_wynik[indx]) - 1U);
    multi_poprzedni_wynik[indx][sizeof(multi_poprzedni_wynik[indx]) - 1U] = '\0';
    multi_poprzedni_styl[indx] = styl;
    multi_wynik_narysowany[indx] = 1U;
}

void SetFreAndBandByAntennaIndex(uint32_t antIndex)
{
    if (antIndex > 4U)
        antIndex = 0U;

    if (antIndex == 0U)
    {
        multi_fr[0] = CFG_GetParam(CFG_PARAM_MULTI_F1);
        multi_fr[1] = CFG_GetParam(CFG_PARAM_MULTI_F2);
        multi_fr[2] = CFG_GetParam(CFG_PARAM_MULTI_F3);
        multi_fr[3] = CFG_GetParam(CFG_PARAM_MULTI_F4);
        multi_fr[4] = CFG_GetParam(CFG_PARAM_MULTI_F5);
        multi_bwNo[0] = CFG_GetParam(CFG_PARAM_MULTI_BW1);
        multi_bwNo[1] = CFG_GetParam(CFG_PARAM_MULTI_BW2);
        multi_bwNo[2] = CFG_GetParam(CFG_PARAM_MULTI_BW3);
        multi_bwNo[3] = CFG_GetParam(CFG_PARAM_MULTI_BW4);
        multi_bwNo[4] = CFG_GetParam(CFG_PARAM_MULTI_BW5);
    }
    else
    {
        uint32_t offset = (antIndex - 1U) * 10U;
        multi_fr[0] = CFG_GetParam(CFG_PARAM_MULTI_2F1 + offset);
        multi_fr[1] = CFG_GetParam(CFG_PARAM_MULTI_2F2 + offset);
        multi_fr[2] = CFG_GetParam(CFG_PARAM_MULTI_2F3 + offset);
        multi_fr[3] = CFG_GetParam(CFG_PARAM_MULTI_2F4 + offset);
        multi_fr[4] = CFG_GetParam(CFG_PARAM_MULTI_2F5 + offset);
        multi_bwNo[0] = CFG_GetParam(CFG_PARAM_MULTI_2BW1 + offset);
        multi_bwNo[1] = CFG_GetParam(CFG_PARAM_MULTI_2BW2 + offset);
        multi_bwNo[2] = CFG_GetParam(CFG_PARAM_MULTI_2BW3 + offset);
        multi_bwNo[3] = CFG_GetParam(CFG_PARAM_MULTI_2BW4 + offset);
        multi_bwNo[4] = CFG_GetParam(CFG_PARAM_MULTI_2BW5 + offset);
    }

    for (int indeks = 0; indeks < MULTISWR_WIERSZE; ++indeks)
    {
        multi_bwNo[indeks] = MultiSWR_NormalizujSpan((uint32_t)multi_bwNo[indeks]);
        multi_bw[indeks] = BSVALUES[multi_bwNo[indeks]];
        multi_pomiar_poprawny[indeks] = 0U;
    }

    MultiSWR_WyczyscPamiecWynikow();
    MultiSWR_RysujNaglowek(antIndex);
}

static void MultiSWR_PobierzKontrolki(UI_KONTROLKA_t kontrolki[msMenus_Length])
{
    const char *etykiety[msMenus_Length] = {
        JEZYK_Tekst(TEKST_WSTECZ),
        JEZYK_Wybierz("Zrzut", "Screen", "Bild", "Снимок"),
        JEZYK_Wybierz("Pamięć 1", "Mem. 1", "Sp. 1", "Пам. 1"),
        JEZYK_Wybierz("< Pam.", "< Mem.", "< Sp.", "< Пам."),
        JEZYK_Wybierz("Pam. >", "Mem. >", "Sp. >", "Пам. >"),
        multi_strona_wierszy == 0U ? "Pasma >" : "< Pasma"
    };
    uint8_t i;

    for (i = 0U; i < msMenus_Length; ++i)
    {
        const UI_PROSTOKAT_t o = UI_ObszarPrzyciskuDolnego(i);
        UI_STYL_t styl = i == MENU_EXIT ? UI_STYL_POWROT : UI_STYL_NORMALNY;
        const bool zaznaczona = i == MENU_ANT0 && CFG_GetParam(CFG_PARAM_MULTI_ANT) == 0U;
        kontrolki[i] = UI_UtworzKontrolke((int16_t)i, o.x, o.y, o.szerokosc, o.wysokosc,
                                           etykiety[i], styl, UI_ROLA_TEKSTU_PRZYCISK,
                                           true, zaznaczona);
    }
}

void DrawMultiSWRMenus()
{
    UI_KONTROLKA_t kontrolki[msMenus_Length];
    MultiSWR_PobierzKontrolki(kontrolki);
    UI_RysujKontrolki(kontrolki, msMenus_Length);
}

static void MultiSWR_RysujWidoczneWiersze(void)
{
    uint8_t indeks;
    const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
    const uint8_t koniec = (uint8_t)(pierwszy + MultiSWR_LiczbaWidocznychWierszy());

    LCD_FillRect(LCD_MakePoint(0U, 32U), LCD_MakePoint(479U, UI_DOLNY_PASEK_Y - 1U),
                 UI_KolorTlaEkranu());
    MultiSWR_RysujNaglowek(CFG_GetParam(CFG_PARAM_MULTI_ANT));
    for (indeks = pierwszy; indeks < koniec; ++indeks)
    {
        multi_wynik_narysowany[indeks] = 0U;
        multi_mini_narysowany[indeks] = 0U;
        multi_rx_narysowany[indeks] = 0U;
        ShowFreq(indeks);
    }
    DrawMultiSWRMenus();
}


static void save_MultiSwrSnapShot(void)
{
    char *fname = 0;
    Date_Time_Stamp();

    fname = SCREENSHOT_SelectFileName();
    if (strlen(fname) == 0)
        return;

    SCREENSHOT_DeleteOldest();
    if (CFG_GetParam(CFG_PARAM_SCREENSHOT_FORMAT))
        SCREENSHOT_SavePNG(fname);
    else
        SCREENSHOT_Save(fname);

    Sleep(500);
    DrawMultiSWRMenus();
}

static void MultiSWR_ZapiszUstawienieWiersza(uint32_t antIndex, int wiersz, int fx, BANDSPAN span_local)
{
    uint32_t baza_f;
    uint32_t baza_bw;

    if (wiersz < 0 || wiersz >= MULTISWR_WIERSZE)
        return;

    if (antIndex == 0U)
    {
        baza_f = CFG_PARAM_MULTI_F1;
        baza_bw = CFG_PARAM_MULTI_BW1;
    }
    else
    {
        uint32_t offset = (antIndex - 1U) * 10U;
        baza_f = CFG_PARAM_MULTI_2F1 + offset;
        baza_bw = CFG_PARAM_MULTI_2BW1 + offset;
    }

    CFG_SetParam(baza_f + (uint32_t)wiersz, (uint32_t)fx);
    CFG_SetParam(baza_bw + (uint32_t)wiersz, (uint32_t)span_local);
}

static void MultiSWR_ZmienWybranyWiersz(int kierunek)
{
    const uint8_t poprzedni = multi_wybrany_wiersz;
    const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
    const uint8_t liczba = MultiSWR_LiczbaWidocznychWierszy();
    uint8_t lokalny = MultiSWR_CzyWierszWidoczny(multi_wybrany_wiersz) ?
                      MultiSWR_LokalnyWiersz(multi_wybrany_wiersz) : 0U;

    if (kierunek > 0)
        lokalny = (uint8_t)((lokalny + 1U) % liczba);
    else
        lokalny = (uint8_t)((lokalny + liczba - 1U) % liczba);

    multi_wybrany_wiersz = (uint8_t)(pierwszy + lokalny);
    MultiSWR_RysujRamkeWiersza(poprzedni);
    MultiSWR_RysujRamkeWiersza(multi_wybrany_wiersz);
}

void MultiSWR_Proc(void)
{
    int touch;
    int fx;

    while (TOUCH_IsPressed())
        ;

    SetColours();
    BackGrColor = UI_KolorTlaEkranu();
    TextColor = UI_KolorTekstu(UI_STYL_NORMALNY);
    Color1 = UI_KolorRamki(UI_STYL_AKTYWNY);
    multi_strona_wierszy = 0U;
    multi_wybrany_wiersz = 0U;

    UI_WyczyscEkran();
    SetFreAndBandByAntennaIndex(CFG_GetParam(CFG_PARAM_MULTI_ANT));
    MultiSWR_RysujWidoczneWiersze();

    Sleep(300);
    WEJSCIA_WyczyscZdarzenia();

    for (;;)
    {
        const uint8_t pierwszy = MultiSWR_PierwszyWierszStrony();
        const uint8_t koniec = (uint8_t)(pierwszy + MultiSWR_LiczbaWidocznychWierszy());
        uint8_t przerysuj_strone = 0U;
        uint8_t j;

        for (j = pierwszy; j < koniec && !przerysuj_strone; ++j)
        {
            int i;
            ShowResult(j);

            for (i = 0; i < 21; ++i)
            {
                WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
                touch = -1;

                if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
                {
                    MultiSWR_WylaczRF();
                    CFG_Flush();
                    return;
                }
                if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
                    MultiSWR_ZmienWybranyWiersz(-1);
                else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
                    MultiSWR_ZmienWybranyWiersz(1);
                else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
                    touch = (int)multi_wybrany_wiersz;

                if (touch == -1)
                    touch = TouchTest();
                if (touch == -1)
                    touch = Scan200(j, i);

                if (TOUCH_Poll(&pt) && pt.y >= UI_DOLNY_PASEK_Y)
                {
                    UI_KONTROLKA_t kontrolki[msMenus_Length];
                    int touchIndex;
                    MultiSWR_PobierzKontrolki(kontrolki);
                    touchIndex = UI_ZnajdzKontrolke(pt, kontrolki, msMenus_Length);
                    if (touchIndex != -1)
                    {
                        TRACK_Beep(1);
                        while (TOUCH_IsPressed())
                            ;
                    }

                    if (touchIndex == MENU_EXIT)
                    {
                        MultiSWR_WylaczRF();
                        CFG_Flush();
                        return;
                    }
                    else if (touchIndex == MENU_SNAP)
                    {
                        MultiSWR_WylaczRF();
                        save_MultiSwrSnapShot();
                    }
                    else if (touchIndex == MENU_ANT0)
                    {
                        CFG_SetParam(CFG_PARAM_MULTI_ANT, 0U);
                        CFG_Flush();
                        SetFreAndBandByAntennaIndex(0U);
                        przerysuj_strone = 1U;
                    }
                    else if (touchIndex == MENU_PDOWN)
                    {
                        uint32_t tmpIndex = CFG_GetParam(CFG_PARAM_MULTI_ANT);
                        tmpIndex = (tmpIndex == 0U) ? 4U : tmpIndex - 1U;
                        CFG_SetParam(CFG_PARAM_MULTI_ANT, tmpIndex);
                        CFG_Flush();
                        SetFreAndBandByAntennaIndex(tmpIndex);
                        przerysuj_strone = 1U;
                    }
                    else if (touchIndex == MENU_PUP)
                    {
                        uint32_t tmpIndex = CFG_GetParam(CFG_PARAM_MULTI_ANT);
                        tmpIndex = (tmpIndex >= 4U) ? 0U : tmpIndex + 1U;
                        CFG_SetParam(CFG_PARAM_MULTI_ANT, tmpIndex);
                        CFG_Flush();
                        SetFreAndBandByAntennaIndex(tmpIndex);
                        przerysuj_strone = 1U;
                    }
                    else if (touchIndex == MENU_STRONA)
                    {
                        multi_strona_wierszy = (uint8_t)((multi_strona_wierszy + 1U) % MULTISWR_LICZBA_STRON);
                        multi_wybrany_wiersz = MultiSWR_PierwszyWierszStrony();
                        przerysuj_strone = 1U;
                    }

                    if (przerysuj_strone)
                    {
                        MultiSWR_WylaczRF();
                        MultiSWR_RysujWidoczneWiersze();
                        break;
                    }
                }

                if (touch >= 0 && touch < MULTISWR_WIERSZE &&
                    MultiSWR_CzyWierszWidoczny((uint8_t)touch))
                {
                    multi_wybrany_wiersz = (uint8_t)touch;
                    MultiSWR_WylaczRF();
                    span = MultiSWR_NormalizujSpan((uint32_t)multi_bwNo[touch]);
                    fx = (int)GetFrequency(multi_fr[touch] != 0U ? multi_fr[touch] : 14000U);
                    multi_fr[touch] = (uint32_t)fx;
                    multi_bwNo[touch] = MultiSWR_NormalizujSpan((uint32_t)span);
                    multi_bw[touch] = BSVALUES[multi_bwNo[touch]];

                    MultiSWR_ZapiszUstawienieWiersza(CFG_GetParam(CFG_PARAM_MULTI_ANT), touch, fx, span);
                    CFG_Flush();
                    multi_wynik_narysowany[touch] = 0U;
                    MultiSWR_RysujWidoczneWiersze();
                    break;
                }
            }
        }
    }
}
//================================================================================================
//END OF Multi SWR
//------------------------------------------------------------------------------------------------

typedef enum
{
    PAN_SWR_AKCJA_WSTECZ = 1,
    PAN_SWR_AKCJA_WYKRES,
    PAN_SWR_AKCJA_ANALIZA,
    PAN_SWR_AKCJA_NARZEDZIA,
    PAN_SWR_AKCJA_ZAKRES,
    PAN_SWR_AKCJA_POMIAR
} PAN_SWR_AKCJA_t;

/*
 * Narzędzia panoramy są zwykłym menu kafelkowym. Osiem operacji nie mieści
 * się czytelnie na jednej stronie, dlatego zamiast zmniejszać pola dzielimy
 * je na dwie strony po maksymalnie sześć standardowych kafli.
 */
#define PAN_SWR_NARZ_STRONA_PAMIEC      0U
#define PAN_SWR_NARZ_STRONA_DODATKOWE  1U
#define PAN_SWR_NARZ_PAMIEC_LICZBA     6U
#define PAN_SWR_NARZ_DODATKOWE_LICZBA  4U

static uint8_t pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_PAMIEC;
static uint8_t pan_swr_narzedzia_fokus = 0U;

static const char *PANVSWR_NazwaTypuWykresu(void)
{
    switch (grType)
    {
    case GRAPH_VSWR: return "SWR";
    case GRAPH_VSWR_Z: return "SWR + Z";
    case GRAPH_VSWR_RX: return "SWR / R / X";
    case GRAPH_RX: return "R / X";
    case GRAPH_S11: return "S11";
    case GRAPH_SMITH: return "Smith";
    case GRAPH_Smooth: return "SWR smooth";
    default: return "SWR";
    }
}

static void PANVSWR_RysujNaglowekStrony(const char *tytul)
{
    UI_RysujPasekGorny(tytul, false, false, 0);
}

static void PANVSWR_RysujPasekNawigacji(void)
{
    UI_AKCJA_t akcje[] = {
        {PAN_SWR_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false},
        {PAN_SWR_AKCJA_WYKRES, JEZYK_Tekst(TEKST_MENU_WYKRES_SWR), UI_STYL_NORMALNY, true,
         pan_swr_strona == PAN_SWR_STRONA_WYKRES},
        {PAN_SWR_AKCJA_ANALIZA, JEZYK_Tekst(TEKST_ANALIZA), UI_STYL_NORMALNY, true,
         pan_swr_strona == PAN_SWR_STRONA_WYNIK},
        {PAN_SWR_AKCJA_NARZEDZIA, JEZYK_Tekst(TEKST_DZIAL_NARZEDZIA), UI_STYL_NORMALNY, true,
         pan_swr_strona == PAN_SWR_STRONA_NARZEDZIA},
        {PAN_SWR_AKCJA_ZAKRES, JEZYK_Tekst(TEKST_ZAKRES), UI_STYL_NORMALNY, true, false},
        {PAN_SWR_AKCJA_POMIAR, JEZYK_Tekst(TEKST_SKANUJ), UI_STYL_AKCENT, true, false},
    };

    UI_RysujPasekAkcji(PAN_SWR_PASEK_Y, PAN_SWR_PASEK_H,
                        akcje, (uint8_t)(sizeof(akcje) / sizeof(akcje[0])));
}

static int16_t PANVSWR_ZnajdzAkcjeNawigacji(LCDPoint punkt)
{
    UI_AKCJA_t akcje[] = {
        {PAN_SWR_AKCJA_WSTECZ, "", UI_STYL_POWROT, true, false},
        {PAN_SWR_AKCJA_WYKRES, "", UI_STYL_NORMALNY, true, false},
        {PAN_SWR_AKCJA_ANALIZA, "", UI_STYL_NORMALNY, true, false},
        {PAN_SWR_AKCJA_NARZEDZIA, "", UI_STYL_NORMALNY, true, false},
        {PAN_SWR_AKCJA_ZAKRES, "", UI_STYL_NORMALNY, true, false},
        {PAN_SWR_AKCJA_POMIAR, "", UI_STYL_AKCENT, true, false},
    };

    return UI_ZnajdzAkcjePaska(punkt, PAN_SWR_PASEK_Y, PAN_SWR_PASEK_H,
                               akcje, (uint8_t)(sizeof(akcje) / sizeof(akcje[0])));
}

static void PANVSWR_UstawStrone(PAN_SWR_STRONA_t strona)
{
    if (strona > PAN_SWR_STRONA_NARZEDZIA)
        strona = PAN_SWR_STRONA_WYKRES;

    /* Automatyczny szybki skan ma sens tylko wtedy, gdy użytkownik widzi wykres. */
    if (strona != PAN_SWR_STRONA_WYKRES)
        autofast = 0U;

    pan_swr_strona = strona;
    ClearScreen = 1;
    redrawRequired = 1;
}

static void PANVSWR_RysujPomocStartowa(void)
{
    UI_RysujPanel(94, 82, 292, 78, JEZYK_Tekst(TEKST_MENU_WYKRES_SWR), UI_STYL_NIEAKTYWNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(),
               116, 108, JEZYK_Tekst(TEKST_SWR_POMOC_USTAW_ZAKRES));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               116, 136, JEZYK_Tekst(TEKST_WYKONAJ_POMIAR_NAJPIERW));
}

static void PANVSWR_RysujStroneWynik(void)
{
    ANALIZA_PRZEBIEGU_WYNIK_t analiza;
    uint32_t f_start_hz;
    uint32_t krok_hz;
    char tekst[112];
    char min_swr[20];

    UI_WyczyscEkran();
    PANVSWR_RysujNaglowekStrony(JEZYK_Tekst(TEKST_ANALIZA_ANTENY));

    if (!isMeasured)
    {
        UI_RysujPoleStatusu(24, 74, 432, 82, JEZYK_Tekst(TEKST_ANALIZA),
                            JEZYK_Tekst(TEKST_WYKONAJ_POMIAR_NAJPIERW), UI_STYL_OSTRZEZENIE);
        PANVSWR_RysujPasekNawigacji();
        return;
    }

    f_start_hz = PANVSWR_StartSkanuHz();
    krok_hz = (BSVALUES[span] * 1000U) / WWIDTH;
    analiza = ANALIZA_PRZEBIEGU_ObliczZMaska(values, pan_maska_poprawnosci, WWIDTH + 1U,
                                             f_start_hz, krok_hz,
                                             (float)CFG_GetParam(CFG_PARAM_R0));

    if (!analiza.poprawna)
    {
        UI_RysujPoleStatusu(24, 74, 432, 82, JEZYK_Tekst(TEKST_ANALIZA),
                            JEZYK_Tekst(TEKST_WYKONAJ_POMIAR_NAJPIERW), UI_STYL_OSTRZEZENIE);
        PANVSWR_RysujPasekNawigacji();
        return;
    }

    snprintf(min_swr, sizeof(min_swr), "%.2f", analiza.min_swr);
    UI_RysujPoleLiczboweGlowne(12, 40, 142, 76, JEZYK_Tekst(TEKST_MINIMUM_SWR), min_swr, "");

    snprintf(tekst, sizeof(tekst), "%.6f MHz\nR %.1f Ohm   X %+.1f Ohm",
             (double)analiza.f_min_swr_hz / 1000000.0, analiza.r_min_ohm, analiza.x_min_ohm);
    UI_RysujPoleStatusu(162, 40, 306, 76, JEZYK_Tekst(TEKST_CZESTOTLIWOSC), tekst, UI_STYL_AKTYWNY);

    if (analiza.rezonans_znaleziony)
    {
        snprintf(tekst, sizeof(tekst), "%.6f MHz   R %.1f Ohm",
                 (double)analiza.f_rezonans_hz / 1000000.0, analiza.r_rezonans_ohm);
        UI_RysujPoleStatusu(12, 122, 456, 42, JEZYK_Tekst(TEKST_REZONANS_X0), tekst, UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(12, 122, 456, 42, JEZYK_Tekst(TEKST_REZONANS_X0),
                            JEZYK_Tekst(TEKST_REZONANS_POZA_ZAKRESEM), UI_STYL_NIEAKTYWNY);
    }

    if (analiza.pasmo_swr_15_znalezione)
    {
        snprintf(tekst, sizeof(tekst), "%.6f - %.6f MHz\n%lu kHz",
                 (double)analiza.f_swr_15_dol_hz / 1000000.0,
                 (double)analiza.f_swr_15_gora_hz / 1000000.0,
                 (unsigned long)((analiza.f_swr_15_gora_hz - analiza.f_swr_15_dol_hz) / 1000U));
        UI_RysujPoleStatusu(12, 170, 222, 54, JEZYK_Tekst(TEKST_PASMO_SWR_15), tekst, UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(12, 170, 222, 54, JEZYK_Tekst(TEKST_PASMO_SWR_15),
                            JEZYK_Tekst(TEKST_PASMO_POZA_ZAKRESEM), UI_STYL_NIEAKTYWNY);
    }

    if (analiza.pasmo_swr_20_znalezione)
    {
        snprintf(tekst, sizeof(tekst), "%.6f - %.6f MHz\n%lu kHz",
                 (double)analiza.f_swr_20_dol_hz / 1000000.0,
                 (double)analiza.f_swr_20_gora_hz / 1000000.0,
                 (unsigned long)((analiza.f_swr_20_gora_hz - analiza.f_swr_20_dol_hz) / 1000U));
        UI_RysujPoleStatusu(246, 170, 222, 54, JEZYK_Tekst(TEKST_PASMO_SWR_20), tekst, UI_STYL_AKTYWNY);
    }
    else
    {
        UI_RysujPoleStatusu(246, 170, 222, 54, JEZYK_Tekst(TEKST_PASMO_SWR_20),
                            JEZYK_Tekst(TEKST_PASMO_POZA_ZAKRESEM), UI_STYL_NIEAKTYWNY);
    }

    PANVSWR_RysujPasekNawigacji();
}

static uint8_t PANVSWR_LiczbaNarzedziNaStronie(void)
{
    return pan_swr_narzedzia_podstrona == PAN_SWR_NARZ_STRONA_DODATKOWE
        ? PAN_SWR_NARZ_DODATKOWE_LICZBA
        : PAN_SWR_NARZ_PAMIEC_LICZBA;
}

static void PANVSWR_RysujStroneNarzedzia(void)
{
    char pamiec[48];
    char m1[32];
    char m2[32];
    char m3[32];
    char widok[48];

    UI_WyczyscEkran();
    PANVSWR_RysujNaglowekStrony(JEZYK_Tekst(TEKST_DZIAL_NARZEDZIA));

    if (pan_swr_narzedzia_podstrona == PAN_SWR_NARZ_STRONA_DODATKOWE)
    {
        UI_RysujKafelKompaktowy(0U, UI_IKONA_SMITH, "Smith",
                                pan_swr_narzedzia_fokus == 0U, true);
        UI_RysujKafelKompaktowy(1U, UI_IKONA_ZRZUTY, JEZYK_Tekst(TEKST_ZAPISZ_ZRZUT),
                                pan_swr_narzedzia_fokus == 1U, isMeasured != 0);
        UI_RysujKafelKompaktowy(2U, UI_IKONA_STROJENIE, JEZYK_Tekst(TEKST_AUTO_SZYBKIE),
                                pan_swr_narzedzia_fokus == 2U, true);
        UI_RysujKafelKompaktowy(3U, UI_IKONA_ZAPISANE,
                                SMITH_T("Pamięć / wykres", "Memory / graph",
                                        "Speicher / Diagramm", "Память / график"),
                                pan_swr_narzedzia_fokus == 3U, true);
    }
    else
    {
        snprintf(pamiec, sizeof(pamiec), "%s: %s",
                 JEZYK_Tekst(TEKST_PAMIEC),
                 memMode == MEMMODE_LOAD ? JEZYK_Tekst(TEKST_WCZYTAJ) : JEZYK_Tekst(TEKST_ZAPISZ));
        snprintf(m1, sizeof(m1), "M1\n%s", isM1Loaded ? "OK" : "--");
        snprintf(m2, sizeof(m2), "M2\n%s", isM2Loaded ? "OK" : "--");
        snprintf(m3, sizeof(m3), "M3\n%s", isM3Loaded ? "OK" : "--");
        snprintf(widok, sizeof(widok), "%s: %s",
                 JEZYK_Tekst(TEKST_TYP_WYKRESU), PANVSWR_NazwaTypuWykresu());

        UI_RysujKafelKompaktowy(0U, UI_IKONA_ZAPISANE, pamiec,
                                pan_swr_narzedzia_fokus == 0U, true);
        UI_RysujKafelKompaktowy(1U, UI_IKONA_ZAPISANE, m1,
                                pan_swr_narzedzia_fokus == 1U, true);
        UI_RysujKafelKompaktowy(2U, UI_IKONA_ZAPISANE, m2,
                                pan_swr_narzedzia_fokus == 2U, true);
        UI_RysujKafelKompaktowy(3U, UI_IKONA_ZAPISANE, m3,
                                pan_swr_narzedzia_fokus == 3U, true);
        UI_RysujKafelKompaktowy(4U, UI_IKONA_WYKRES_SWR, widok,
                                pan_swr_narzedzia_fokus == 4U, true);
        UI_RysujKafelKompaktowy(5U, UI_IKONA_DZIAL_NARZEDZIA,
                                SMITH_T("Więcej", "More", "Mehr", "Ещё"),
                                pan_swr_narzedzia_fokus == 5U, true);
    }

    PANVSWR_RysujPasekNawigacji();
}

static void PANVSWR_PokazAnalize(void)
{
    PANVSWR_UstawStrone(PAN_SWR_STRONA_WYNIK);
}

static void PANVSWR_DokumentacjaPrzygotujDane(void)
{
    uint32_t i;

    /*
     * Generator instrukcji ma tworzyć powtarzalny, czytelny przykład bez
     * włączania toru RF. Impedancja przechodzi przez 50+j0 w środku pasma,
     * więc na wszystkich trzech stronach widać sensowne dane demonstracyjne.
     * Po narysowaniu strony flaga isMeasured jest zerowana, więc te dane nie
     * mogą zostać omyłkowo potraktowane jako prawdziwy pomiar użytkownika.
     */
    f1 = 7100000U;
    span = BS400;
    grType = GRAPH_VSWR_RX;
    cursorPos = WWIDTH / 2U;
    ManualCursor = 1;
    cursorVisible = 1;
    autofast = 0U;
    memMode = MEMMODE_LOAD;
    isM1Loaded = 1U;
    isM2Loaded = 0U;
    isM3Loaded = 0U;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        const float t = ((float)i - (float)(WWIDTH / 2U)) / (float)(WWIDTH / 2U);
        const float r = 50.0f + 14.0f * t * t;
        const float x = 55.0f * t;
        values[i] = r + x * I;
        pan_maska_poprawnosci[i] = 1U;
    }
    pan_punkty_poprawne = WWIDTH + 1U;
    pan_punkty_odrzucone = 0U;
    pan_skan_przerwany = 0U;
    isMeasured = 1;
}

uint32_t PANVSWR_DokumentacjaLiczbaStron(void)
{
    return 4U;
}

const char *PANVSWR_DokumentacjaNazwaStrony(uint32_t strona)
{
    static const char *const nazwy[] =
    {
        "swr_wykres",
        "swr_analiza",
        "swr_narzedzia_pamiec",
        "swr_narzedzia_dodatkowe"
    };
    return strona < 4U ? nazwy[strona] : "swr";
}

void PANVSWR_DokumentacjaRysujStrone(uint32_t strona)
{
    PANVSWR_DokumentacjaPrzygotujDane();
    UI_WyczyscEkran();

    switch (strona)
    {
    case 0U:
        pan_swr_strona = PAN_SWR_STRONA_WYKRES;
        ClearScreen = 1;
        RedrawWindow();
        break;
    case 1U:
        pan_swr_strona = PAN_SWR_STRONA_WYNIK;
        PANVSWR_RysujStroneWynik();
        break;
    case 2U:
        pan_swr_strona = PAN_SWR_STRONA_NARZEDZIA;
        pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_PAMIEC;
        pan_swr_narzedzia_fokus = 0U;
        PANVSWR_RysujStroneNarzedzia();
        break;
    case 3U:
        pan_swr_strona = PAN_SWR_STRONA_NARZEDZIA;
        pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_DODATKOWE;
        pan_swr_narzedzia_fokus = 0U;
        PANVSWR_RysujStroneNarzedzia();
        break;
    default:
        pan_swr_strona = PAN_SWR_STRONA_WYKRES;
        ClearScreen = 1;
        RedrawWindow();
        break;
    }

    /* Ekran pozostaje narysowany do wykonania BMP, ale logika programu nie
     * zachowuje danych demonstracyjnych jako aktywnego pomiaru. */
    isMeasured = 0;
    pan_punkty_poprawne = 0U;
    pan_punkty_odrzucone = 0U;
}

/* ========================================================================
 * Dokumentacja z rzeczywistym skanem
 * ======================================================================== */
static uint32_t pan_dok_min_index = 0U;
static uint32_t pan_dok_start_hz = 0U;

static uint32_t PANVSWR_DokumentacjaRealnaZnajdzMinimum(void)
{
    uint32_t i;
    uint32_t najlepszy = 0U;
    float najlepszy_swr = 1.0e30f;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        float swr;
        if (!pan_maska_poprawnosci[i])
            continue;
        swr = DSP_CalcVSWR(values[i]);
        if (!isfinite(swr) || swr < 1.0f)
            continue;
        if (swr < najlepszy_swr)
        {
            najlepszy_swr = swr;
            najlepszy = i;
        }
    }
    return najlepszy;
}

uint8_t PANVSWR_DokumentacjaRealnaSkanuj(uint32_t start_hz, BANDSPAN zakres)
{
    if ((uint32_t)zakres >= (uint32_t)(sizeof(BSVALUES) / sizeof(BSVALUES[0])))
        return 0U;

    /*
     * Tryb start+span jest jednoznaczny i pozwala zapisać w manifeście
     * dokładne granice sesji. CFG_SetParam zmienia tylko roboczy stan RAM;
     * generator nadrzędny przywraca wszystkie wartości po zakończeniu.
     */
    CFG_SetParam(CFG_PARAM_PAN_CENTER_F, 0U);
    CFG_SetParam(CFG_PARAM_PAN_F1, start_hz);
    CFG_SetParam(CFG_PARAM_PAN_SPAN, (uint32_t)zakres);

    f1 = start_hz;
    span = zakres;
    autofast = 0U;
    ManualCursor = 1;
    cursorVisible = 1;
    pan_swr_strona = PAN_SWR_STRONA_WYKRES;
    grType = GRAPH_VSWR;
    ClearScreen = 1;

    ScanRX();
    if (!isMeasured || pan_punkty_poprawne == 0U)
        return 0U;

    pan_dok_start_hz = PANVSWR_StartSkanuHz();
    pan_dok_min_index = PANVSWR_DokumentacjaRealnaZnajdzMinimum();
    cursorPos = pan_dok_min_index;
    return pan_maska_poprawnosci[pan_dok_min_index] ? 1U : 0U;
}

uint32_t PANVSWR_DokumentacjaRealnaMinimumHz(void)
{
    const uint32_t krok_hz = (BSVALUES[span] * 1000U) / WWIDTH;
    return pan_dok_start_hz + pan_dok_min_index * krok_hz;
}

uint32_t PANVSWR_DokumentacjaRealnaMarkerHz(void)
{
    const uint32_t krok_hz = (BSVALUES[span] * 1000U) / WWIDTH;
    return pan_dok_start_hz + cursorPos * krok_hz;
}

static uint32_t PANVSWR_DokumentacjaRealnaIndeksMarkera(PAN_DOK_WIDOK_t widok)
{
    const uint32_t przesuniecie = WWIDTH / 5U;

    if (widok == PAN_DOK_WIDOK_SWR_LEWO)
        return pan_dok_min_index > przesuniecie ? pan_dok_min_index - przesuniecie : 0U;
    if (widok == PAN_DOK_WIDOK_SWR_PRAWO)
    {
        const uint32_t kandydat = pan_dok_min_index + przesuniecie;
        return kandydat <= WWIDTH ? kandydat : WWIDTH;
    }
    return pan_dok_min_index;
}

void PANVSWR_DokumentacjaRealnaRysuj(PAN_DOK_WIDOK_t widok)
{
    if (!isMeasured)
        return;

    cursorPos = PANVSWR_DokumentacjaRealnaIndeksMarkera(widok);
    ManualCursor = 1;
    cursorVisible = 1;
    autofast = 0U;

    switch (widok)
    {
    case PAN_DOK_WIDOK_ANALIZA:
        pan_swr_strona = PAN_SWR_STRONA_WYNIK;
        PANVSWR_RysujStroneWynik();
        break;
    case PAN_DOK_WIDOK_RX:
        pan_swr_strona = PAN_SWR_STRONA_WYKRES;
        grType = GRAPH_RX;
        ClearScreen = 1;
        RedrawWindow();
        break;
    case PAN_DOK_WIDOK_SMITH:
        cursorPos = pan_dok_min_index;
        smith_pelny_ekran = 1U;
        SMITH_PelnyRysuj();
        smith_pelny_ekran = 0U;
        break;
    case PAN_DOK_WIDOK_SWR_LEWO:
    case PAN_DOK_WIDOK_SWR_PRAWO:
    case PAN_DOK_WIDOK_SWR_MIN:
    default:
        pan_swr_strona = PAN_SWR_STRONA_WYKRES;
        grType = GRAPH_VSWR;
        ClearScreen = 1;
        RedrawWindow();
        break;
    }
}

void DrawLoadStoreStatus(void);

void DrawFootText(void)
{
    PANVSWR_RysujPasekNawigacji();

    /*
     * Ręczne przesuwanie kursora pozostaje bezpośrednio przy wykresie.
     * Pozostałe funkcje zostały przeniesione na czytelne strony i nie
     * zabierają już miejsca osiom oraz danym pomiarowym.
     */
    if (pan_swr_strona == PAN_SWR_STRONA_WYKRES)
    {
        UI_RysujPrzycisk(4, 94, 40, 34, "<", UI_STYL_NORMALNY, FONT_FRANBIG);
        UI_RysujPrzycisk(4, 134, 40, 34, ">", UI_STYL_NORMALNY, FONT_FRANBIG);
    }
}

void ZoomMinus(void)
{
    if (span > 0)
    {
        span--;
        ScanRXFast();
        redrawRequired = 1;
    }
}
void ZoomPlus(void)
{
    if (span < BS1000M) // DL8MBY
    {
        span++;
        ScanRXFast();
        redrawRequired = 1;
    }
}

void SWR_Exit0(void) //                     Button 0
{
    rqExitSWR = true; // exit program
}

void Switch_Menu(void)
{
    /* Zachowana nazwa dla zgodności wewnętrznej; nowy interfejs ma 3 strony. */
    PANVSWR_UstawStrone(PAN_SWR_STRONA_NARZEDZIA);
}

void Store(void)
{
    autofast = 0U;

    if (memMode == MEMMODE_LOAD)
    {
        /* Tryb zapisu ma sens dopiero po wykonaniu pomiaru. */
        if (isMeasured)
        {
            Saving = 1U;
            isStored = 1;
            memMode = MEMMODE_STORE;
        }
    }
    else
    {
        memMode = MEMMODE_LOAD;
    }

    redrawRequired = 1;
}

void DiagType(void) //                      Button3 (central)
{
    autofast = 0;

    /*
     * Smith nie jest już jednym z małych typów wykresu panoramy. Ma osobny
     * pełnoekranowy widok, więc cykl obejmuje wyłącznie wykresy, które można
     * czytelnie rysować w obszarze roboczym panoramy.
     */
    if (grType == GRAPH_VSWR)
        grType = GRAPH_VSWR_Z;
    else if (grType == GRAPH_VSWR_Z)
        grType = GRAPH_VSWR_RX;
    else if (grType == GRAPH_VSWR_RX)
        grType = GRAPH_RX;
    else if ((grType == GRAPH_RX) && (CFG_GetParam(CFG_PARAM_S11_SHOW) == 1))
        grType = GRAPH_S11;
    else
        grType = GRAPH_VSWR;


    ClearScreen = 1;
    redrawRequired = 1;
}

void Save_Snap(void) // Save Snapshot         Button4
{
    autofast = 0;
    save_snapshot();
}

void Auto_Fast(void)
{
    if (autofast == 0U)
    {
        autofast = 1U;
        holdScale = 0;
    }
    else
    {
        autofast = 0U;
    }
    redrawRequired = 1;
}

//Use by Frequency Sweep Measurement
void Frequency(void)
{
    FreqkHz = f1 / 1000;
    if (PanFreqWindow(&FreqkHz, &span))
    {
        //Span or frequency has been changed
        f1 = 1000 * FreqkHz;
        isMeasured = 0;
    }
    redrawRequired = 1;
}

void Scan(void)
{
    PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);

    if (autofast == 0U)
    {
        uint32_t poprawny;
        FONT_Write(FONT_FRANBIG, LCD_RED, BackGrColor, 165, 104, JEZYK_Tekst(TEKST_TRWA_POMIAR));
        ScanRX();
        if (isMeasured && !PANVSWR_CzyPunktPoprawny(cursorPos) &&
            PANVSWR_ZnajdzNajblizszyPoprawny(cursorPos, &poprawny))
            cursorPos = poprawny;
    }
    else
    {
        autofast = 0U;
    }

    redrawRequired = 1;
}

static void SMITH_PelnyEkran(void)
{
    uint8_t koniec = 0U;

    /* Pełny Smith jest niezależnym widokiem i nie zmienia typu wykresu panoramy. */
    autofast = 0;
    smith_pelny_ekran = 1U;
    while (TOUCH_IsPressed())
        Sleep(10);

    SMITH_PelnyRysuj();
    LCD_ShowActiveLayerOnly();

    while (!koniec)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie;

        Sleep(10);
        zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            koniec = 1U;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        {
            Scan();
            SMITH_PelnyRysuj();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK)
        {
            SMITH_PelnyAnalizaEkran();
            SMITH_PelnyRysuj();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO)
        {
            uint32_t nowy;
            if (PANVSWR_ZnajdzPoprawnyWKierunku(cursorPos, -1, &nowy))
            {
                cursorPos = nowy;
                ManualCursor = 1;
                SMITH_PelnyRysuj();
            }
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO)
        {
            uint32_t nowy;
            if (PANVSWR_ZnajdzPoprawnyWKierunku(cursorPos, 1, &nowy))
            {
                cursorPos = nowy;
                ManualCursor = 1;
                SMITH_PelnyRysuj();
            }
        }

        if (TOUCH_Poll(&pt))
        {
            UI_AKCJA_t akcje[4];
            int16_t akcja;
            SMITH_PelnyPobierzAkcje(akcje);
            akcja = UI_ZnajdzAkcjePaska(pt, SMITH_PELNY_PRZYCISK_Y,
                                        SMITH_PELNY_PRZYCISK_H, akcje, 4U);
            if (akcja == SMITH_AKCJA_WSTECZ)
            {
                TOUCH_CzekajNaPuszczenie(25U);
                koniec = 1U;
            }
            else if (akcja == SMITH_AKCJA_SKANUJ)
            {
                TOUCH_CzekajNaPuszczenie(25U);
                Scan();
                SMITH_PelnyRysuj();
            }
            else if (akcja == SMITH_AKCJA_ANALIZA)
            {
                TOUCH_CzekajNaPuszczenie(25U);
                SMITH_PelnyAnalizaEkran();
                SMITH_PelnyRysuj();
            }
            else if (akcja == SMITH_AKCJA_ZRZUT)
            {
                TOUCH_CzekajNaPuszczenie(25U);
                save_snapshot();
                SMITH_PelnyRysuj();
            }
            else
            {
                const int32_t dx = (int32_t)pt.x - SMITH_PELNY_CX;
                const int32_t dy = (int32_t)pt.y - SMITH_PELNY_CY;
                const int32_t r2 = SMITH_PELNY_PROMIEN * SMITH_PELNY_PROMIEN;

                if ((dx * dx + dy * dy) <= r2)
                {
                    SMITH_PelnyUstawKursorDotykiem(&pt);
                    TOUCH_CzekajNaPuszczenie(20U);
                    SMITH_PelnyRysuj();
                }
            }
        }
    }

    smith_pelny_ekran = 0U;
    while (TOUCH_IsPressed())
        Sleep(10);
    redrawRequired = 1;
}

//Save Measure data to uSD Card by KD8CEC
static void StoreRXP(uint8_t memoryIndex, float complex *loadMemSpace)
{
    FRESULT res;
    FIL fp = {0};
    TCHAR path[64];
    uint32_t tmpHeader[100] = {0}; //TempHeader
    uint8_t plik_otwarty = 0;

    if (!CFG_CzyKartaSDDostepna())
    {
        KOMUNIKAT_Pokaz(TEKST_OSTRZEZENIE, TEKST_BRAK_KARTY_SD);
        return;
    }

    sprintf(path, "%s/rx%03d.yhw", g_aa_dir, memoryIndex);
    f_mkdir(g_aa_dir);
    res = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (FR_OK != res)
        goto BLAD_ZAPISU;
    plik_otwarty = 1;

    tmpHeader[0] = 57; //magic
    tmpHeader[1] = modstrw;
    tmpHeader[2] = f1;
    tmpHeader[3] = span;

    RXPHeader[memoryIndex][0] = tmpHeader[1];
    RXPHeader[memoryIndex][1] = tmpHeader[2];
    RXPHeader[memoryIndex][2] = tmpHeader[3];

    UINT bw = 0;
    uint32_t loadLength = sizeof(uint32_t) * RX_FILE_HEADER_LEN;
    res = f_write(&fp, tmpHeader, loadLength, &bw);
    if (FR_OK != res || bw != loadLength)
        goto BLAD_ZAPISU;

    loadLength = sizeof(float complex) * (WWIDTH + 1);
    bw = 0;
    res = f_write(&fp, loadMemSpace, loadLength, &bw);
    if (FR_OK != res || bw != loadLength)
        goto BLAD_ZAPISU;

    f_close(&fp);
    return;

BLAD_ZAPISU:
    if (plik_otwarty)
        f_close(&fp);
    f_unlink(path);
    KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_BLAD_ZAPISU_PLIKU);
}

//Load Stored File
uint8_t LoadRXP(uint8_t memoryIndex, float complex *loadMemSpace)
{
    FRESULT res;
    FIL fp;
    TCHAR path[64];
    uint32_t tmpHeader[100] = {0}; //TempHeader

    sprintf(path, "%s/rx%03d.yhw", g_aa_dir, memoryIndex);
    res = f_open(&fp, path, FA_READ | FA_OPEN_EXISTING);

    if (FR_OK != res)
    {
        return 1;
    }

    //Read Header
    UINT br = 0;
    uint32_t loadLength = sizeof(uint32_t) * (RX_FILE_HEADER_LEN);
    res = f_read(&fp, tmpHeader, loadLength, &br);

    if (FR_OK != res || (loadLength != br))
    {
        f_close(&fp);
        return 2;
    }

    //Read Data
    loadLength = sizeof(float complex) * (WWIDTH + 1);
    res = f_read(&fp, loadMemSpace, loadLength, &br);
    f_close(&fp);

    if (FR_OK != res || (loadLength != br))
    {
        return 2;
    }

    /*
    modstrw = tmpHeader[1];
    f1 = tmpHeader[2];
    span = tmpHeader[3];
    */
    //memoryIndex = 1 ~ 10; 0 is current if need
    RXPHeader[memoryIndex][0] = tmpHeader[1];
    RXPHeader[memoryIndex][1] = tmpHeader[2];
    RXPHeader[memoryIndex][2] = tmpHeader[3];

    return 0;
}

void Mem1(void) //                                  M1
{
    if (memMode == MEMMODE_LOAD && isM1Loaded)
    {
        //Load Data
        memcpy(values, SavedValues1, (WWIDTH + 1) * 8);

        modstrw = RXPHeader[1][0];
        f1 = RXPHeader[1][1];
        span = RXPHeader[1][2];
        PANVSWR_OdtworzMaskeZValues();
        isMeasured = pan_punkty_poprawne > 0U;
        redrawRequired = 1;
    }

    if (memMode == MEMMODE_STORE && isMeasured)
    {
        memcpy(SavedValues1, values, (WWIDTH + 1) * 8);
        LCD_Rectangle(LCD_MakePoint(454, 100), LCD_MakePoint(479, 125), 0xffff0000); //  Red
        StoreRXP(1, SavedValues1);
        isM1Loaded = 1;
    }

    while (TOUCH_IsPressed())
        ;
    DrawLoadStoreStatus();
}

void Mem2(void) //                                  M2
{
    if (memMode == MEMMODE_LOAD && isM2Loaded)
    {
        //Load Data
        memcpy(values, SavedValues2, (WWIDTH + 1) * 8);

        modstrw = RXPHeader[2][0];
        f1 = RXPHeader[2][1];
        span = RXPHeader[2][2];
        PANVSWR_OdtworzMaskeZValues();
        isMeasured = pan_punkty_poprawne > 0U;
        redrawRequired = 1;
    }

    if (memMode == MEMMODE_STORE && isMeasured)
    {
        memcpy(SavedValues2, values, (WWIDTH + 1) * 8);
        LCD_Rectangle(LCD_MakePoint(454, 100), LCD_MakePoint(479, 125), 0xffff0000); //  Red
        StoreRXP(2, SavedValues2);
        isM2Loaded = 1;
    }
    while (TOUCH_IsPressed())
        ;
    DrawLoadStoreStatus();
}

void Mem3(void) //                                      M3
{
    if (memMode == MEMMODE_LOAD && isM3Loaded)
    {
        //Load Data
        memcpy(values, SavedValues3, (WWIDTH + 1) * 8);

        modstrw = RXPHeader[3][0];
        f1 = RXPHeader[3][1];
        span = RXPHeader[3][2];
        PANVSWR_OdtworzMaskeZValues();
        isMeasured = pan_punkty_poprawne > 0U;
        redrawRequired = 1;
    }

    if (memMode == MEMMODE_STORE && isMeasured)
    {
        memcpy(SavedValues3, values, (WWIDTH + 1) * 8);
        LCD_Rectangle(LCD_MakePoint(454, 100), LCD_MakePoint(479, 125), 0xffff0000); //  Red
        StoreRXP(3, SavedValues3);
        isM3Loaded = 1;
    }

    while (TOUCH_IsPressed())
        ;
    DrawLoadStoreStatus();
}

//Stored and Load using SD-Card by KD8CEC
//ianlee
void DrawLoadStoreStatus()
{
    /*
     * Stan pamięci jest teraz częścią strony Narzędzia. Nie rysujemy już
     * trzech małych kontrolek na krawędzi wykresu; po zmianie stanu
     * odświeżamy całą stronę wspólnym mechanizmem.
     */
    redrawRequired = 1;
}

static int Beeper = 0;

void OneBeep(int repeatNumber)
{ // one or (repeatNumber) of beeps
    int counter = repeatNumber;
    if ((BeepOn1 == 1) && (Beeper == 1))
    {
        while (--counter >= 0)
        {
            UB_TIMER2_Init_FRQ(880);
            UB_TIMER2_Start();
            Sleep(100);
            UB_TIMER2_Stop();
        }
    }
}

static bool PANVSWR_WykonajNarzedzie(uint8_t indeks)
{
    if (pan_swr_narzedzia_podstrona == PAN_SWR_NARZ_STRONA_DODATKOWE)
    {
        switch (indeks)
        {
        case 0U:
            SMITH_PelnyEkran();
            redrawRequired = 1;
            return true;

        case 1U:
            if (isMeasured)
            {
                PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
                RedrawWindow();
                LCD_ShowActiveLayerOnly();
                Save_Snap();
            }
            return true;

        case 2U:
            PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
            Auto_Fast();
            return true;

        case 3U:
            pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_PAMIEC;
            pan_swr_narzedzia_fokus = 0U;
            redrawRequired = 1;
            return true;

        default:
            return false;
        }
    }

    switch (indeks)
    {
    case 0U:
        Store();
        return true;

    case 1U:
    {
        const uint8_t byl_tryb_wczytaj = (uint8_t)(memMode == MEMMODE_LOAD);
        Mem1();
        if (byl_tryb_wczytaj && isM1Loaded)
            PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
        return true;
    }

    case 2U:
    {
        const uint8_t byl_tryb_wczytaj = (uint8_t)(memMode == MEMMODE_LOAD);
        Mem2();
        if (byl_tryb_wczytaj && isM2Loaded)
            PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
        return true;
    }

    case 3U:
    {
        const uint8_t byl_tryb_wczytaj = (uint8_t)(memMode == MEMMODE_LOAD);
        Mem3();
        if (byl_tryb_wczytaj && isM3Loaded)
            PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
        return true;
    }

    case 4U:
        DiagType();
        return true;

    case 5U:
        pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_DODATKOWE;
        pan_swr_narzedzia_fokus = 0U;
        redrawRequired = 1;
        return true;

    default:
        return false;
    }
}

static bool PANVSWR_ObsluzNawigacje(LCDPoint punkt)
{
    int16_t akcja = PANVSWR_ZnajdzAkcjeNawigacji(punkt);

    switch (akcja)
    {
    case PAN_SWR_AKCJA_WSTECZ:
        rqExitSWR = true;
        return true;

    case PAN_SWR_AKCJA_WYKRES:
        PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
        return true;

    case PAN_SWR_AKCJA_ANALIZA:
        PANVSWR_PokazAnalize();
        return true;

    case PAN_SWR_AKCJA_NARZEDZIA:
        if (pan_swr_strona != PAN_SWR_STRONA_NARZEDZIA)
        {
            pan_swr_narzedzia_podstrona = PAN_SWR_NARZ_STRONA_PAMIEC;
            pan_swr_narzedzia_fokus = 0U;
        }
        PANVSWR_UstawStrone(PAN_SWR_STRONA_NARZEDZIA);
        return true;

    case PAN_SWR_AKCJA_ZAKRES:
        PANVSWR_UstawStrone(PAN_SWR_STRONA_WYKRES);
        Frequency();
        return true;

    case PAN_SWR_AKCJA_POMIAR:
        Scan();
        return true;

    default:
        return false;
    }
}

static bool PANVSWR_ObsluzNarzedzia(LCDPoint punkt)
{
    const uint8_t liczba = PANVSWR_LiczbaNarzedziNaStronie();
    const int16_t indeks = UI_KafelKompaktowyPoDotyku(punkt, liczba);

    if (indeks < 0)
        return false;

    pan_swr_narzedzia_fokus = (uint8_t)indeks;
    return PANVSWR_WykonajNarzedzie((uint8_t)indeks);
}

static void PANVSWR_ObsluzDotykWykresu(LCDPoint punkt)
{
    if (punkt.x <= 44U && punkt.y >= 94U && punkt.y <= 128U)
    {
        autofast = 0U;
        redrawRequired = 0;
        OneBeep(1);
        DecrCursor();
        while (TOUCH_IsPressed())
        {
            DecrCursor();
            if (cursorChangeCount >= 5U)
                Beeper = 0;
        }
        cursorChangeCount = 0U;
        return;
    }

    if (punkt.x <= 44U && punkt.y >= 134U && punkt.y <= 168U)
    {
        autofast = 0U;
        redrawRequired = 0;
        OneBeep(1);
        IncrCursor();
        while (TOUCH_IsPressed())
        {
            IncrCursor();
            if (cursorChangeCount >= 5U)
                Beeper = 0;
        }
        cursorChangeCount = 0U;
        return;
    }

    /* Dotknięcie samej krzywej pozostaje skrótem do kolejnego typu wykresu. */
    if (punkt.y >= Y0 && punkt.y <= Y0 + WHEIGHT &&
        punkt.x > X0 && punkt.x < X0 + WWIDTH)
    {
        DiagType();
        OneBeep(1);
        return;
    }

    /* Obszar pod osią X służy wyłącznie do ustawiania kursora pomiarowego. */
    if (punkt.y > Y0 + WHEIGHT && punkt.y < PAN_SWR_PASEK_Y && punkt.x >= X0)
    {
        uint32_t wskazany = punkt.x - X0;
        uint32_t poprawny;

        if (wskazany > WWIDTH)
            wskazany = WWIDTH;

        if (PANVSWR_ZnajdzNajblizszyPoprawny(wskazany, &poprawny))
        {
            cursorPos = poprawny;
            ManualCursor = 1;
            redrawRequired = 1;
            OneBeep(1);
        }
    }
}

static void PANVSWR_RysujKlatke(void)
{
    activeLayerX = !BSP_LCD_GetActiveLayer();
    BSP_LCD_SelectLayer(activeLayerX);
    RedrawWindow();
    LCD_ShowActiveLayerOnly();
}

void PANVSWR2_Proc(void) // **************************************************************************+*********
{
    pan_wykres_aktywny = 1U;
    rqExitSWR = false;
    pan_swr_strona = PAN_SWR_STRONA_WYKRES;
    activeLayerX = 1U;
    cursorVisible = 0;
    ClearScreen = 1;
    autofast = 0U;
    Saving = 0U;
    redrawRequired = 0;

    /* SetColours ustala paletę samego wykresu. Nie nadpisujemy jej
     * paletą menu, bo wtedy przełącznik jasne/ciemne nie miał żadnego efektu. */
    SetColours();
    loglog = CFG_GetParam(CFG_PARAM_LOGLOG);
    AutoCursor = CFG_GetParam(CFG_PARAM_CURSOR);
    ManualCursor = 0;
    cursorPos = CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 1 ? WWIDTH / 2U : 0U;
    isStored = 0;
    holdScale = 0;
    f1 = CFG_GetParam(CFG_PARAM_PAN_F1);
    GetBS(f1 / 1000U);

    /*
     * Obie warstwy zaczynają od tego samego, czystego stanu. Każde późniejsze
     * pełne odświeżenie powstaje na warstwie niewidocznej i jest pokazywane
     * dopiero po zakończeniu rysowania. To usuwa charakterystyczne miganie
     * starego ekranu podczas zmiany strony, typu wykresu i zakresu.
     */
    BSP_LCD_SelectLayer(0U);
    LCD_FillAll(BackGrColor);
    BSP_LCD_SelectLayer(1U);
    LCD_FillAll(BackGrColor);
    activeLayerX = 1U;
    BSP_LCD_SelectLayer(activeLayerX);

    isM1Loaded = 0;
    isM2Loaded = 0;
    isM3Loaded = 0;

    SavedValues1 = (float complex *)SDRH_malloc(sizeof(float complex) * (WWIDTH + 1U));
    SavedValues2 = (float complex *)SDRH_malloc(sizeof(float complex) * (WWIDTH + 1U));
    SavedValues3 = (float complex *)SDRH_malloc(sizeof(float complex) * (WWIDTH + 1U));

    if (SavedValues1 != 0 && LoadRXP(1, SavedValues1) == 0)
        isM1Loaded = 1;
    if (SavedValues2 != 0 && LoadRXP(2, SavedValues2) == 0)
        isM2Loaded = 1;
    if (SavedValues3 != 0 && LoadRXP(3, SavedValues3) == 0)
        isM3Loaded = 1;

    LoadBkups();
    grType = GRAPH_VSWR;
    pan_skan_przerwany = 0U;

    if (!isMeasured)
    {
        memset(pan_maska_poprawnosci, 0, sizeof(pan_maska_poprawnosci));
        pan_punkty_poprawne = 0U;
        pan_punkty_odrzucone = 0U;
    }

    if (0U == modstrw)
        modstrw = FONT_GetStrPixelWidth(FONT_FRAN, modstr);

    RedrawWindow();
    LCD_ShowActiveLayerOnly();

    /* Nie dziedziczymy dotknięcia, którym użytkownik wszedł do tego ekranu. */
    while (TOUCH_IsPressed())
        Sleep(10U);

    while (!rqExitSWR)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie;
        bool obsluzono_dotyk = false;

        Sleep(0); /* podtrzymuje obsługę automatycznego usypiania */
        zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            rqExitSWR = true;
        else if (zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            Scan();
        else if (pan_swr_strona == PAN_SWR_STRONA_NARZEDZIA &&
                 (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO ||
                  zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO))
        {
            const uint8_t liczba = PANVSWR_LiczbaNarzedziNaStronie();
            pan_swr_narzedzia_fokus = zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO
                ? (uint8_t)((pan_swr_narzedzia_fokus + 1U) % liczba)
                : (uint8_t)((pan_swr_narzedzia_fokus + liczba - 1U) % liczba);
            redrawRequired = 1;
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK && pan_swr_strona == PAN_SWR_STRONA_NARZEDZIA)
            (void)PANVSWR_WykonajNarzedzie(pan_swr_narzedzia_fokus);
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK && pan_swr_strona == PAN_SWR_STRONA_WYKRES)
            Scan();

        if (TOUCH_Poll(&pt))
        {
            Beeper = 1;
            obsluzono_dotyk = PANVSWR_ObsluzNawigacje(pt);

            if (!obsluzono_dotyk && pan_swr_strona == PAN_SWR_STRONA_NARZEDZIA)
                obsluzono_dotyk = PANVSWR_ObsluzNarzedzia(pt);
            else if (!obsluzono_dotyk && pan_swr_strona == PAN_SWR_STRONA_WYKRES)
            {
                PANVSWR_ObsluzDotykWykresu(pt);
                obsluzono_dotyk = true;
            }

            if (obsluzono_dotyk && TOUCH_IsPressed())
                TOUCH_CzekajNaPuszczenie(25U);
        }

        if (autofast == 0U)
        {
            holdScale = 0;
        }
        else
        {
            /* Automat zawsze pokazuje bieżący wykres i odświeża go atomowo. */
            if (pan_swr_strona != PAN_SWR_STRONA_WYKRES)
                pan_swr_strona = PAN_SWR_STRONA_WYKRES;

            activeLayerX = !BSP_LCD_GetActiveLayer();
            BSP_LCD_SelectLayer(activeLayerX);
            ScanRXFast();
            ClearScreen = 1;
            RedrawWindow();
            LCD_ShowActiveLayerOnly();
            holdScale = 1;
            redrawRequired = 0;
            autosleep_timer = 30000U;
        }

        if (redrawRequired != 0)
        {
            PANVSWR_RysujKlatke();
            redrawRequired = 0;
        }
    }

    Sleep(100U);
    pan_wykres_aktywny = 0U;
    if (SavedValues3 != 0) SDRH_free(SavedValues3);
    if (SavedValues2 != 0) SDRH_free(SavedValues2);
    if (SavedValues1 != 0) SDRH_free(SavedValues1);
}

int QuartzSel;

static float kwarc_pojemnosc_uchwytu_f = 0.0f;
static float kwarc_przewodnosc_uchwytu_s = 0.0f;
static uint32_t kwarc_f_pojemnosci_hz = 0U;
static KWARC_WYNIK_t kwarc_ostatni_wynik;
static KWARC_SERIA_t kwarc_seria;
static KWARC_DOPASOWANIE_t kwarc_dopasowanie;
static uint8_t kwarc_biezacy_dodany = 0U;
static uint8_t kwarc_liczba_do_filtra = 4U;

#define KWARC_PASEK_Y UI_DOLNY_PASEK_Y
#define KWARC_PASEK_H UI_DOLNY_PRZYCISK_WYSOKOSC
#define KWARC_SERIA_WIERSZY_NA_STRONE 7U

typedef enum
{
    KWARC_AKCJA_DODAJ = 1,
    KWARC_AKCJA_SERIA,
    KWARC_AKCJA_KOLEJNY,
    KWARC_AKCJA_WSTECZ
} KWARC_AKCJA_t;

typedef enum
{
    KWARC_KROK_AKCJA_USTAW_F = 101,
    KWARC_KROK_AKCJA_DALEJ,
    KWARC_KROK_AKCJA_ZERUJ,
    KWARC_KROK_AKCJA_MIERZ,
    KWARC_KROK_AKCJA_WSTECZ
} KWARC_KROK_AKCJA_t;

typedef enum
{
    KWARC_SERIA_AKCJA_POPRZEDNIA = 1,
    KWARC_SERIA_AKCJA_NASTEPNA,
    KWARC_SERIA_AKCJA_DOBIERZ,
    KWARC_SERIA_AKCJA_WYCZYSC,
    KWARC_SERIA_AKCJA_WSTECZ
} KWARC_SERIA_AKCJA_t;

static const char *KWARC_T(const char *pl, const char *en, const char *de, const char *ru)
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

static BANDSPAN KWARC_OgraniczZakres(BANDSPAN zakres, BANDSPAN maksimum)
{
    return zakres > maksimum ? maksimum : zakres;
}

static uint32_t KWARC_WybierzCzestotliwoscPojemnosci(uint32_t srodek_hz, BANDSPAN zakres)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint64_t polowa_hz = ((uint64_t)BSVALUES[zakres] * 1000ULL) / 2ULL;
    const uint64_t odstep_zakresu_hz = polowa_hz + 150000ULL;
    const uint64_t odstep_wzgledny_hz = (uint64_t)srodek_hz / 10ULL;
    const uint64_t odstep_hz = odstep_zakresu_hz > odstep_wzgledny_hz ?
                               odstep_zakresu_hz : odstep_wzgledny_hz;

    /* C0 mierzymy z dala od rezonansu. Dla wysokich częstotliwości samo
     * odsunięcie o pół szerokości skanu było za małe i gałąź motionalna
     * zanieczyszczała wynik. Minimum 10% f nominalnej daje znacznie
     * spokojniejszy punkt do pomiaru pojemności statycznej. */
    if ((uint64_t)srodek_hz > odstep_hz && (uint64_t)srodek_hz - odstep_hz >= fmin)
        return (uint32_t)((uint64_t)srodek_hz - odstep_hz);

    if ((uint64_t)srodek_hz + odstep_hz <= fmax)
        return (uint32_t)((uint64_t)srodek_hz + odstep_hz);

    return 0U;
}

static void KWARC_RysujPostepSkanu(uint8_t procent, uint32_t f_hz)
{
    char tekst[80];

    if (procent == 0U)
    {
        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_TYTUL));
        UI_RysujPanel(34, 82, 412, 108,
                      KWARC_T("Skan rezonansu kwarcu", "Crystal resonance scan",
                              "Quarz-Resonanzscan", "Скан резонанса кварца"),
                      UI_STYL_AKCENT);
        UI_RysujWsteczDolny(false);
    }

    snprintf(tekst, sizeof(tekst), "%u%%   %.6f MHz", (unsigned)procent,
             (double)f_hz / 1000000.0);
    UI_RysujWskaznikPostepu(62, 132, 356, 24, procent, tekst);
}

static bool KWARC_ZmierzAdmitancje(uint32_t czestotliwosc_hz, float complex *admitancja_s)
{
    float complex z;
    uint32_t liczba_usrednien = CFG_GetParam(CFG_PARAM_PAN_NSCANS);

    if (czestotliwosc_hz == 0U || admitancja_s == NULL)
        return false;

    if (liczba_usrednien < 3U)
        liczba_usrednien = 3U;

    /* Pierwszy pomiar ustala generator i mieszacze po zmianie częstotliwości.
     * Drugi jest właściwym pomiarem z uśrednianiem. */
    (void)PANVSWR_PobierzS11(czestotliwosc_hz, 2, 0U, &z);
    Sleep(10);
    if (!PANVSWR_PobierzS11(czestotliwosc_hz, (int)liczba_usrednien, 0U, &z) ||
        !isfinite(crealf(z)) || !isfinite(cimagf(z)) || cabsf(z) < 1.0e-9f)
    {
        GEN_SetMeasurementFreq(0);
        return false;
    }

    GEN_SetMeasurementFreq(0);
    *admitancja_s = 1.0f / z;
    return isfinite(crealf(*admitancja_s)) && isfinite(cimagf(*admitancja_s));
}

static float complex KWARC_ModelAdmitancjiUchwytu(uint32_t czestotliwosc_hz)
{
    const float omega = 6.28318530717958647692f * (float)czestotliwosc_hz;
    return kwarc_przewodnosc_uchwytu_s + I * omega * kwarc_pojemnosc_uchwytu_f;
}

static bool KWARC_ZmierzPojemnoscPoZerowaniu(uint32_t czestotliwosc_hz, float *pojemnosc_f)
{
    float complex y_calkowite;
    float complex y_elementu;
    float c;

    if (pojemnosc_f == NULL ||
        !KWARC_ZmierzAdmitancje(czestotliwosc_hz, &y_calkowite))
        return false;

    y_elementu = y_calkowite - KWARC_ModelAdmitancjiUchwytu(czestotliwosc_hz);
    c = cimagf(y_elementu) /
        (6.28318530717958647692f * (float)czestotliwosc_hz);

    /* Typowe C0 kwarców leży w pojedynczych pF, ale pozostawiamy szeroki
     * margines dla nietypowych rezonatorów i uchwytów. */
    if (!isfinite(c) || c < 5.0e-14f || c > 2.0e-10f)
        return false;

    *pojemnosc_f = c;
    return true;
}

static void KWARC_SkompensujUchwytWSkanie(float f_start_hz, float krok_hz)
{
    uint32_t i;

    for (i = 0U; i <= WWIDTH; ++i)
    {
        float complex y;
        const uint32_t f_hz = (uint32_t)lroundf(f_start_hz + (float)i * krok_hz);

        if (!pan_maska_poprawnosci[i] || !isfinite(crealf(values[i])) ||
            !isfinite(cimagf(values[i])) || cabsf(values[i]) < 1.0e-9f)
            continue;

        y = 1.0f / values[i];
        y -= KWARC_ModelAdmitancjiUchwytu(f_hz);
        if (!isfinite(crealf(y)) || !isfinite(cimagf(y)) || cabsf(y) < 1.0e-12f)
        {
            values[i] = NAN + NAN * I;
            pan_maska_poprawnosci[i] = 0U;
            continue;
        }
        values[i] = 1.0f / y;
    }
    PANVSWR_ZliczJakosc(pan_maska_poprawnosci);
}

static bool KWARC_Skanuj(uint32_t srodek_hz, BANDSPAN zakres,
                         float *f_start_hz, float *krok_hz)
{
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint64_t szerokosc_hz = (uint64_t)BSVALUES[zakres] * 1000ULL;
    const uint64_t polowa_hz = szerokosc_hz / 2ULL;
    uint64_t start_hz;
    float krok;
    uint32_t i;
    uint32_t liczba_usrednien = CFG_GetParam(CFG_PARAM_PAN_NSCANS);

    if (f_start_hz == NULL || krok_hz == NULL || srodek_hz < fmin || srodek_hz > fmax)
        return false;

    if (szerokosc_hz == 0ULL || szerokosc_hz > (uint64_t)(fmax - fmin))
        return false;

    if ((uint64_t)srodek_hz >= polowa_hz)
        start_hz = (uint64_t)srodek_hz - polowa_hz;
    else
        start_hz = 0ULL;

    if (start_hz < fmin)
        start_hz = fmin;
    if (start_hz + szerokosc_hz > fmax)
        start_hz = (uint64_t)fmax - szerokosc_hz;

    krok = (float)szerokosc_hz / (float)WWIDTH;
    if (krok <= 0.0f)
        return false;

    if (liczba_usrednien == 0U)
        liczba_usrednien = 1U;

    KWARC_RysujPostepSkanu(0U, (uint32_t)start_hz);
    memset(pan_maska_poprawnosci, 0, sizeof(pan_maska_poprawnosci));
    for (i = 0U; i <= WWIDTH; ++i)
    {
        const uint32_t f_hz = (uint32_t)llroundf((float)start_hz + (float)i * krok);
        WEJSCIE_ZDARZENIE_t zdarzenie;

        {
            float complex z;
            if (PANVSWR_PobierzS11(f_hz, (int)liczba_usrednien, 0U, &z))
            {
                values[i] = z;
                pan_maska_poprawnosci[i] = 1U;
            }
            else
            {
                values[i] = NAN + NAN * I;
            }
        }

        if ((i & 7U) == 0U)
        {
            KWARC_RysujPostepSkanu((uint8_t)((100UL * i) / WWIDTH), f_hz);
            zdarzenie = WEJSCIA_PobierzZdarzenie();
            if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ ||
                zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            {
                GEN_SetMeasurementFreq(0);
                return false;
            }
        }
    }

    KWARC_RysujPostepSkanu(100U, (uint32_t)(start_hz + szerokosc_hz));
    GEN_SetMeasurementFreq(0);
    PANVSWR_ZliczJakosc(pan_maska_poprawnosci);
    isMeasured = pan_punkty_poprawne > 0U;
    *f_start_hz = (float)start_hz;
    *krok_hz = krok;
    return isMeasured;
}

static bool KWARC_CzekajNaDalej(void)
{
    while (TOUCH_IsPressed())
        ;

    for (;;)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return false;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OK || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
            return true;
        if (TOUCH_Poll(&pt))
        {
            const UI_PROSTOKAT_t dalej = UI_ObszarPrzyciskuDolnego(1U);
            if (UI_CzyDotknietoWstecz(pt))
                return false;
            if (UI_CzyPunktWObszarze(pt, &dalej))
                return true;
        }
        Sleep(5);
    }
}

static void KWARC_RysujPasekWyniku(void)
{
    const UI_AKCJA_t akcje[4] =
    {
        { KWARC_AKCJA_DODAJ,
          kwarc_biezacy_dodany ?
              KWARC_T("Dodano", "Added", "Hinzugef.", "Добавлен") :
              KWARC_T("Dodaj", "Add", "Hinzufügen", "Добавить"),
          kwarc_biezacy_dodany ? UI_STYL_NIEAKTYWNY : UI_STYL_AKCENT,
          !kwarc_biezacy_dodany && kwarc_seria.liczba < KWARC_SERIA_MAKS,
          false },
        { KWARC_AKCJA_SERIA, KWARC_T("Seria", "Series", "Serie", "Серия"),
          UI_STYL_NORMALNY, true, false },
        { KWARC_AKCJA_KOLEJNY, KWARC_T("Kolejny", "Next", "Nächster", "Следующий"),
          UI_STYL_AKTYWNY, true, false },
        { KWARC_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
    };

    UI_RysujPasekAkcji(KWARC_PASEK_Y, KWARC_PASEK_H, akcje, 4U);
}

static void KWARC_RysujWynik(const KWARC_WYNIK_t *wynik)
{
    char fs[24];
    char fp[24];
    char wiersz[72];

    if (wynik == NULL)
        return;

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_WYNIK_TYTUL));

    snprintf(fs, sizeof(fs), "%.6f", wynik->fs_hz / 1.0e6f);
    snprintf(fp, sizeof(fp), "%.6f", wynik->fp_hz / 1.0e6f);

    /*
     * Jednostka w osobnym dużym napisie zabierała miejsce sześciu cyfrom
     * częstotliwości i na 480 px potrafiła wejść na wartość Fp. Jednostkę
     * przenosimy do etykiety pola; sama liczba ma dzięki temu całą szerokość.
     */
    UI_RysujPoleLiczboweGlowne(10, 42, 225, 72, "Fs [MHz]", fs, "");
    UI_RysujPoleLiczboweGlowne(245, 42, 225, 72, "Fp [MHz]", fp, "");

    UI_RysujPanel(10, 120, 460, 96, JEZYK_Tekst(TEKST_KWARC_METODA_INFO), UI_STYL_NORMALNY);
    snprintf(wiersz, sizeof(wiersz), "C0 %.3f pF     Cm %.5f pF", wynik->c0_f * 1.0e12f, wynik->cm_f * 1.0e12f);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 24, 144, wiersz);
    snprintf(wiersz, sizeof(wiersz), "Lm %.4f mH     Rm %.2f Ohm", wynik->lm_h * 1.0e3f, wynik->rm_ohm);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 24, 168, wiersz);
    snprintf(wiersz, sizeof(wiersz), "Q %.0f   Fp-Fs %.1f Hz   %s %u/%u",
             wynik->q, wynik->fp_hz - wynik->fs_hz,
             KWARC_T("seria", "series", "Serie", "серия"),
             (unsigned)kwarc_seria.liczba, (unsigned)KWARC_SERIA_MAKS);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 24, 192, wiersz);

    KWARC_RysujPasekWyniku();
}

static void KWARC_RysujDobieranie(void)
{
    char tekst[96];
    char wybrane[96];
    uint8_t i;
    size_t uzyte = 0U;

    UI_WyczyscEkran();
    UI_RysujNaglowek(KWARC_T("Dobieranie kwarców", "Crystal matching", "Quarz-Auswahl", "Подбор кварцев"));

    snprintf(tekst, sizeof(tekst), "%s: %u",
             KWARC_T("Liczba do filtra", "Crystals for filter", "Quarze im Filter", "Кварцев в фильтре"),
             (unsigned)kwarc_liczba_do_filtra);
    UI_RysujPoleInformacyjne(135, 38, 210, 44,
                             KWARC_T("Zestaw", "Set", "Satz", "Набор"), tekst);

    memset(&kwarc_dopasowanie, 0, sizeof(kwarc_dopasowanie));
    if (KWARC_SERIA_Dobierz(&kwarc_seria, kwarc_liczba_do_filtra, &kwarc_dopasowanie))
    {
        wybrane[0] = '\0';
        for (i = 0U; i < kwarc_dopasowanie.liczba && uzyte + 8U < sizeof(wybrane); ++i)
        {
            const int n = snprintf(wybrane + uzyte, sizeof(wybrane) - uzyte,
                                   "%s#%u", i == 0U ? "" : " ",
                                   (unsigned)(kwarc_dopasowanie.indeksy[i] + 1U));
            if (n <= 0)
                break;
            uzyte += (size_t)n;
        }

        UI_RysujPanel(18, 90, 444, 128,
                      KWARC_T("Najlepiej dopasowany komplet", "Best matched set", "Bester Satz", "Лучший комплект"), UI_STYL_AKTYWNY);
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_AKTYWNY), BackGrColor, 34, 113, wybrane);
        snprintf(tekst, sizeof(tekst), "%s %.6f MHz   ΔFs %.1f Hz",
                 KWARC_T("Fs śr.", "Fs avg", "Fs Mittel", "Fs сред."),
                 kwarc_dopasowanie.srednia_fs_hz / 1.0e6f, kwarc_dopasowanie.rozrzut_fs_hz);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 34, 150, tekst);
        snprintf(tekst, sizeof(tekst), "%s %.2f Ohm   ΔRm %.1f%%",
                 KWARC_T("Rm śr.", "Rm avg", "Rm Mittel", "Rm сред."),
                 kwarc_dopasowanie.sredni_rm_ohm, kwarc_dopasowanie.rozrzut_rm_proc);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 34, 174, tekst);
        snprintf(tekst, sizeof(tekst), "Q min %.0f   %s %.0f",
                 kwarc_dopasowanie.q_min,
                 KWARC_T("Q śr.", "Q avg", "Q Mittel", "Q сред."),
                 kwarc_dopasowanie.q_srednie);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 34, 198, tekst);
    }
    else
    {
        UI_RysujPanel(18, 90, 444, 128,
                      KWARC_T("Za mało pomiarów", "Not enough measurements", "Zu wenige Messungen", "Недостаточно измерений"), UI_STYL_OSTRZEZENIE);
        snprintf(tekst, sizeof(tekst), "%s: %u, %s: %u",
                 KWARC_T("zebrano", "measured", "gemessen", "измерено"),
                 (unsigned)kwarc_seria.liczba,
                 KWARC_T("potrzeba", "needed", "benötigt", "нужно"),
                 (unsigned)kwarc_liczba_do_filtra);
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BackGrColor, 50, 145, tekst);
    }

    {
        const UI_AKCJA_t akcje[3] =
        {
            { 1, "-", UI_STYL_NORMALNY, kwarc_liczba_do_filtra > 2U, false },
            { 2, "+", UI_STYL_NORMALNY, kwarc_liczba_do_filtra < KWARC_ZESTAW_MAKS, false },
            { 3, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };
        UI_RysujPasekAkcji(KWARC_PASEK_Y, KWARC_PASEK_H, akcje, 3U);
    }
}

static void KWARC_OtworzDobieranie(void)
{
    while (TOUCH_IsPressed())
        Sleep(5);

    if (kwarc_liczba_do_filtra < 2U)
        kwarc_liczba_do_filtra = 2U;
    if (kwarc_liczba_do_filtra > KWARC_ZESTAW_MAKS)
        kwarc_liczba_do_filtra = KWARC_ZESTAW_MAKS;

    KWARC_RysujDobieranie();
    for (;;)
    {
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        int16_t akcja = 0;

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO && kwarc_liczba_do_filtra > 2U)
        {
            --kwarc_liczba_do_filtra;
            KWARC_RysujDobieranie();
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO && kwarc_liczba_do_filtra < KWARC_ZESTAW_MAKS)
        {
            ++kwarc_liczba_do_filtra;
            KWARC_RysujDobieranie();
        }
        if (TOUCH_Poll(&pt))
            akcja = UI_ZnajdzAkcjePaska(pt, KWARC_PASEK_Y, KWARC_PASEK_H,
                (const UI_AKCJA_t[]){
                    {1, "-", UI_STYL_NORMALNY, kwarc_liczba_do_filtra > 2U, false},
                    {2, "+", UI_STYL_NORMALNY, kwarc_liczba_do_filtra < KWARC_ZESTAW_MAKS, false},
                    {3, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false}}, 3U);

        if (akcja == 1 && kwarc_liczba_do_filtra > 2U)
        {
            --kwarc_liczba_do_filtra;
            KWARC_RysujDobieranie();
        }
        else if (akcja == 2 && kwarc_liczba_do_filtra < KWARC_ZESTAW_MAKS)
        {
            ++kwarc_liczba_do_filtra;
            KWARC_RysujDobieranie();
        }
        else if (akcja == 3)
            return;
        Sleep(5);
    }
}

static void KWARC_RysujSerie(uint8_t strona)
{
    char tekst[96];
    uint8_t pierwszy = (uint8_t)(strona * KWARC_SERIA_WIERSZY_NA_STRONE);
    uint8_t i;
    uint8_t ostatni = (uint8_t)(pierwszy + KWARC_SERIA_WIERSZY_NA_STRONE);
    const uint8_t stron = kwarc_seria.liczba == 0U ? 1U :
        (uint8_t)((kwarc_seria.liczba + KWARC_SERIA_WIERSZY_NA_STRONE - 1U) / KWARC_SERIA_WIERSZY_NA_STRONE);

    if (ostatni > kwarc_seria.liczba)
        ostatni = kwarc_seria.liczba;

    UI_WyczyscEkran();
    UI_RysujNaglowek(KWARC_T("Seria kwarców", "Crystal series", "Quarz-Serie", "Серия кварцев"));
    snprintf(tekst, sizeof(tekst), "%s %u/%u   %s %u/%u",
             KWARC_T("Pomiarów", "Measurements", "Messungen", "Измерений"),
             (unsigned)kwarc_seria.liczba, (unsigned)KWARC_SERIA_MAKS,
             KWARC_T("strona", "page", "Seite", "стр."),
             (unsigned)(strona + 1U), (unsigned)stron);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 16, 38, tekst);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_AKCENT), BackGrColor, 16, 61,
               " #      Fs [MHz]       Rm [Ohm]      Q");

    if (kwarc_seria.liczba == 0U)
    {
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), BackGrColor, 90, 130,
                   KWARC_T("Brak zapisanych pomiarów", "No saved measurements", "Keine Messungen", "Нет измерений"));
    }
    else
    {
        for (i = pierwszy; i < ostatni; ++i)
        {
            const KWARC_REKORD_t *r = &kwarc_seria.rekordy[i];
            const bool wybrany = KWARC_SERIA_CzyWybrany(&kwarc_dopasowanie, i);
            snprintf(tekst, sizeof(tekst), "%c%02u   %10.6f      %8.2f    %6.0f",
                     wybrany ? '*' : ' ', (unsigned)r->numer,
                     r->wynik.fs_hz / 1.0e6f, r->wynik.rm_ohm, r->wynik.q);
            FONT_Write(FONT_FRAN, wybrany ? UI_KolorTekstu(UI_STYL_AKTYWNY) : TextColor,
                       BackGrColor, 16, 84 + (int)(i - pierwszy) * 20, tekst);
        }
    }

    {
        const UI_AKCJA_t akcje[5] =
        {
            { KWARC_SERIA_AKCJA_POPRZEDNIA, "<", UI_STYL_NORMALNY, strona > 0U, false },
            { KWARC_SERIA_AKCJA_NASTEPNA, ">", UI_STYL_NORMALNY, (uint8_t)(strona + 1U) < stron, false },
            { KWARC_SERIA_AKCJA_DOBIERZ, KWARC_T("Dobierz", "Match", "Auswahl", "Подбор"),
              UI_STYL_AKCENT, kwarc_seria.liczba >= 2U, false },
            { KWARC_SERIA_AKCJA_WYCZYSC, KWARC_T("Wyczyść", "Clear", "Leeren", "Очистить"),
              UI_STYL_OSTRZEZENIE, kwarc_seria.liczba > 0U, false },
            { KWARC_SERIA_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };
        UI_RysujPasekAkcji(KWARC_PASEK_Y, KWARC_PASEK_H, akcje, 5U);
    }
}

static void KWARC_OtworzSerie(void)
{
    uint8_t strona = 0U;

    while (TOUCH_IsPressed())
        Sleep(5);
    KWARC_RysujSerie(strona);

    for (;;)
    {
        const uint8_t stron = kwarc_seria.liczba == 0U ? 1U :
            (uint8_t)((kwarc_seria.liczba + KWARC_SERIA_WIERSZY_NA_STRONE - 1U) / KWARC_SERIA_WIERSZY_NA_STRONE);
        const WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        int16_t akcja = 0;
        UI_AKCJA_t akcje[5] =
        {
            { KWARC_SERIA_AKCJA_POPRZEDNIA, "<", UI_STYL_NORMALNY, strona > 0U, false },
            { KWARC_SERIA_AKCJA_NASTEPNA, ">", UI_STYL_NORMALNY, (uint8_t)(strona + 1U) < stron, false },
            { KWARC_SERIA_AKCJA_DOBIERZ, KWARC_T("Dobierz", "Match", "Auswahl", "Подбор"), UI_STYL_AKCENT, kwarc_seria.liczba >= 2U, false },
            { KWARC_SERIA_AKCJA_WYCZYSC, KWARC_T("Wyczyść", "Clear", "Leeren", "Очистить"), UI_STYL_OSTRZEZENIE, kwarc_seria.liczba > 0U, false },
            { KWARC_SERIA_AKCJA_WSTECZ, JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false },
        };

        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            return;
        if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_LEWO && strona > 0U)
        {
            --strona;
            KWARC_RysujSerie(strona);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OBROT_PRAWO && (uint8_t)(strona + 1U) < stron)
        {
            ++strona;
            KWARC_RysujSerie(strona);
        }
        else if (zdarzenie == WEJSCIE_ZDARZENIE_OK && kwarc_seria.liczba >= 2U)
        {
            KWARC_OtworzDobieranie();
            KWARC_RysujSerie(strona);
        }

        if (TOUCH_Poll(&pt))
            akcja = UI_ZnajdzAkcjePaska(pt, KWARC_PASEK_Y, KWARC_PASEK_H, akcje, 5U);

        if (akcja == KWARC_SERIA_AKCJA_POPRZEDNIA && strona > 0U)
        {
            --strona;
            KWARC_RysujSerie(strona);
        }
        else if (akcja == KWARC_SERIA_AKCJA_NASTEPNA && (uint8_t)(strona + 1U) < stron)
        {
            ++strona;
            KWARC_RysujSerie(strona);
        }
        else if (akcja == KWARC_SERIA_AKCJA_DOBIERZ && kwarc_seria.liczba >= 2U)
        {
            KWARC_OtworzDobieranie();
            KWARC_RysujSerie(strona);
        }
        else if (akcja == KWARC_SERIA_AKCJA_WYCZYSC && kwarc_seria.liczba > 0U)
        {
            KWARC_SERIA_Inicjalizuj(&kwarc_seria);
            memset(&kwarc_dopasowanie, 0, sizeof(kwarc_dopasowanie));
            kwarc_biezacy_dodany = 0U;
            strona = 0U;
            KWARC_RysujSerie(strona);
        }
        else if (akcja == KWARC_SERIA_AKCJA_WSTECZ)
            return;

        Sleep(5);
    }
}

static void KWARC_PrzygotujKolejnyPomiar(void)
{
    QuStep = 2;
    QuartzSel = 2;
    kwarc_biezacy_dodany = 0U;
    KWARC_RysujKrokSterowania();
}

static void KWARC_ObsluzAkcjeWyniku(int16_t akcja)
{
    if (akcja == KWARC_AKCJA_DODAJ)
    {
        if (!kwarc_biezacy_dodany &&
            KWARC_SERIA_Dodaj(&kwarc_seria, &kwarc_ostatni_wynik))
        {
            kwarc_biezacy_dodany = 1U;
            memset(&kwarc_dopasowanie, 0, sizeof(kwarc_dopasowanie));
            KWARC_RysujWynik(&kwarc_ostatni_wynik);
        }
    }
    else if (akcja == KWARC_AKCJA_SERIA)
    {
        KWARC_OtworzSerie();
        KWARC_RysujWynik(&kwarc_ostatni_wynik);
    }
    else if (akcja == KWARC_AKCJA_KOLEJNY)
    {
        KWARC_PrzygotujKolejnyPomiar();
    }
    else if (akcja == KWARC_AKCJA_WSTECZ)
    {
        rqExitSWR = true;
    }
}

static void KWARC_RysujKrokSterowania(void)
{
    char ftekst[24];
    char opis[96];
    UI_AKCJA_t akcje[3];
    uint8_t liczba = 0U;
    const uint32_t f_hz = CFG_GetParam(CFG_PARAM_MEAS_F);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_TYTUL));
    snprintf(ftekst, sizeof(ftekst), "%.6f", (double)f_hz / 1000000.0);
    UI_RysujPoleLiczboweGlowne(90, 40, 300, 70,
                               KWARC_T("f nominalna", "nominal f", "Nenn-f", "f ном."),
                               ftekst, "MHz");

    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_WSTECZ,
        JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, true, false};

    if (QuStep == 0)
    {
        UI_RysujPanel(24, 118, 432, 82,
                      KWARC_T("Krok 1/3 - częstotliwość", "Step 1/3 - frequency",
                              "Schritt 1/3 - Frequenz", "Шаг 1/3 - частота"),
                      UI_STYL_NORMALNY);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 38, 148,
                   KWARC_T("Jeżeli częstotliwość jest dobra, wybierz Dalej.",
                           "If the frequency is correct, choose Continue.",
                           "Ist die Frequenz korrekt, Weiter wählen.",
                           "Если частота верна, нажмите Далее."));
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_USTAW_F,
            KWARC_T("Ustaw f", "Set f", "f setzen", "Задать f"), UI_STYL_NORMALNY, true, false};
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_DALEJ,
            JEZYK_Tekst(TEKST_KWARC_DALEJ), UI_STYL_AKCENT, true, false};
    }
    else if (QuStep == 1 && !sCalib)
    {
        UI_RysujPanel(24, 118, 432, 82,
                      KWARC_T("Krok 2/3 - pusty uchwyt", "Step 2/3 - empty fixture",
                              "Schritt 2/3 - leerer Halter", "Шаг 2/3 - пустой держатель"),
                      UI_STYL_NORMALNY);
        snprintf(opis, sizeof(opis), "%s %.6f MHz",
                 KWARC_T("Zerowanie przy", "Zeroing at", "Nullen bei", "Обнуление на"),
                 (double)KWARC_WybierzCzestotliwoscPojemnosci(f_hz, BS1000) / 1000000.0);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 38, 143, opis);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 38, 164,
                   KWARC_T("Wyjmij kwarc i zostaw ten sam pusty uchwyt.",
                           "Remove the crystal and leave the same fixture empty.",
                           "Quarz entfernen; denselben Halter leer lassen.",
                           "Снимите кварц и оставьте тот же держатель пустым."));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 38, 184,
                   KWARC_T("To kompensacja pojemności uchwytu, a nie druga OSL.",
                           "This compensates fixture capacitance; it is not a second OSL.",
                           "Dies kompensiert die Halterkapazität; keine zweite OSL.",
                           "Это компенсация ёмкости держателя, а не вторая OSL."));
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_ZERUJ,
            KWARC_T("Zeruj", "Zero", "Nullen", "Обнулить"), UI_STYL_AKCENT, true, false};
    }
    else if (QuStep == 1)
    {
        UI_RysujPanel(24, 118, 432, 82,
                      KWARC_T("Uchwyt wyzerowany", "Fixture zeroed",
                              "Halter genullt", "Держатель обнулён"),
                      UI_STYL_AKTYWNY);
        snprintf(opis, sizeof(opis), "%s %.3f pF   G %.3f mS",
                 KWARC_T("resztkowe C", "residual C", "Rest-C", "остаточная C"),
                 (double)kwarc_pojemnosc_uchwytu_f * 1.0e12,
                 (double)kwarc_przewodnosc_uchwytu_s * 1.0e3);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 38, 145, opis);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 38, 171,
                   KWARC_T("Wynik zapisany tylko dla tej sesji. Teraz wybierz Dalej.",
                           "Stored for this session only. Now choose Continue.",
                           "Nur für diese Sitzung gespeichert. Jetzt Weiter.",
                           "Сохранено только для этой сессии. Теперь Далее."));
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_ZERUJ,
            KWARC_T("Powtórz", "Again", "Nochmal", "Повторить"), UI_STYL_NORMALNY, true, false};
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_DALEJ,
            JEZYK_Tekst(TEKST_KWARC_DALEJ), UI_STYL_AKCENT, true, false};
    }
    else
    {
        UI_RysujPanel(24, 118, 432, 82,
                      KWARC_T("Krok 3/3 - pomiar", "Step 3/3 - measurement",
                              "Schritt 3/3 - Messung", "Шаг 3/3 - измерение"),
                      UI_STYL_AKTYWNY);
        FONT_Write(FONT_FRAN, TextColor, BackGrColor, 38, 145,
                   KWARC_T("Włóż kwarc do tego samego uchwytu.",
                           "Insert the crystal into the same fixture.",
                           "Quarz in denselben Halter einsetzen.",
                           "Установите кварц в тот же держатель."));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), BackGrColor, 38, 171,
                   KWARC_T("Mierz kwarc uruchomi skan Fs/Fp i obliczenie modelu BVD.",
                           "Measure crystal starts the Fs/Fp scan and BVD model.",
                           "Quarz messen startet Fs/Fp-Scan und BVD-Modell.",
                           "Измерить запускает поиск Fs/Fp и модель BVD."));
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_USTAW_F,
            KWARC_T("Zmień f", "Change f", "f ändern", "Изм. f"), UI_STYL_NORMALNY, true, false};
        akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_MIERZ,
            KWARC_T("Mierz", "Measure", "Messen", "Измерить"), UI_STYL_AKCENT, true, false};
    }

    UI_RysujPasekAkcji(KWARC_PASEK_Y, KWARC_PASEK_H, akcje, liczba);
}

static void KWARC_UstawCzestotliwosc(void)
{
    SWR_SetFrequency();
    /* Ogólny edytor panoramy ustawia historycznie span=300 kHz. Pomiar
     * kwarcu ma własny szeroki skan 1 MHz, dlatego nie dziedziczymy tego
     * efektu ubocznego. Zmiana f unieważnia zerowanie uchwytu. */
    span = BS1000;
    f1 = CFG_GetParam(CFG_PARAM_MEAS_F);
    sCalib = 0;
    kwarc_pojemnosc_uchwytu_f = 0.0f;
    kwarc_przewodnosc_uchwytu_s = 0.0f;
    kwarc_f_pojemnosci_hz = 0U;
    QuStep = 0;
    KWARC_RysujKrokSterowania();
}

static void KWARC_ObsluzAkcjeKroku(int16_t akcja)
{
    if (akcja == KWARC_KROK_AKCJA_WSTECZ)
    {
        rqExitSWR = true;
        return;
    }

    if (akcja == KWARC_KROK_AKCJA_USTAW_F)
    {
        KWARC_UstawCzestotliwosc();
        return;
    }

    if (QuStep == 0 && akcja == KWARC_KROK_AKCJA_DALEJ)
    {
        QuStep = 1;
        KWARC_RysujKrokSterowania();
        return;
    }

    if (QuStep == 1 && akcja == KWARC_KROK_AKCJA_ZERUJ)
    {
        if (QuCalibrate())
            KWARC_RysujKrokSterowania();
        else
            KWARC_RysujKrokSterowania();
        return;
    }

    if (QuStep == 1 && sCalib && akcja == KWARC_KROK_AKCJA_DALEJ)
    {
        QuStep = 2;
        KWARC_RysujKrokSterowania();
        return;
    }

    if (QuStep == 2 && akcja == KWARC_KROK_AKCJA_MIERZ)
    {
        if (QuMeasure())
        {
            QuStep = 3;
            QuartzSel = 3;
        }
        else
        {
            QuStep = 2;
            KWARC_RysujKrokSterowania();
        }
    }
}

void QuNextStep(void)
{
    /* OK/START ma wykonywać akcję naturalną dla bieżącego kroku, a nie
     * zmuszać użytkownika do otwierania edytora częstotliwości. */
    if (QuStep == 0)
        KWARC_ObsluzAkcjeKroku(KWARC_KROK_AKCJA_DALEJ);
    else if (QuStep == 1 && !sCalib)
        KWARC_ObsluzAkcjeKroku(KWARC_KROK_AKCJA_ZERUJ);
    else if (QuStep == 1)
        KWARC_ObsluzAkcjeKroku(KWARC_KROK_AKCJA_DALEJ);
    else if (QuStep == 2)
        KWARC_ObsluzAkcjeKroku(KWARC_KROK_AKCJA_MIERZ);
    else
        KWARC_PrzygotujKolejnyPomiar();
}

static bool QuCalibrate(void)
{
    const BANDSPAN zakres = BS1000;
    const uint32_t srodek_hz = CFG_GetParam(CFG_PARAM_MEAS_F);
    float complex y_uchwytu;
    float c_uchwytu;
    float g_uchwytu;

    kwarc_f_pojemnosci_hz = KWARC_WybierzCzestotliwoscPojemnosci(srodek_hz, zakres);
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_TYTUL));
    UI_RysujPanel(34, 82, 412, 108,
                  KWARC_T("Zerowanie pustego uchwytu", "Zeroing empty fixture",
                          "Leeren Halter nullen", "Обнуление пустого держателя"),
                  UI_STYL_AKCENT);
    UI_RysujWskaznikPostepu(62, 132, 356, 24, 10U,
                            KWARC_T("Pomiar uchwytu", "Measuring fixture",
                                    "Halter messen", "Измерение держателя"));

    if (kwarc_f_pojemnosci_hz == 0U ||
        !KWARC_ZmierzAdmitancje(kwarc_f_pojemnosci_hz, &y_uchwytu))
    {
        sCalib = 0;
        kwarc_przewodnosc_uchwytu_s = 0.0f;
        kwarc_pojemnosc_uchwytu_f = 0.0f;
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_KALIBRACJA_BLAD);
        return false;
    }

    /* Zerowanie przechowuje model równoległy G+C pustego uchwytu. To jest
     * odporniejsze od dawnej próby wyliczenia C z bardzo dużej impedancji
     * OPEN. Po OSL resztkowa reaktancja może być bliska zeru albo nawet
     * lekko indukcyjna; nie wolno wtedy odrzucać całej procedury. */
    g_uchwytu = crealf(y_uchwytu);
    c_uchwytu = cimagf(y_uchwytu) /
        (6.28318530717958647692f * (float)kwarc_f_pojemnosci_hz);

    kwarc_przewodnosc_uchwytu_s =
        (isfinite(g_uchwytu) && g_uchwytu > 0.0f) ? g_uchwytu : 0.0f;
    kwarc_pojemnosc_uchwytu_f =
        (isfinite(c_uchwytu) && c_uchwytu > 0.0f) ? c_uchwytu : 0.0f;

    /* Oczywisty przypadek zwarcia lub włożonego elementu nie może zostać
     * zaakceptowany jako pusty uchwyt. 20 mS odpowiada ok. 50 omom. */
    if (cabsf(y_uchwytu) > 0.020f || kwarc_pojemnosc_uchwytu_f > 2.0e-10f)
    {
        sCalib = 0;
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_KALIBRACJA_BLAD);
        return false;
    }

    sCalib = 1;
    UI_RysujWskaznikPostepu(62, 132, 356, 24, 100U,
                            KWARC_T("Uchwyt wyzerowany", "Fixture zeroed",
                                    "Halter genullt", "Держатель обнулён"));
    (void)KAL_META_Zapisz(KAL_META_KWARC, -1);
    Sleep(250);
    return true;
}

static bool QuMeasure(void)
{
    const uint32_t srodek_hz = CFG_GetParam(CFG_PARAM_MEAS_F);
    BANDSPAN zakres_szeroki = KWARC_OgraniczZakres(span, BS1000);
    float c0_f;
    float f_start_hz;
    float krok_hz;
    float fs_wstepne;
    float fp_wstepne;
    float rm_wstepne;
    float fs_dokladne;
    float fp_dokladne;
    float rm_dokladne;
    float f_start_fs;
    float krok_fs;
    float f_start_fp;
    float krok_fp;
    char tekst[96];

    if (!sCalib || kwarc_f_pojemnosci_hz == 0U)
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_KALIBRACJA_BLAD);
        return false;
    }

    if (!KWARC_ZmierzPojemnoscPoZerowaniu(kwarc_f_pojemnosci_hz, &c0_f))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_C0_BLAD);
        return false;
    }

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_TYTUL));
    FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, 110, 120, JEZYK_Tekst(TEKST_TRWA_POMIAR));

    if (!KWARC_Skanuj(srodek_hz, zakres_szeroki, &f_start_hz, &krok_hz))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }
    KWARC_SkompensujUchwytWSkanie(f_start_hz, krok_hz);
    if (!KWARC_ZnajdzRezonansSzeregowy(values, pan_maska_poprawnosci, WWIDTH + 1U,
                                       f_start_hz, krok_hz, (float)srodek_hz,
                                       c0_f,
                                       &fs_wstepne, &rm_wstepne) ||
        !KWARC_ZnajdzRezonansRownolegly(values, pan_maska_poprawnosci, WWIDTH + 1U,
                                        f_start_hz, krok_hz, fs_wstepne,
                                        &fp_wstepne) || fp_wstepne <= fs_wstepne)
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }

    /* Pokazujemy szeroki skan przed pomiarami precyzyjnymi. Uzytkownik widzi,
       czy analizator rzeczywiscie znalazl parę rezonans/antyrezonans. */
    f1 = srodek_hz;
    span = zakres_szeroki;
    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_KWARC_TYTUL));
    DrawGrid(GRAPH_RX);
    DrawRX(1, 0);
    snprintf(tekst, sizeof(tekst), "Fs %.6f   Fp %.6f MHz",
             fs_wstepne / 1000000.0f, fp_wstepne / 1000000.0f);

    /*
     * DrawGrid() rysuje podpisy osi częstotliwości tuż pod wykresem. Na
     * ekranie kwarcu w tym samym miejscu pokazujemy Fs/Fp, więc stare opisy
     * przebijały spod wyniku. Czyścimy cały pas pod wykresem i przeznaczamy
     * go wyłącznie na jedną, czytelną linię wyniku. Dolna krawędź wykresu
     * (y=188) pozostaje nietknięta.
     */
    LCD_FillRect(LCD_MakePoint(20, Y0 + WHEIGHT + 1),
                 LCD_MakePoint(451, KWARC_PASEK_Y - 2U), BackGrColor);
    FONT_Write(FONT_FRAN, TextColor, BackGrColor, 72, Y0 + WHEIGHT + 6, tekst);
    UI_RysujWsteczDolny(false);
    {
        const UI_PROSTOKAT_t dalej = UI_ObszarPrzyciskuDolnego(1U);
        UI_RysujPrzycisk(dalej.x, dalej.y, dalej.szerokosc, dalej.wysokosc,
                         JEZYK_Tekst(TEKST_KWARC_DALEJ), UI_STYL_AKCENT, FONT_FRAN);
    }

    if (!KWARC_CzekajNaDalej())
        return false;

    /* Osobne, waskie skany nie gubia Fp, jak dzialo sie w starym kodzie.
       Pierwszy doprecyzowuje Fs/Rm, drugi niezaleznie doprecyzowuje Fp. */
    if (!KWARC_Skanuj((uint32_t)lroundf(fs_wstepne), BS10, &f_start_fs, &krok_fs))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }
    KWARC_SkompensujUchwytWSkanie(f_start_fs, krok_fs);
    if (!KWARC_ZnajdzRezonansSzeregowy(values, pan_maska_poprawnosci, WWIDTH + 1U,
                                       f_start_fs, krok_fs, fs_wstepne,
                                       c0_f,
                                       &fs_dokladne, &rm_dokladne))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }

    if (!KWARC_Skanuj((uint32_t)lroundf(fp_wstepne), BS20, &f_start_fp, &krok_fp))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }
    KWARC_SkompensujUchwytWSkanie(f_start_fp, krok_fp);
    if (!KWARC_ZnajdzRezonansRownolegly(values, pan_maska_poprawnosci, WWIDTH + 1U,
                                        f_start_fp, krok_fp, fp_wstepne,
                                        &fp_dokladne))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }

    if (!KWARC_ObliczModel(fs_dokladne, fp_dokladne, rm_dokladne, c0_f, &kwarc_ostatni_wynik))
    {
        KOMUNIKAT_Pokaz(TEKST_BLAD, TEKST_KWARC_REZONANS_BLAD);
        return false;
    }

    kwarc_biezacy_dodany = 0U;
    KWARC_RysujWynik(&kwarc_ostatni_wynik);
    return true;
}

void Quartz_proc(void)
{
    pan_wykres_aktywny = 0U;
    QuartzSel = 0;
    /* Pomiar kwarcu nie może dziedziczyć przypadkowego zakresu z L/C lub
     * panoramy. Szeroki skan BVD ma stałe 1 MHz wokół f nominalnej. */
    span = BS1000;

    QuStep = 0;
    sCalib = 0;
    freqChg = 0;
    rqExitSWR = false;
    kwarc_pojemnosc_uchwytu_f = 0.0f;
    kwarc_przewodnosc_uchwytu_s = 0.0f;
    kwarc_f_pojemnosci_hz = 0U;
    kwarc_biezacy_dodany = 0U;
    kwarc_liczba_do_filtra = 4U;
    memset(&kwarc_ostatni_wynik, 0, sizeof(kwarc_ostatni_wynik));
    KWARC_SERIA_Inicjalizuj(&kwarc_seria);
    memset(&kwarc_dopasowanie, 0, sizeof(kwarc_dopasowanie));

    SetColours();
    BackGrColor = UI_KolorTlaEkranu();
    fxs = CFG_GetParam(CFG_PARAM_MEAS_F);
    fxkHzs = fxs / 1000U;
    f1 = fxs;
    KWARC_RysujKrokSterowania();

    while (TOUCH_IsPressed())
        ;

    for (;;)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();

        if (rqExitSWR || zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
        {
            GEN_SetMeasurementFreq(0);
            return;
        }

        if (zdarzenie == WEJSCIE_ZDARZENIE_OK || zdarzenie == WEJSCIE_ZDARZENIE_START_STOP)
        {
            if (QuStep == 3)
                KWARC_ObsluzAkcjeWyniku(KWARC_AKCJA_KOLEJNY);
            else
                QuNextStep();
        }

        if (TOUCH_Poll(&pt))
        {
            if (QuStep == 3)
            {
                const UI_AKCJA_t akcje[4] =
                {
                    { KWARC_AKCJA_DODAJ, "", UI_STYL_AKCENT,
                      !kwarc_biezacy_dodany && kwarc_seria.liczba < KWARC_SERIA_MAKS, false },
                    { KWARC_AKCJA_SERIA, "", UI_STYL_NORMALNY, true, false },
                    { KWARC_AKCJA_KOLEJNY, "", UI_STYL_AKTYWNY, true, false },
                    { KWARC_AKCJA_WSTECZ, "", UI_STYL_POWROT, true, false },
                };
                KWARC_ObsluzAkcjeWyniku(UI_ZnajdzAkcjePaska(pt, KWARC_PASEK_Y,
                                                            KWARC_PASEK_H, akcje, 4U));
            }
            else
            {
                UI_AKCJA_t akcje[3];
                uint8_t liczba = 0U;

                akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_WSTECZ, "", UI_STYL_POWROT, true, false};
                if (QuStep == 0)
                {
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_USTAW_F, "", UI_STYL_NORMALNY, true, false};
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_DALEJ, "", UI_STYL_AKCENT, true, false};
                }
                else if (QuStep == 1 && !sCalib)
                {
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_ZERUJ, "", UI_STYL_AKCENT, true, false};
                }
                else if (QuStep == 1)
                {
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_ZERUJ, "", UI_STYL_NORMALNY, true, false};
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_DALEJ, "", UI_STYL_AKCENT, true, false};
                }
                else
                {
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_USTAW_F, "", UI_STYL_NORMALNY, true, false};
                    akcje[liczba++] = (UI_AKCJA_t){KWARC_KROK_AKCJA_MIERZ, "", UI_STYL_AKCENT, true, false};
                }
                KWARC_ObsluzAkcjeKroku(UI_ZnajdzAkcjePaska(pt, KWARC_PASEK_Y, KWARC_PASEK_H, akcje, liczba));
            }
        }

        Sleep(5);
    }
}
