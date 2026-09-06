#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

//==============================================================
// KD8CEC'S DEBUG CODE
//DEBUGER OPTION
//must remark below 1line before release
//#define _DEBUG_UART
#ifdef _DEBUG_UART
void DBGUART_Init(void);
void DBG_Str(const char *str);
int DBG_Printf(const char *fmt, ...);
#endif
//==============================================================

#include "LCD.h"
#include "wersja_projektu.h"
#define AAVERSION "3.0d"      //Must be 4 characters
#define AAVERSION_CECV PROJEKT_WERSJA_KROTKA

/*bool DatumDDMMYYYY = true; */

#define DatumDDMMYYYY false

/*
 * V2.1: użytkowy zakres pomiarowy zaczyna się od 100 kHz. Niższe
 * częstotliwości działały generatorowo, ale testy wzorców wykazały zbyt dużą
 * niepewność korekcji, aby traktować je jako normalny zakres pomiarowy.
 */
#define BAND_FMIN 100000ul

//#define MAX_BAND_FREQ  450000000ul
//#define MAX_BAND_FREQ  600000000ul
//#define MAX_BAND_FREQ  890000000ul
#define MAX_BAND_FREQ (unsigned long)1450000000ul

#if (BAND_FMIN % 10000) != 0
#error "Incorrect band limit settings"
#endif

typedef enum
{
    CFG_SYNTH_SI5351 = 0,
    CFG_SYNTH_ADF4350 = 1,
    CFG_SYNTH_ADF4351 = 2,
    CFG_SYNTH_SI5338A = 3,
} CFG_SYNTH_TYPE_t;

typedef enum
{
    CFG_PROFIL_SPRZETU_WLASNY = 0,
    CFG_PROFIL_SPRZETU_EU1KY = 1,
    CFG_PROFIL_SPRZETU_MINI600 = 2,
    CFG_PROFIL_SPRZETU_MINI1300 = 3
} CFG_PROFIL_SPRZETU_t;

typedef enum
{
    CFG_BT_MODUL_BRAK = 0,
    CFG_BT_MODUL_HC05 = 1,
    CFG_BT_MODUL_HC06 = 2
} CFG_BT_MODUL_t;

typedef enum
{
    CFG_ADF_REF_SI5351_CLK2 = 0,
    CFG_ADF_REF_ZEWNETRZNE = 1
} CFG_ADF_REF_SOURCE_t;

typedef enum
{
    CFG_S1P_TYPE_S_MA = 0,
    CFG_S1P_TYPE_S_RI = 1,
    CFG_S1P_TYPE_Z_RI = 2,
} CFG_S1P_TYPE_t;

typedef enum
{
    CFG_PROTO_AA600 = 0,
    CFG_PROTO_LINSMITH = 1,
    CFG_PROTO_NANOVNA = 2,
    CFG_PROTO_MINIVNA = 3
} CFG_SEREMUL_TYPE_t;

