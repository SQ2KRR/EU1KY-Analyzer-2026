#include "config.h"
#include "ff.h"
#include "crash.h"
#include "gen.h"
#include "format_konfiguracji.h"
#include "jezyk.h"
#include "stm32746g_discovery_sd.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include <string.h>
#include <stdint.h>

static uint32_t g_cfg_array[CFG_NUM_PARAMS] = {0};
#define CFG_MULTI_DOMYSLNY_SPAN 5U /* BS100: 100 kHz */
#define CFG_MULTI_MAKS_SPAN 29U
const char *g_aa_dir = "/aa";
static const char *g_cfg_dir = "/aa/config";
static const char *g_cfg_fpath = "/aa/config/config.bin";
static const char *g_cfg_tmp_fpath = "/aa/config/config.tmp";
static const char *g_cfg_bak_fpath = "/aa/config/config.bak";
static const char *g_cfg_old_fpath = "/aa/config/config.old";
static const char *g_cfg_bad_fpath = "/aa/config/config.bad";
/*
 * FatFs pracuje bez LFN (_USE_LFN=0), dlatego wszystkie nazwy muszą mieścić
 * się w formacie 8.3. Dłuższe nazwy są czytelne w kodzie, ale na karcie SD
 * kończą się błędem nazwy i blokują transakcyjny zapis.
 */
static const char *g_cfg_v2_bak_fpath = "/aa/config/cfgv2.bak";
static const char *g_cfg_rollback_tmp_fpath = "/aa/config/cfgrb.tmp";

static bool g_karta_sd_dostepna = true;

/*
 * Stan karty i stan zapisu konfiguracji są celowo rozdzielone. Pojedynczy
 * FR_DISK_ERR podczas tworzenia config.tmp nie oznacza, że karta zniknęła.
 * Dzięki temu odczyt kalibracji i późniejsza ponowna próba zapisu pozostają
 * możliwe, a diagnostyka pokazuje rzeczywistą przyczynę problemu.
 */
static CFG_SD_DIAGNOSTYKA_t g_sd_diag = {
    .karta_fizyczna = CFG_SD_STAN_NIE_SPRAWDZONO,
    .system_plikow = CFG_SD_STAN_NIE_SPRAWDZONO,
    .odczyt = CFG_SD_STAN_NIE_SPRAWDZONO,
    .zapis = CFG_SD_STAN_NIE_SPRAWDZONO,
    .katalog_aa = CFG_SD_STAN_NIE_SPRAWDZONO,
    .konfiguracja = CFG_SD_STAN_NIE_SPRAWDZONO,
    .ostatni_blad_fatfs = (uint8_t)FR_OK,
    .ostatnia_operacja = CFG_SD_OPERACJA_BRAK,
};

extern FATFS SDFatFs;
extern char SDPath[4];
extern void Sleep(uint32_t nms);

const char *g_cfg_osldir = "/aa/osl";
static uint32_t resetRequired = 0;
uint8_t ColourSelection;
bool FatLines;
int BeepOn1;
static uint32_t rqExit;
uint32_t BackGrColor;
uint32_t CurvColor;
uint32_t TextColor;
uint32_t Color1;
uint32_t Color2;
uint32_t Color3;
uint32_t Color4;
uint32_t Color5;

typedef enum
{
    CFG_PARAM_T_U8,  //8-bit unsigned
    CFG_PARAM_T_U16, //16-bit unsigned
    CFG_PARAM_T_U32, //32-bit unsigned
    CFG_PARAM_T_S8,  //8-bit signed
    CFG_PARAM_T_S16, //16-bit signed
    CFG_PARAM_T_S32, //32-bit signed
    CFG_PARAM_T_F32, //32-bit float
    CFG_PARAM_T_CH,  //char**[]
} CFG_PARAM_TYPE_t;

typedef struct
{
    CFG_PARAM_t id;            //ID of the configuration parameter, see CFG_PARAM_t
    const char *idstring;      //Short parameter name to be displayed
    uint32_t nvalues;          //Number of values in allowed values array. Can be 0 if not relevant.
    const int32_t *values;     //Array of integer values that can be selected for parameter. Length is specified in .values
    const char **strvalues;    //Array of alternative string representations for values that can be selected for parameter. Length of the array must be in .values
    CFG_PARAM_TYPE_t type;     //Parameter value type, see CFG_PARAM_TYPE_t
    const char *dstring;       //Detailed description of the parameter
    uint32_t repeatdelay;      //Nonzero if continuous tap of value should be detected. Number of ms to sleep between callbacks
    uint32_t (*isvalid)(void); //Optional callback that can be defined. This function should return zero if parameter should not be displayed.
    uint32_t resetRequired;    //Nonzero if reset is required to apply parameter
} CFG_CHANGEABLE_PARAM_DESCR_t;

//Integer array macro
#define CFG_IARR(...) \
    (const int32_t[]) { __VA_ARGS__ }
//Character array macro
#define CFG_SARR(...) \
    (const char *[]) { __VA_ARGS__ }
//Float array macro
#define CFG_FARR(...) \
    (const float[]) { __VA_ARGS__ }

/*
//Callback that returns nonzero if Si5351 frequency synthesizer is selected
static uint32_t isSi5351(void)
{
    return (uint32_t)(CFG_SYNTH_SI5351 == CFG_GetParam(CFG_PARAM_SYNTH_TYPE));
}

//Callback that returns nonzero if ADF4350 frequency synthesizer is selected
static uint32_t isADF4350(void)
{
    return (uint32_t)(CFG_SYNTH_ADF4350 == CFG_GetParam(CFG_PARAM_SYNTH_TYPE));
}

//Callback that returns nonzero if ADF4351 frequency synthesizer is selected
static uint32_t isADF4351(void)
{
    return (uint32_t)(CFG_SYNTH_ADF4351 == CFG_GetParam(CFG_PARAM_SYNTH_TYPE));
}
*/

static uint32_t isShowHidden(void)
{
    return 1 == CFG_GetParam(CFG_PARAM_SHOW_HIDDEN);
}

static uint32_t isNigdyNiePokazuj(void)
{
    return 0U;
}

static uint32_t isSiReference(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);

    /*
     * Parametry źródła odniesienia są częścią jawnej „Konfiguracji
     * zaawansowanej”. Nie wymagamy już dodatkowego przełącznika
     * SHOW_HIDDEN: użytkownik, który świadomie wszedł do tego ekranu, ma
     * widzieć komplet nastaw potrzebnych do wybranego generatora.
     */
    return (typ == CFG_SYNTH_SI5351 || typ == CFG_SYNTH_ADF4350 || typ == CFG_SYNTH_ADF4351);
}

static uint32_t isSiOnly(void)
{
    return (CFG_SYNTH_SI5351 == CFG_GetParam(CFG_PARAM_SYNTH_TYPE));
}
//Array of user changeable parameters descriptors
static const CFG_CHANGEABLE_PARAM_DESCR_t cfg_ch_descr_table[] =
    {
        {.id = CFG_PARAM_OSL_SELECTED,
         .idstring = "OSL_SELECTED",
         .nvalues = 17,
         .values = CFG_IARR(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1),
         .strvalues = CFG_SARR("A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "None"),
         .type = CFG_PARAM_T_S32,
         .dstring = "Selected OSL file"},

        {
            .id = CFG_PARAM_SYNTH_TYPE,
            .idstring = "SYNTH_TYPE",
            .nvalues = 4,
            .values = CFG_IARR(CFG_SYNTH_SI5351, CFG_SYNTH_ADF4350, CFG_SYNTH_ADF4351, CFG_SYNTH_SI5338A),
            .strvalues = CFG_SARR("Si5351A", "2x ADF4350", "2x ADF4351", "Si5338A"),
            .type = CFG_PARAM_T_U32,
            .dstring = "RF synthesizer hardware. Use only hardware actually fitted to the analyzer. ADF variants require two PLL modules on SPI2 and use Si5351 CLK2 as 27 MHz reference.",
            .resetRequired = 1,
        },
        {
            .id = CFG_PARAM_SI5351_XTAL_FREQ,
            .idstring = "SI5351_XTAL_FREQ",
            .nvalues = 6,
            .values = CFG_IARR(25000000ul, 26000000ul, 27000000ul, 30000000ul, 32000000ul, 33000000ul),
            .type = CFG_PARAM_T_U32,
            .dstring = "Si5351 XTAL frequency, Hz",
            .isvalid = isSiReference,
        },
        {
            .id = CFG_PARAM_SI5351_BUS_BASE_ADDR,
            .idstring = "SI5351_BUS_BASE_ADDR",
            .nvalues = 5,
            .values = CFG_IARR(0x00, 0xC0, 0xC4, 0xCE, 0xDE),
            .strvalues = CFG_SARR("Auto", "C0h", "C4h", "CEh", "DEh"),
            .type = CFG_PARAM_T_U8,
            .dstring = "Si5351 I2C address; Auto checks C0h/C4h/CEh/DEh",
            .isvalid = isSiReference,
        },
        {
            .id = CFG_PARAM_SI5351_CORR,
            .idstring = "SI5351_CORR",
            .type = CFG_PARAM_T_S16,
            .dstring = "Si5351 XTAL frequency correction, Hz",
            .isvalid = isSiReference,
            .repeatdelay = 2, // Modifed for more speed adjust
        },
        {
            .id = CFG_PARAM_SI5351_MAX_FREQ,
            .idstring = "SI5351_MAX_FREQ",
            .type = CFG_PARAM_T_U32,
            .nvalues = 6, // was 2
            .strvalues = CFG_SARR("160 MHz", "200 MHz", "260 MHz", "270 MHz", "280 MHz", "290 MHz"),
            .values = CFG_IARR(160000000ul, 200000000ul, 260000000ul, 270000000ul, 280000000ul, 290000000ul),
            .dstring = "Maximum direct Si5351 output used by the measurement plan. 160 MHz is the conservative default; higher values require a new HW/OSL calibration and verification on standards.",
            .isvalid = isSiOnly,
        },
        {.id = CFG_PARAM_SI5351_CAPS,
         .idstring = "SI5351_CAPS",
         .type = CFG_PARAM_T_U8,
         .nvalues = 3,
         .strvalues = CFG_SARR("6 pF", "8 pF", "10 pF"),
         .values = CFG_IARR(1, 2, 3),
         .dstring = "Crystal Internal Load Capacitance. Recalibrate F if changed.",
         .isvalid = isSiReference,
         .resetRequired = 1},

        /*
         * Plan RF jest zebrany w jednym miejscu edytora. Kolejność tabeli
         * jest jednocześnie kolejnością przycisków „Poprzedni/Następny”,
         * dlatego nie rozrzucamy zakresu i harmonicznych po całym menu.
         */
        {
            .id = CFG_PARAM_HARMONICZNA_MAX,
            .idstring = "MEAS_MAX_HARMONIC",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(1, 3, 5, 7),
            .strvalues = CFG_SARR("H1 - direct", "H3 - recommended", "H5 - experimental", "H7 - experimental"),
            .dstring = "Highest odd harmonic allowed for RF measurement/generator extension. H3 is the conservative default; H5/H7 require verification on standards.",
            .isvalid = isSiOnly,
        },
        {.id = CFG_PARAM_BAND_FMIN,
         .idstring = "BAND_FMIN",
         .type = CFG_PARAM_T_U32,
         .nvalues = 5,
         .values = CFG_IARR(100000ul, 200000ul, 300000ul, 400000ul, 500000ul),
         .strvalues = CFG_SARR("100 kHz", "200 kHz*", "300 kHz*", "400 kHz*", "500 kHz*"),
         .dstring = "Lower frequency band limit. If changed, full recalibration is required.",
         .resetRequired = 1},
        {.id = CFG_PARAM_BAND_FMAX,
         .idstring = "BAND_FMAX",
         .type = CFG_PARAM_T_U32,
         .nvalues = 16, // ** WK **
         .values = CFG_IARR(150000000ul, 200000000ul, 300000000ul, 450000000ul, 480000000ul, 500000000ul, 550000000ul,
                            590000000ul, 600000000ul, 810000000ul, 870000000ul, 1000000000ul, 1300000000ul, 1350000000ul, 1400000000ul, 1450000000ul), // ** WK **
         .strvalues = CFG_SARR("150 MHz", "200 MHz*", "300 MHz*", "450 MHz*", "480 MHz*", "500 MHz*", "550 MHz*",
                               "590 MHz*", "600 MHz*", "810 MHz*", "870 MHz*", "1000 MHz*", "1300 MHz*", "1350 MHz*", "1400 MHz*", "1450 MHz*"), // ** WK **
         .dstring = "Upper measurement limit. Values above the verified range of a given hardware build are experimental; changing this setting requires OSL and verification on known standards.",
         .resetRequired = 1},
        {
            .id = CFG_PARAM_R0,
            .idstring = "Z0",
            .nvalues = 6,
            .values = CFG_IARR(28, 50, 75, 100, 150, 300),
            .type = CFG_PARAM_T_U32,
            .dstring = "Selected base impedance (Z0) for Smith Chart and VSWR"},
        {.id = CFG_PARAM_OSL_RLOAD,
         .idstring = "OSL_RLOAD",
         .nvalues = 4,
         .values = CFG_IARR(50, 75, 100, 150),
         .type = CFG_PARAM_T_U32,
         .dstring = "LOAD R for OSL calibration, Ohm"},
        {.id = CFG_PARAM_OSL_RSHORT,
         .idstring = "OSL_RSHORT",
         .nvalues = 3,
         .values = CFG_IARR(0, 5, 10),
         .type = CFG_PARAM_T_U32,
         .dstring = "SHORT R for OSL calibration, Ohm"},
        {.id = CFG_PARAM_OSL_ROPEN,
         .idstring = "OSL_ROPEN",
         .nvalues = 6,
         .values = CFG_IARR(300, 333, 500, 750, 1000, 999999),
         .strvalues = CFG_SARR("300", "333", "500", "750", "1000", "Open"),
         .type = CFG_PARAM_T_U32,
         .dstring = "OPEN R for OSL calibration, Ohm"},
        {.id = CFG_PARAM_OSL_NSCANS,
         .idstring = "OSL_NSCANS",
         .nvalues = 7,
         .values = CFG_IARR(1, 3, 5, 7, 9, 11, 15),
         .type = CFG_PARAM_T_U32,
         .dstring = "Number of scans to average during OSL calibration at each F"},
        {.id = CFG_PARAM_MEAS_NSCANS,
         .idstring = "MEAS_NSCANS",
         .nvalues = 7,
         .values = CFG_IARR(1, 3, 5, 7, 9, 11, 15),
         .type = CFG_PARAM_T_U32,
         .dstring = "Number of scans to average in measurement window"},
        {.id = CFG_PARAM_PAN_NSCANS,
         .idstring = "PAN_NSCANS",
         .nvalues = 7,
         .values = CFG_IARR(1, 3, 5, 7, 9, 11, 15),
         .type = CFG_PARAM_T_U32,
         .dstring = "Number of scans to average in panoramic window"},

        {.id = CFG_PARAM_PAN_AUTOSPEED,
         .idstring = "PAN AUTO SPEED",
         .nvalues = 7,
         .values = CFG_IARR(4, 8, 10, 12, 14, 16, 20),
         .type = CFG_PARAM_T_U32,
         .dstring = "Automatic measuring speed in panoramic"},
        {.id = CFG_PARAM_S21_AUTOSPEED,
         .idstring = "S21 AUTO SPEED",
         .nvalues = 7,
         .values = CFG_IARR(4, 8, 10, 12, 14, 16, 20),
         .type = CFG_PARAM_T_U32,
         .dstring = "Automatic measuring speed in VNA |S21| Gain"},
        {.id = CFG_PARAM_LIN_ATTENUATION,
         .idstring = "LIN_ATTENUATION",
         .nvalues = 11,
         .values = CFG_IARR(0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30),
         .type = CFG_PARAM_T_U8,
         .dstring = "Linear audio inputs attenuation, dB. Requires reset.",
         .isvalid = isShowHidden,
         .resetRequired = 1},
        {.id = CFG_PARAM_BRIDGE_RM,
         .idstring = "BRIDGE_RM",
         .nvalues = 4,
         .values = (int32_t *)CFG_FARR(1.f, 2.f, 5.1f, 10.f),
         .type = CFG_PARAM_T_F32,
         .dstring = "Bridge Rm value, Ohm",
         .isvalid = isShowHidden},
        {.id = CFG_PARAM_BRIDGE_RADD,
         .idstring = "BRIDGE_RADD",
         .nvalues = 7,
         .values = (int32_t *)CFG_FARR(33.f, 51.f, 75.f, 100.f, 120.f, 150.f, 200.f),
         .type = CFG_PARAM_T_F32,
         .dstring = "Bridge Radd value, Ohm",
         .isvalid = isShowHidden},
        {.id = CFG_PARAM_PAN_CENTER_F,
         .idstring = "PAN_CENTER_F",
         .nvalues = 2,
         .values = CFG_IARR(0, 1),
         .strvalues = CFG_SARR("Start F", "Center F"),
         .type = CFG_PARAM_T_U32,
         .dstring = "Select setting start or center F in panoramic window."},
        {.id = CFG_PARAM_COM_PORT,
         .idstring = "COM_PORT",
         .nvalues = 2,
         .values = CFG_IARR(COM1, COM2),
         .strvalues = CFG_SARR("COM1 (ST-Link)", "COM2 (on the shield)"),
         .type = CFG_PARAM_T_U32,
         .dstring = "Select serial port to be used for remote control. Requires reset.",
         .isvalid = isShowHidden,
         .resetRequired = 1},
        {.id = CFG_PARAM_COM_SPEED,
         .idstring = "COM_SPEED",
         .nvalues = 5,
         .values = CFG_IARR(9600, 19200, 38400, 57600, 115200),
         .type = CFG_PARAM_T_U32,
         .dstring = "Serial port baudrate. Requires reset.",
         .isvalid = isShowHidden,
         .resetRequired = 1},
        {.id = CFG_PARAM_LOWPWR_TIME,
         .idstring = "LOW POWER TIMER",
         .nvalues = 6,
         .values = CFG_IARR(0, 30000, 60000, 120000, 180000, 300000),
         .strvalues = CFG_SARR("Off", "30s", "1 min", "2 min", "3 min", "5 min"),
         .type = CFG_PARAM_T_U32,
         .dstring = "Enter low power mode (display off) after this period of inactivity. Tap to wake up."},
        {
            .id = CFG_PARAM_S11_SHOW,
            .idstring = "S11_GRAPH_SHOW",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("No", "Yes"),
            .dstring = "Set to Yes to show S11 graph in the panoramic window",
        },
        {
            .id = CFG_PARAM_S1P_TYPE,
            .idstring = "S1P FILE TYPE",
            .type = CFG_PARAM_T_U32,
            .nvalues = 3,
            .values = CFG_IARR(CFG_S1P_TYPE_S_MA, CFG_S1P_TYPE_S_RI, CFG_S1P_TYPE_Z_RI),
            .strvalues = CFG_SARR("S MA R 50", "S RI R 50", "Z RI R50"),
            .dstring = "Touchstone S1P file type saved with screenshot. Default is S MA R 50.",
        },
        {
            .id = CFG_PARAM_SCREENSHOT_FORMAT,
            .idstring = "SCREENSHOT_FORMAT",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("BMP", "PNG"),
            .isvalid = isShowHidden,
            .dstring = "Screenshot file format",
        },
        {
            .id = CFG_PARAM_TDR_VF,
            .idstring = "TDR Vf",
            .dstring = "Velocity factor for TDR, percent (1..100)",
            .type = CFG_PARAM_T_U8,
            .repeatdelay = 100,
        },
        {
            .id = CFG_PARAM_SHOW_HIDDEN,
            .idstring = "SHOW_HIDDEN",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("No", "Yes"),
            .dstring = "Show hidden menu parameters.",
        },
        {
            //additions 19.09.2020 DH1AKF (DG2DRF, VE7IT)
            .id = CFG_PARAM_SEREMUL, // added by VE7IT
            .idstring = "SERIAL_EMULATION",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(CFG_PROTO_AA600, CFG_PROTO_LINSMITH, CFG_PROTO_NANOVNA, CFG_PROTO_MINIVNA),
            .strvalues = CFG_SARR("AA600 / AntScope", "N2PK Linsmith", "NanoVNA", "miniVNA / VNA-J (exp.)"),
            .dstring = "Serial remote protocol emulation",
        },
        {.id = CFG_PARAM_BT_SPEED,
         .idstring = "BT_SPEED",
         .nvalues = 5,
         .values = CFG_IARR(9600, 19200, 38400, 57600, 115200),
         .type = CFG_PARAM_T_U32,
         .dstring = "Bluetooth baudrate. Requires reset.",
         .isvalid = isShowHidden,
         .resetRequired = 1},
        {
            .id = CFG_PARAM_REGION, // added by DG2DRF
            .idstring = "IARU Region  ",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(0, 1, 2, 3),
            .strvalues = CFG_SARR("REGION 1", "REGION 2", "REGION 3", "SRD"), // Short Range Devices
            .dstring = "Select IARU Region",
        },
        {
            .id = CFG_PARAM_ORIENTATION,         // added by DH1AKF
            .idstring = "Screen 0/180 degrees ", // 29.09.2020
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("0 degrees", "180 degrees"),
            .dstring = "Screen Orientation",
        },
        {
            .id = CFG_PARAM_LOGLOG,         // added by DH1AKF
            .idstring = "SWR Log / LogLog", // 05.10.2020
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("Logarithmic", "Double Log"),
            .dstring = "SWR scale",
        },
        {
            /*
             * Umieszczony zaraz obok skali wykresu SWR (a nie na końcu
             * długiej listy parametrów), żeby dało się go znaleźć kilkoma
             * naciśnięciami "Dalej" od już istniejącego, pokrewnego
             * ustawienia, zamiast przewijać przez dziesiątki innych pozycji.
             */
            .id = CFG_PARAM_KOLOR_KRZYWEJ_SWR,
            .idstring = "SWR_CURVE_COLOR",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(0, 1, 2, 3),
            .strvalues = CFG_SARR("Stalowy", "Zielony", "Bursztynowy", "Czerwony"),
            .dstring = "Kolor głównej krzywej na wykresie SWR w stylu retro.",
        },
        {
            .id = CFG_PARAM_CURSOR,  // added by DH1AKF
            .idstring = "Cursor",    // 05.10.2020
            .type = CFG_PARAM_T_U32, // extended 03.11.2020 (Strict Auto Cursor)
            .nvalues = 3,
            .values = CFG_IARR(0, 1, 2),
            .strvalues = CFG_SARR("No Auto Cursor", "Auto Cursor", "Strict Auto Cursor"),
            .dstring = "AutoCursor behavior",
        },
        {
            .id = CFG_PARAM_ATTENUATOR,       // added by DH1AKF
            .idstring = "Attenuator for S21", // 03.11.2020 / 19.11.2020
            .type = CFG_PARAM_T_U32,
            .nvalues = 7,
            .values = CFG_IARR(10, 16, 20, 30, 40, 50, 60),
            .strvalues = CFG_SARR("10 dB", "16 dB", "20 dB", "30 dB", "40 dB", "50 dB", "60 dB"),
            .dstring = "Calibrate S21 with additional Attenuator",
        },
        {
            .id = CFG_PARAM_JEZYK,
            .idstring = "Language / Język",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(0, 1, 2, 4),
            .strvalues = CFG_SARR("Polski", "English", "Deutsch", "Русский"),
            .dstring = "Język interfejsu / User interface language",
        },
        {
            .id = CFG_PARAM_TRYB_INTERFEJSU,
            .idstring = "Interface mode / Tryb",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("Advanced / Zaaw.", "Basic / Podstawowy"),
            .dstring = "Main interface mode / Tryb glownego interfejsu",
        },
        {
            .id = CFG_PARAM_ShowLogoTime, // added by DH1AKF
            .idstring = "Show Logo Time", // 16.11.2020
            .type = CFG_PARAM_T_U32,
            .nvalues = 7,
            .values = CFG_IARR(0, 5, 10, 20, 30, 60, 3600),
            .strvalues = CFG_SARR("0 s", "5 s", "10 s", "20 s", "30 s", "1 min", "1 hour"),
            .dstring = "Choose the time for logo view",
        },
        {
            /*
             * Pole pozostaje w tablicy i w formacie CFG dla zgodności ze
             * starszymi konfiguracjami. Od v2.02-test4 aktywny jest tylko
             * klasyczny wygląd, więc nie pokazujemy tego parametru w edytorze.
             */
            .id = CFG_PARAM_UI_MOTYW,
            .idstring = "UI theme / Motyw UI (legacy)",
            .type = CFG_PARAM_T_U32,
            .nvalues = 1,
            .values = CFG_IARR(0),
            .strvalues = CFG_SARR("Classic / Klasyczny"),
            .dstring = "Legacy field retained for configuration compatibility; classic UI only.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_PORT_EXT_PS,
            .idstring = "PORT_EXTENSION_PS",
            .type = CFG_PARAM_T_U32,
            .nvalues = 13,
            .values = CFG_IARR(0, 100, 250, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000),
            .strvalues = CFG_SARR("Off", "100 ps", "250 ps", "500 ps", "1 ns", "2 ns", "5 ns", "10 ns", "20 ns", "50 ns", "100 ns", "200 ns", "500 ns"),
            .dstring = "Electrical delay from the OSL reference plane to the DUT, one way. 0 disables correction. This changes phase only; cable loss is not compensated.",
        },
        {
            .id = CFG_PARAM_ZESTAW_IKON,
            .idstring = "ICON_SET",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("Retro", "Classic blue"),
            .dstring = "Two final interface styles: full Victorian/antique Retro or high-contrast Classic blue with white icons.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_TDR_FILTR_CZASU,
            .idstring = "TDR_TIME_FILTER",
            .type = CFG_PARAM_T_U32,
            .nvalues = 4,
            .values = CFG_IARR(0, 1, 2, 3),
            .strvalues = CFG_SARR("Off", "Savitzky-Golay", "Kalman", "Savitzky+Kalman"),
            .dstring = "Experimental post-IFFT TDR smoothing. It does not increase physical resolution.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_TDR_POROWNAJ_FILTR,
            .idstring = "TDR_COMPARE_FILTER",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("Off", "On"),
            .dstring = "Draw raw TDR response together with the filtered response.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_MASKA_MODELI_RF,
            .idstring = "RF_MODEL_MASK",
            .type = CFG_PARAM_T_U32,
            .nvalues = 1, .values = CFG_IARR(31), .strvalues = CFG_SARR("Custom"),
            .dstring = "Bit mask of RF-component models enabled in comparison.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_MASKA_METOD_Q,
            .idstring = "Q_METHOD_MASK",
            .type = CFG_PARAM_T_U32,
            .nvalues = 1, .values = CFG_IARR(15), .strvalues = CFG_SARR("Custom"),
            .dstring = "Bit mask of Q-estimation methods enabled in comparison.",
            .isvalid = isNigdyNiePokazuj,
        },
        {
            .id = CFG_PARAM_KABEL_PROFIL_AKTYWNY,
            .idstring = "CABLE_PROFILE_ACTIVE",
            .type = CFG_PARAM_T_U32,
            .nvalues = 2,
            .values = CFG_IARR(0, 1),
            .strvalues = CFG_SARR("Off", "On"),
            .dstring = "Experimental cable de-embedding using an OPEN/SHORT profile.",
            .isvalid = isNigdyNiePokazuj,
        },
};

static const uint32_t cfg_ch_descr_table_num = sizeof(cfg_ch_descr_table) / sizeof(CFG_CHANGEABLE_PARAM_DESCR_t);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"

static uint32_t CFG_WersjaProgramuU32(void)
{
    uint32_t wersja = 0;
    memcpy(&wersja, AAVERSION, sizeof(wersja));
    return wersja;
}

static void CFG_UstawDomyslne(void)
{
    memset(g_cfg_array, 0, sizeof(g_cfg_array));

    CFG_SetParam(CFG_PARAM_VERSION, CFG_WersjaProgramuU32());
    CFG_SetParam(CFG_PARAM_PAN_F1, 14000000ul);
    CFG_SetParam(CFG_PARAM_PAN_SPAN, 1ul);
    CFG_SetParam(CFG_PARAM_MEAS_F, 14000000ul);
    CFG_SetParam(CFG_PARAM_SYNTH_TYPE, 0);
    CFG_SetParam(CFG_PARAM_SI5351_XTAL_FREQ, 27000000ul);
    CFG_SetParam(CFG_PARAM_SI5351_BUS_BASE_ADDR, 0x00); // Auto: tylko znane adresy Si5351.
    CFG_SetParam(CFG_PARAM_SI5351_CORR, 0);
    CFG_SetParam(CFG_PARAM_OSL_SELECTED, 0ul);
    CFG_SetParam(CFG_PARAM_R0, 50);
    CFG_SetParam(CFG_PARAM_OSL_RLOAD, 50);
    CFG_SetParam(CFG_PARAM_OSL_RSHORT, 5);
    CFG_SetParam(CFG_PARAM_OSL_ROPEN, 500);
    CFG_SetParam(CFG_PARAM_OSL_NSCANS, 7);
    CFG_SetParam(CFG_PARAM_MEAS_NSCANS, 7);
    CFG_SetParam(CFG_PARAM_PAN_NSCANS, 3);
    CFG_SetParam(CFG_PARAM_LIN_ATTENUATION, 6);
    CFG_SetParam(CFG_PARAM_F_LO_DIV_BY_TWO, 0);
    CFG_SetParam(CFG_PARAM_GEN_F, 14000000ul);
    CFG_SetParam(CFG_PARAM_PAN_CENTER_F, 0);
    CFG_SetParam(CFG_PARAM_JEZYK, 0); // Polski jest jezykiem referencyjnym EU1KY-PL.
    CFG_SetParam(CFG_PARAM_TRYB_INTERFEJSU, 1); // Nowa konfiguracja startuje w spokojnym trybie podstawowym.

    float tmp = 5.1f;
    memcpy(&g_cfg_array[CFG_PARAM_BRIDGE_RM], &tmp, sizeof(tmp));
    tmp = 200.f;
    memcpy(&g_cfg_array[CFG_PARAM_BRIDGE_RADD], &tmp, sizeof(tmp));
    tmp = 51.f;
    memcpy(&g_cfg_array[CFG_PARAM_BRIDGE_RLOAD], &tmp, sizeof(tmp));

    CFG_SetParam(CFG_PARAM_COM_PORT, COM1);
    CFG_SetParam(CFG_PARAM_COM_SPEED, 38400);
    CFG_SetParam(CFG_PARAM_LOWPWR_TIME, 0);
    CFG_SetParam(CFG_PARAM_3RD_HARMONIC_ENABLED, 0);
    CFG_SetParam(CFG_PARAM_S11_SHOW, 1);
    CFG_SetParam(CFG_PARAM_S1P_TYPE, 0);
    CFG_SetParam(CFG_PARAM_SHOW_HIDDEN, 0);
    CFG_SetParam(CFG_PARAM_SCREENSHOT_FORMAT, 0);
    CFG_SetParam(CFG_PARAM_BAND_FMIN, BAND_FMIN);
    CFG_SetParam(CFG_PARAM_BAND_FMAX, GEN_SI5351_FMAX_EFEKTYWNE_HZ);
    CFG_SetParam(CFG_PARAM_SI5351_MAX_FREQ, GEN_SI5351_LIMIT_BEZPIECZNY_HZ);
    CFG_SetParam(CFG_PARAM_SI5351_CAPS, 3);
    CFG_SetParam(CFG_PARAM_TDR_VF, 66);
    CFG_SetParam(CFG_PARAM_Volt_max_Display, 4200);
    CFG_SetParam(CFG_PARAM_Volt_min_Display, 3200);
    CFG_SetParam(CFG_PARAM_Daylight, 0);
    CFG_SetParam(CFG_PARAM_Fatlines, 0);
    CFG_SetParam(CFG_PARAM_BeepOn, 1);
    CFG_SetParam(CFG_PARAM_Date, 20190322);
    CFG_SetParam(CFG_PARAM_Time, 1930);
    CFG_SetParam(CFG_PARAM_MULTI_F1, 3600);
    CFG_SetParam(CFG_PARAM_MULTI_BW1, CFG_MULTI_DOMYSLNY_SPAN);
    CFG_SetParam(CFG_PARAM_LC_OSL_SELECTED, 0);
    CFG_SetParam(CFG_PARAM_LC_INDEX, 2);
    CFG_SetParam(CFG_PARAM_S21_F1, 14000000ul);
    CFG_SetParam(CFG_PARAM_S21_SPAN, 1ul);
    CFG_SetParam(CFG_PARAM_MULTI_ANT, 0);
    CFG_SetParam(CFG_PARAM_MULTI_2F1, 14000);
    CFG_SetParam(CFG_PARAM_MULTI_2BW1, CFG_MULTI_DOMYSLNY_SPAN);
    CFG_SetParam(CFG_PARAM_MULTI_3F1, 14000);
    CFG_SetParam(CFG_PARAM_MULTI_3BW1, CFG_MULTI_DOMYSLNY_SPAN);
    CFG_SetParam(CFG_PARAM_MULTI_4F1, 14000);
    CFG_SetParam(CFG_PARAM_MULTI_4BW1, CFG_MULTI_DOMYSLNY_SPAN);
    CFG_SetParam(CFG_PARAM_MULTI_5F1, 14000);
    CFG_SetParam(CFG_PARAM_MULTI_5BW1, CFG_MULTI_DOMYSLNY_SPAN);
    CFG_SetParam(CFG_PARAM_PAN_AUTOSPEED, 8);
    CFG_SetParam(CFG_PARAM_S21_AUTOSPEED, 8);
    CFG_SetParam(CFG_PARAM_SEREMUL, 0);
    CFG_SetParam(CFG_PARAM_BT_SPEED, 9600);
    CFG_SetParam(CFG_PARAM_REGION, 0);
    CFG_SetParam(CFG_PARAM_ORIENTATION, 0);
    CFG_SetParam(CFG_PARAM_LOGLOG, 1);
    CFG_SetParam(CFG_PARAM_CURSOR, 1);
    CFG_SetParam(CFG_PARAM_ATTENUATOR, 40);
    CFG_SetParam(CFG_PARAM_ShowLogoTime, 10);
    CFG_SetParam(CFG_PARAM_UI_MOTYW, 0);
    CFG_SetParam(CFG_PARAM_TDR_VF_X10000, 6600U);
    CFG_SetParam(CFG_PARAM_HARMONICZNA_MAX, 3U);
    CFG_SetParam(CFG_PARAM_PORT_EXT_PS, 0U);
    CFG_SetParam(CFG_PARAM_ZESTAW_IKON, 0U);
    CFG_SetParam(CFG_PARAM_TDR_FILTR_CZASU, 0U);
    CFG_SetParam(CFG_PARAM_TDR_POROWNAJ_FILTR, 1U);
    CFG_SetParam(CFG_PARAM_MASKA_MODELI_RF, 0x1FU);
    CFG_SetParam(CFG_PARAM_MASKA_METOD_Q, 0x0FU);
    CFG_SetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY, 0U);
    CFG_SetParam(CFG_PARAM_OSL_RLOAD_MOHM, 50000U);
    CFG_SetParam(CFG_PARAM_OSL_RLOAD_ZRODLO, 0U);
    CFG_SetParam(CFG_PARAM_OSL_RSHORT_MOHM, 5000U);
    CFG_SetParam(CFG_PARAM_OSL_RSHORT_ZRODLO, 0U);
    CFG_SetParam(CFG_PARAM_OSL_ROPEN_MOHM, 500000U);
    CFG_SetParam(CFG_PARAM_OSL_ROPEN_ZRODLO, 0U);

    /* test15: profil plytki nie narzuca typu generatora. */
    CFG_SetParam(CFG_PARAM_PROFIL_SPRZETU, CFG_PROFIL_SPRZETU_EU1KY);
    CFG_SetParam(CFG_PARAM_SYNTH_AUTODETEKCJA, 1U);
    CFG_SetParam(CFG_PARAM_SI5338_BUS_ADDR, 0U);
    CFG_SetParam(CFG_PARAM_SI5338_VCO_FREQ, 2500000000U);
    CFG_SetParam(CFG_PARAM_SI5338_FMAX, 350000000U);
    CFG_SetParam(CFG_PARAM_SI5338_OUT_F0, 0U);
    CFG_SetParam(CFG_PARAM_SI5338_OUT_LO, 1U);
    CFG_SetParam(CFG_PARAM_ADF_REF_SOURCE, CFG_ADF_REF_SI5351_CLK2);
    CFG_SetParam(CFG_PARAM_ADF_REF_FREQ, 27000000U);
    CFG_SetParam(CFG_PARAM_BT_MODUL, CFG_BT_MODUL_BRAK);
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_1, 0U);
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_2, 0U);
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_3, 0U);
    CFG_SetParam(CFG_PARAM_SKANER_RF_FMIN_HZ, 3500000U);
    CFG_SetParam(CFG_PARAM_SKANER_RF_FMAX_HZ, 3900000U);
    CFG_SetParam(CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY, 0U);
    CFG_SetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM, 22000U);
    CFG_SetParam(CFG_PARAM_KOLOR_KRZYWEJ_SWR, 0U);
}