typedef enum
{
    CFG_PARAM_VERSION,              //4 characters of version string
    CFG_PARAM_PAN_F1,               //Initial frequency for panoramic window
    CFG_PARAM_PAN_SPAN,             //Span for panoramic window
    CFG_PARAM_MEAS_F,               //Measurement window frequency
    CFG_PARAM_SYNTH_TYPE,           //Synthesizer type used: 0 - Si5351a
    CFG_PARAM_SI5351_XTAL_FREQ,     //Si5351a Xtal frequency, Hz
    CFG_PARAM_SI5351_BUS_BASE_ADDR, //Si5351a I2C bus base address
    CFG_PARAM_SI5351_CORR,          //Si5351a Xtal correction (signed, int16_t)
    CFG_PARAM_OSL_SELECTED,         //Selected OSL file
    CFG_PARAM_R0,                   //Base R0 for G measurements
    CFG_PARAM_OSL_RLOAD,            //RLOAD for OSL calibration
    CFG_PARAM_OSL_RSHORT,           //RSHORT for OSL calibration
    CFG_PARAM_OSL_ROPEN,            //ROPEN for OSL calibration
    CFG_PARAM_OSL_NSCANS,           //Number of scans to average during OSL
    CFG_PARAM_MEAS_NSCANS,          //Number of scans to average in measurement window
    CFG_PARAM_PAN_NSCANS,           //Number of scans to average in panoramic window
    CFG_PARAM_LIN_ATTENUATION,      //Linear audio input attenuation, dB
    CFG_PARAM_F_LO_DIV_BY_TWO,      //LO frequency is divided by two in quadrature mixer
    CFG_PARAM_GEN_F,                //Frequency for generator window, Hz
    CFG_PARAM_PAN_CENTER_F,         //Way of setting panoramic window. 0: F0+bandspan, 1: Fcenter +/- Bandspan/2
    CFG_PARAM_BRIDGE_RM,            //Value of measurement resistor in bridge, float32
    CFG_PARAM_BRIDGE_RADD,          //Value of series resistor in bridge, float32
    CFG_PARAM_BRIDGE_RLOAD,         //Value of load resistor in bridge, float32
    CFG_PARAM_COM_PORT,             //Serial (COM) port to be used: COM1 or COM2
    CFG_PARAM_COM_SPEED,            //Serial (COM) port speed, bps
    CFG_PARAM_LOWPWR_TIME,          //Time in milliseconds after which to lower power consumption mode (0 - disabled)
    CFG_PARAM_3RD_HARMONIC_ENABLED, //Enable setting frequency on 3rd harmonic (1) above BAND_FMAX, or disabe (0)
    CFG_PARAM_S11_SHOW,             //Show S11 graph in the panoramic window
    CFG_PARAM_S1P_TYPE,             //Type of Touchstone S1P file saved with panoramic screenshot
    CFG_PARAM_SHOW_HIDDEN,          //Show hidden options in configuration menu
    CFG_PARAM_SCREENSHOT_FORMAT,    //If 0, use BMP format for screenshots, otherwise use PNG
    CFG_PARAM_BAND_FMIN,            //Minimum frequency of the device's working band, Hz
    CFG_PARAM_BAND_FMAX,            //Maximum frequency of the device's working band, Hz
    CFG_PARAM_SI5351_MAX_FREQ,      // Maks. częstotliwość bezpośrednia Si5351; 160 MHz to bezpieczna wartość domyślna
    CFG_PARAM_SI5351_CAPS,          //Si5351a crystal capacitors setting
    CFG_PARAM_TDR_VF,               //Velocity factor for TDR, % (1..100)
    CFG_PARAM_MULTI_F1,             //Frequency 1 for multi SWR window
    CFG_PARAM_MULTI_F2,             //Frequency 2 for multi SWR window
    CFG_PARAM_MULTI_F3,             //Frequency 3 for multi SWR window
    CFG_PARAM_MULTI_F4,             //Frequency 4 for multi SWR window
    CFG_PARAM_MULTI_F5,             //Frequency 5 for multi SWR window
    CFG_PARAM_MULTI_BW1,            //Bandwidth 1 for multi SWR window
    CFG_PARAM_MULTI_BW2,            //Bandwidth 2 for multi SWR window
    CFG_PARAM_MULTI_BW3,            //Bandwidth 3 for multi SWR window
    CFG_PARAM_MULTI_BW4,            //Bandwidth 4 for multi SWR window
    CFG_PARAM_MULTI_BW5,            //Bandwidth 5 for multi SWR window
    CFG_PARAM_Volt_max,             //Maximum Voltage (with full Accu)
    CFG_PARAM_Volt_max_Display,     //Maximum displayed Voltage (with full Accu) 0 = Voltage Display off
    CFG_PARAM_Volt_max_Factor,      //Factor for correct Max Voltage (Accu has to be loaded)
    CFG_PARAM_Volt_min_Display,     //Minimum Voltage (Accu must be loaded immediately)
    CFG_PARAM_Daylight,             // Daylight (1)  Inhouse  (0)
    CFG_PARAM_Fatlines,             // Fat Lines (1) Thin Lines (0)
    CFG_PARAM_BeepOn,               // Beep on (1) Beep off (0)
    CFG_PARAM_Date,                 // Date yyyymmdd
    CFG_PARAM_Time,                 // Time hhmm

    //Added by KD8CEC
    CFG_PARAM_LC_OSL_SELECTED, //rezerwa historyczna formatu CFG; V2.1 L/C uzywa wspolnej OSL S11
    CFG_PARAM_LC_INDEX,        //Default Frequency Index for LC Meter

    CFG_PARAM_S21_F1,   //S21 Gain Frequency
    CFG_PARAM_S21_SPAN, //S21 Gain Span

    //Extend Configuration for Multi SWR window (Frequency Group)
    CFG_PARAM_MULTI_ANT, //Frequency group (as Antenna Index 0 ~ 4)

    //Antenna 2
    CFG_PARAM_MULTI_2F1,  //Frequency 1 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2F2,  //Frequency 2 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2F3,  //Frequency 3 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2F4,  //Frequency 4 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2F5,  //Frequency 5 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2BW1, //Bandwidth 1 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2BW2, //Bandwidth 2 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2BW3, //Bandwidth 3 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2BW4, //Bandwidth 4 for Antenna 2 in multi SWR window
    CFG_PARAM_MULTI_2BW5, //Bandwidth 5 for Antenna 2 in multi SWR window

    //Antenna 3
    CFG_PARAM_MULTI_3F1,  //Frequency 1 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3F2,  //Frequency 2 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3F3,  //Frequency 3 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3F4,  //Frequency 4 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3F5,  //Frequency 5 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3BW1, //Bandwidth 1 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3BW2, //Bandwidth 2 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3BW3, //Bandwidth 3 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3BW4, //Bandwidth 4 for Antenna 3 in multi SWR window
    CFG_PARAM_MULTI_3BW5, //Bandwidth 5 for Antenna 3 in multi SWR window

    //Antenna 4
    CFG_PARAM_MULTI_4F1,  //Frequency 1 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4F2,  //Frequency 2 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4F3,  //Frequency 3 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4F4,  //Frequency 4 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4F5,  //Frequency 5 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4BW1, //Bandwidth 1 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4BW2, //Bandwidth 2 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4BW3, //Bandwidth 3 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4BW4, //Bandwidth 4 for Antenna 4 in multi SWR window
    CFG_PARAM_MULTI_4BW5, //Bandwidth 5 for Antenna 4 in multi SWR window

    //Antenna 5
    CFG_PARAM_MULTI_5F1,  //Frequency 1 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5F2,  //Frequency 2 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5F3,  //Frequency 3 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5F4,  //Frequency 4 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5F5,  //Frequency 5 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5BW1, //Bandwidth 1 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5BW2, //Bandwidth 2 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5BW3, //Bandwidth 3 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5BW4, //Bandwidth 4 for Antenna 5 in multi SWR window
    CFG_PARAM_MULTI_5BW5, //Bandwidth 5 for Antenna 5 in multi SWR window

    //AUTO SPEED
    CFG_PARAM_PAN_AUTOSPEED, //Panorama Auto Speed
    CFG_PARAM_S21_AUTOSPEED, //Panorama Auto Speed

    // added by ve7it to support multiple serial remote control protocols
    CFG_PARAM_SEREMUL,
    //  add by DG2DRF to support Bluetooth
    CFG_PARAM_BT_SPEED,
    // add by DG2DRF to support Region change
    CFG_PARAM_REGION,
    // added by DH1AKF (05.10.2020)
    CFG_PARAM_ORIENTATION,
    CFG_PARAM_LOGLOG,
    CFG_PARAM_CURSOR,
    CFG_PARAM_ATTENUATOR,   // DH1AKF 03.11.2020
    CFG_PARAM_ShowLogoTime, // DH1AKF 16.11.2020
    CFG_PARAM_JEZYK,        // EU1KY-PL: jezyk interfejsu, 0=PL, 1=EN, 2=DE, 3=ES, 4=RU, 5=JP
    CFG_PARAM_ZNAK_0,       // znak krotkofalarski, bajty 0..3
    CFG_PARAM_ZNAK_1,       // bajty 4..7
    CFG_PARAM_ZNAK_2,       // bajty 8..11
    CFG_PARAM_ZNAK_3,       // bajty 12..15
    CFG_PARAM_TRYB_INTERFEJSU, // 0=zaawansowany, 1=prosty; dopisany na koncu dla zgodnosci pliku
    CFG_PARAM_UI_MOTYW,        // pole legacy; v2.02-test4 wymusza 0=klasyczny
    CFG_PARAM_TDR_VF_X10000,   // EU1KY-PL: Vf TDR z rozdzielczoscia 0,0001; 6600 = 0,6600
    CFG_PARAM_HARMONICZNA_MAX, // Maksymalna harmoniczna planu RF: 1/3/5/7
    CFG_PARAM_PORT_EXT_PS,      // EU1KY-PL test47: port extension, opoznienie w jedna strone [ps], 0=wylaczone
    CFG_PARAM_ZESTAW_IKON,      // 0=Retro, 1=Klasyczny niebieski
    CFG_PARAM_TDR_FILTR_CZASU,   // 0=wyl., 1=Savitzky-Golay, 2=Kalman, 3=SG+Kalman
    CFG_PARAM_TDR_POROWNAJ_FILTR,// 0=tylko wynik, 1=rysuj surowy + filtrowany
    CFG_PARAM_MASKA_MODELI_RF,   // bity POR_MODELI_*; domyslnie wszystkie
    CFG_PARAM_MASKA_METOD_Q,     // bity METODA_Q_*; domyslnie wszystkie
    CFG_PARAM_KABEL_PROFIL_AKTYWNY, // 0=wyl., 1=de-embedding aktywnego profilu kabla
    CFG_PARAM_OSL_RLOAD_MOHM,    // dokladna rezystancja wzorca srodkowego [mOhm]
    CFG_PARAM_OSL_RLOAD_ZRODLO,  // 0=wartosc nominalna/ustawienia, 1=zewnetrzny pomiar DC
    CFG_PARAM_OSL_RSHORT_MOHM,   // dokladna rezystancja wzorca niskiego [mOhm]
    CFG_PARAM_OSL_RSHORT_ZRODLO, // 0=wartosc nominalna/ustawienia, 1=zewnetrzny pomiar DC
    CFG_PARAM_OSL_ROPEN_MOHM,    // dokladna rezystancja wzorca wysokiego [mOhm]
    CFG_PARAM_OSL_ROPEN_ZRODLO,  // 0=wartosc nominalna/ustawienia, 1=zewnetrzny pomiar DC

    /* EU1KY-PL 2026 test15: sprzet, alternatywne syntezery i lacznosc.
     * Parametry sa dopisane WYŁĄCZNIE na koncu, aby nie zmieniac numerow
     * istniejacych pol config.bin. */
    CFG_PARAM_PROFIL_SPRZETU,       // 0=wlasny, 1=EU1KY, 2=Mini600, 3=Mini1300
    CFG_PARAM_SYNTH_AUTODETEKCJA,   // 0=recznie, 1=skanuj I2C w diagnostyce
    CFG_PARAM_SI5338_BUS_ADDR,      // 0=Auto, albo 8-bitowy adres BSP (E0h/E2h/wlasny)
    CFG_PARAM_SI5338_VCO_FREQ,      // rzeczywista czestotliwosc VCO/PLL Si5338 [Hz]
    CFG_PARAM_SI5338_FMAX,          // zweryfikowany limit wyjscia Si5338 [Hz]
    CFG_PARAM_SI5338_OUT_F0,        // wyjscie Si5338 0..3 dla F0
    CFG_PARAM_SI5338_OUT_LO,        // wyjscie Si5338 0..3 dla LO
    CFG_PARAM_ADF_REF_SOURCE,       // 0=Si5351 CLK2, 1=zewnetrzne odniesienie
    CFG_PARAM_ADF_REF_FREQ,         // czestotliwosc odniesienia ADF [Hz]
    CFG_PARAM_BT_MODUL,             // 0=brak, 1=HC-05, 2=HC-06

    /*
     * V2.1: trzy pozycje pozostają celowo jako rezerwa formatu config.bin.
     * Dawniej należały do opcjonalnego rozszerzenia sieciowego. Nie wolno ich usuwać ani
     * przesuwać, bo zmieniłoby to numery parametrów zapisanych na karcie SD.
     */
    CFG_PARAM_REZERWA_LACZNOSC_1,
    CFG_PARAM_REZERWA_LACZNOSC_2,
    CFG_PARAM_REZERWA_LACZNOSC_3,

    /*
     * V2.1 RFSCAN8: ostatni zakres skanera RF. Pola są dopisane wyłącznie
     * na końcu, więc nie zmieniają numerów żadnych istniejących parametrów.
     * Nie są pokazywane w edytorze konfiguracji — służą tylko do przywracania
     * ostatnio używanego zakresu po ponownym wejściu do Skanera RF / restarcie.
     */
    CFG_PARAM_SKANER_RF_FMIN_HZ,
    CFG_PARAM_SKANER_RF_FMAX_HZ,

    /* Pole zachowane wyłącznie dla zgodności formatu CFG ze starszym V2.1.
     * Produkcyjny firmware zawsze normalizuje je do 0. */
    CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY,

    /*
     * V2.1: niezależny rezystor kontrolny do weryfikacji toru pomiarowego.
     * Wartość jest zapisana w mOhm i celowo NIE jest częścią wzorców OSL.
     * Dzięki temu można sprawdzić interpolację pomiaru elementem, którego
     * analizator nie używał do wyznaczenia współczynników kalibracji.
     */
    CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM,

    /*
     * Kolor głównej krzywej na wykresie SWR w stylu retro. Dopisany na końcu
     * dla zgodności formatu pliku CFG ze starszymi zapisanymi konfiguracjami.
     */
    CFG_PARAM_KOLOR_KRZYWEJ_SWR, // 0=Stalowy (domyslny), 1=Zielony, 2=Bursztynowy, 3=Czerwony

    //For count of params ---------------------
    CFG_NUM_PARAMS
} CFG_PARAM_t;