static void CFG_WalidujKrytyczne(void)
{
    /*
     * test23: numery 3 (dawny ES) i 5 (dawny JP) moga pozostac w starym
     * config.bin. Normalizujemy je do polskiego juz podczas wczytywania,
     * aby takze edytor konfiguracji widzial poprawna, dozwolona wartosc.
     * Rosyjski zachowuje historyczny numer 4.
     */
    {
        const uint32_t jezyk = CFG_GetParam(CFG_PARAM_JEZYK);
        if (jezyk != (uint32_t)JEZYK_POLSKI &&
            jezyk != (uint32_t)JEZYK_ANGIELSKI &&
            jezyk != (uint32_t)JEZYK_NIEMIECKI &&
            jezyk != (uint32_t)JEZYK_ROSYJSKI)
        {
            CFG_SetParam(CFG_PARAM_JEZYK, (uint32_t)JEZYK_POLSKI);
        }
    }

    /* test15: Si5338A ma rzeczywisty sterownik wyjsc MultiSynth. Wybór
     * pozostaje jawny, bo Mini600/Mini1300 nie identyfikuje rodzaju syntezera. */
    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) > CFG_SYNTH_SI5338A)
        CFG_SetParam(CFG_PARAM_SYNTH_TYPE, CFG_SYNTH_SI5351);

    if (CFG_GetParam(CFG_PARAM_SEREMUL) > CFG_PROTO_MINIVNA)
        CFG_SetParam(CFG_PARAM_SEREMUL, CFG_PROTO_AA600);

    if (CFG_GetParam(CFG_PARAM_BAND_FMAX) > MAX_BAND_FREQ)
        CFG_SetParam(CFG_PARAM_BAND_FMAX, MAX_BAND_FREQ);

    {
        const uint32_t max_si5351 = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
        if (max_si5351 != 160000000ul && max_si5351 != 200000000ul &&
            max_si5351 != 260000000ul && max_si5351 != 270000000ul &&
            max_si5351 != 280000000ul && max_si5351 != 290000000ul)
        {
            CFG_SetParam(CFG_PARAM_SI5351_MAX_FREQ, GEN_SI5351_LIMIT_BEZPIECZNY_HZ);
        }
    }

    /*
     * 160 MHz pozostaje konserwatywną wartością domyślną, lecz nie jest
     * wymuszana. Użytkownik może świadomie wybrać wyższy limit bezpośredni
     * i inną harmoniczną. Zmiana planu unieważnia metadane HW/OSL, a jego
     * rzeczywistą użyteczność potwierdza dopiero nowa kalibracja i wzorce.
     */
    if (CFG_GetParam(CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY) != 0U)
        CFG_SetParam(CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY, 0U);

    {
        const uint32_t rezystor_kontrolny_mohm =
            CFG_GetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM);
        if (rezystor_kontrolny_mohm < 100U || rezystor_kontrolny_mohm > 1000000000U)
            CFG_SetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM, 22000U);
    }

    {
        uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
        const uint8_t migracja_niskiego_pasma = (uint8_t)(fmin < BAND_FMIN);
        if (fmin < BAND_FMIN)
            fmin = BAND_FMIN;
        if (fmin > 500000ul)
            fmin = 500000ul;
        /* Edytor pracuje co 100 kHz; uszkodzony lub historyczny wpis
         * sprowadzamy do najbliższego niższego poprawnego kroku. */
        fmin = (fmin / 100000ul) * 100000ul;
        if (fmin < BAND_FMIN)
            fmin = BAND_FMIN;
        CFG_SetParam(CFG_PARAM_BAND_FMIN, fmin);

        /* Stara tabela L/C miała dodatkowy zakres 20..100 kHz. Przy
         * migracji z takiej konfiguracji zachowujemy ten sam fizyczny zakres
         * przez przesunięcie indeksu o jedną pozycję. */
        if (migracja_niskiego_pasma)
        {
            uint32_t indeks_lc = CFG_GetParam(CFG_PARAM_LC_INDEX);
            if (indeks_lc > 0U)
                --indeks_lc;
            if (indeks_lc > 4U)
                indeks_lc = 4U;
            CFG_SetParam(CFG_PARAM_LC_INDEX, indeks_lc);
        }
    }

    /*
     * Migracja dokładnie historycznego zestawu fabrycznego 3/1/1.
     * Innych wartości użytkownika nie zmieniamy. Testy V2.1 wykazały, że
     * 7/7/3 daje wyraźnie mniejszy rozrzut bez nadmiernego spowolnienia UI.
     */
    if (CFG_GetParam(CFG_PARAM_OSL_NSCANS) == 3U &&
        CFG_GetParam(CFG_PARAM_MEAS_NSCANS) == 1U &&
        CFG_GetParam(CFG_PARAM_PAN_NSCANS) == 1U)
    {
        CFG_SetParam(CFG_PARAM_OSL_NSCANS, 7U);
        CFG_SetParam(CFG_PARAM_MEAS_NSCANS, 7U);
        CFG_SetParam(CFG_PARAM_PAN_NSCANS, 3U);
    }

    if ((CFG_GetParam(CFG_PARAM_BAND_FMAX) <= BAND_FMIN) ||
        (CFG_GetParam(CFG_PARAM_BAND_FMAX) % 1000000ul != 0))
        CFG_SetParam(CFG_PARAM_BAND_FMAX, 150000000ul);
    if (CFG_GetParam(CFG_PARAM_TDR_VF) < 1 || CFG_GetParam(CFG_PARAM_TDR_VF) > 100)
        CFG_SetParam(CFG_PARAM_TDR_VF, 66);
    /* Dokladniejszy Vf jest dopisany na koncu konfiguracji, wiec stare pliki
       zachowuja zgodnosc. Gdy nowego pola brak lub jest uszkodzone, startujemy
       od historycznego procentowego Vf. */
    if (CFG_GetParam(CFG_PARAM_TDR_VF_X10000) < 100U ||
        CFG_GetParam(CFG_PARAM_TDR_VF_X10000) > 10000U)
        CFG_SetParam(CFG_PARAM_TDR_VF_X10000, CFG_GetParam(CFG_PARAM_TDR_VF) * 100U);

    {
        const uint32_t harmoniczna_max = CFG_GetParam(CFG_PARAM_HARMONICZNA_MAX);
        if (harmoniczna_max != 1U && harmoniczna_max != 3U &&
            harmoniczna_max != 5U && harmoniczna_max != 7U)
        {
            /* RF160: H3 daje bezpieczny zakres do 480 MHz przy limicie Si5351 160 MHz.
             * H5/H7 pozostaja swiadomym trybem eksperymentalnym. */
            CFG_SetParam(CFG_PARAM_HARMONICZNA_MAX, 3U);
        }
    }
    {
        const uint32_t opoznienie_ps = CFG_GetParam(CFG_PARAM_PORT_EXT_PS);
        /* 500 ns odpowiada już dziesiątkom metrów typowego kabla. Większa
         * wartość w pliku konfiguracji jest traktowana jako uszkodzony zapis,
         * a nie jako polecenie obrócenia fazy o przypadkową liczbę okresów. */
        if (opoznienie_ps > 500000U)
            CFG_SetParam(CFG_PARAM_PORT_EXT_PS, 0U);
    }
    if (CFG_GetParam(CFG_PARAM_PROFIL_SPRZETU) > CFG_PROFIL_SPRZETU_MINI1300)
        CFG_SetParam(CFG_PARAM_PROFIL_SPRZETU, CFG_PROFIL_SPRZETU_WLASNY);
    if (CFG_GetParam(CFG_PARAM_SYNTH_AUTODETEKCJA) > 1U)
        CFG_SetParam(CFG_PARAM_SYNTH_AUTODETEKCJA, 1U);
    {
        const uint32_t a = CFG_GetParam(CFG_PARAM_SI5338_BUS_ADDR);
        if (a != 0U && (a < 0x10U || a > 0xFEU || (a & 1U) != 0U))
            CFG_SetParam(CFG_PARAM_SI5338_BUS_ADDR, 0U);
    }
    {
        const uint32_t vco = CFG_GetParam(CFG_PARAM_SI5338_VCO_FREQ);
        if (vco < 2200000000U || vco > 2840000000U)
            CFG_SetParam(CFG_PARAM_SI5338_VCO_FREQ, 2500000000U);
    }
    {
        const uint32_t fmax = CFG_GetParam(CFG_PARAM_SI5338_FMAX);
        if (fmax < 5000000U || fmax > 710000000U)
            CFG_SetParam(CFG_PARAM_SI5338_FMAX, 350000000U);
    }
    if (CFG_GetParam(CFG_PARAM_SI5338_OUT_F0) > 3U)
        CFG_SetParam(CFG_PARAM_SI5338_OUT_F0, 0U);
    if (CFG_GetParam(CFG_PARAM_SI5338_OUT_LO) > 3U)
        CFG_SetParam(CFG_PARAM_SI5338_OUT_LO, 1U);
    if (CFG_GetParam(CFG_PARAM_SI5338_OUT_F0) == CFG_GetParam(CFG_PARAM_SI5338_OUT_LO))
        CFG_SetParam(CFG_PARAM_SI5338_OUT_LO, (CFG_GetParam(CFG_PARAM_SI5338_OUT_F0) + 1U) & 3U);
    if (CFG_GetParam(CFG_PARAM_ADF_REF_SOURCE) > CFG_ADF_REF_ZEWNETRZNE)
        CFG_SetParam(CFG_PARAM_ADF_REF_SOURCE, CFG_ADF_REF_SI5351_CLK2);
    {
        const uint32_t ref = CFG_GetParam(CFG_PARAM_ADF_REF_FREQ);
        if (ref < 1000000U || ref > 100000000U)
            CFG_SetParam(CFG_PARAM_ADF_REF_FREQ, 27000000U);
    }
    if (CFG_GetParam(CFG_PARAM_BT_MODUL) > CFG_BT_MODUL_HC06)
        CFG_SetParam(CFG_PARAM_BT_MODUL, CFG_BT_MODUL_BRAK);

    /* Dawne pola rozszerzenia bezprzewodowego pozostają tylko rezerwą formatu. */
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_1, 0U);
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_2, 0U);
    CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_3, 0U);

    /* Ostatni zakres Skanera RF ma być zawsze poprawny także po migracji
     * starszego config.bin albo zmianie limitu pasma urządzenia. */
    {
        uint32_t dol = CFG_GetParam(CFG_PARAM_SKANER_RF_FMIN_HZ);
        uint32_t gora = CFG_GetParam(CFG_PARAM_SKANER_RF_FMAX_HZ);
        const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
        const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

        if (dol < fmin || dol >= fmax || gora <= dol || gora > fmax)
        {
            dol = 3500000U;
            gora = 3900000U;
            if (dol < fmin) dol = fmin;
            if (gora > fmax) gora = fmax;
            if (gora <= dol)
            {
                dol = fmin;
                gora = fmax;
            }
            CFG_SetParam(CFG_PARAM_SKANER_RF_FMIN_HZ, dol);
            CFG_SetParam(CFG_PARAM_SKANER_RF_FMAX_HZ, gora);
        }
    }

    if (CFG_GetParam(CFG_PARAM_Volt_max_Display) != 0 &&
        (CFG_GetParam(CFG_PARAM_Volt_max_Display) < 2000 || CFG_GetParam(CFG_PARAM_Volt_max_Display) > 10000))
        CFG_SetParam(CFG_PARAM_Volt_max_Display, 4200);
    if (CFG_GetParam(CFG_PARAM_Volt_min_Display) < 1000 ||
        (CFG_GetParam(CFG_PARAM_Volt_max_Display) != 0 &&
         CFG_GetParam(CFG_PARAM_Volt_min_Display) >= CFG_GetParam(CFG_PARAM_Volt_max_Display)))
        CFG_SetParam(CFG_PARAM_Volt_min_Display, 3200);
    if (CFG_GetParam(CFG_PARAM_Volt_max_Factor) > 2000)
        CFG_SetParam(CFG_PARAM_Volt_max_Factor, 0);
    if (CFG_GetParam(CFG_PARAM_PAN_AUTOSPEED) < 4 || CFG_GetParam(CFG_PARAM_PAN_AUTOSPEED) > 20)
        CFG_SetParam(CFG_PARAM_PAN_AUTOSPEED, 8);
    if (CFG_GetParam(CFG_PARAM_S21_AUTOSPEED) < 4 || CFG_GetParam(CFG_PARAM_S21_AUTOSPEED) > 20)
        CFG_SetParam(CFG_PARAM_S21_AUTOSPEED, 8);

    /*
     * Multi SWR zapisuje numer pozycji z tablicy BANDSPAN. Stare ustawienia
     * domyslne 100000 byly wartoscia w Hz, a nie poprawnym indeksem.
     * Naprawiamy taki zapis i chronimy wszystkie piec zestawow przed
     * niekontrolowanym indeksem tablicy BSVALUES[].
     */
    const CFG_PARAM_t multi_bw_bazy[5] = {
        CFG_PARAM_MULTI_BW1, CFG_PARAM_MULTI_2BW1, CFG_PARAM_MULTI_3BW1,
        CFG_PARAM_MULTI_4BW1, CFG_PARAM_MULTI_5BW1};
    for (uint32_t zestaw = 0U; zestaw < 5U; ++zestaw)
    {
        for (uint32_t wiersz = 0U; wiersz < 5U; ++wiersz)
        {
            const CFG_PARAM_t id = (CFG_PARAM_t)(multi_bw_bazy[zestaw] + wiersz);
            const uint32_t wartosc = CFG_GetParam(id);
            if (wartosc == 100000U || wartosc > CFG_MULTI_MAKS_SPAN)
                CFG_SetParam(id, CFG_MULTI_DOMYSLNY_SPAN);
        }
    }
    if (CFG_GetParam(CFG_PARAM_MULTI_ANT) > 4U)
        CFG_SetParam(CFG_PARAM_MULTI_ANT, 0U);
    /*
     * Motyw pozostaje klasyczny, ale UI18 ponownie udostepnia dwa sposoby
     * rysowania ikon. Stare lub uszkodzone wartosci sa sprowadzane do
     * bezpiecznego domyslnego zestawu rc6-test17.
     */
    if (CFG_GetParam(CFG_PARAM_UI_MOTYW) != 0U)
        CFG_SetParam(CFG_PARAM_UI_MOTYW, 0U);
    if (CFG_GetParam(CFG_PARAM_ZESTAW_IKON) > 1U)
        CFG_SetParam(CFG_PARAM_ZESTAW_IKON, 0U);
    if (CFG_GetParam(CFG_PARAM_TDR_FILTR_CZASU) > 3U)
        CFG_SetParam(CFG_PARAM_TDR_FILTR_CZASU, 0U);
    if (CFG_GetParam(CFG_PARAM_TDR_POROWNAJ_FILTR) > 1U)
        CFG_SetParam(CFG_PARAM_TDR_POROWNAJ_FILTR, 1U);
    {
        const uint32_t maska = CFG_GetParam(CFG_PARAM_MASKA_MODELI_RF) & 0x1FU;
        CFG_SetParam(CFG_PARAM_MASKA_MODELI_RF, maska != 0U ? maska : 0x1FU);
    }
    {
        const uint32_t maska = CFG_GetParam(CFG_PARAM_MASKA_METOD_Q) & 0x0FU;
        CFG_SetParam(CFG_PARAM_MASKA_METOD_Q, maska != 0U ? maska : 0x0FU);
    }
    if (CFG_GetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY) > 1U)
        CFG_SetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY, 0U);
    {
        uint32_t rload_mohm = CFG_GetParam(CFG_PARAM_OSL_RLOAD_MOHM);
        if (rload_mohm < 1000U || rload_mohm > 1000000U)
        {
            rload_mohm = CFG_GetParam(CFG_PARAM_OSL_RLOAD) * 1000U;
            if (rload_mohm < 1000U || rload_mohm > 1000000U)
                rload_mohm = 50000U;
            CFG_SetParam(CFG_PARAM_OSL_RLOAD_MOHM, rload_mohm);
            CFG_SetParam(CFG_PARAM_OSL_RLOAD_ZRODLO, 0U);
        }
    }
    if (CFG_GetParam(CFG_PARAM_OSL_RLOAD_ZRODLO) > 1U)
        CFG_SetParam(CFG_PARAM_OSL_RLOAD_ZRODLO, 0U);
    {
        uint32_t rshort_mohm = CFG_GetParam(CFG_PARAM_OSL_RSHORT_MOHM);
        if (rshort_mohm > 1000000U)
        {
            const uint64_t z_legacy = (uint64_t)CFG_GetParam(CFG_PARAM_OSL_RSHORT) * 1000ULL;
            rshort_mohm = z_legacy <= 1000000ULL ? (uint32_t)z_legacy : 5000U;
            CFG_SetParam(CFG_PARAM_OSL_RSHORT_MOHM, rshort_mohm);
            CFG_SetParam(CFG_PARAM_OSL_RSHORT_ZRODLO, 0U);
        }
    }
    if (CFG_GetParam(CFG_PARAM_OSL_RSHORT_ZRODLO) > 1U)
        CFG_SetParam(CFG_PARAM_OSL_RSHORT_ZRODLO, 0U);
    {
        uint32_t ropen_mohm = CFG_GetParam(CFG_PARAM_OSL_ROPEN_MOHM);
        if (ropen_mohm < 1000U || ropen_mohm > 1000000000U)
        {
            const uint64_t z_legacy = (uint64_t)CFG_GetParam(CFG_PARAM_OSL_ROPEN) * 1000ULL;
            ropen_mohm = (z_legacy >= 1000ULL && z_legacy <= 1000000000ULL) ? (uint32_t)z_legacy : 500000U;
            CFG_SetParam(CFG_PARAM_OSL_ROPEN_MOHM, ropen_mohm);
            CFG_SetParam(CFG_PARAM_OSL_ROPEN_ZRODLO, 0U);
        }
    }
    if (CFG_GetParam(CFG_PARAM_OSL_ROPEN_ZRODLO) > 1U)
        CFG_SetParam(CFG_PARAM_OSL_ROPEN_ZRODLO, 0U);
}

static bool CFG_WczytajNowyFormat(FIL *plik)
{
    FORMAT_KONFIG_NAGLOWEK_t naglowek = {0};
    UINT odczytano = 0;

    if (f_read(plik, &naglowek, sizeof(naglowek), &odczytano) != FR_OK || odczytano != sizeof(naglowek))
        return false;

    if (!FORMAT_KONFIG_CzyNaglowekPoprawny(&naglowek, sizeof(g_cfg_array)))
        return false;

    uint32_t dane[CFG_NUM_PARAMS] = {0};
    if (f_read(plik, dane, naglowek.rozmiar_danych, &odczytano) != FR_OK || odczytano != naglowek.rozmiar_danych)
        return false;

    if (FORMAT_KONFIG_ObliczCRC32((const uint8_t *)dane, naglowek.rozmiar_danych) != naglowek.crc32)
        return false;

    memcpy(g_cfg_array, dane, naglowek.rozmiar_danych);

    /*
     * Pole Vf x10000 dopisano na końcu tablicy w test30. Jeżeli plik CFG1
     * pochodzi ze starszej wersji, zachowujemy ustawione przez użytkownika
     * procentowe Vf zamiast podstawiać nowe domyślne 0,6600.
     */
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_TDR_VF_X10000)
        CFG_SetParam(CFG_PARAM_TDR_VF_X10000, CFG_GetParam(CFG_PARAM_TDR_VF) * 100U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_HARMONICZNA_MAX)
        CFG_SetParam(CFG_PARAM_HARMONICZNA_MAX, 3U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_PORT_EXT_PS)
        CFG_SetParam(CFG_PARAM_PORT_EXT_PS, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_ZESTAW_IKON)
        CFG_SetParam(CFG_PARAM_ZESTAW_IKON, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_TDR_FILTR_CZASU)
        CFG_SetParam(CFG_PARAM_TDR_FILTR_CZASU, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_TDR_POROWNAJ_FILTR)
        CFG_SetParam(CFG_PARAM_TDR_POROWNAJ_FILTR, 1U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_MASKA_MODELI_RF)
        CFG_SetParam(CFG_PARAM_MASKA_MODELI_RF, 0x1FU);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_MASKA_METOD_Q)
        CFG_SetParam(CFG_PARAM_MASKA_METOD_Q, 0x0FU);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_KABEL_PROFIL_AKTYWNY)
        CFG_SetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_RLOAD_MOHM)
        CFG_SetParam(CFG_PARAM_OSL_RLOAD_MOHM, CFG_GetParam(CFG_PARAM_OSL_RLOAD) * 1000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_RLOAD_ZRODLO)
        CFG_SetParam(CFG_PARAM_OSL_RLOAD_ZRODLO, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_RSHORT_MOHM)
        CFG_SetParam(CFG_PARAM_OSL_RSHORT_MOHM, CFG_GetParam(CFG_PARAM_OSL_RSHORT) * 1000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_RSHORT_ZRODLO)
        CFG_SetParam(CFG_PARAM_OSL_RSHORT_ZRODLO, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_ROPEN_MOHM)
        CFG_SetParam(CFG_PARAM_OSL_ROPEN_MOHM, CFG_GetParam(CFG_PARAM_OSL_ROPEN) * 1000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_OSL_ROPEN_ZRODLO)
        CFG_SetParam(CFG_PARAM_OSL_ROPEN_ZRODLO, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_PROFIL_SPRZETU) CFG_SetParam(CFG_PARAM_PROFIL_SPRZETU, CFG_PROFIL_SPRZETU_EU1KY);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SYNTH_AUTODETEKCJA) CFG_SetParam(CFG_PARAM_SYNTH_AUTODETEKCJA, 1U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5338_BUS_ADDR) CFG_SetParam(CFG_PARAM_SI5338_BUS_ADDR, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5338_VCO_FREQ) CFG_SetParam(CFG_PARAM_SI5338_VCO_FREQ, 2500000000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5338_FMAX) CFG_SetParam(CFG_PARAM_SI5338_FMAX, 350000000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5338_OUT_F0) CFG_SetParam(CFG_PARAM_SI5338_OUT_F0, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5338_OUT_LO) CFG_SetParam(CFG_PARAM_SI5338_OUT_LO, 1U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_ADF_REF_SOURCE) CFG_SetParam(CFG_PARAM_ADF_REF_SOURCE, CFG_ADF_REF_SI5351_CLK2);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_ADF_REF_FREQ) CFG_SetParam(CFG_PARAM_ADF_REF_FREQ, 27000000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_BT_MODUL) CFG_SetParam(CFG_PARAM_BT_MODUL, CFG_BT_MODUL_BRAK);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_REZERWA_LACZNOSC_1) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_1, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_REZERWA_LACZNOSC_2) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_2, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_REZERWA_LACZNOSC_3) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_3, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SKANER_RF_FMIN_HZ) CFG_SetParam(CFG_PARAM_SKANER_RF_FMIN_HZ, 3500000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SKANER_RF_FMAX_HZ) CFG_SetParam(CFG_PARAM_SKANER_RF_FMAX_HZ, 3900000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY) CFG_SetParam(CFG_PARAM_SI5351_TRYB_EKSPERYMENTALNY, 0U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM)
        CFG_SetParam(CFG_PARAM_WERYFIKACJA_R_KONTROLNY_MOHM, 22000U);
    if (naglowek.liczba_parametrow <= (uint32_t)CFG_PARAM_KOLOR_KRZYWEJ_SWR)
        CFG_SetParam(CFG_PARAM_KOLOR_KRZYWEJ_SWR, 0U);

    CFG_SetParam(CFG_PARAM_VERSION, CFG_WersjaProgramuU32());
    return true;
}

static bool CFG_WczytajStaryFormat(FIL *plik, DWORD rozmiar_pliku)
{
    /*
     * Surowy format historyczny nie mial naglowka i nigdy nie mogl byc
     * dluzszy niz jawny zakres rollbacku. Wczesniej uszkodzony plik CFG1
     * (np. z przeklamanym magic) mogl zostac omylkowo uznany za stary raw,
     * bo czytalismy z niego tylko poczatek tablicy. To potrafilo zaladowac
     * przypadkowe wartosci zamiast uruchomic mechanizm odzyskiwania.
     */
    if (rozmiar_pliku == 0U ||
        (rozmiar_pliku & 3U) != 0U ||
        rozmiar_pliku > (DWORD)(CFG_LEGACY_NUM_PARAMS * sizeof(uint32_t)))
        return false;

    const UINT do_odczytu = (UINT)rozmiar_pliku;
    UINT odczytano = 0;

    if (f_lseek(plik, 0) != FR_OK)
        return false;
    if (f_read(plik, g_cfg_array, do_odczytu, &odczytano) != FR_OK || odczytano != do_odczytu)
        return false;

    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_TDR_VF_X10000 * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_TDR_VF_X10000, CFG_GetParam(CFG_PARAM_TDR_VF) * 100U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_HARMONICZNA_MAX * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_HARMONICZNA_MAX, 3U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_PORT_EXT_PS * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_PORT_EXT_PS, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_ZESTAW_IKON * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_ZESTAW_IKON, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_TDR_FILTR_CZASU * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_TDR_FILTR_CZASU, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_TDR_POROWNAJ_FILTR * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_TDR_POROWNAJ_FILTR, 1U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_MASKA_MODELI_RF * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_MASKA_MODELI_RF, 0x1FU);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_MASKA_METOD_Q * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_MASKA_METOD_Q, 0x0FU);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_KABEL_PROFIL_AKTYWNY * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_KABEL_PROFIL_AKTYWNY, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_RLOAD_MOHM * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_RLOAD_MOHM, CFG_GetParam(CFG_PARAM_OSL_RLOAD) * 1000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_RLOAD_ZRODLO * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_RLOAD_ZRODLO, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_RSHORT_MOHM * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_RSHORT_MOHM, CFG_GetParam(CFG_PARAM_OSL_RSHORT) * 1000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_RSHORT_ZRODLO * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_RSHORT_ZRODLO, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_ROPEN_MOHM * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_ROPEN_MOHM, CFG_GetParam(CFG_PARAM_OSL_ROPEN) * 1000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_OSL_ROPEN_ZRODLO * sizeof(uint32_t)))
        CFG_SetParam(CFG_PARAM_OSL_ROPEN_ZRODLO, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_PROFIL_SPRZETU * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_PROFIL_SPRZETU, CFG_PROFIL_SPRZETU_EU1KY);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SYNTH_AUTODETEKCJA * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SYNTH_AUTODETEKCJA, 1U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SI5338_BUS_ADDR * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SI5338_BUS_ADDR, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SI5338_VCO_FREQ * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SI5338_VCO_FREQ, 2500000000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SI5338_FMAX * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SI5338_FMAX, 350000000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SI5338_OUT_F0 * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SI5338_OUT_F0, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SI5338_OUT_LO * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SI5338_OUT_LO, 1U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_ADF_REF_SOURCE * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_ADF_REF_SOURCE, CFG_ADF_REF_SI5351_CLK2);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_ADF_REF_FREQ * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_ADF_REF_FREQ, 27000000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_BT_MODUL * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_BT_MODUL, CFG_BT_MODUL_BRAK);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_REZERWA_LACZNOSC_1 * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_1, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_REZERWA_LACZNOSC_2 * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_2, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_REZERWA_LACZNOSC_3 * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_REZERWA_LACZNOSC_3, 0U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SKANER_RF_FMIN_HZ * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SKANER_RF_FMIN_HZ, 3500000U);
    if (rozmiar_pliku <= (DWORD)((uint32_t)CFG_PARAM_SKANER_RF_FMAX_HZ * sizeof(uint32_t))) CFG_SetParam(CFG_PARAM_SKANER_RF_FMAX_HZ, 3900000U);

    CFG_SetParam(CFG_PARAM_VERSION, CFG_WersjaProgramuU32());
    return true;
}

static void CFG_ZachowajUszkodzonyPlik(void)
{
    if (!g_karta_sd_dostepna)
        return;

    (void)f_unlink(g_cfg_bad_fpath);
    (void)f_rename(g_cfg_fpath, g_cfg_bad_fpath);
}

static void CFG_SD_UstawBlad(CFG_SD_OPERACJA_t operacja, FRESULT wynik)
{
    g_sd_diag.ostatnia_operacja = operacja;
    g_sd_diag.ostatni_blad_fatfs = (uint8_t)wynik;
}

static bool CFG_SD_UtworzLubPotwierdzKatalog(const char *sciezka,
                                              CFG_SD_OPERACJA_t operacja,
                                              CFG_SD_STAN_t *stan)
{
    FRESULT wynik = f_mkdir(sciezka);

    if (wynik == FR_OK || wynik == FR_EXIST)
    {
        if (stan != NULL)
            *stan = CFG_SD_STAN_OK;
        return true;
    }

    if (stan != NULL)
        *stan = CFG_SD_STAN_BLAD;
    CFG_SD_UstawBlad(operacja, wynik);
    return false;
}

static bool CFG_SD_PrzygotujKatalogi(void)
{
    if (!CFG_SD_UtworzLubPotwierdzKatalog(g_aa_dir,
                                           CFG_SD_OPERACJA_MKDIR_AA,
                                           &g_sd_diag.katalog_aa))
        return false;

    if (!CFG_SD_UtworzLubPotwierdzKatalog(g_cfg_dir,
                                           CFG_SD_OPERACJA_MKDIR_CONFIG,
                                           NULL))
        return false;

    return true;
}

void CFG_SD_UstawStanStartowy(bool karta_fizyczna, uint8_t wynik_montowania_fatfs)
{
    const FRESULT wynik = (FRESULT)wynik_montowania_fatfs;

    memset(&g_sd_diag, 0, sizeof(g_sd_diag));
    g_sd_diag.karta_fizyczna = karta_fizyczna ? CFG_SD_STAN_OK : CFG_SD_STAN_BRAK;
    g_sd_diag.system_plikow = (wynik == FR_OK) ? CFG_SD_STAN_OK :
                              (karta_fizyczna ? CFG_SD_STAN_BLAD : CFG_SD_STAN_BRAK);
    g_sd_diag.odczyt = CFG_SD_STAN_NIE_SPRAWDZONO;
    g_sd_diag.zapis = CFG_SD_STAN_NIE_SPRAWDZONO;
    g_sd_diag.katalog_aa = CFG_SD_STAN_NIE_SPRAWDZONO;
    g_sd_diag.konfiguracja = CFG_SD_STAN_NIE_SPRAWDZONO;
    g_sd_diag.ostatni_blad_fatfs = (uint8_t)wynik;
    g_sd_diag.ostatnia_operacja = (wynik == FR_OK) ? CFG_SD_OPERACJA_BRAK : CFG_SD_OPERACJA_MONTOWANIE;
    g_karta_sd_dostepna = (wynik == FR_OK);
}

void CFG_SD_PobierzDiagnostyke(CFG_SD_DIAGNOSTYKA_t *stan)
{
    if (stan != NULL)
        *stan = g_sd_diag;
}

const char *CFG_SD_NazwaBleduFatFs(uint8_t kod)
{
    switch ((FRESULT)kod)
    {
    case FR_OK: return "FR_OK";
    case FR_DISK_ERR: return "FR_DISK_ERR";
    case FR_INT_ERR: return "FR_INT_ERR";
    case FR_NOT_READY: return "FR_NOT_READY";
    case FR_NO_FILE: return "FR_NO_FILE";
    case FR_NO_PATH: return "FR_NO_PATH";
    case FR_INVALID_NAME: return "FR_INVALID_NAME";
    case FR_DENIED: return "FR_DENIED";
    case FR_EXIST: return "FR_EXIST";
    case FR_INVALID_OBJECT: return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED: return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE: return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED: return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM: return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED: return "FR_MKFS_ABORTED";
    case FR_TIMEOUT: return "FR_TIMEOUT";
    case FR_LOCKED: return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE: return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER: return "FR_INVALID_PARAMETER";
    default: return "FR_?";
    }
}

const char *CFG_SD_NazwaOperacji(CFG_SD_OPERACJA_t operacja)
{
    switch (operacja)
    {
    case CFG_SD_OPERACJA_BRAK: return "-";
    case CFG_SD_OPERACJA_MONTOWANIE: return "f_mount";
    case CFG_SD_OPERACJA_STAT_CONFIG: return "f_stat config.bin";
    case CFG_SD_OPERACJA_MKDIR_AA: return "f_mkdir /aa";
    case CFG_SD_OPERACJA_MKDIR_CONFIG: return "f_mkdir /aa/config";
    case CFG_SD_OPERACJA_OPEN_CONFIG: return "f_open config.bin";
    case CFG_SD_OPERACJA_READ_CONFIG: return "f_read config.bin";
    case CFG_SD_OPERACJA_OPEN_TMP: return "f_open config.tmp";
    case CFG_SD_OPERACJA_WRITE_HEADER: return "f_write naglowek";
    case CFG_SD_OPERACJA_WRITE_DATA: return "f_write dane";
    case CFG_SD_OPERACJA_SYNC_TMP: return "f_sync config.tmp";
    case CFG_SD_OPERACJA_CLOSE_TMP: return "f_close config.tmp";
    case CFG_SD_OPERACJA_RENAME_BAK: return "f_rename config.bak";
    case CFG_SD_OPERACJA_RENAME_CONFIG: return "f_rename config.bin";
    default: return "?";
    }
}

void CFG_UstawDostepnoscKartySD(bool dostepna)
{
    g_karta_sd_dostepna = dostepna;
    g_sd_diag.system_plikow = dostepna ? CFG_SD_STAN_OK :
                              (g_sd_diag.karta_fizyczna == CFG_SD_STAN_BRAK ?
                               CFG_SD_STAN_BRAK : CFG_SD_STAN_NIE_SPRAWDZONO);
}

bool CFG_CzyKartaSDDostepna(void)
{
    return g_karta_sd_dostepna;
}

bool CFG_SD_SprobujPrzywrocic(void)
{
    FRESULT wynik = FR_NOT_READY;
    uint8_t proba;

    /*
     * Ta funkcja jest celowo „twardym” odzyskaniem karty. Używamy jej tylko
     * wtedy, gdy firmware już uważa wolumin za niedostępny. Dzięki temu nie
     * odmontowujemy działającej karty podczas normalnej pracy.
     *
     * Kolejność ma znaczenie: najpierw odpinamy stary obiekt FatFs i sterownik,
     * potem resetujemy BSP SD, ponownie podpinamy sterownik i wymuszamy realny
     * mount (opt=1). FR_NOT_READY po starcie lub po USB nie może więc zostać
     * „zapamiętany” na zawsze aż do restartu analizatora.
     */
    if (g_karta_sd_dostepna)
        return true;

    for (proba = 0U; proba < 2U; ++proba)
    {
        (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
        (void)FATFS_UnLinkDriver(SDPath);
        BSP_SD_DeInit();
        Sleep((proba == 0U) ? 80U : 180U);

        if (FATFS_LinkDriver(&SD_Driver, SDPath) != 0U)
        {
            wynik = FR_NOT_ENABLED;
            continue;
        }

        wynik = f_mount(&SDFatFs, (TCHAR const *)SDPath, 1);
        CFG_SD_UstawStanStartowy(BSP_SD_IsDetected() == SD_PRESENT, (uint8_t)wynik);
        if (wynik == FR_OK)
            return true;

        (void)FATFS_UnLinkDriver(SDPath);
    }

    CFG_SD_UstawStanStartowy(BSP_SD_IsDetected() == SD_PRESENT, (uint8_t)wynik);
    return false;
}

static bool CFG_ZapiszKonfiguracjeRaz(void);

/*
 * Odczyt awaryjnej kopii CFG1. Kopii zapasowych nie interpretujemy jako
 * historycznego RAW: config.tmp/config.old/config.bak powstaja w obecnym
 * formacie z naglowkiem i CRC, wiec brak poprawnego CRC oznacza, ze kopii
 * nie wolno uzyc.
 */
static bool CFG_WczytajKopieCFG1(const char *sciezka)
{
    FILINFO info = {0};
    FIL plik = {0};
    FRESULT wynik;
    bool poprawny = false;

    wynik = f_stat(sciezka, &info);
    if (wynik != FR_OK || info.fsize < sizeof(FORMAT_KONFIG_NAGLOWEK_t))
        return false;

    wynik = f_open(&plik, sciezka, FA_READ | FA_OPEN_EXISTING);
    if (wynik != FR_OK)
        return false;

    CFG_UstawDomyslne();
    poprawny = CFG_WczytajNowyFormat(&plik);
    (void)f_close(&plik);
    return poprawny;
}

/*
 * Proba odzyskania ustawien po przerwanym zapisie albo uszkodzeniu glownego
 * config.bin. Kolejnosc jest celowa:
 *   config.tmp - najnowszy, w pelni zapisany i zsynchronizowany kandydat,
 *   config.old - poprzedni config.bin zachowany podczas transakcji,
 *   config.bak - ostatnia zakonczona kopia zapasowa.
 *
 * Po poprawnym odczycie odtwarzamy config.bin z danych w RAM. Zaden poprawny
 * backup nie jest kasowany zanim nowy config.bin nie zostanie zapisany.
 */
static bool CFG_SprobujOdzyskacKonfiguracje(void)
{
    const char *kandydaci[] = { g_cfg_tmp_fpath, g_cfg_old_fpath, g_cfg_bak_fpath };

    for (uint32_t i = 0U; i < (uint32_t)(sizeof(kandydaci) / sizeof(kandydaci[0])); ++i)
    {
        if (CFG_WczytajKopieCFG1(kandydaci[i]))
        {
            /*
             * Nie przepisujemy odzyskanej kopii przez zwykly Flush, bo
             * mechanizm rotacji moglby skasowac plik, z ktorego wlasnie
             * odzyskujemy dane. Po walidacji CRC wystarczy atomowy rename.
             * Nawet gdy rename chwilowo sie nie uda, pozostawiamy poprawne
             * ustawienia w RAM i NIE zastepujemy ich domyslnymi.
             */
            (void)f_unlink(g_cfg_fpath);
            if (f_rename(kandydaci[i], g_cfg_fpath) == FR_OK)
                return true;

            return true;
        }
    }

    CFG_UstawDomyslne();
    return false;
}

static bool CFG_ZapiszKonfiguracjeRaz(void)
{
    FORMAT_KONFIG_NAGLOWEK_t naglowek = {0};
    FIL plik = {0};
    FRESULT wynik;
    FRESULT wynik_zamkniecia;
    UINT zapisano = 0U;

    if (!CFG_SD_PrzygotujKatalogi())
    {
        g_sd_diag.zapis = CFG_SD_STAN_BLAD;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        return false;
    }

    /*
     * Po utworzeniu katalogow odmontowujemy i ponownie montujemy wolumin.
     * Na swiezej karcie eliminuje to zaleznosc pierwszej alokacji pliku od
     * stanu FatFs pozostawionego bezposrednio po f_mkdir().
     */
    (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
    wynik = f_mount(&SDFatFs, (TCHAR const *)SDPath, 1);
    if (wynik != FR_OK)
    {
        g_sd_diag.system_plikow = CFG_SD_STAN_BLAD;
        g_sd_diag.zapis = CFG_SD_STAN_BLAD;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_MONTOWANIE, wynik);
        return false;
    }
    g_sd_diag.system_plikow = CFG_SD_STAN_OK;

    FORMAT_KONFIG_UtworzNaglowek(&naglowek, CFG_NUM_PARAMS,
                                 (const uint8_t *)g_cfg_array, sizeof(g_cfg_array));

    wynik = f_open(&plik, g_cfg_tmp_fpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (wynik != FR_OK)
    {
        g_sd_diag.zapis = CFG_SD_STAN_BLAD;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_OPEN_TMP, wynik);
        return false;
    }

    wynik = f_write(&plik, &naglowek, sizeof(naglowek), &zapisano);
    if (wynik != FR_OK || zapisano != sizeof(naglowek))
    {
        if (wynik == FR_OK)
            wynik = FR_DENIED;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_WRITE_HEADER, wynik);
        goto blad_zapisu;
    }

    zapisano = 0U;
    wynik = f_write(&plik, g_cfg_array, sizeof(g_cfg_array), &zapisano);
    if (wynik != FR_OK || zapisano != sizeof(g_cfg_array))
    {
        if (wynik == FR_OK)
            wynik = FR_DENIED;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_WRITE_DATA, wynik);
        goto blad_zapisu;
    }

    wynik = f_sync(&plik);
    if (wynik != FR_OK)
    {
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_SYNC_TMP, wynik);
        goto blad_zapisu;
    }

    wynik_zamkniecia = f_close(&plik);
    memset(&plik, 0, sizeof(plik));
    if (wynik_zamkniecia != FR_OK)
    {
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_CLOSE_TMP, wynik_zamkniecia);
        goto blad_po_zamknieciu;
    }

    /*
     * Transakcja odporna na przerwanie zasilania:
     * 1. nowy plik jest juz kompletny i zsynchronizowany jako config.tmp,
     * 2. dotychczasowy config.bin przesuwamy do config.old,
     * 3. dopiero potem podstawiamy config.tmp jako config.bin,
     * 4. po sukcesie poprzedni config.old staje sie config.bak.
     *
     * W kazdym momencie pozostaje co najmniej jedna pelna kopia, ktora
     * CFG_Init() potrafi odzyskac. Wczesniejszy kod najpierw kasowal
     * config.bak; przy uszkodzonym config.bin moglo to zniszczyc jedyna dobra
     * kopie i zapisac ustawienia domyslne.
     */
    {
        bool byl_stary_config = false;

        wynik = f_unlink(g_cfg_old_fpath);
        if (wynik != FR_OK && wynik != FR_NO_FILE)
        {
            CFG_SD_UstawBlad(CFG_SD_OPERACJA_RENAME_BAK, wynik);
            goto blad_po_zamknieciu;
        }

        wynik = f_rename(g_cfg_fpath, g_cfg_old_fpath);
        if (wynik == FR_OK)
            byl_stary_config = true;
        else if (wynik != FR_NO_FILE)
        {
            CFG_SD_UstawBlad(CFG_SD_OPERACJA_RENAME_BAK, wynik);
            goto blad_po_zamknieciu;
        }

        wynik = f_rename(g_cfg_tmp_fpath, g_cfg_fpath);
        if (wynik != FR_OK)
        {
            if (byl_stary_config)
                (void)f_rename(g_cfg_old_fpath, g_cfg_fpath);
            (void)f_unlink(g_cfg_tmp_fpath);
            CFG_SD_UstawBlad(CFG_SD_OPERACJA_RENAME_CONFIG, wynik);
            g_sd_diag.zapis = CFG_SD_STAN_BLAD;
            g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
            return false;
        }

        /* Dopiero po zainstalowaniu nowego config.bin obracamy backup. */
        if (byl_stary_config)
        {
            wynik = f_unlink(g_cfg_bak_fpath);
            if (wynik == FR_OK || wynik == FR_NO_FILE)
            {
                wynik = f_rename(g_cfg_old_fpath, g_cfg_bak_fpath);
                if (wynik != FR_OK)
                {
                    /* Nowy config.bin jest poprawny; config.old zostawiamy
                     * jako dodatkowa kopie zamiast ryzykowac utrate danych. */
                    CFG_SD_UstawBlad(CFG_SD_OPERACJA_RENAME_BAK, wynik);
                }
            }
            else
            {
                /* Nie kasujemy config.old. Mamy wtedy trzy kopie zamiast jednej. */
                CFG_SD_UstawBlad(CFG_SD_OPERACJA_RENAME_BAK, wynik);
            }
        }
    }

    g_sd_diag.zapis = CFG_SD_STAN_OK;
    g_sd_diag.konfiguracja = CFG_SD_STAN_OK;
    g_sd_diag.katalog_aa = CFG_SD_STAN_OK;
    g_sd_diag.ostatni_blad_fatfs = (uint8_t)FR_OK;
    g_sd_diag.ostatnia_operacja = CFG_SD_OPERACJA_BRAK;
    return true;

blad_zapisu:
    (void)f_close(&plik);
blad_po_zamknieciu:
    (void)f_unlink(g_cfg_tmp_fpath);
    g_sd_diag.zapis = CFG_SD_STAN_BLAD;
    g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
    return false;
}

// Inicjalizacja konfiguracji. Brak karty SD nie blokuje pracy przyrządu:
// analizator uruchamia się wtedy na bezpiecznych ustawieniach domyślnych.
void CFG_Init(void)
{
    FILINFO informacje = {0};
    FRESULT wynik;

    CFG_UstawDomyslne();

    if (!g_karta_sd_dostepna)
    {
        CFG_WalidujKrytyczne();
        return;
    }

    wynik = f_stat(g_cfg_fpath, &informacje);
    if (wynik != FR_OK && wynik != FR_NO_FILE && wynik != FR_NO_PATH)
    {
        g_sd_diag.odczyt = CFG_SD_STAN_BLAD;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_STAT_CONFIG, wynik);

    }

    if (wynik == FR_NO_FILE || wynik == FR_NO_PATH)
    {
        /*
         * Brak config.bin nie oznacza od razu swiezej karty. Mogl nastapic
         * reset dokladnie pomiedzy rename(config.bin -> config.old) i
         * rename(config.tmp -> config.bin). Najpierw probujemy odzyskac jedna
         * z kopii transakcyjnych, a dopiero potem tworzymy domyslna konfiguracje.
         */
        g_sd_diag.odczyt = CFG_SD_STAN_OK;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BRAK;
        if (!CFG_SprobujOdzyskacKonfiguracje())
            CFG_Flush();
        else
            g_sd_diag.konfiguracja = CFG_SD_STAN_OK;
        CFG_WalidujKrytyczne();
        return;
    }

    if (wynik != FR_OK)
    {
        g_sd_diag.odczyt = CFG_SD_STAN_BLAD;
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        CFG_SD_UstawBlad(CFG_SD_OPERACJA_STAT_CONFIG, wynik);
        CFG_WalidujKrytyczne();
        return;
    }

    g_sd_diag.katalog_aa = CFG_SD_STAN_OK;
    g_sd_diag.odczyt = CFG_SD_STAN_OK;

    if (informacje.fsize == 0U)
    {
        g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
        CFG_ZachowajUszkodzonyPlik();
        if (!CFG_SprobujOdzyskacKonfiguracje())
            CFG_Flush();
        else
            g_sd_diag.konfiguracja = CFG_SD_STAN_OK;
        CFG_WalidujKrytyczne();
        return;
    }

    {
        FIL plik = {0};
        uint32_t magic = 0U;
        UINT odczytano = 0U;
        bool poprawny = false;
        bool stary_format = false;

        wynik = f_open(&plik, g_cfg_fpath, FA_READ | FA_OPEN_EXISTING);
        if (wynik != FR_OK)
        {
            g_sd_diag.odczyt = CFG_SD_STAN_BLAD;
            g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
            CFG_SD_UstawBlad(CFG_SD_OPERACJA_OPEN_CONFIG, wynik);
            CFG_WalidujKrytyczne();
            return;
        }

        wynik = f_read(&plik, &magic, sizeof(magic), &odczytano);
        if (wynik != FR_OK)
        {
            g_sd_diag.odczyt = CFG_SD_STAN_BLAD;
            g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
            CFG_SD_UstawBlad(CFG_SD_OPERACJA_READ_CONFIG, wynik);
        }
        else if (odczytano == sizeof(magic))
        {
            wynik = f_lseek(&plik, 0U);
            if (wynik == FR_OK)
            {
                if (magic == FORMAT_KONFIG_MAGIC)
                    poprawny = CFG_WczytajNowyFormat(&plik);
                else
                {
                    stary_format = true;
                    poprawny = CFG_WczytajStaryFormat(&plik, informacje.fsize);
                }
            }
            else
            {
                g_sd_diag.odczyt = CFG_SD_STAN_BLAD;
                CFG_SD_UstawBlad(CFG_SD_OPERACJA_READ_CONFIG, wynik);
            }
        }

        (void)f_close(&plik);

        if (!poprawny)
        {
            /*
             * Krótki plik, niezgodne CRC albo uszkodzony format: zachowujemy
             * dowod jako config.bad i NAJPIERW próbujemy odzyskac tmp/old/bak.
             * Dopiero brak wszystkich poprawnych kopii pozwala zapisac
             * konfiguracje domyslna.
             */
            g_sd_diag.konfiguracja = CFG_SD_STAN_BLAD;
            CFG_ZachowajUszkodzonyPlik();
            if (!CFG_SprobujOdzyskacKonfiguracje())
            {
                CFG_UstawDomyslne();
                CFG_Flush();
            }
            else
            {
                g_sd_diag.odczyt = CFG_SD_STAN_OK;
                g_sd_diag.konfiguracja = CFG_SD_STAN_OK;
                g_sd_diag.ostatni_blad_fatfs = (uint8_t)FR_OK;
                g_sd_diag.ostatnia_operacja = CFG_SD_OPERACJA_BRAK;
            }
        }
        else
        {
            g_sd_diag.odczyt = CFG_SD_STAN_OK;
            g_sd_diag.konfiguracja = CFG_SD_STAN_OK;
            g_sd_diag.ostatni_blad_fatfs = (uint8_t)FR_OK;
            g_sd_diag.ostatnia_operacja = CFG_SD_OPERACJA_BRAK;
            if (stary_format)
            {
                /* Jednorazowa automatyczna migracja starego surowego pliku. */
                CFG_Flush();
            }
        }
    }

    CFG_WalidujKrytyczne();
}
#pragma GCC diagnostic pop