/*
 * Ostatni parametr istniejący już w gałęzi v1.29-test32. Tę granicę wykorzystuje
 * jawna funkcja przygotowania powrotu do starszego firmware: starszy program
 * dostaje dokładnie taki surowy config.bin, jaki potrafił czytać, a dwa nowsze
 * parametry (Hmax i Port Extension) nie są wciskane do starego formatu.
 */
#define CFG_LEGACY_NUM_PARAMS ((uint32_t)CFG_PARAM_TDR_VF_X10000 + 1U)

extern const char *g_cfg_osldir;
extern const char *g_aa_dir;

extern uint8_t ColourSelection;
extern bool FatLines;
extern int BeepOn1;
extern uint32_t BackGrColor;
extern uint32_t CurvColor;
extern uint32_t TextColor;
extern uint32_t Color1;
extern uint32_t Color2;
extern uint32_t Color3;
extern uint32_t Color4;
extern void SetColours();

typedef enum
{
    CFG_SD_STAN_NIE_SPRAWDZONO = 0,
    CFG_SD_STAN_OK,
    CFG_SD_STAN_BLAD,
    CFG_SD_STAN_BRAK
} CFG_SD_STAN_t;

typedef enum
{
    CFG_SD_OPERACJA_BRAK = 0,
    CFG_SD_OPERACJA_MONTOWANIE,
    CFG_SD_OPERACJA_STAT_CONFIG,
    CFG_SD_OPERACJA_MKDIR_AA,
    CFG_SD_OPERACJA_MKDIR_CONFIG,
    CFG_SD_OPERACJA_OPEN_CONFIG,
    CFG_SD_OPERACJA_READ_CONFIG,
    CFG_SD_OPERACJA_OPEN_TMP,
    CFG_SD_OPERACJA_WRITE_HEADER,
    CFG_SD_OPERACJA_WRITE_DATA,
    CFG_SD_OPERACJA_SYNC_TMP,
    CFG_SD_OPERACJA_CLOSE_TMP,
    CFG_SD_OPERACJA_RENAME_BAK,
    CFG_SD_OPERACJA_RENAME_CONFIG
} CFG_SD_OPERACJA_t;