uint32_t CFG_GetParam(CFG_PARAM_t param)
{
    assert_param(param >= 0 && param < CFG_NUM_PARAMS);
    return g_cfg_array[param];
}

void CFG_SetParam(CFG_PARAM_t param, uint32_t value)
{
    uint32_t poprzedni_mohm = 0U;

    assert_param(param >= 0 && param < CFG_NUM_PARAMS);

    if (param == CFG_PARAM_OSL_RSHORT)
        poprzedni_mohm = CFG_GetOslRshortMilliOhm();
    else if (param == CFG_PARAM_OSL_RLOAD)
        poprzedni_mohm = CFG_GetOslRloadMilliOhm();
    else if (param == CFG_PARAM_OSL_ROPEN)
        poprzedni_mohm = CFG_GetOslRopenMilliOhm();

    g_cfg_array[param] = value;

    /*
     * Pola OSL_RSHORT/RLOAD/ROPEN sa historyczne i przechowuja tylko cale
     * omy. Dokladne wartosci z omomierza sa dopisane na koncu konfiguracji,
     * aby nie zmieniac numerow istniejacych parametrow ani formatu rollbacku.
     *
     * Zmiana dowolnego wzorca przez stary edytor oznacza swiadomy powrot do
     * wartosci nominalnej. Synchronizujemy wtedy pole mOhm, zdejmujemy znacznik
     * [DC] i uniewazniamy aktywny profil OSL. Nie kasujemy plikow kalibracyjnych.
     */
    if (param == CFG_PARAM_OSL_RSHORT)
    {
        const uint64_t mohm = (uint64_t)value * 1000ULL;
        if (mohm <= 1000000ULL)
        {
            g_cfg_array[CFG_PARAM_OSL_RSHORT_MOHM] = (uint32_t)mohm;
            g_cfg_array[CFG_PARAM_OSL_RSHORT_ZRODLO] = 0U;
            if ((uint32_t)mohm != poprzedni_mohm)
            {
                g_cfg_array[CFG_PARAM_OSL_SELECTED] = ~0U;
            }
        }
    }
    else if (param == CFG_PARAM_OSL_RLOAD)
    {
        const uint64_t mohm = (uint64_t)value * 1000ULL;
        if (mohm <= 1000000ULL)
        {
            g_cfg_array[CFG_PARAM_OSL_RLOAD_MOHM] = (uint32_t)mohm;
            g_cfg_array[CFG_PARAM_OSL_RLOAD_ZRODLO] = 0U;
            if ((uint32_t)mohm != poprzedni_mohm)
            {
                g_cfg_array[CFG_PARAM_OSL_SELECTED] = ~0U;
            }
        }
    }
    else if (param == CFG_PARAM_OSL_ROPEN)
    {
        const uint64_t mohm = (uint64_t)value * 1000ULL;
        if (mohm <= 1000000000ULL)
        {
            g_cfg_array[CFG_PARAM_OSL_ROPEN_MOHM] = (uint32_t)mohm;
            g_cfg_array[CFG_PARAM_OSL_ROPEN_ZRODLO] = 0U;
            if ((uint32_t)mohm != poprzedni_mohm)
            {
                g_cfg_array[CFG_PARAM_OSL_SELECTED] = ~0U;
            }
        }
    }
}

uint32_t CFG_GetOslRshortMilliOhm(void)
{
    uint32_t mohm = g_cfg_array[CFG_PARAM_OSL_RSHORT_MOHM];
    if (mohm > 1000000U)
    {
        const uint64_t z_legacy = (uint64_t)g_cfg_array[CFG_PARAM_OSL_RSHORT] * 1000ULL;
        mohm = z_legacy <= 1000000ULL ? (uint32_t)z_legacy : 5000U;
    }
    return mohm;
}

uint32_t CFG_GetOslRloadMilliOhm(void)
{
    uint32_t mohm = g_cfg_array[CFG_PARAM_OSL_RLOAD_MOHM];
    if (mohm < 1000U || mohm > 1000000U)
    {
        const uint64_t z_legacy = (uint64_t)g_cfg_array[CFG_PARAM_OSL_RLOAD] * 1000ULL;
        mohm = (z_legacy >= 1000ULL && z_legacy <= 1000000ULL) ? (uint32_t)z_legacy : 50000U;
    }
    return mohm;
}

uint32_t CFG_GetOslRopenMilliOhm(void)
{
    uint32_t mohm = g_cfg_array[CFG_PARAM_OSL_ROPEN_MOHM];
    if (mohm < 1000U || mohm > 1000000000U)
    {
        const uint64_t z_legacy = (uint64_t)g_cfg_array[CFG_PARAM_OSL_ROPEN] * 1000ULL;
        mohm = (z_legacy >= 1000ULL && z_legacy <= 1000000000ULL) ? (uint32_t)z_legacy : 500000U;
    }
    return mohm;
}

float CFG_GetOslRshortOhm(void) { return (float)CFG_GetOslRshortMilliOhm() / 1000.0f; }
float CFG_GetOslRloadOhm(void) { return (float)CFG_GetOslRloadMilliOhm() / 1000.0f; }
float CFG_GetOslRopenOhm(void) { return (float)CFG_GetOslRopenMilliOhm() / 1000.0f; }

bool CFG_CzyOslRshortZPomiaruZewnetrznego(void)
{
    return g_cfg_array[CFG_PARAM_OSL_RSHORT_ZRODLO] == 1U;
}

bool CFG_CzyOslRloadZPomiaruZewnetrznego(void)
{
    return g_cfg_array[CFG_PARAM_OSL_RLOAD_ZRODLO] == 1U;
}

bool CFG_CzyOslRopenZPomiaruZewnetrznego(void)
{
    return g_cfg_array[CFG_PARAM_OSL_ROPEN_ZRODLO] == 1U;
}

bool CFG_CzyOslWszystkieWzorceZPomiaruZewnetrznego(void)
{
    return CFG_CzyOslRshortZPomiaruZewnetrznego() &&
           CFG_CzyOslRloadZPomiaruZewnetrznego() &&
           CFG_CzyOslRopenZPomiaruZewnetrznego();
}

static void CFG_UniewaznijKalibracjePoZmianieWzorca(void)
{
    g_cfg_array[CFG_PARAM_OSL_SELECTED] = ~0U;
}

bool CFG_UstawOslRshortZPomiaruZewnetrznego(uint32_t miliohm)
{
    /* W trybie dokladnym oczekujemy rzeczywistego wzorca ok. 5 Ohm.
       Zbyt szeroki zakres utrudnialby wykrycie pomylki o przecinek. */
    if (miliohm < 1000U || miliohm > 20000U)
        return false;
    g_cfg_array[CFG_PARAM_OSL_RSHORT] = (miliohm + 500U) / 1000U;
    g_cfg_array[CFG_PARAM_OSL_RSHORT_MOHM] = miliohm;
    g_cfg_array[CFG_PARAM_OSL_RSHORT_ZRODLO] = 1U;
    CFG_UniewaznijKalibracjePoZmianieWzorca();
    return true;
}

bool CFG_UstawOslRloadZPomiaruZewnetrznego(uint32_t miliohm)
{
    /* Wzorzec srodkowy moze byc np. 50 lub 75 Ohm. */
    if (miliohm < 10000U || miliohm > 200000U)
        return false;
    g_cfg_array[CFG_PARAM_OSL_RLOAD] = (miliohm + 500U) / 1000U;
    g_cfg_array[CFG_PARAM_OSL_RLOAD_MOHM] = miliohm;
    g_cfg_array[CFG_PARAM_OSL_RLOAD_ZRODLO] = 1U;
    CFG_UniewaznijKalibracjePoZmianieWzorca();
    return true;
}

bool CFG_UstawOslRopenZPomiaruZewnetrznego(uint32_t miliohm)
{
    /* Wzorzec wysoki w tej konstrukcji ma ok. 500 Ohm. Nie jest to
       klasyczne rozwarcie nieskonczone. */
    if (miliohm < 100000U || miliohm > 2000000U)
        return false;
    g_cfg_array[CFG_PARAM_OSL_ROPEN] = (miliohm + 500U) / 1000U;
    g_cfg_array[CFG_PARAM_OSL_ROPEN_MOHM] = miliohm;
    g_cfg_array[CFG_PARAM_OSL_ROPEN_ZRODLO] = 1U;
    CFG_UniewaznijKalibracjePoZmianieWzorca();
    return true;
}

static void CFG_FormatujRezystancjeMiliohm(char *bufor, uint32_t rozmiar,
                                            uint32_t mohm, bool pokaz_zrodlo,
                                            bool z_pomiaru_dc)
{
    const char sep = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    if (bufor == NULL || rozmiar == 0U)
        return;
    snprintf(bufor, rozmiar, "%lu%c%03lu Ohm%s",
             (unsigned long)(mohm / 1000U), sep,
             (unsigned long)(mohm % 1000U),
             (pokaz_zrodlo && z_pomiaru_dc) ? " [DC]" : "");
}

void CFG_FormatujOslRshort(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo)
{
    CFG_FormatujRezystancjeMiliohm(bufor, rozmiar, CFG_GetOslRshortMilliOhm(),
                                   pokaz_zrodlo, CFG_CzyOslRshortZPomiaruZewnetrznego());
}

void CFG_FormatujOslRload(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo)
{
    CFG_FormatujRezystancjeMiliohm(bufor, rozmiar, CFG_GetOslRloadMilliOhm(),
                                   pokaz_zrodlo, CFG_CzyOslRloadZPomiaruZewnetrznego());
}

void CFG_FormatujOslRopen(char *bufor, uint32_t rozmiar, bool pokaz_zrodlo)
{
    CFG_FormatujRezystancjeMiliohm(bufor, rozmiar, CFG_GetOslRopenMilliOhm(),
                                   pokaz_zrodlo, CFG_CzyOslRopenZPomiaruZewnetrznego());
}

void CFG_Flush(void)
{
#ifdef _DEBUG_UART
    DBG_Str("Flush !!!");
#endif

    if (!g_karta_sd_dostepna)
        return;

    CFG_SetParam(CFG_PARAM_VERSION, CFG_WersjaProgramuU32());
    (void)CFG_ZapiszKonfiguracjeRaz();
}

bool CFG_FlushSprawdzony(void)
{
    if (!g_karta_sd_dostepna)
        return false;

    CFG_SetParam(CFG_PARAM_VERSION, CFG_WersjaProgramuU32());
    return CFG_ZapiszKonfiguracjeRaz();
}

bool CFG_PrzygotujPowrotDoStarszejWersji(void)
{
    FIL plik = {0};
    UINT zapisano = 0U;
    const UINT rozmiar_legacy = (UINT)(CFG_LEGACY_NUM_PARAMS * sizeof(uint32_t));
    FRESULT wynik;

    /*
     * Funkcja jest uruchamiana wyłącznie świadomie z Narzędzi serwisowych.
     * Najpierw zapisujemy aktualny CFG1, następnie zachowujemy go jako
     * cfgv2.bak i dopiero na końcu podstawiamy surowy plik zgodny ze
     * starszą gałęzią. Jeżeli użytkownik nie wgra starszego firmware i po
     * prostu uruchomi ponownie v2.0, CFG_Init() rozpozna format historyczny i
     * automatycznie zmigruje go z powrotem do CFG1.
     */
    if (!g_karta_sd_dostepna)
        return false;

    CFG_Flush();
    {
        CFG_SD_DIAGNOSTYKA_t stan_sd;
        CFG_SD_PobierzDiagnostyke(&stan_sd);
        if (!g_karta_sd_dostepna || stan_sd.zapis != CFG_SD_STAN_OK)
            return false;
    }

    (void)f_mkdir(g_aa_dir);
    (void)f_mkdir(g_cfg_dir);
    (void)f_unlink(g_cfg_rollback_tmp_fpath);

    wynik = f_open(&plik, g_cfg_rollback_tmp_fpath, FA_CREATE_ALWAYS | FA_WRITE);
    if (wynik != FR_OK)
        return false;

    wynik = f_write(&plik, g_cfg_array, rozmiar_legacy, &zapisano);
    if (wynik == FR_OK && zapisano == rozmiar_legacy)
        wynik = f_sync(&plik);
    (void)f_close(&plik);

    if (wynik != FR_OK || zapisano != rozmiar_legacy)
    {
        (void)f_unlink(g_cfg_rollback_tmp_fpath);
        return false;
    }

    (void)f_unlink(g_cfg_v2_bak_fpath);
    wynik = f_rename(g_cfg_fpath, g_cfg_v2_bak_fpath);
    if (wynik != FR_OK)
    {
        (void)f_unlink(g_cfg_rollback_tmp_fpath);
        return false;
    }

    if (f_rename(g_cfg_rollback_tmp_fpath, g_cfg_fpath) != FR_OK)
    {
        /* Nie pozostawiamy urządzenia bez konfiguracji po częściowej operacji. */
        (void)f_rename(g_cfg_v2_bak_fpath, g_cfg_fpath);
        (void)f_unlink(g_cfg_rollback_tmp_fpath);
        return false;
    }

    return true;
}


static uint32_t CFG_GetNextValue(uint32_t param_idx, uint32_t param_value)
{
    if (param_idx >= cfg_ch_descr_table_num)
        return param_value + 1;

    const CFG_CHANGEABLE_PARAM_DESCR_t *pd = &cfg_ch_descr_table[param_idx];
    if (0 == pd->nvalues)
    {
        //If no default values specified:
        return param_value + 1;
    }

    uint32_t i;
    for (i = 0; i < pd->nvalues; i++)
    {
        if (param_value == pd->values[i])
        {
            //Value is among the defaults
            if (i == pd->nvalues - 1)
                return pd->values[0]; //Wrap around the last one
            return pd->values[i + 1];
        }
    }
    //Oops, if we get here then the value is not among default ones
    //Just return the last default value
    return pd->values[pd->nvalues - 1];
}

static uint32_t CFG_GetPrevValue(uint32_t param_idx, uint32_t param_value)
{
    if (param_idx >= cfg_ch_descr_table_num)
        return param_value - 1;

    const CFG_CHANGEABLE_PARAM_DESCR_t *pd = &cfg_ch_descr_table[param_idx];
    if (0 == pd->nvalues)
    {
        //If no default values specified:
        return param_value - 1;
    }

    uint32_t i;
    for (i = 0; i < pd->nvalues; i++)
    {
        if (param_value == pd->values[i])
        {
            //Value is among the defaults
            if (i == 0)
                return pd->values[pd->nvalues - 1]; //Wrap around 0
            return pd->values[i - 1];
        }
    }
    //Oops, if we get here then the value is not among default ones
    //Just return the first default value
    return pd->values[0];
}

static const char *CFG_WartoscLokalna(CFG_PARAM_t id, uint32_t wartosc)
{
    switch (id)
    {
    case CFG_PARAM_OSL_SELECTED:
        if ((int32_t)wartosc < 0)
            return JEZYK_Wybierz("Brak", "None", "Keine", "Нет");
        break;
    case CFG_PARAM_SYNTH_TYPE:
        if (wartosc == CFG_SYNTH_SI5351)
            return JEZYK_Wybierz("Si5351A - EU1KY", "Si5351A - EU1KY", "Si5351A - EU1KY", "Si5351A - EU1KY");
        if (wartosc == CFG_SYNTH_ADF4350)
            return JEZYK_Wybierz("2x ADF4350 - niezweryfikowane", "2x ADF4350 - unverified", "2x ADF4350 - ungeprüft", "2x ADF4350 - не проверено");
        if (wartosc == CFG_SYNTH_ADF4351)
            return JEZYK_Wybierz("2x ADF4351 - niezweryfikowane", "2x ADF4351 - unverified", "2x ADF4351 - ungeprüft", "2x ADF4351 - не проверено");
        return JEZYK_Wybierz("Si5338A - niezweryfikowane", "Si5338A - unverified", "Si5338A - ungeprüft", "Si5338A - не проверено");
    case CFG_PARAM_OSL_RSHORT:
    {
        static char rshort_dokladny[40];
        CFG_FormatujOslRshort(rshort_dokladny, sizeof(rshort_dokladny), true);
        return rshort_dokladny;
    }
    case CFG_PARAM_OSL_RLOAD:
    {
        static char rload_dokladny[40];
        CFG_FormatujOslRload(rload_dokladny, sizeof(rload_dokladny), true);
        return rload_dokladny;
    }
    case CFG_PARAM_OSL_ROPEN:
    {
        static char ropen_dokladny[40];
        if (!CFG_CzyOslRopenZPomiaruZewnetrznego() && wartosc == 999999U)
            return JEZYK_Wybierz("Rozwarcie", "Open", "Leerlauf", "ХХ");
        CFG_FormatujOslRopen(ropen_dokladny, sizeof(ropen_dokladny), true);
        return ropen_dokladny;
    }
    case CFG_PARAM_PAN_CENTER_F:
        return wartosc == 0U
            ? JEZYK_Wybierz("Częst. początkowa", "Start frequency", "Startfrequenz", "Начальная частота")
            : JEZYK_Wybierz("Częst. środkowa", "Center frequency", "Mittenfrequenz", "Центральная частота");
    case CFG_PARAM_LOWPWR_TIME:
        if (wartosc == 0U)
            return JEZYK_Wybierz("Wyłączone", "Off", "Aus", "Выкл.");
        break;
    case CFG_PARAM_COM_PORT:
        break;
    case CFG_PARAM_ShowLogoTime:
        break;
    case CFG_PARAM_S11_SHOW:
    case CFG_PARAM_SHOW_HIDDEN:
        return wartosc
            ? JEZYK_Wybierz("Tak", "Yes", "Ja", "Да")
            : JEZYK_Wybierz("Nie", "No", "Nein", "Нет");
    case CFG_PARAM_ORIENTATION:
        return wartosc
            ? JEZYK_Wybierz("180 stopni", "180 degrees", "180 Grad", "180 градусов")
            : JEZYK_Wybierz("0 stopni", "0 degrees", "0 Grad", "0 градусов");
    case CFG_PARAM_LOGLOG:
        return wartosc
            ? JEZYK_Wybierz("Podwójnie logarytmiczna", "Double logarithmic", "Doppelt logarithmisch", "Двойная логарифмическая")
            : JEZYK_Wybierz("Logarytmiczna", "Logarithmic", "Logarithmisch", "Логарифмическая");
    case CFG_PARAM_CURSOR:
        if (wartosc == 0U)
            return JEZYK_Wybierz("Bez automatycznego kursora", "No automatic cursor", "Kein automatischer Cursor", "Без автокурсора");
        if (wartosc == 1U)
            return JEZYK_Wybierz("Automatyczny kursor", "Automatic cursor", "Automatischer Cursor", "Автокурсор");
        if (wartosc == 2U)
            return JEZYK_Wybierz("Ścisły automatyczny kursor", "Strict automatic cursor", "Strenger Auto-Cursor", "Строгий автокурсор");
        break;
    case CFG_PARAM_TRYB_INTERFEJSU:
        return wartosc
            ? JEZYK_Wybierz("Podstawowy", "Basic", "Grundmodus", "Базовый")
            : JEZYK_Wybierz("Zaawansowany", "Advanced", "Erweitert", "Расширенный");
    case CFG_PARAM_UI_MOTYW:
        (void)wartosc;
        return JEZYK_Wybierz("Klasyczny", "Classic", "Klassisch", "Классический");
    case CFG_PARAM_HARMONICZNA_MAX:
        if (wartosc == 1U)
            return JEZYK_Wybierz("H1 - bezpośrednio", "H1 - direct", "H1 - direkt", "H1 - напрямую");
        if (wartosc == 3U)
            return JEZYK_Wybierz("H3 - zalecana", "H3 - recommended", "H3 - empfohlen", "H3 - рекомендуется");
        if (wartosc == 5U)
            return JEZYK_Wybierz("H5 - eksperymentalna", "H5 - experimental", "H5 - experimentell", "H5 - экспериментальная");
        return JEZYK_Wybierz("H7 - eksperymentalna", "H7 - experimental", "H7 - experimentell", "H7 - экспериментальная");
    case CFG_PARAM_PORT_EXT_PS:
        if (wartosc == 0U)
            return JEZYK_Wybierz("Wyłączona", "Off", "Aus", "Выкл.");
        break;
    case CFG_PARAM_ZESTAW_IKON:
        return wartosc == 0U
            ? JEZYK_Wybierz("Retro", "Retro", "Retro", "Ретро")
            : JEZYK_Wybierz("Klasyczny niebieski", "Classic blue", "Klassisch blau", "Классический синий");
    case CFG_PARAM_TDR_FILTR_CZASU:
        if (wartosc == 0U) return JEZYK_Wybierz("Wyłączony", "Off", "Aus", "Выкл.");
        if (wartosc == 1U) return "Savitzky-Golay";
        if (wartosc == 2U) return "Kalman";
        return "Savitzky + Kalman";
    case CFG_PARAM_TDR_POROWNAJ_FILTR:
        return wartosc ? JEZYK_Wybierz("Tak", "Yes", "Ja", "Да")
                       : JEZYK_Wybierz("Nie", "No", "Nein", "Нет");
    case CFG_PARAM_KABEL_PROFIL_AKTYWNY:
        return wartosc ? JEZYK_Wybierz("Włączona", "On", "Ein", "Вкл.")
                       : JEZYK_Wybierz("Wyłączona", "Off", "Aus", "Выкл.");
    case CFG_PARAM_KOLOR_KRZYWEJ_SWR:
        if (wartosc == 1U)
            return JEZYK_Wybierz("Zielony", "Green", "Grün", "Зелёный");
        if (wartosc == 2U)
            return JEZYK_Wybierz("Bursztynowy", "Amber", "Bernstein", "Янтарный");
        if (wartosc == 3U)
            return JEZYK_Wybierz("Czerwony", "Red", "Rot", "Красный");
        return JEZYK_Wybierz("Stalowy", "Steel", "Stahl", "Стальной");
    default:
        break;
    }
    return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
// Zwraca tekstową reprezentację wartości parametru w aktualnym języku.
const char *CFG_GetStringValue(uint32_t param_idx)
{
    if (param_idx >= cfg_ch_descr_table_num)
        return "";

    const CFG_CHANGEABLE_PARAM_DESCR_t *pd = &cfg_ch_descr_table[param_idx];
    uint32_t uval = CFG_GetParam(pd->id);
    uint32_t i;
    const char *wartosc_lokalna = CFG_WartoscLokalna(pd->id, uval);

    if (wartosc_lokalna != 0)
        return wartosc_lokalna;

    if (0 != pd->strvalues)
    {
        for (i = 0; i < pd->nvalues; i++)
        {
            if (uval == pd->values[i])
                return pd->strvalues[i];
        }
    }

    static char tstr[32];
    switch (pd->type)
    {
    case CFG_PARAM_T_U8:
        snprintf(tstr, sizeof(tstr), "%u (%02Xh)", (unsigned int)((uint8_t)uval), (unsigned int)((uint8_t)uval));
        break;
    case CFG_PARAM_T_U16:
        snprintf(tstr, sizeof(tstr), "%u", (unsigned int)((uint16_t)uval));
        break;
    case CFG_PARAM_T_U32:
        snprintf(tstr, sizeof(tstr), "%u", (unsigned int)((uint32_t)uval));
        break;
    case CFG_PARAM_T_S8:
        snprintf(tstr, sizeof(tstr), "%d", (int)((int8_t)uval));
        break;
    case CFG_PARAM_T_S16:
        snprintf(tstr, sizeof(tstr), "%d", (int)((int16_t)uval));
        break;
    case CFG_PARAM_T_S32:
        snprintf(tstr, sizeof(tstr), "%d", (int)((int32_t)uval));
        break;
    case CFG_PARAM_T_F32:
        snprintf(tstr, sizeof(tstr), "%.7g", (double)*(float *)&uval);
        break;
    case CFG_PARAM_T_CH:
        memcpy(tstr, &uval, 4U);
        tstr[4] = '\0';
        break;
    default:
        return "";
    }
    return tstr;
}
#pragma GCC diagnostic pop

static const char *CFG_NazwaLokalna(CFG_PARAM_t id)
{
    switch (id)
    {
    case CFG_PARAM_OSL_SELECTED: return JEZYK_Wybierz("Profil kalibracji OSL", "OSL calibration profile", "OSL-Kalibrierprofil", "Профиль калибровки OSL");
    case CFG_PARAM_SYNTH_TYPE: return JEZYK_Wybierz("Typ generatora RF", "RF generator type", "HF-Generatortyp", "Тип RF-генератора");
    case CFG_PARAM_R0: return JEZYK_Wybierz("Impedancja odniesienia Z0", "Reference impedance Z0", "Bezugsimpedanz Z0", "Опорное сопротивление Z0");
    case CFG_PARAM_SI5351_XTAL_FREQ: return JEZYK_Wybierz("Częstotliwość kwarcu Si5351", "Si5351 crystal frequency", "Si5351-Quarzfrequenz", "Частота кварца Si5351");
    case CFG_PARAM_SI5351_BUS_BASE_ADDR: return JEZYK_Wybierz("Adres I2C Si5351", "Si5351 I2C address", "Si5351-I2C-Adresse", "Адрес I2C Si5351");
    case CFG_PARAM_SI5351_CORR: return JEZYK_Wybierz("Korekcja częstotliwości Si5351", "Si5351 frequency correction", "Si5351-Frequenzkorrektur", "Коррекция частоты Si5351");
    case CFG_PARAM_SI5351_MAX_FREQ: return JEZYK_Wybierz("Maksymalna częstotliwość Si5351", "Maximum Si5351 frequency", "Maximale Si5351-Frequenz", "Максимальная частота Si5351");
    case CFG_PARAM_SI5351_CAPS: return JEZYK_Wybierz("Pojemność obciążenia kwarcu", "Crystal load capacitance", "Quarz-Lastkapazität", "Нагрузочная ёмкость кварца");
    case CFG_PARAM_OSL_RLOAD: return JEZYK_Wybierz("Wzorzec OSL - środkowy", "OSL middle standard", "OSL-Normal mittel", "Эталон OSL - средний");
    case CFG_PARAM_OSL_RSHORT: return JEZYK_Wybierz("Wzorzec OSL - niski", "OSL low standard", "OSL-Normal niedrig", "Эталон OSL - низкий");
    case CFG_PARAM_OSL_ROPEN: return JEZYK_Wybierz("Wzorzec OSL - wysoki", "OSL high standard", "OSL-Normal hoch", "Эталон OSL - высокий");
    case CFG_PARAM_OSL_NSCANS: return JEZYK_Wybierz("Uśrednianie kalibracji OSL", "OSL calibration averaging", "OSL-Kalibrierung mitteln", "Усреднение калибровки OSL");
    case CFG_PARAM_MEAS_NSCANS: return JEZYK_Wybierz("Uśrednianie pomiaru", "Measurement averaging", "Messung mitteln", "Усреднение измерения");
    case CFG_PARAM_PAN_NSCANS: return JEZYK_Wybierz("Uśrednianie wykresu", "Plot averaging", "Diagramm mitteln", "Усреднение графика");
    case CFG_PARAM_PAN_AUTOSPEED: return JEZYK_Wybierz("Szybkość automatyczna wykresu", "Automatic plot speed", "Automatische Diagrammgeschwindigkeit", "Автоскорость графика");
    case CFG_PARAM_S21_AUTOSPEED: return JEZYK_Wybierz("Szybkość automatyczna S21", "Automatic S21 speed", "Automatische S21-Geschwindigkeit", "Автоскорость S21");
    case CFG_PARAM_LIN_ATTENUATION: return JEZYK_Wybierz("Tłumienie wejścia audio", "Audio input attenuation", "Audio-Eingangsdämpfung", "Ослабление аудиовхода");
    case CFG_PARAM_BRIDGE_RM: return JEZYK_Wybierz("Mostek pomiarowy Rm", "Measurement bridge Rm", "Messbrücke Rm", "Измерительный мост Rm");
    case CFG_PARAM_BRIDGE_RADD: return JEZYK_Wybierz("Mostek pomiarowy Radd", "Measurement bridge Radd", "Messbrücke Radd", "Измерительный мост Radd");
    case CFG_PARAM_PAN_CENTER_F: return JEZYK_Wybierz("Sposób ustawiania częstotliwości", "Frequency reference mode", "Frequenz-Bezugsart", "Режим задания частоты");
    case CFG_PARAM_COM_PORT: return JEZYK_Wybierz("Port sterowania szeregowego", "Serial control port", "Serieller Steuerport", "Последовательный порт управления");
    case CFG_PARAM_COM_SPEED: return JEZYK_Wybierz("Prędkość portu szeregowego", "Serial port speed", "Baudrate der seriellen Schnittstelle", "Скорость последовательного порта");
    case CFG_PARAM_LOWPWR_TIME: return JEZYK_Wybierz("Automatyczne wygaszenie ekranu", "Automatic screen blanking", "Automatische Bildschirmabschaltung", "Автоотключение экрана");
    case CFG_PARAM_S11_SHOW: return JEZYK_Wybierz("Pokaż wykres S11", "Show S11 plot", "S11-Diagramm anzeigen", "Показывать график S11");
    case CFG_PARAM_S1P_TYPE: return JEZYK_Wybierz("Format pliku Touchstone S1P", "Touchstone S1P format", "Touchstone-S1P-Format", "Формат Touchstone S1P");
    case CFG_PARAM_BAND_FMIN: return JEZYK_Wybierz("Dolna granica zakresu", "Lower frequency limit", "Untere Frequenzgrenze", "Нижняя граница частоты");
    case CFG_PARAM_BAND_FMAX: return JEZYK_Wybierz("Górna granica zakresu", "Upper frequency limit", "Obere Frequenzgrenze", "Верхняя граница частоты");
    case CFG_PARAM_SCREENSHOT_FORMAT: return JEZYK_Wybierz("Format zrzutu ekranu", "Screenshot format", "Screenshot-Format", "Формат снимка экрана");
    case CFG_PARAM_TDR_VF: return JEZYK_Wybierz("Współczynnik skrócenia TDR Vf", "TDR velocity factor Vf", "TDR-Verkürzungsfaktor Vf", "Коэффициент укорочения TDR Vf");
    case CFG_PARAM_SHOW_HIDDEN: return JEZYK_Wybierz("Pokaż ukryte parametry serwisowe", "Show hidden service parameters", "Versteckte Serviceparameter anzeigen", "Показывать скрытые сервисные параметры");
    case CFG_PARAM_SEREMUL: return JEZYK_Wybierz("Protokół zdalnego sterowania", "Remote-control protocol", "Fernsteuerprotokoll", "Протокол дистанционного управления");
    case CFG_PARAM_BT_SPEED: return JEZYK_Wybierz("Prędkość Bluetooth", "Bluetooth speed", "Bluetooth-Geschwindigkeit", "Скорость Bluetooth");
    case CFG_PARAM_REGION: return JEZYK_Wybierz("Region IARU", "IARU region", "IARU-Region", "Регион IARU");
    case CFG_PARAM_ORIENTATION: return JEZYK_Wybierz("Orientacja ekranu", "Screen orientation", "Bildschirmausrichtung", "Ориентация экрана");
    case CFG_PARAM_LOGLOG: return JEZYK_Wybierz("Skala wykresu SWR", "SWR plot scale", "SWR-Diagrammskala", "Шкала графика SWR");
    case CFG_PARAM_CURSOR: return JEZYK_Wybierz("Automatyczny kursor", "Automatic cursor", "Automatischer Cursor", "Автоматический курсор");
    case CFG_PARAM_ATTENUATOR: return JEZYK_Wybierz("Tłumik kalibracyjny S21", "S21 calibration attenuator", "S21-Kalibrierdämpfer", "Калибровочный аттенюатор S21");
    case CFG_PARAM_JEZYK: return JEZYK_Wybierz("Język interfejsu", "Interface language", "Oberflächensprache", "Язык интерфейса");
    case CFG_PARAM_TRYB_INTERFEJSU: return JEZYK_Wybierz("Poziom interfejsu", "Interface level", "Oberflächenstufe", "Уровень интерфейса");
    case CFG_PARAM_ShowLogoTime: return JEZYK_Wybierz("Czas wyświetlania ekranu startowego", "Startup screen duration", "Dauer des Startbildschirms", "Время стартового экрана");
    case CFG_PARAM_UI_MOTYW: return JEZYK_Wybierz("Wygląd klasyczny", "Classic appearance", "Klassische Ansicht", "Классический вид");
    case CFG_PARAM_TDR_VF_X10000: return JEZYK_Wybierz("Vf TDR x10000", "TDR Vf x10000", "TDR Vf x10000", "Vf TDR x10000");
    case CFG_PARAM_HARMONICZNA_MAX: return JEZYK_Wybierz("Maksymalna harmoniczna RF", "Maximum RF harmonic", "Maximale RF-Harmonische", "Максимальная гармоника RF");
    case CFG_PARAM_PORT_EXT_PS: return JEZYK_Wybierz("Kompensacja kabla - opóźnienie", "Cable compensation - delay", "Kabelkompensation - Verzögerung", "Компенсация кабеля - задержка");
    case CFG_PARAM_ZESTAW_IKON: return JEZYK_Wybierz(
        "Dwa finalne style: Retro z pergaminem, mosiądzem i ozdobnymi ramkami albo Klasyczny niebieski z białymi ikonami i wysokim kontrastem. Nie wpływa na pomiary.",
        "Two final styles: parchment-and-brass Retro with ornate frames, or high-contrast Classic blue with white icons. Measurements are unaffected.",
        "Zwei finale Stile: Retro mit Pergament, Messing und Zierrahmen oder kontrastreiches Klassisch-Blau mit weißen Symbolen. Messungen bleiben unverändert.",
        "Два финальных стиля: ретро с пергаментом, латунью и декоративными рамками либо контрастный классический синий с белыми иконками. На измерения не влияет.");
    case CFG_PARAM_TDR_FILTR_CZASU: return JEZYK_Wybierz("Filtr TDR po IFFT", "TDR post-IFFT filter", "TDR-Filter nach IFFT", "Фильтр TDR после IFFT");
    case CFG_PARAM_TDR_POROWNAJ_FILTR: return JEZYK_Wybierz("Porównaj filtr TDR", "Compare TDR filter", "TDR-Filter vergleichen", "Сравнить фильтр TDR");
    case CFG_PARAM_MASKA_MODELI_RF: return JEZYK_Wybierz("Aktywne modele RF", "Enabled RF models", "Aktive HF-Modelle", "Активные ВЧ-модели");
    case CFG_PARAM_MASKA_METOD_Q: return JEZYK_Wybierz("Aktywne metody Q", "Enabled Q methods", "Aktive Q-Methoden", "Активные методы Q");
    case CFG_PARAM_KABEL_PROFIL_AKTYWNY: return JEZYK_Wybierz("Kompensacja kabla", "Cable compensation", "Kabelkompensation", "Компенсация кабеля");
    case CFG_PARAM_KOLOR_KRZYWEJ_SWR: return JEZYK_Wybierz("Kolor krzywej SWR", "SWR curve color", "SWR-Kurvenfarbe", "Цвет кривой КСВ");
    default: return 0;
    }
}

static const char *CFG_OpisLokalny(CFG_PARAM_t id)
{
    switch (id)
    {
    case CFG_PARAM_OSL_SELECTED: return JEZYK_Wybierz("Profil A...P odczytywany z katalogu /aa/osl na karcie SD.", "Profile A...P read from /aa/osl on the SD card.", "Profil A...P aus /aa/osl auf der SD-Karte.", "Профиль A...P читается из /aa/osl на SD-карте.");
    case CFG_PARAM_SYNTH_TYPE: return JEZYK_Wybierz("Wybierz wyłącznie generator rzeczywiście zamontowany w urządzeniu. Standardowy EU1KY używa Si5351A. Warianty ADF4350/ADF4351 i Si5338A pozostają dla zgodnych odmian sprzętu; zmiana wymaga restartu i ponownej kalibracji.", "Select only the generator actually fitted to the device. Standard EU1KY uses Si5351A. ADF4350/ADF4351 and Si5338A remain for compatible hardware variants; changing it requires restart and recalibration.", "Nur den tatsächlich eingebauten Generator wählen. Standard-EU1KY verwendet Si5351A. ADF4350/ADF4351 und Si5338A bleiben für kompatible Hardwarevarianten; Änderung erfordert Neustart und Neukalibrierung.", "Выбирайте только генератор, реально установленный в приборе. Стандартный EU1KY использует Si5351A. ADF4350/ADF4351 и Si5338A оставлены для совместимых вариантов аппаратуры; после изменения нужны перезапуск и повторная калибровка.");
    case CFG_PARAM_R0: return JEZYK_Wybierz("Impedancja odniesienia używana przy SWR i wykresie Smitha.", "Reference impedance used for SWR and Smith chart.", "Bezugsimpedanz für SWR und Smith-Diagramm.", "Опорное сопротивление для SWR и диаграммы Смита.");
    case CFG_PARAM_SI5351_XTAL_FREQ: return JEZYK_Wybierz("Nominalna częstotliwość rezonatora Si5351. Typowo 25 lub 27 MHz; zły wybór może dawać Brak sygnału.", "Nominal Si5351 crystal frequency. Usually 25 or 27 MHz; a wrong value may cause no RF signal.", "Nennfrequenz des Si5351-Quarzes. Meist 25 oder 27 MHz; ein falscher Wert kann zu fehlendem RF-Signal führen.", "Номинальная частота кварца Si5351. Обычно 25 или 27 МГц; неверное значение может привести к отсутствию RF-сигнала.");
    case CFG_PARAM_SI5351_BUS_BASE_ADDR: return JEZYK_Wybierz("Wybór z zamkniętej listy: Auto, C0h, C4h, CEh lub DEh. Są to 8-bitowe adresy używane przez sterownik I2C (odpowiednio 7-bit: 60h, 62h, 67h, 6Fh). Nie ma dowolnego wpisywania adresu.", "Choose from a fixed list: Auto, C0h, C4h, CEh or DEh. These are the 8-bit I2C addresses used by the driver (7-bit: 60h, 62h, 67h, 6Fh). Arbitrary address entry is not allowed.", "Aus fester Liste wählen: Auto, C0h, C4h, CEh oder DEh. Dies sind die vom I2C-Treiber verwendeten 8-Bit-Adressen (7-Bit: 60h, 62h, 67h, 6Fh). Freie Adresseingabe ist nicht möglich.", "Выбор из фиксированного списка: Auto, C0h, C4h, CEh или DEh. Это 8-битные адреса драйвера I2C (7-бит: 60h, 62h, 67h, 6Fh). Произвольный ввод адреса запрещён.");
    case CFG_PARAM_SI5351_CORR: return JEZYK_Wybierz("Dokładna korekcja częstotliwości generatora Si5351 w Hz.", "Fine Si5351 frequency correction in Hz.", "Feine Frequenzkorrektur des Si5351 in Hz.", "Точная коррекция частоты Si5351 в Гц.");
    case CFG_PARAM_SI5351_MAX_FREQ: return JEZYK_Wybierz("Maksymalna częstotliwość bezpośrednia Si5351 używana przez plan pomiarowy. 160 MHz jest wartością konserwatywną; 200-290 MHz pozostają świadomymi nastawami wymagającymi nowej HW/OSL i weryfikacji wzorcami.", "Maximum direct Si5351 frequency used by the measurement plan. 160 MHz is conservative; 200-290 MHz are deliberate settings requiring new HW/OSL calibration and verification with standards.", "Maximale direkte Si5351-Frequenz des Messplans. 160 MHz ist konservativ; 200-290 MHz erfordern neue HW/OSL-Kalibrierung und Prüfung mit Normalen.", "Максимальная прямая частота Si5351 в плане измерения. 160 МГц — консервативное значение; 200-290 МГц требуют новой HW/OSL и проверки эталонами.");
    case CFG_PARAM_SI5351_CAPS: return JEZYK_Wybierz("Wewnętrzna pojemność obciążenia kwarcu. Po zmianie skalibruj częstotliwość.", "Internal crystal load capacitance. Recalibrate frequency after changing it.", "Interne Quarz-Lastkapazität. Nach Änderung die Frequenz neu kalibrieren.", "Внутренняя нагрузочная ёмкость кварца. После изменения откалибруйте частоту.");
    case CFG_PARAM_OSL_RLOAD: return JEZYK_Wybierz("Rezystancja środkowego wzorca OSL, typowo około 50 Ohm, ale może to być np. 75 Ohm. W trybie dokładnym wpisz rzeczywisty pomiar DC; obliczenia użyją wartości z rozdzielczością 0,001 Ohm.", "Resistance of the middle OSL standard, typically about 50 ohm but e.g. 75 ohm is also supported. Exact mode uses the measured DC value with 0.001 ohm resolution.", "Widerstand des mittleren OSL-Normals, typisch etwa 50 Ohm; auch z.B. 75 Ohm ist möglich. Der exakte DC-Wert wird mit 0,001 Ohm Auflösung verwendet.", "Сопротивление среднего эталона OSL, обычно около 50 Ом; возможно, например, 75 Ом. Точный режим использует измеренное DC-значение с шагом 0,001 Ом.");
    case CFG_PARAM_OSL_RSHORT: return JEZYK_Wybierz("Rezystancja niskiego wzorca OSL, typowo około 5 Ohm. W tej konstrukcji nie jest to idealne zwarcie. W trybie dokładnym wpisz wynik omomierza.", "Resistance of the low OSL standard, typically about 5 ohm. In this design it is not an ideal short. Enter the ohmmeter result in exact mode.", "Widerstand des niedrigen OSL-Normals, typisch etwa 5 Ohm. In diesem Aufbau ist es kein idealer Kurzschluss. Im exakten Modus den Ohmmeterwert eingeben.", "Сопротивление низкого эталона OSL, обычно около 5 Ом. В этой конструкции это не идеальное КЗ. В точном режиме введите результат омметра.");
    case CFG_PARAM_OSL_ROPEN: return JEZYK_Wybierz("Rezystancja wysokiego wzorca OSL, typowo około 500 Ohm. W tej konstrukcji nie jest to idealne rozwarcie. W trybie dokładnym wpisz wynik omomierza.", "Resistance of the high OSL standard, typically about 500 ohm. In this design it is not an ideal open. Enter the ohmmeter result in exact mode.", "Widerstand des hohen OSL-Normals, typisch etwa 500 Ohm. In diesem Aufbau ist es kein idealer Leerlauf. Im exakten Modus den Ohmmeterwert eingeben.", "Сопротивление высокого эталона OSL, обычно около 500 Ом. В этой конструкции это не идеальный ХХ. В точном режиме введите результат омметра.");
    case CFG_PARAM_OSL_NSCANS: return JEZYK_Wybierz("Liczba pomiarów uśrednianych w każdym punkcie kalibracji OSL.", "Number of measurements averaged at each OSL calibration point.", "Anzahl der gemittelten Messungen je OSL-Kalibrierpunkt.", "Число измерений, усредняемых в каждой точке калибровки OSL.");
    case CFG_PARAM_MEAS_NSCANS: return JEZYK_Wybierz("Liczba próbek uśrednianych przy pomiarze pojedynczym.", "Number of samples averaged in single measurement.", "Anzahl gemittelter Messungen bei Einzelmessung.", "Число отсчётов, усредняемых при одиночном измерении.");
    case CFG_PARAM_PAN_NSCANS: return JEZYK_Wybierz("Liczba próbek uśrednianych dla każdego punktu wykresu.", "Number of samples averaged for each plot point.", "Anzahl gemittelter Messungen je Diagrammpunkt.", "Число отсчётов, усредняемых для каждой точки графика.");
    case CFG_PARAM_PAN_AUTOSPEED: return JEZYK_Wybierz("Kompromis pomiędzy szybkością odświeżania i stabilnością wykresu.", "Trade-off between refresh speed and plot stability.", "Kompromiss zwischen Aktualisierungsgeschwindigkeit und Diagrammstabilität.", "Компромисс между скоростью обновления и стабильностью графика.");
    case CFG_PARAM_S21_AUTOSPEED: return JEZYK_Wybierz("Kompromis pomiędzy szybkością i stabilnością automatycznego pomiaru S21.", "Trade-off between speed and stability of automatic S21 measurement.", "Kompromiss zwischen Geschwindigkeit und Stabilität der automatischen S21-Messung.", "Компромисс между скоростью и стабильностью автоматического измерения S21.");
    case CFG_PARAM_LIN_ATTENUATION: return JEZYK_Wybierz("Tłumienie liniowych wejść audio w dB. Zmiana wymaga restartu.", "Linear audio-input attenuation in dB. A restart is required after changing it.", "Dämpfung der linearen Audioeingänge in dB. Nach Änderung ist ein Neustart nötig.", "Ослабление линейных аудиовходов в дБ. После изменения требуется перезапуск.");
    case CFG_PARAM_BRIDGE_RM: return JEZYK_Wybierz("Parametr rezystancyjny mostka pomiarowego. Zmieniaj tylko po weryfikacji sprzętu.", "Measurement-bridge resistance parameter. Change only after hardware verification.", "Widerstandsparameter der Messbrücke. Nur nach Hardwareprüfung ändern.", "Параметр сопротивления измерительного моста. Изменять только после проверки аппаратуры.");
    case CFG_PARAM_BRIDGE_RADD: return JEZYK_Wybierz("Dodatkowa rezystancja mostka pomiarowego. Zmieniaj tylko po weryfikacji sprzętu.", "Additional measurement-bridge resistance. Change only after hardware verification.", "Zusätzlicher Widerstand der Messbrücke. Nur nach Hardwareprüfung ändern.", "Дополнительное сопротивление измерительного моста. Изменять только после проверки аппаратуры.");
    case CFG_PARAM_PAN_CENTER_F: return JEZYK_Wybierz("Określa, czy ustawiana częstotliwość jest początkiem czy środkiem zakresu.", "Chooses whether the entered frequency is the start or center of the span.", "Legt fest, ob die eingegebene Frequenz Anfang oder Mitte des Bereichs ist.", "Определяет, является ли заданная частота началом или центром диапазона.");
    case CFG_PARAM_COM_PORT: return JEZYK_Wybierz("Port używany do sterowania z komputera. Zmiana wymaga restartu.", "Port used for PC control. A restart is required after changing it.", "Port für PC-Steuerung. Nach Änderung ist ein Neustart nötig.", "Порт управления с ПК. После изменения требуется перезапуск.");
    case CFG_PARAM_COM_SPEED: return JEZYK_Wybierz("Prędkość transmisji portu szeregowego. Zmiana wymaga restartu.", "Serial-port data rate. A restart is required after changing it.", "Datenrate der seriellen Schnittstelle. Nach Änderung ist ein Neustart nötig.", "Скорость последовательного порта. После изменения требуется перезапуск.");
    case CFG_PARAM_LOWPWR_TIME: return JEZYK_Wybierz("Po tym czasie bezczynności ekran zostanie wyłączony. Dotknięcie go wybudzi.", "The screen turns off after this idle time. Touch it to wake it.", "Nach dieser Leerlaufzeit wird der Bildschirm abgeschaltet. Berühren zum Aufwecken.", "После этого времени бездействия экран выключится. Коснитесь его для пробуждения.");
    case CFG_PARAM_S11_SHOW: return JEZYK_Wybierz("Włącza dodatkowy wykres S11 w widoku panoramicznym.", "Enables an additional S11 plot in panoramic view.", "Aktiviert ein zusätzliches S11-Diagramm in der Panoramaansicht.", "Включает дополнительный график S11 в панорамном режиме.");
    case CFG_PARAM_S1P_TYPE: return JEZYK_Wybierz("Format danych Touchstone zapisywanych razem ze zrzutem pomiaru.", "Touchstone data format saved with the measurement snapshot.", "Touchstone-Datenformat, das mit dem Mess-Snapshot gespeichert wird.", "Формат Touchstone, сохраняемый вместе со снимком измерения.");
    case CFG_PARAM_BAND_FMIN: return JEZYK_Wybierz("Zmiana dolnej granicy wymaga ponownej pełnej kalibracji OSL.", "Changing the lower limit requires a new full OSL calibration.", "Eine Änderung der unteren Grenze erfordert eine neue vollständige OSL-Kalibrierung.", "Изменение нижней границы требует новой полной калибровки OSL.");
    case CFG_PARAM_BAND_FMAX: return JEZYK_Wybierz("Zalecany zakres domyślny kończy się na 600 MHz. Wyższy zakres jest zaawansowany: po zmianie wykonaj pełną OSL i sprawdź pasmo na znanych wzorcach.", "The recommended default range ends at 600 MHz. Higher range is advanced: after changing it, perform full OSL and verify the band with known standards.", "Der empfohlene Standardbereich endet bei 600 MHz. Höhere Bereiche sind erweitert: danach vollständige OSL durchführen und mit bekannten Normalen prüfen.", "Рекомендуемый диапазон по умолчанию заканчивается на 600 МГц. Для более высоких частот выполните полную OSL и проверьте диапазон на известных эталонах.");
    case CFG_PARAM_SCREENSHOT_FORMAT: return JEZYK_Wybierz("Format plików zrzutów zapisywanych na karcie SD.", "File format for screenshots saved on the SD card.", "Dateiformat der auf SD gespeicherten Screenshots.", "Формат файлов снимков, сохраняемых на SD-карте.");
    case CFG_PARAM_TDR_VF: return JEZYK_Wybierz("Współczynnik prędkości propagacji w kablu, podany w procentach.", "Cable propagation velocity factor, given as a percentage.", "Ausbreitungsfaktor des Kabels in Prozent.", "Коэффициент скорости распространения в кабеле, в процентах.");
    case CFG_PARAM_SHOW_HIDDEN: return JEZYK_Wybierz("Odsłania rzadkie parametry niskopoziomowe i serwisowe. Typ generatora, zakres, kwarc, adres I2C i plan harmoniczny są już widoczne bez tego przełącznika w Konfiguracji zaawansowanej.", "Reveals rare low-level and service parameters. Generator type, range, crystal, I2C address and harmonic plan are already visible in Advanced configuration without this switch.", "Zeigt seltene Low-Level- und Serviceparameter. Generatortyp, Bereich, Quarz, I2C-Adresse und Harmonischenplan sind in der erweiterten Konfiguration bereits ohne diesen Schalter sichtbar.", "Открывает редкие низкоуровневые и сервисные параметры. Тип генератора, диапазон, кварц, адрес I2C и план гармоник уже видны в расширенной конфигурации без этого переключателя.");
    case CFG_PARAM_SEREMUL: return JEZYK_Wybierz("Emulowany protokół komunikacji używany przez program komputerowy.", "Emulated communication protocol used by PC software.", "Emuliertes Kommunikationsprotokoll für PC-Software.", "Эмулируемый протокол связи для программ ПК.");
    case CFG_PARAM_BT_SPEED: return JEZYK_Wybierz("Prędkość transmisji modułu Bluetooth. Zmiana wymaga restartu.", "Bluetooth module data rate. A restart is required after changing it.", "Datenrate des Bluetooth-Moduls. Nach Änderung ist ein Neustart nötig.", "Скорость модуля Bluetooth. После изменения требуется перезапуск.");
    case CFG_PARAM_REGION: return JEZYK_Wybierz("Wybór regionu określającego zestaw pasm i częstotliwości.", "Selects the region that defines the band and frequency set.", "Wählt die Region für Band- und Frequenzbelegung.", "Выбирает регион, определяющий набор диапазонов и частот.");
    case CFG_PARAM_ORIENTATION: return JEZYK_Wybierz("Obrót obrazu i dotyku o 0 lub 180 stopni.", "Rotates display and touch coordinates by 0 or 180 degrees.", "Dreht Anzeige und Touch um 0 oder 180 Grad.", "Поворачивает изображение и сенсор на 0 или 180 градусов.");
    case CFG_PARAM_LOGLOG: return JEZYK_Wybierz("Sposób skalowania osi wykresu SWR.", "Scaling method for the SWR plot axes.", "Skalierung der Achsen im SWR-Diagramm.", "Способ масштабирования осей графика SWR.");
    case CFG_PARAM_CURSOR: return JEZYK_Wybierz("Sposób automatycznego ustawiania kursora na wykresie.", "Automatic cursor placement mode on the plot.", "Modus für automatische Cursorposition im Diagramm.", "Режим автоматической установки курсора на графике.");
    case CFG_PARAM_ATTENUATOR: return JEZYK_Wybierz("Nominalne tłumienie fizycznego tłumika 50 om włączanego między S2 i S1 w drugim kroku kalibracji S21.", "Nominal value of the physical 50-ohm attenuator inserted between S2 and S1 in S21 calibration step 2.", "Dämpferwert für die S21-Kalibrierung.", "Номинал аттенюатора для калибровки тракта S21.");
    case CFG_PARAM_JEZYK: return JEZYK_Wybierz("Język wszystkich nowych ekranów i komunikatów programu.", "Language used by program screens and messages.", "Sprache der Programmbildschirme und Meldungen.", "Язык экранов и сообщений программы.");
    case CFG_PARAM_TRYB_INTERFEJSU: return JEZYK_Wybierz("Podstawowy pokazuje zalecane metody; zaawansowany odsłania wybór metod i porównania.", "Basic shows recommended methods; advanced exposes method selection and comparisons.", "Grundmodus zeigt empfohlene Methoden; Erweitert zeigt Methodenwahl und Vergleiche.", "Базовый режим показывает рекомендуемые методы; расширенный открывает выбор и сравнение.");
    case CFG_PARAM_ShowLogoTime: return JEZYK_Wybierz("Czas pozostawienia ekranu startowego przed wejściem do menu.", "Time the startup screen remains visible before the menu.", "Anzeigedauer des Startbildschirms vor dem Menü.", "Время показа стартового экрана перед меню.");
    case CFG_PARAM_UI_MOTYW: return JEZYK_Wybierz("Pole zgodności ze starszymi konfiguracjami; używany jest tylko wygląd klasyczny.", "Compatibility field for older configurations; only classic appearance is used.", "Kompatibilitätsfeld für ältere Konfigurationen; nur die klassische Ansicht wird verwendet.", "Поле совместимости со старыми конфигурациями; используется только классический вид.");
    case CFG_PARAM_TDR_VF_X10000: return JEZYK_Wybierz("Dokładny współczynnik Vf TDR; 6600 oznacza 0,6600.", "Precise TDR Vf; 6600 means 0.6600.", "Genauer TDR-Vf; 6600 bedeutet 0,6600.", "Точный Vf TDR; 6600 означает 0,6600.");
    case CFG_PARAM_HARMONICZNA_MAX: return JEZYK_Wybierz("H3 jest ustawieniem domyślnym. H5/H7 odblokowują wyższy zakres eksperymentalny; po zmianie wykonaj OSL i weryfikację pasma na wzorcach.", "H3 is the default. H5/H7 unlock a higher experimental range; after changing it, perform OSL and verify the band with standards.", "H3 ist Standard. H5/H7 öffnen einen höheren experimentellen Bereich; danach OSL und Bandprüfung mit Normalen durchführen.", "H3 используется по умолчанию. H5/H7 открывают более высокий экспериментальный диапазон; после изменения выполните OSL и проверку на эталонах.");
    case CFG_PARAM_PORT_EXT_PS: return JEZYK_Wybierz("Opóźnienie w jedną stronę od płaszczyzny OSL. Koryguje fazę w ekranach S11, ale nie straty kabla ani TDR/L/C/kwarc; 0 wyłącza.", "One-way delay from the OSL reference plane. Corrects phase in S11 views, but not cable loss or TDR/L/C/crystal; 0 disables it.", "Einwegverzögerung ab OSL-Bezugsebene. Korrigiert die Phase in S11, nicht aber Kabelverlust oder TDR/L/C/Quarz; 0 schaltet aus.", "Односторонняя задержка от плоскости OSL. Корректирует фазу S11, но не потери кабеля и не TDR/L/C/кварц; 0 отключает.");
    case CFG_PARAM_ZESTAW_IKON: return JEZYK_Wybierz("Wybór pełnego stylu interfejsu: Retro (ciemny brąz, mosiądz i pergaminowe ikony) albo Klasyczny niebieski (granat, biel i cyjan). Zmiana obejmuje paletę, ramki, pasek statusu i ikony; nie wpływa na pomiary.", "Full interface style: Retro (dark brown, brass and parchment icons) or Classic blue (navy, white and cyan). The change covers palette, frames, status bar and icons; measurements are unaffected.", "Vollständiger Oberflächenstil: Retro (dunkles Braun, Messing und Pergament-Symbole) oder Klassisch Blau (Marineblau, Weiß und Cyan). Palette, Rahmen, Statusleiste und Symbole werden gemeinsam umgeschaltet; Messungen bleiben unverändert.", "Полный стиль интерфейса: Ретро (тёмно-коричневый, латунь и пергаментные иконки) или Классический синий (тёмно-синий, белый и голубой). Меняются палитра, рамки, строка состояния и иконки; на измерения это не влияет.");
    case CFG_PARAM_TDR_FILTR_CZASU: return JEZYK_Wybierz("Eksperymentalne wygładzanie odpowiedzi czasowej po IFFT. Nie zwiększa rzeczywistej rozdzielczości TDR i może tłumić bardzo wąskie odbicia.", "Experimental smoothing after IFFT. It does not improve true TDR resolution and may attenuate very narrow reflections.", "Experimentelle Glättung nach IFFT. Sie erhöht die reale TDR-Auflösung nicht und kann sehr schmale Reflexionen dämpfen.", "Экспериментальное сглаживание после IFFT. Не повышает реальное разрешение TDR и может ослаблять очень узкие отражения.");
    case CFG_PARAM_TDR_POROWNAJ_FILTR: return JEZYK_Wybierz("Gdy włączone, TDR rysuje jednocześnie przebieg surowy i filtrowany, aby ocenić wpływ metody.", "When enabled, TDR draws raw and filtered responses together so the method's effect is visible.", "Zeigt bei Aktivierung Roh- und Filterkurve gleichzeitig, damit der Einfluss sichtbar bleibt.", "При включении TDR одновременно показывает исходную и фильтрованную кривые для оценки влияния метода.");
    case CFG_PARAM_MASKA_MODELI_RF: return JEZYK_Wybierz("Maska modeli używanych w porównaniu Elementów RF. Ustawienie zaawansowane; pozostaw wszystkie metody, jeśli nie prowadzisz świadomej diagnostyki.", "Models included in RF-component comparison. Advanced setting; keep all methods unless performing deliberate diagnostics.", "Modelle im HF-Bauteilevergleich. Erweiterte Einstellung; für normale Arbeit alle Methoden aktiviert lassen.", "Модели сравнения ВЧ-элементов. Расширенная настройка; при обычной работе оставьте все методы.");
    case CFG_PARAM_MASKA_METOD_Q: return JEZYK_Wybierz("Maska metod używanych w porównaniu dobroci Q. Ustawienie zaawansowane; pełne porównanie najlepiej wykrywa niezgodność metod.", "Methods included in Q comparison. Advanced setting; the full comparison is best at detecting disagreement.", "Methoden im Q-Vergleich. Erweiterte Einstellung; der vollständige Vergleich erkennt Abweichungen am sichersten.", "Методы сравнения Q. Расширенная настройка; полное сравнение лучше выявляет расхождения.");
    case CFG_PARAM_KABEL_PROFIL_AKTYWNY: return JEZYK_Wybierz("Usuwa wpływ zapisanego profilu kabla OPEN/SHORT z pomiarów antenowych. Nie jest zwykłym odejmowaniem impedancji.", "Removes the saved OPEN/SHORT cable profile from antenna measurements. This is not simple impedance subtraction.", "Entfernt das gespeicherte OPEN/SHORT-Kabelprofil aus Antennenmessungen. Keine einfache Impedanzsubtraktion.", "Удаляет сохранённый профиль кабеля OPEN/SHORT из измерений антенны. Это не простое вычитание импеданса.");
    case CFG_PARAM_KOLOR_KRZYWEJ_SWR: return JEZYK_Wybierz("Kolor głównej krzywej na wykresie SWR w stylu retro. Nie zmienia siatki ani drugiej krzywej R/X.", "Color of the main curve on the retro-style SWR chart. Does not change the grid or the secondary R/X curve.", "Farbe der Hauptkurve im Retro-SWR-Diagramm. Ändert weder das Gitter noch die zweite R/X-Kurve.", "Цвет основной кривой на графике КСВ в ретро-стиле. Не влияет на сетку и вторую кривую R/X.");
    default: return 0;
    }
}

const char *CFG_GetStringDescr(uint32_t param_idx)
{
    const char *tekst;
    if (param_idx >= cfg_ch_descr_table_num)
        return "";

    tekst = CFG_OpisLokalny(cfg_ch_descr_table[param_idx].id);
    return tekst != 0 ? tekst : cfg_ch_descr_table[param_idx].dstring;
}

const char *CFG_GetStringName(uint32_t param_idx)
{
    const char *tekst;
    if (param_idx >= cfg_ch_descr_table_num)
        return "";

    tekst = CFG_NazwaLokalna(cfg_ch_descr_table[param_idx].id);
    return tekst != 0 ? tekst : cfg_ch_descr_table[param_idx].idstring;
}

//===============================================================================
// Configuration parameters setting window
//===============================================================================
#include "LCD.h"
#include "touch.h"
#include "font.h"
#include "textbox.h"
#include "ui_wspolny.h"
extern void Sleep(uint32_t);

static uint32_t selected_param = 0;
static TEXTBOX_CTX_t *pctx = 0;
static uint32_t hbNameIdx = 0;
static uint32_t hbDescrIdx = 0;
static uint32_t hbDescrIdx2 = 0;
static uint32_t hbValIdx = 0;
static uint32_t hbIndexIdx = 0;
static uint32_t hbPrevValueIdx = 0;
static uint32_t hbNextValueIdx = 0;

//Added by KD8CEC, Version 0.35
static uint32_t hbPrevValueIdxBig = 0;
static uint32_t hbNextValueIdxBig = 0;

static uint8_t CFG_CzyPoczatekUTF8(const char *tekst, size_t indeks)
{
    return (uint8_t)(((unsigned char)tekst[indeks] & 0xC0U) != 0x80U);
}

static size_t CFG_UTF8_BezpiecznaGranica(const char *tekst, size_t maksimum)
{
    const size_t dlugosc = tekst != 0 ? strlen(tekst) : 0U;

    if (maksimum > dlugosc)
        maksimum = dlugosc;
    while (maksimum > 0U && maksimum < dlugosc && !CFG_CzyPoczatekUTF8(tekst, maksimum))
        --maksimum;
    return maksimum;
}

static void CFG_KopiujZakresUTF8(char *cel, size_t rozmiar, const char *zrodlo, size_t ile)
{
    if (cel == 0 || rozmiar == 0U)
        return;
    cel[0] = '\0';
    if (zrodlo == 0)
        return;

    if (ile > rozmiar - 1U)
        ile = rozmiar - 1U;
    ile = CFG_UTF8_BezpiecznaGranica(zrodlo, ile);
    memcpy(cel, zrodlo, ile);
    cel[ile] = '\0';
}

static size_t CFG_ZnajdzPodzialOpisu(const char *opis, uint32_t maks_szerokosc)
{
    const size_t dlugosc = strlen(opis);
    size_t i;
    size_t ostatnia_granica = 0U;
    size_t ostatnia_spacja = 0U;
    char proba[192];

    for (i = 1U; i <= dlugosc; ++i)
    {
        if (i < dlugosc && !CFG_CzyPoczatekUTF8(opis, i))
            continue;

        CFG_KopiujZakresUTF8(proba, sizeof(proba), opis, i);
        if (FONT_GetStrPixelWidth(FONT_FRAN, proba) > maks_szerokosc)
            break;

        ostatnia_granica = i;
        if (i < dlugosc && opis[i - 1U] == ' ')
            ostatnia_spacja = i;
    }

    if (ostatnia_spacja > 0U)
        return ostatnia_spacja;
    if (ostatnia_granica > 0U)
        return ostatnia_granica;
    return CFG_UTF8_BezpiecznaGranica(opis, 1U);
}

static void CFG_PodzielOpis(const char *opis, char *linia1, uint32_t rozmiar1, char *linia2, uint32_t rozmiar2)
{
    const uint32_t maks_szerokosc = 432U;
    size_t podzial;
    const char *druga;

    if (linia1 == 0 || linia2 == 0 || rozmiar1 == 0U || rozmiar2 == 0U)
        return;
    linia1[0] = '\0';
    linia2[0] = '\0';
    if (opis == 0 || opis[0] == '\0')
        return;

    if (FONT_GetStrPixelWidth(FONT_FRAN, opis) <= maks_szerokosc)
    {
        CFG_KopiujZakresUTF8(linia1, rozmiar1, opis, strlen(opis));
        return;
    }

    podzial = CFG_ZnajdzPodzialOpisu(opis, maks_szerokosc);
    CFG_KopiujZakresUTF8(linia1, rozmiar1, opis, podzial);

    druga = opis + podzial;
    while (*druga == ' ')
        ++druga;
    CFG_KopiujZakresUTF8(linia2, rozmiar2, druga, strlen(druga));
}

void SetConfigButtonsText()
{
    /* Długie tłumaczenia rosyjskie wymagają większych buforów. Trzymamy je w
     * zewnętrznym SDRAM, aby nie odbierać miejsca ciasnej pamięci wewnętrznej. */
    static char opis1[192] __attribute__((section(".user_sdram")));
    static char opis2[192] __attribute__((section(".user_sdram")));
    static char indeks[48];
    const char *nazwa;
    const char *wartosc;
    TEXTBOX_t *pole_nazwy;
    TEXTBOX_t *pole_wartosci;
    //added by KD8CEC
    if (cfg_ch_descr_table[selected_param].id == CFG_PARAM_SI5351_CORR)
    {
        TEXTBOX_SetText(pctx, hbPrevValueIdxBig, " << * 200 ");
        TEXTBOX_SetText(pctx, hbNextValueIdxBig, " * 200 >> ");
    }
    else
    {
        TEXTBOX_SetText(pctx, hbPrevValueIdxBig, "");
        TEXTBOX_SetText(pctx, hbNextValueIdxBig, "");
    }

    snprintf(indeks, sizeof(indeks),
             JEZYK_Wybierz("Ustawienie %lu/%lu", "Setting %lu/%lu", "Einstellung %lu/%lu", "Настройка %lu/%lu"),
             (unsigned long)(selected_param + 1U), (unsigned long)cfg_ch_descr_table_num);
    TEXTBOX_SetText(pctx, hbIndexIdx, indeks);
    nazwa = CFG_GetStringName(selected_param);
    wartosc = CFG_GetStringValue(selected_param);
    pole_nazwy = TEXTBOX_Find(pctx, hbNameIdx);
    pole_wartosci = TEXTBOX_Find(pctx, hbValIdx);
    if (pole_nazwy != 0)
        pole_nazwy->font = FONT_GetStrPixelWidth(FONT_FRANBIG, nazwa) <= 426U ? FONT_FRANBIG : FONT_FRAN;
    if (pole_wartosci != 0)
        pole_wartosci->font = FONT_GetStrPixelWidth(FONT_FRANBIG, wartosc) <= 286U ? FONT_FRANBIG : FONT_FRAN;
    TEXTBOX_SetText(pctx, hbNameIdx, nazwa);
    TEXTBOX_SetText(pctx, hbValIdx, wartosc);
    CFG_PodzielOpis(CFG_GetStringDescr(selected_param), opis1, sizeof(opis1), opis2, sizeof(opis2));
    TEXTBOX_SetText(pctx, hbDescrIdx, opis1);
    TEXTBOX_SetText(pctx, hbDescrIdx2, opis2);
    TEXTBOX_Find(pctx, hbPrevValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
    TEXTBOX_Find(pctx, hbNextValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
}

static void _hit_prev(void)
{
    for (;;)
    {
        if (--selected_param >= cfg_ch_descr_table_num)
            selected_param = cfg_ch_descr_table_num - 1;
        //Bypass changeable parameters for which isvalid() is defined and it returns zero
        if (cfg_ch_descr_table[selected_param].isvalid != 0)
        {
            if (cfg_ch_descr_table[selected_param].isvalid())
                break;
        }
        else
            break;
    }

    //Changed by KD8CEC
    SetConfigButtonsText();
    /*
    TEXTBOX_SetText(pctx, hbNameIdx, CFG_GetStringName(selected_param));
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    CFG_PodzielOpis(CFG_GetStringDescr(selected_param), opis1, sizeof(opis1), opis2, sizeof(opis2));
    TEXTBOX_SetText(pctx, hbDescrIdx, opis1);
    TEXTBOX_SetText(pctx, hbDescrIdx2, opis2);
    TEXTBOX_Find(pctx, hbPrevValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
    TEXTBOX_Find(pctx, hbNextValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
    */
}

static void _hit_next(void)
{
    for (;;)
    {
        selected_param++;
        if (selected_param >= cfg_ch_descr_table_num)
            selected_param = 0;
        //Bypass changeable parameters for which isvalid() is defined and it returns zero
        if (cfg_ch_descr_table[selected_param].isvalid != 0)
        {
            if (cfg_ch_descr_table[selected_param].isvalid())
                break;
        }
        else
            break;
    }

    SetConfigButtonsText();
    /*
    TEXTBOX_SetText(pctx, hbNameIdx, CFG_GetStringName(selected_param));
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    CFG_PodzielOpis(CFG_GetStringDescr(selected_param), opis1, sizeof(opis1), opis2, sizeof(opis2));
    TEXTBOX_SetText(pctx, hbDescrIdx, opis1);
    TEXTBOX_SetText(pctx, hbDescrIdx2, opis2);
    TEXTBOX_Find(pctx, hbPrevValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
    TEXTBOX_Find(pctx, hbNextValueIdx)->nowait = cfg_ch_descr_table[selected_param].repeatdelay;
    */
}

static void _hit_save(void)
{
    /* Pełny edytor nie może omijać zabezpieczeń RF160. Jeśli użytkownik
       spróbuje wpisać >160 MHz bez jawnego trybu eksperymentalnego,
       walidacja przywróci bezpieczny plan przed zapisem. */
    CFG_WalidujKrytyczne();
    CFG_Flush();
    GEN_Init(); //In case if synthesizer type has changed
    if (0 != resetRequired)
    {
        Sleep(200);
        NVIC_SystemReset();
    }
    rqExit = 1;
}

static void _hit_ex(void)
{
    rqExit = 1;
}

static void _hit_prev_value(void)
{
    uint32_t currentValue = CFG_GetParam(cfg_ch_descr_table[selected_param].id);
    uint32_t prevValue = CFG_GetPrevValue(selected_param, currentValue);
    CFG_SetParam(cfg_ch_descr_table[selected_param].id, prevValue);
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    resetRequired += cfg_ch_descr_table[selected_param].resetRequired;
}

static void _hit_next_value(void)
{
    uint32_t currentValue = CFG_GetParam(cfg_ch_descr_table[selected_param].id);
    uint32_t nextValue = CFG_GetNextValue(selected_param, currentValue);
    CFG_SetParam(cfg_ch_descr_table[selected_param].id, nextValue);
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    resetRequired += cfg_ch_descr_table[selected_param].resetRequired;
}

//added by KD8CEC
static void _hit_prev_valueBig(void)
{
    uint32_t currentValue = CFG_GetParam(cfg_ch_descr_table[selected_param].id);
    uint32_t prevValue = currentValue - 200;

    CFG_SetParam(cfg_ch_descr_table[selected_param].id, prevValue);
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    resetRequired += cfg_ch_descr_table[selected_param].resetRequired;
}

static void _hit_next_valueBig(void)
{
    uint32_t currentValue = CFG_GetParam(cfg_ch_descr_table[selected_param].id);
    uint32_t nextValue = currentValue + 200;
    CFG_SetParam(cfg_ch_descr_table[selected_param].id, nextValue);
    TEXTBOX_SetText(pctx, hbValIdx, CFG_GetStringValue(selected_param));
    resetRequired += cfg_ch_descr_table[selected_param].resetRequired;
}

extern uint8_t NotSleepMode;

/* ========================================================================
 * Dokumentacja automatyczna
 * ======================================================================== */
uint32_t CFG_DokumentacjaLiczbaParametrow(void)
{
    return cfg_ch_descr_table_num;
}

const char *CFG_DokumentacjaIdParametru(uint32_t param_idx)
{
    if (param_idx >= cfg_ch_descr_table_num)
        return "";
    return cfg_ch_descr_table[param_idx].idstring != 0 ? cfg_ch_descr_table[param_idx].idstring : "";
}

const char *CFG_DokumentacjaNazwaParametru(uint32_t param_idx)
{
    return CFG_GetStringName(param_idx);
}

const char *CFG_DokumentacjaOpisParametru(uint32_t param_idx)
{
    return CFG_GetStringDescr(param_idx);
}

uint8_t CFG_DokumentacjaCzyZaawansowany(uint32_t param_idx)
{
    if (param_idx >= cfg_ch_descr_table_num)
        return 0U;
    return cfg_ch_descr_table[param_idx].isvalid != 0 ? 1U : 0U;
}


void CFG_DokumentacjaRysujParametr(uint32_t param_idx)
{
    char indeks[40];
    char opis1[192];
    char opis2[192];
    const char *nazwa;
    const char *wartosc;
    const LCDColor tlo_pola = UI_KolorTlaPola();

    if (cfg_ch_descr_table_num == 0U)
        return;
    if (param_idx >= cfg_ch_descr_table_num)
        param_idx = cfg_ch_descr_table_num - 1U;

    nazwa = CFG_GetStringName(param_idx);
    wartosc = CFG_GetStringValue(param_idx);
    CFG_PodzielOpis(CFG_GetStringDescr(param_idx), opis1, sizeof(opis1), opis2, sizeof(opis2));

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_EDYTOR_KONFIGURACJI));
    UI_RysujPanel(10, 74, 460, 144, 0, UI_STYL_NORMALNY);

    UI_RysujPrzycisk(10, 36, 150, 32, JEZYK_Tekst(TEKST_POPRZEDNI_PARAMETR), UI_STYL_NORMALNY, FONT_FRAN);
    UI_RysujPrzycisk(320, 36, 150, 32, JEZYK_Tekst(TEKST_NASTEPNY_PARAMETR), UI_STYL_NORMALNY, FONT_FRAN);
    snprintf(indeks, sizeof(indeks),
             JEZYK_Wybierz("Ustawienie %lu/%lu", "Setting %lu/%lu", "Einstellung %lu/%lu", "Настройка %lu/%lu"),
             (unsigned long)(param_idx + 1U), (unsigned long)cfg_ch_descr_table_num);
    FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), UI_KolorTlaEkranu(), 176, 45, indeks);

    FONT_Write(FONT_FRANBIG, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, 22, 82, nazwa);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo_pola, 22, 110, opis1);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), tlo_pola, 22, 127, opis2);

    UI_RysujPrzycisk(22, 151, 58, 44, "<", UI_STYL_NORMALNY, FONT_FRANBIG);
    UI_RysujPrzycisk(90, 151, 300, 44, wartosc, UI_STYL_AKCENT, FONT_FRANBIG);
    UI_RysujPrzycisk(400, 151, 58, 44, ">", UI_STYL_NORMALNY, FONT_FRANBIG);

    if (cfg_ch_descr_table[param_idx].id == CFG_PARAM_SI5351_CORR)
    {
        FONT_Write(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, 40, 202, "<< * 200");
        FONT_Write_RightAlign(FONT_FRAN, UI_KolorRamki(UI_STYL_AKCENT), tlo_pola, 310, 202, 448, "* 200 >>");
    }
    else if (CFG_DokumentacjaCzyZaawansowany(param_idx))
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), tlo_pola, 22, 202,
                   JEZYK_Wybierz("Parametr zaawansowany / sprzętowy", "Advanced / hardware parameter",
                                 "Erweiterter / Hardware-Parameter",
                                 "Расширенный / аппаратный параметр"));
    }

    /*
     * Dolny pasek jest wspólny dla całego interfejsu. Edytor konfiguracji
     * nie może mieć własnych, szerokich przycisków w tej strefie, bo wtedy
     * dotyk i audyt geometrii widzą inny układ niż pozostałe ekrany.
     */
    UI_RysujPrzycisk(0U, UI_DOLNY_PASEK_Y,
                     UI_DOLNY_PRZYCISK_SZEROKOSC, UI_DOLNY_PRZYCISK_WYSOKOSC,
                     JEZYK_Tekst(TEKST_WSTECZ), UI_STYL_POWROT, FONT_FRAN);
    UI_RysujPrzycisk(UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP,
                     UI_DOLNY_PASEK_Y,
                     UI_DOLNY_PRZYCISK_SZEROKOSC, UI_DOLNY_PRZYCISK_WYSOKOSC,
                     JEZYK_Tekst(TEKST_ZAPISZ), UI_STYL_AKTYWNY, FONT_FRAN);
}