typedef struct
{
    CFG_SD_STAN_t karta_fizyczna;
    CFG_SD_STAN_t system_plikow;
    CFG_SD_STAN_t odczyt;
    CFG_SD_STAN_t zapis;
    CFG_SD_STAN_t katalog_aa;
    CFG_SD_STAN_t konfiguracja;
    uint8_t ostatni_blad_fatfs;
    CFG_SD_OPERACJA_t ostatnia_operacja;
} CFG_SD_DIAGNOSTYKA_t;

void CFG_SD_UstawStanStartowy(bool karta_fizyczna, uint8_t wynik_montowania_fatfs);
void CFG_SD_PobierzDiagnostyke(CFG_SD_DIAGNOSTYKA_t *stan);
const char *CFG_SD_NazwaBleduFatFs(uint8_t kod);
const char *CFG_SD_NazwaOperacji(CFG_SD_OPERACJA_t operacja);

void CFG_UstawDostepnoscKartySD(bool dostepna);
bool CFG_CzyKartaSDDostepna(void);
bool CFG_SD_SprobujPrzywrocic(void);
void CFG_Init(void);
uint32_t CFG_GetParam(CFG_PARAM_t param);
void CFG_SetParam(CFG_PARAM_t param, uint32_t value);
uint32_t CFG_GetOslRshortMilliOhm(void);
uint32_t CFG_GetOslRloadMilliOhm(void);
uint32_t CFG_GetOslRopenMilliOhm(void);
float CFG_GetOslRshortOhm(void);
float CFG_GetOslRloadOhm(void);
float CFG_GetOslRopenOhm(void);
bool CFG_CzyOslRshortZPomiaruZewnetrznego(void);
bool CFG_CzyOslRloadZPomiaruZewnetrznego(void);
bool CFG_CzyOslRopenZPomiaruZewnetrznego(void);
bool CFG_CzyOslWszystkieWzorceZPomiaruZewnetrznego(void);
bool CFG_UstawOslRshortZPomiaruZewnetrznego(uint32_t miliohm);
bool CFG_UstawOslRloadZPomiaruZewnetrznego(uint32_t miliohm);
bool CFG_UstawOslRopenZPomiaruZewnetrznego(uint32_t miliohm);
void CFG_FormatujOslRshort(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo);
void CFG_FormatujOslRload(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo);
void CFG_FormatujOslRopen(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo);
void CFG_Flush(void);
bool CFG_FlushSprawdzony(void);
bool CFG_PrzygotujPowrotDoStarszejWersji(void);
void CFG_ParamWnd(void);
void CFG_ParamWndOdParametru(CFG_PARAM_t parametr_startowy);

/*
 * Interfejs tylko do generatora dokumentacji. Nie zmienia konfiguracji.
 * Dzięki niemu automat może narysować dokładnie każdy parametr edytora
 * z tego samego katalogu, którego używa ekran interaktywny.
 */
uint32_t CFG_DokumentacjaLiczbaParametrow(void);
const char *CFG_DokumentacjaIdParametru(uint32_t param_idx);
const char *CFG_DokumentacjaNazwaParametru(uint32_t param_idx);
const char *CFG_DokumentacjaOpisParametru(uint32_t param_idx);
uint8_t CFG_DokumentacjaCzyZaawansowany(uint32_t param_idx);
void CFG_DokumentacjaRysujParametr(uint32_t param_idx);



#endif // _CONFIG_H_