// Changeable parameters editor window
// See these parameters described in cfg_ch_descr_table
static uint32_t CFG_ZnajdzIndeksParametru(CFG_PARAM_t id)
{
    uint32_t i;
    for (i = 0U; i < cfg_ch_descr_table_num; ++i)
    {
        if (cfg_ch_descr_table[i].id == id)
            return i;
    }
    return 0U;
}

void CFG_ParamWndOdParametru(CFG_PARAM_t parametr_startowy)
{
    while (TOUCH_IsPressed())
        ;
    rqExit = 0;
    resetRequired = 0;
    selected_param = CFG_ZnajdzIndeksParametru(parametr_startowy);

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_EDYTOR_KONFIGURACJI));
    UI_RysujPanel(10, 74, 460, 144, 0, UI_STYL_NORMALNY);

    TEXTBOX_t hbPrevParam = {.x0 = 10, .y0 = 36, .text = " < Prev param ", .tekst_id = TEXTBOX_TEKST(TEKST_POPRZEDNI_PARAMETR), .font = FONT_FRAN, .width = 150, .height = 32, .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hit_prev};
    TEXTBOX_t hbIndex = {.x0 = 170, .y0 = 36, .text = " ", .font = FONT_FRAN, .width = 140, .height = 32, .center = 1, .border = 0, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaEkranu(), .cb = 0, .nowait = 1};
    TEXTBOX_t hbNextParam = {.x0 = 320, .y0 = 36, .text = " Next param > ", .tekst_id = TEXTBOX_TEKST(TEKST_NASTEPNY_PARAMETR), .font = FONT_FRAN, .width = 150, .height = 32, .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hit_next};
    TEXTBOX_t hbEx = {.x0 = 0, .y0 = UI_DOLNY_PASEK_Y, .text = " Back ",
                      .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ), .rola = TEXTBOX_ROLA_WSTECZ,
                      .font = FONT_FRAN, .width = UI_DOLNY_PRZYCISK_SZEROKOSC,
                      .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1,
                      .fgcolor = UI_KolorTekstu(UI_STYL_POWROT),
                      .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_POWROT), .cb = _hit_ex};
    TEXTBOX_t hbSave = {.x0 = UI_DOLNY_PRZYCISK_SZEROKOSC + UI_DOLNY_PRZYCISK_ODSTEP,
                        .y0 = UI_DOLNY_PASEK_Y, .text = " Save ",
                        .tekst_id = TEXTBOX_TEKST(TEKST_ZAPISZ), .font = FONT_FRAN,
                        .width = UI_DOLNY_PRZYCISK_SZEROKOSC,
                        .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1,
                        .fgcolor = UI_KolorTekstu(UI_STYL_AKTYWNY),
                        .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKTYWNY), .cb = _hit_save};

    TEXTBOX_t hbParamName = {.x0 = 22, .y0 = 82, .text = "    ", .font = FONT_FRANBIG, .width = 436, .height = 26, .center = 0, .border = 0, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPola(), .cb = 0, .nowait = 1};
    TEXTBOX_t hbParamDescr = {.x0 = 22, .y0 = 110, .text = "    ", .font = FONT_FRAN, .width = 436, .height = 16, .center = 0, .border = 0, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPola(), .cb = 0, .nowait = 1};
    TEXTBOX_t hbParamDescr2 = {.x0 = 22, .y0 = 127, .text = "    ", .font = FONT_FRAN, .width = 436, .height = 16, .center = 0, .border = 0, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPola(), .cb = 0, .nowait = 1};
    TEXTBOX_t hbValue = {.x0 = 90, .y0 = 151, .text = "    ", .font = FONT_FRANBIG, .width = 300, .height = 44, .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_AKCENT), .cb = 0, .nowait = 1};
    TEXTBOX_t hbPrevValue = {.x0 = 22, .y0 = 151, .text = " < ", .font = FONT_FRANBIG, .width = 58, .height = 44, .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hit_prev_value};
    TEXTBOX_t hbNextValue = {.x0 = 400, .y0 = 151, .text = " > ", .font = FONT_FRANBIG, .width = 58, .height = 44, .center = 1, .border = 1, .fgcolor = UI_KolorTekstu(UI_STYL_NORMALNY), .bgcolor = UI_KolorTlaPrzycisku(UI_STYL_NORMALNY), .cb = _hit_next_value};

    // Duzy krok jest pokazywany tylko przy korekcji generatora Si5351.
    TEXTBOX_t hbPrevValueBig = {.x0 = 22, .y0 = 200, .text = "", .font = FONT_FRAN, .width = 150, .height = 22, .center = 1, .border = 0, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPola(), .cb = _hit_prev_valueBig};
    TEXTBOX_t hbNextValueBig = {.x0 = 308, .y0 = 200, .text = "", .font = FONT_FRAN, .width = 150, .height = 22, .center = 1, .border = 0, .fgcolor = UI_KolorRamki(UI_STYL_AKCENT), .bgcolor = UI_KolorTlaPola(), .cb = _hit_next_valueBig};

    TEXTBOX_CTX_t ctx = {0};
    pctx = &ctx;
    TEXTBOX_InitContext(pctx);

    TEXTBOX_Append(pctx, &hbPrevParam);
    hbIndexIdx = TEXTBOX_Append(pctx, &hbIndex);
    TEXTBOX_Append(pctx, &hbNextParam);
    TEXTBOX_Append(pctx, &hbEx);
    TEXTBOX_Append(pctx, &hbSave);
    hbPrevValueIdx = TEXTBOX_Append(pctx, &hbPrevValue);
    hbNextValueIdx = TEXTBOX_Append(pctx, &hbNextValue);

    //Added by KD8CEC, Version 0.35
    hbPrevValueIdxBig = TEXTBOX_Append(pctx, &hbPrevValueBig);
    hbNextValueIdxBig = TEXTBOX_Append(pctx, &hbNextValueBig);

    hbNameIdx = TEXTBOX_Append(pctx, &hbParamName);
    hbDescrIdx = TEXTBOX_Append(pctx, &hbParamDescr);
    hbDescrIdx2 = TEXTBOX_Append(pctx, &hbParamDescr2);
    hbValIdx = TEXTBOX_Append(pctx, &hbValue);
    TEXTBOX_DrawContext(&ctx);
    SetConfigButtonsText();

    NotSleepMode = 1;
    for (;;)
    {
        if (TEXTBOX_HitTest(&ctx))
        {
            if (rqExit)
            {
                CFG_Init();
                break;
            }
            Sleep(50);
        }
        Sleep(0);
    }
    NotSleepMode = 0;
}

void CFG_ParamWnd(void)
{
    /*
     * Główne wejście „Konfiguracja zaawansowana” zaczyna się od sprzętu RF.
     * OSL ma własny, czytelny ekran w Ustawienia > Kalibracja.
     */
    CFG_ParamWndOdParametru(CFG_PARAM_SYNTH_TYPE);
}
