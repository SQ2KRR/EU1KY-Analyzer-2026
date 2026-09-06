/*
 *   (c) Wolfgang Kiefer, DH1AKF, 2018
 *   woki@online.de
 *
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include "arm_math.h"
#include <complex.h>
#include <string.h>

#include "LCD.h"
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
#include "textbox.h"
#include "spectr.h"
#include "sdram_heap.h"
#include "jezyk.h"
#include "ui_wspolny.h"
#include "ui_edytor_liczby.h"
#include "wejscia_uzytkownika.h"

extern int16_t audioBuf[(NSAMPLES + NDUMMY) * 2];
extern void GEN_SetLOFreq(uint32_t frqu1);
extern unsigned long GetUpper(int i);
extern unsigned long GetLower(int i);
float *rfft_mags;

extern float rfft_input[NSAMPLES];
extern float rfft_output[NSAMPLES];
extern const float complex *prfft;
extern float windowfunc[NSAMPLES];

int SpectrumExec(int graph);
static void Skaner_RysujEkranGlowny(void);
static void Skaner_RysujPostepSkanowania(uint32_t czestotliwosc_hz);
static void Skaner_WodospadResetujSrednia(void);
static void Skaner_WodospadZbudujLinie(void);
static int Skaner_RF_WykonajPrzebiegEnergii(uint8_t pokaz_postep);
static int Skaner_RF_PrzygotujWodospadNiezaleznie(void);

#define Fieldw1 70
#define FieldH 36

static const uint32_t Scale_Factors[] = {
    10, 8, 10, 10, 8, 10, 10, 8, 10, 10, 8, 10, 10, 8, 10, 10,
    10, 10, 8, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
};
//static int16_t maxMag;
//static int16_t mag;
static int AreaSelected, cycl3;
static unsigned long upper, lower;
static BANDSPAN span;
static uint8_t skaner_zakres_dokladny;
static int selector, sExit;
static int sScan_Meas, sRepaint;
static LCDPoint pt;
static int FreqFound;
static uint32_t f1; // kHz
static unsigned long actFreq;
//static uint32_t  power00;
static float SumVal;
float value1, value2, value3;

static void Calc_Power_fft_audiobuf() __attribute__((unused));

static void Calc_fft_audiobuf(int ch)
{
    //Only one channel
    int i;
    arm_rfft_fast_instance_f32 S;

    int16_t *pBuf = &audioBuf[NDUMMY + (ch != 0)];
    for (i = 0; i < NSAMPLES; i++)
    {

        rfft_input[i] = (float)*pBuf * windowfunc[i]; // Blackman window
        pBuf += 2;
    }

    arm_rfft_fast_init_f32(&S, NSAMPLES);
    arm_rfft_fast_f32(&S, rfft_input, rfft_output, 0);

    for (i = 0; i < NSAMPLES / 2; i++)
    {
        float complex binf = prfft[i];
        rfft_mags[i] = cabsf(binf) / (NSAMPLES / 2);
    }
}

static void Calc_Power_fft_audiobuf() // calculate u * i ->power
{
    //two channels
    int i;
    arm_rfft_fast_instance_f32 S;

    int16_t *pBuf = &audioBuf[NDUMMY + 0];
    for (i = 0; i < NSAMPLES; i++)
    {
        rfft_input[i] = (float)*pBuf * windowfunc[i]; // Blackman window
        pBuf += 2;
    }

    arm_rfft_fast_init_f32(&S, NSAMPLES);
    arm_rfft_fast_f32(&S, rfft_input, rfft_output, 0);

    for (i = 0; i < NSAMPLES / 2; i++)
    {
        float complex binf = prfft[i];
        rfft_mags[i] = cabsf(binf) / (NSAMPLES / 2);
    }

    pBuf = &audioBuf[NDUMMY + 1];
    for (i = 0; i < NSAMPLES; i++)
    {
        rfft_input[i] = (float)*pBuf * windowfunc[i]; // Blackman window
        pBuf += 2;
    }

    arm_rfft_fast_init_f32(&S, NSAMPLES);
    arm_rfft_fast_f32(&S, rfft_input, rfft_output, 0);

    for (i = 0; i < NSAMPLES / 2; i++)
    {
        float complex binf1 = prfft[i];
        rfft_mags[i] = cabsf(binf1) / (NSAMPLES / 2);
    }
}

static float Maxmag, Average;
static float skaner_ostatni_szczyt_fft;
static float skaner_ostatnie_tlo_fft;
static float skaner_ostatni_stosunek_db;

/* Os częstotliwości ma własny, stały pas nad historią. Dzięki temu użytkownik
 * od razu widzi gdzie na szerokim zakresie leży dana stacja, a napisy nie są
 * przesuwane razem z kolejnymi liniami wodospadu. */
#define SKANER_WODOSPAD_SKALA_Y_GORA 129U
#define SKANER_WODOSPAD_SKALA_Y_DOL  145U
#define SKANER_WODOSPAD_Y_GORA       146U
#define SKANER_WODOSPAD_Y_DOL        218U
#define SKANER_WODOSPAD_PELNY_SKALA_Y_GORA 0U
#define SKANER_WODOSPAD_PELNY_SKALA_Y_DOL  17U
#define SKANER_WODOSPAD_PELNY_GORA          18U
#define SKANER_WODOSPAD_PELNY_DOL           271U

typedef struct
{
    uint8_t wazny;
    uint32_t czestotliwosc_hz;
    float stosunek_db;
    float moc_dbm;
} SKANER_WYNIK_t;

/*
 * Wodospad RF nie może opierać koloru na bezwzględnej amplitudzie FFT.
 * Wzmocnienie i tło toru zmieniają się z częstotliwością, więc szeroki skan
 * (np. 88..108 MHz) potrafił zostać znormalizowany do jednolitego niebieskiego
 * pola mimo pewnego sygnału 20+ dB ponad lokalnym tłem.
 *
 * Dla każdego położenia LO zapisujemy więc lokalny stosunek szczyt/tło w dB.
 * Jest to dokładnie ta sama wielkość, którą użytkownik widzi w polu
 * „Szczyt / tło FFT”, tylko zachowana dla całego przebiegu.
 */
/*
 * RFSCAN5: wodospad nie korzysta już z maksimum pojedynczego prążka FFT.
 * To maksimum jest bardzo dobrym detektorem wąskiej nośnej, ale w samym
 * szumie rośnie wraz z liczbą przeglądanych prążków i na sprzęcie dawało
 * pozorne 14..16 dB. Dodatkowo szeroka stacja WFM rozkłada energię na wiele
 * prążków, więc pojedyncze maksimum nie jest dobrym miernikiem jej obecności.
 *
 * W każdym punkcie RF mierzymy średnią moc w szerokim oknie IF 3..21 kHz,
 * przy LO ustawionym o rzeczywiste IF toru (około 10,03 kHz) poniżej badanego punktu. Jest to ten sam kierunek
 * heterodyny, którego używa normalny tor pomiarowy (RF = LO + IF).
 * Prawie 200 prążków jest uśrednianych energetycznie (mag^2), dzięki czemu
 * szum ma mały rozrzut. RFSCAN6 dodatkowo składa te punkty w kanały WFM.
 */
static float __attribute__((section(".user_sdram"))) skaner_wodospad_moc_db[UI_WODOSPAD_SZEROKOSC];
static float __attribute__((section(".user_sdram"))) skaner_wodospad_srednia_db[UI_WODOSPAD_SZEROKOSC];
static float __attribute__((section(".user_sdram"))) skaner_wodospad_nad_tlem_db[UI_WODOSPAD_SZEROKOSC];
static float __attribute__((section(".user_sdram"))) skaner_wodospad_moc_wzgledna[UI_WODOSPAD_SZEROKOSC];
static uint16_t __attribute__((section(".user_sdram"))) skaner_wodospad_licznik[UI_WODOSPAD_SZEROKOSC];
static uint8_t skaner_wodospad_pelny;
static uint8_t skaner_wodospad_ciagly;
static uint8_t skaner_wodospad_ma_srednia;
static uint8_t skaner_wynik_energia;
static uint32_t skaner_wodospad_przebiegi_gotowe;
static uint32_t skaner_wodospad_przebieg_biezacy;
static uint16_t skaner_wodospad_postep_permille;
static uint32_t skaner_wodospad_postep_f_hz;
static uint8_t skaner_wodospad_w_trakcie;

/* Jedna linia szerokiego skanu powstaje wolno (20 MHz to ponad tysiąc FFT).
 * Trzy piksele wysokości sprawiają, że pierwszy ukończony przebieg jest od razu
 * widoczny na ekranie, bez fałszowania osi częstotliwości. */
#define SKANER_WODOSPAD_GRUBOSC_WIERSZA 3U

/* Stałe detektora energii RF.
 * Nominalne IF około 10 kHz trzyma użyteczny sygnał z dala od DC i przecieku LO.
 * Okno 3..21 kHz wykorzystuje prawie całe użyteczne pasmo audio bez DC.
 * Maksymalny krok 15 kHz daje dla 88..108 MHz około 1334 pomiarów/przebieg,
 * czyli pełne pokrycie bez ogromnego wydłużenia czasu skanu. */
#define SKANER_RF_IF_DOL_HZ               3000U
#define SKANER_RF_IF_GORA_HZ             21000U
#define SKANER_RF_KROK_MAX_HZ            15000U
#define SKANER_RF_MAX_PROBEK_PRZEBIEGU    2400U
#define SKANER_RF_EMA_NOWA                 0.45f

/* RFSCAN6: radiofonia UKF FM wymaga innego detektora niż wąskie nośne.
 * Stacja WFM rozkłada energię na około 150..200 kHz. Punktowy pomiar 10..20 kHz
 * może więc wyglądać tylko nieznacznie mocniej od szumu, mimo bardzo silnego
 * nadajnika. W paśmie 87,5..108,5 MHz po pełnym przebiegu integrujemy energię
 * kanałowo w oknie +/-100 kHz i porównujemy ją z pasmami bocznymi oddalonymi
 * od środka. Dzięki temu 10 kW WFM nie ginie jako kilka losowych kropek.
 */
#define SKANER_FM_DOL_HZ                 87500000UL
#define SKANER_FM_GORA_HZ               108500000UL
#define SKANER_FM_POL_KANALU_HZ           100000UL
#define SKANER_FM_TLO_WEW_HZ              250000UL
#define SKANER_FM_TLO_ZEW_HZ              800000UL
#define SKANER_FM_TLO_LIMIT_DB                4.0f
#define SKANER_FM_PROG_NAD_TLEM_DB            0.20f
#define SKANER_FM_DB_PELNA_SKALA              6.0f
#define SKANER_FM_PROG_STACJI_DB               0.55f
#define SKANER_FM_PROG_PUNKT_DB                1.4f
#define SKANER_FM_MIN_SZEROKOSC_HZ            50000UL

/* Dla pozostałych zakresów zostaje ogólny detektor energii. */
#define SKANER_WODOSPAD_PROG_NAD_TLEM_DB 1.5f
#define SKANER_WODOSPAD_DB_PELNA_SKALA  10.0f
#define SKANER_WODOSPAD_PROG_STACJI_DB    2.5f
#define SKANER_WODOSPAD_PROG_PUNKT_DB     5.0f
#define SKANER_WODOSPAD_MAX_MARKEROW     16U

static uint16_t skaner_wodospad_markery_x[SKANER_WODOSPAD_MAX_MARKEROW];
static uint16_t skaner_wodospad_markery_p[SKANER_WODOSPAD_MAX_MARKEROW];
static float skaner_wodospad_markery_db[SKANER_WODOSPAD_MAX_MARKEROW];
static uint8_t skaner_wodospad_liczba_markerow;

/* RFSCAN7: osobna, krótka pamięć zajętości kanałów FM.
 * Nie fałszuje historii wodospadu: wygładza tylko decyzję detektora między
 * sąsiednimi pełnymi przebiegami. Silna stacja WFM zmienia chwilowy rozkład
 * energii wraz z modulacją, dlatego pojedynczy przebieg może na moment zejść
 * pod próg mimo stałej obecności nadajnika. Pamięć zanika o 0,18 dB na pełny
 * przebieg i jest natychmiast podnoszona przez nowy pomiar. */
static float __attribute__((section(".user_sdram"))) skaner_fm_pamiec_db[UI_WODOSPAD_SZEROKOSC];
static uint8_t skaner_fm_pamiec_wazna;
#define SKANER_FM_PAMIEC_SPAD_DB                0.10f
#define SKANER_FM_KANAL_WYSWIETL_POL_HZ       90000UL
#define SKANER_FM_RASTER_HZ                   100000UL

#define SKANER_BIN_MIN 4
#define SKANER_BIN_MAX 192
#define SKANER_PROG_SZCZYT_TLO 2.0f
#define SKANER_BIN_HZ ((float)FSAMPLE / (float)NSAMPLES)

/*
 * Wyszukuje najsilniejszy prążek w paśmie audio powstałym po mieszaniu RF z LO.
 *
 * Historyczna wersja korzystała z globalnego Maxmag podczas wyszukiwania maksimum.
 * Jeżeli kolejny pomiar nie przekroczył poprzedniego maksimum, idxmax pozostawał -1,
 * a kod odczytywał rfft_mags[-2]. Było to wyjście poza tablicę dokładnie w sytuacji,
 * w której skaner miał problem ze znalezieniem sygnału. Tutaj maksimum i tło są
 * liczone lokalnie dla każdego widma. Globalne Maxmag pozostaje jedynie wynikiem
 * bieżącego pomiaru, bo starsza część algorytmu używa go do porównywania kolejnych LO.
 */
int CalcMaxBin(unsigned long frqu1)
{
    int n;
    int idxmax = SKANER_BIN_MIN;
    int liczba_tla = 0;
    float val;
    float maksimum = -1.0f;
    float suma_tla = 0.0f;
    float tlo;

    if (rfft_mags == 0)
    {
        Maxmag = 0.0f;
        return 0;
    }

    if (frqu1 != 0U)
        GEN_SetLOFreq(frqu1);

    DSP_Sample();
    Calc_fft_audiobuf(0);

    for (n = SKANER_BIN_MIN; n <= SKANER_BIN_MAX; n++)
    {
        val = fabsf(rfft_mags[n]);
        if (!isfinite(val))
            continue;
        if (val > maksimum)
        {
            maksimum = val;
            idxmax = n;
        }
    }

    if (!(maksimum > 0.0f) || !isfinite(maksimum))
    {
        Maxmag = 0.0f;
        skaner_ostatni_szczyt_fft = 0.0f;
        skaner_ostatnie_tlo_fft = 0.0f;
        skaner_ostatni_stosunek_db = -INFINITY;
        return 0;
    }

    /*
     * Średnie tło liczymy z tego samego widma, z pominięciem maksimum i jego
     * bezpośrednich sąsiadów. Dzięki temu kryterium wykrycia ma jawne znaczenie:
     * szczyt musi być co najmniej dwa razy większy od średniego tła amplitudowego.
     */
    for (n = SKANER_BIN_MIN; n <= SKANER_BIN_MAX; n++)
    {
        if (abs(n - idxmax) <= 1)
            continue;
        val = fabsf(rfft_mags[n]);
        if (!isfinite(val))
            continue;
        suma_tla += val;
        liczba_tla++;
    }

    tlo = (liczba_tla > 0) ? (suma_tla / (float)liczba_tla) : 0.0f;
    if (!isfinite(tlo) || tlo < 0.0f)
        tlo = 0.0f;

    if (!(Average > 0.0f) || !isfinite(Average))
        Average = tlo;
    else
        Average = (2.0f * Average + tlo) / 3.0f;

    Maxmag = maksimum;
    SumVal = suma_tla;
    skaner_ostatni_szczyt_fft = maksimum;
    skaner_ostatnie_tlo_fft = tlo;
    if (tlo > 1e-20f)
        skaner_ostatni_stosunek_db = 20.0f * log10f(maksimum / tlo);
    else
        skaner_ostatni_stosunek_db = INFINITY;

    if (tlo <= 1e-20f)
        return idxmax;
    if (maksimum >= SKANER_PROG_SZCZYT_TLO * tlo)
        return idxmax;
    return 0;
}

float ParabolicInterpolation(float y1, float y2, float y3, //values for frequencies x1, x2, x3
                             float x1, float x2, float x3, //y values of  of respective frequencies
                             float x)                      //Frequency between x2 and x3 where we want to interpolate result
{
    float z1 = (y3 - y2) / (x3 - x2);
    float z2 = (y2 - y1) / (x2 - x1);
    float a = (z1 - z2) / (x3 - x1); // a may be zero (if all points are collinear)
    float b = (z1 * (x2 - x1) + z2 * (x3 - x2)) / (x3 - x1);
    float res = (x - x2) * (a * (x - x2) + b) + y2; // avoid the powf function
    return res;
}

// List 10*log10(u) ->  attenuation in dB
float fV[14] = {49.f, 42.52f, 39.6f, 37.56f, 34.61f, 32.55f, 27.61f, 22.63f, 17.8f, 14.76f, 13.26f, 12.7f, 7.3f, 5.f}; // values (raw) for 0 dB .. -100 dB
float dB[14] = {5, 0, -6, -10, -16, -20, -30, -40, -50, -60, -70, -80, -90, -100};                                     // two more than necessary, because of the upper and lower end

float Calc_dB1(float z)
{
    int i;
    for (i = 1; i < 13; i++)
    {
        if (z >= fV[i])
            break;
    }
    return ParabolicInterpolation(dB[i - 1], dB[i], dB[i + 1], fV[i - 1], fV[i], fV[i + 1], z);
}

float Calc_dB(float z)
{

    return 2.1252f * z - 91.8f;
}

float DSP_GetValue(uint32_t freq, int iterations)
{
    float val;
    val = DSP_MeasureTrack(freq, 0, 0, iterations); // TX przez S2
    return val;
    /* if (val < 0.0000001f)// was 0.3
            val= 0.0000001f;
        return val;*/
}

//static char string1[40];
static unsigned long fx;
static int binx, found;

static SKANER_WYNIK_t Skaner_PobierzWynik(void)
{
    SKANER_WYNIK_t wynik = {0U, 0U, -INFINITY, NAN};
    float power_raw;
    float napiecie_mv = NAN;
    float moc_mw = NAN;
    int64_t czestotliwosc_hz;
    const uint32_t fmax_synth_hz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);

    if (found != 1 || fx == 0U || !(Maxmag > 0.0f) || !isfinite(Maxmag))
        return wynik;

    if (skaner_wynik_energia)
        czestotliwosc_hz = (int64_t)fx;
    else
        czestotliwosc_hz = (int64_t)fx - (int64_t)lroundf((float)binx * SKANER_BIN_HZ);
    if (czestotliwosc_hz < 0)
        czestotliwosc_hz = 0;
    if (czestotliwosc_hz > UINT32_MAX)
        czestotliwosc_hz = UINT32_MAX;

    /*
     * To przeliczenie pozostaje wyłącznie wskaźnikiem orientacyjnym. Tor nie
     * ma wzorcowania amplitudy w funkcji częstotliwości, więc dBm nie może być
     * traktowane jak pomiar mocy z miernika laboratoryjnego.
     */
    if (!skaner_wynik_energia)
    {
        power_raw = Maxmag * 5.0f;
        if ((uint32_t)czestotliwosc_hz > fmax_synth_hz)
            power_raw *= 1.5f;

        if (power_raw > 500.0f)
            napiecie_mv = power_raw * 0.0149f - 0.5587f;
        else if (power_raw > 3.5f)
            napiecie_mv = power_raw * 0.0137f - 0.0462f;

        if (isfinite(napiecie_mv) && napiecie_mv > 0.0f)
        {
            moc_mw = napiecie_mv * napiecie_mv / 50000.0f;
            if (moc_mw > 0.0f)
                wynik.moc_dbm = 10.0f * log10f(moc_mw);
        }
    }

    wynik.wazny = 1U;
    wynik.czestotliwosc_hz = (uint32_t)czestotliwosc_hz;
    wynik.stosunek_db = skaner_ostatni_stosunek_db;
    return wynik;
}

void FX_ShowResult(void)
{
    char czestotliwosc[32];
    char stosunek[24];
    char poziom[28];
    float power_raw;
    float napiecie_mv = NAN;
    float moc_mw = NAN;
    float moc_dbm = NAN;
    int64_t czestotliwosc_hz;
    const uint32_t fmax_synth_hz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    const uint32_t tlo = UI_KolorTlaEkranu();

    LCD_FillRect(LCD_MakePoint(146, 86), LCD_MakePoint(479, 229), tlo);

    if (found != 1 || fx == 0U || !(Maxmag > 0.0f) || !isfinite(Maxmag))
    {
        UI_RysujPanel(150, 90, 326, 128, JEZYK_Tekst(TEKST_SKANER_RF_TYTUL), UI_STYL_OSTRZEZENIE);
        FONT_Write(FONT_FRANBIG, UI_KolorTekstu(UI_STYL_OSTRZEZENIE), UI_KolorTlaPola(),
                   168, 133, JEZYK_Tekst(TEKST_SKANER_RF_BRAK));
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
                   160, 186, JEZYK_Tekst(TEKST_SKANER_RF_OPIS_2));
        return;
    }

    /*
     * Częstotliwość sygnału wynika z aktualnej częstotliwości LO i położenia
     * prążka FFT. Szerokość prążka to FSAMPLE/NSAMPLES = 93,75 Hz.
     */
    if (skaner_wynik_energia)
        czestotliwosc_hz = (int64_t)fx;
    else
        czestotliwosc_hz = (int64_t)fx - (int64_t)lroundf((float)binx * SKANER_BIN_HZ);
    if (czestotliwosc_hz < 0)
        czestotliwosc_hz = 0;
    if (czestotliwosc_hz > UINT32_MAX)
        czestotliwosc_hz = UINT32_MAX;
    if (skaner_wynik_energia)
        UI_FormatujCzestotliwoscMHzKrotko((uint32_t)czestotliwosc_hz, czestotliwosc, sizeof(czestotliwosc));
    else
        UI_FormatujCzestotliwoscMHz((uint32_t)czestotliwosc_hz, czestotliwosc, sizeof(czestotliwosc));

    /*
     * Stare przeliczenie amplitudy FFT na mV zachowujemy wyłącznie jako wskazanie
     * orientacyjne. W źródłach historycznych jest ono opisane komentarzem
     * "must be better calibrated" i nie ma śladu wzorcowania całego toru w funkcji
     * częstotliwości. Nie wolno więc przedstawiać tej liczby jako pomiaru mocy.
     */
    if (!skaner_wynik_energia)
    {
        power_raw = Maxmag * 5.0f;
        if ((uint32_t)czestotliwosc_hz > fmax_synth_hz)
            power_raw *= 1.5f;

        if (power_raw > 500.0f)
            napiecie_mv = power_raw * 0.0149f - 0.5587f;
        else if (power_raw > 3.5f)
            napiecie_mv = power_raw * 0.0137f - 0.0462f;

        if (isfinite(napiecie_mv) && napiecie_mv > 0.0f)
        {
            moc_mw = napiecie_mv * napiecie_mv / 50000.0f; /* założenie historyczne: 50 Ohm */
            if (moc_mw > 0.0f)
                moc_dbm = 10.0f * log10f(moc_mw);
        }
    }

    if (isfinite(skaner_ostatni_stosunek_db))
        snprintf(stosunek, sizeof(stosunek), "%.1f dB", (double)skaner_ostatni_stosunek_db);
    else
        snprintf(stosunek, sizeof(stosunek), "> 60 dB");

    if (skaner_wynik_energia)
        snprintf(poziom, sizeof(poziom), "%s", JEZYK_Wybierz("względny", "relative", "relativ", "относит."));
    else if (isfinite(moc_dbm))
        snprintf(poziom, sizeof(poziom), "~%.1f dBm", (double)moc_dbm);
    else
        snprintf(poziom, sizeof(poziom), "niewzorcowany");

    UI_RysujPoleWartosci(150, 90, 326, 54, JEZYK_Tekst(TEKST_SKANER_RF_CZESTOTLIWOSC), czestotliwosc);
    UI_RysujPoleWartosci(150, 150, 158, 54, JEZYK_Tekst(TEKST_SKANER_RF_SZCZYT_TLO), stosunek);
    UI_RysujPoleWartosci(314, 150, 162, 54, JEZYK_Tekst(TEKST_SKANER_RF_POZIOM), poziom);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
               151, 211, JEZYK_Tekst(TEKST_SKANER_RF_POZIOM_INFO));
}

void SetUpperLower(void)
{
    uint64_t srodek_hz;
    uint64_t zakres_hz;
    uint64_t dol_hz;
    uint64_t gora_hz;
    const uint32_t fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    /*
     * Pasmo i ręczny wybór Od-Do są zakresami dokładnymi. Nie wolno ich
     * później zamieniać na najbliższy standardowy BANDSPAN, bo np. pasmo
     * 88-108 MHz ma pozostać dokładnie 88-108 MHz.
     */
    if (skaner_zakres_dokladny)
    {
        if (lower < fmin_hz)
            lower = fmin_hz;
        if (upper > fmax_hz)
            upper = fmax_hz;
        if (upper <= lower)
        {
            lower = fmin_hz;
            upper = fmax_hz;
        }
        return;
    }

    if ((uint32_t)span > (uint32_t)BS1000M)
        span = BS1000M;

    srodek_hz = (uint64_t)f1 * 1000ULL;
    zakres_hz = (uint64_t)BSVALUES[span] * 1000ULL;

    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) != 0U && span >= 5)
    {
        const uint64_t polowa = zakres_hz / 2ULL;
        dol_hz = (srodek_hz > polowa) ? (srodek_hz - polowa) : 0ULL;
        gora_hz = srodek_hz + polowa;
    }
    else
    {
        dol_hz = srodek_hz;
        gora_hz = srodek_hz + zakres_hz;
    }

    if (dol_hz < (uint64_t)fmin_hz)
        dol_hz = fmin_hz;
    if (gora_hz > (uint64_t)fmax_hz)
        gora_hz = fmax_hz;

    if (gora_hz <= dol_hz)
    {
        lower = fmin_hz;
        upper = fmax_hz;
        return;
    }

    lower = (unsigned long)dol_hz;
    upper = (unsigned long)gora_hz;
}

int OneBandExec(void)
{
    int res;

    found = 0;
    FreqFound = 0;
    Maxmag = 0.0f;
    skaner_wynik_energia = 1U;
    Skaner_WodospadResetujSrednia();

    /* Ten sam detektor energii obsługuje teraz pojedynczy skan i wodospad.
     * Dzięki temu ekran "Skanuj" nie może wskazywać losowego maksimum FFT,
     * którego później nie widać w historii. */
    res = Skaner_RF_WykonajPrzebiegEnergii(1U);
    if (res < 0)
        return -1;
    if (res == 0)
    {
        FX_ShowResult();
        return 0;
    }

    Skaner_WodospadZbudujLinie();
    FreqFound = found;
    FX_ShowResult();
    return found;
}

int ScanFull(void)
{
    int res;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    found = 0;
    Maxmag = 0.0f;
    FreqFound = 0;

    /*
     * Historyczna funkcja "Full Scale" była w rzeczywistości skanem 0,5-30 MHz.
     * Zachowujemy ten użyteczny skrót, ale respektujemy rzeczywisty zakres urządzenia.
     */
    lower = (fmin > 500000U) ? fmin : 500000U;
    upper = (fmax < 30000000U) ? fmax : 30000000U;
    if (upper <= lower)
    {
        FX_ShowResult();
        return 0;
    }

    res = SpectrumExec(0);
    if (res >= 2 && fx > 100000U)
    {
        const uint32_t margines = 100000U;
        lower = (fx > margines) ? fx - margines : fmin;
        upper = (fx <= fmax - (fmax >= margines ? margines : 0U)) ? fx + margines : fmax;
        if (lower < fmin)
            lower = fmin;
        if (upper > fmax)
            upper = fmax;
        if (upper > lower)
            res = SpectrumExec(0);
    }
    if (res == -1)
        return 0;

    found = (res >= 2) ? 1 : 0;
    FX_ShowResult();
    FreqFound = found;
    return found;
}

void Full(void)
{
    if (sScan_Meas == 1)
        return;
    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    selector = 3;
    UI_RysujPoleStatusu(150, 148, 326, 70, JEZYK_Tekst(TEKST_SKANER_RF_TYTUL),
                        JEZYK_Tekst(TEKST_SKANER_RF_SKANOWANIE), UI_STYL_AKCENT);
    ScanFull();
    selector = 0;
}

int OneBand(void)
{
    int ret;

    if (sScan_Meas == 1)
        return 0;
    FreqFound = 0;
    cycl3 = 49;
    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    selector = 1;
    SetUpperLower();

    UI_RysujPoleStatusu(150, 148, 326, 70, JEZYK_Tekst(TEKST_SKANER_RF_TYTUL),
                        JEZYK_Tekst(TEKST_SKANER_RF_SKANOWANIE), UI_STYL_AKCENT);

    ret = OneBandExec();
    selector = 0;
    return ret;
}

static void HamBandsExec(void)
{
    int k;
    int ret = 0;
    unsigned long fx1 = 0;
    int binx1 = 0;
    float maxmag1 = 0.0f;
    float stosunek1 = -INFINITY;
    const uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    found = 0;
    Maxmag = 0.0f;

    if (AreaSelected != 1)
        return;

    /*
     * Stara pętla kończyła się na k=7, więc pod nazwą "Ham Bands" skanowała
     * tylko część pasm do 20 m. Teraz przechodzimy po wszystkich zdefiniowanych
     * pasmach i kończymy po przekroczeniu 30 MHz. Zakres urządzenia nadal jest
     * nadrzędnym ograniczeniem.
     */
    for (k = 0; k < 32; k++)
    {
        uint32_t pasmo_dol = (uint32_t)GetLower(k);
        uint32_t pasmo_gora = (uint32_t)GetUpper(k);

        if (pasmo_dol == 0U || pasmo_gora == 0U)
            break;
        if (pasmo_dol > 30000000U)
            break;
        if (pasmo_gora < fmin || pasmo_dol > fmax)
            continue;

        lower = (pasmo_dol < fmin) ? fmin : pasmo_dol;
        upper = (pasmo_gora > fmax) ? fmax : pasmo_gora;
        if (upper <= lower)
            continue;

        ret = SpectrumExec(0);
        if (ret == -1)
            return;
        if (ret >= 2 && Maxmag > maxmag1)
        {
            maxmag1 = Maxmag;
            stosunek1 = skaner_ostatni_stosunek_db;
            fx1 = fx;
            binx1 = binx;
            found = 1;
        }
    }

    if (found)
    {
        Maxmag = maxmag1;
        skaner_ostatni_stosunek_db = stosunek1;
        fx = fx1;
        binx = binx1;
    }
    FX_ShowResult();
    FreqFound = found;
}

void HamBands(void)
{
    if (sScan_Meas == 1)
        return;
    FreqFound = 0;
    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    selector = 2;
    UI_RysujPoleStatusu(150, 148, 326, 70, JEZYK_Tekst(TEKST_SKANER_RF_TYTUL),
                        JEZYK_Tekst(TEKST_SKANER_RF_SKANOWANIE), UI_STYL_AKCENT);
    HamBandsExec();
    selector = 0;
}

void SCExit(void)
{
    if (sScan_Meas == 1)
        sRepaint = 1;
    else
        sExit = 1;
}

static BANDSPAN Skaner_DobierzZakresDlaPasma(uint32_t szerokosc_hz)
{
    uint32_t i;

    if (szerokosc_hz == 0U)
        return BS400;

    for (i = (uint32_t)BS2; i <= (uint32_t)BS1000M; ++i)
    {
        if ((uint64_t)BSVALUES[i] * 1000ULL >= (uint64_t)szerokosc_hz)
            return (BANDSPAN)i;
    }
    return BS1000M;
}

static void Skaner_UstawZakresDokladny(uint32_t dol_hz, uint32_t gora_hz)
{
    const uint32_t fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    if (dol_hz < fmin_hz)
        dol_hz = fmin_hz;
    if (gora_hz > fmax_hz)
        gora_hz = fmax_hz;
    if (gora_hz <= dol_hz)
        return;

    lower = dol_hz;
    upper = gora_hz;
    span = Skaner_DobierzZakresDlaPasma(gora_hz - dol_hz);
    skaner_zakres_dokladny = 1U;

    if (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) != 0U)
        f1 = (dol_hz + (gora_hz - dol_hz) / 2U) / 1000U;
    else
        f1 = dol_hz / 1000U;

    AreaSelected = 1;
}

/*
 * RFSCAN8: skaner pamięta ostatnio wybrany zakres, ale nie zapisuje karty SD
 * po każdym dotknięciu. Parametry są utrwalane dopiero przy normalnym wyjściu
 * ze skanera. Dzięki temu nie zużywamy niepotrzebnie karty i nie dokładamy
 * opóźnień do obsługi ekranu.
 */
static void Skaner_ZapiszOstatniZakres(void)
{
    const uint32_t dol = (uint32_t)lower;
    const uint32_t gora = (uint32_t)upper;
    uint8_t zmiana = 0U;

    if (gora <= dol)
        return;

    if (CFG_GetParam(CFG_PARAM_SKANER_RF_FMIN_HZ) != dol)
    {
        CFG_SetParam(CFG_PARAM_SKANER_RF_FMIN_HZ, dol);
        zmiana = 1U;
    }
    if (CFG_GetParam(CFG_PARAM_SKANER_RF_FMAX_HZ) != gora)
    {
        CFG_SetParam(CFG_PARAM_SKANER_RF_FMAX_HZ, gora);
        zmiana = 1U;
    }

    if (zmiana && CFG_CzyKartaSDDostepna())
        (void)CFG_FlushSprawdzony();
}

static void Skaner_WybierzPasmo(void)
{
    uint32_t dol_hz = (uint32_t)lower;
    uint32_t gora_hz = (uint32_t)upper;
    const uint32_t fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);

    while (TOUCH_IsPressed())
        ;
    Sleep(80);

    if (!PanFreq_WybierzPasmoZakresEx(&dol_hz, &gora_hz, fmin_hz, fmax_hz))
    {
        Skaner_RysujEkranGlowny();
        return;
    }

    /* Pełne pasmo jest zachowane. Stary limit 10 MHz był ograniczeniem UI,
     * nie samego skanera: SpectrumExec przechodzi po całym lower..upper i
     * dla szerokiego zakresu agreguje kolejne kroki LO do pikseli ekranu. */
    Skaner_UstawZakresDokladny(dol_hz, gora_hz);
    Skaner_ZapiszOstatniZakres();
    Skaner_RysujEkranGlowny();
}

static void Skaner_UstawOdDo(void)
{
    uint32_t dol_hz = (uint32_t)lower;
    uint32_t gora_hz = (uint32_t)upper;
    const uint32_t fmin_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
    const uint32_t fmax_hz = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint32_t krok_hz = 1000U;

    while (TOUCH_IsPressed())
        ;
    Sleep(80);

    if (!UI_EdytujCzestotliwoscHzEx(dol_hz, fmin_hz,
                                     fmax_hz > krok_hz ? fmax_hz - krok_hz : fmax_hz,
                                     krok_hz,
                                     JEZYK_Wybierz("Początek zakresu", "Range start", "Bereichsanfang", "Начало диапазона"),
                                     &dol_hz))
    {
        Skaner_RysujEkranGlowny();
        return;
    }

    if (gora_hz <= dol_hz)
        gora_hz = dol_hz + krok_hz;
    if (gora_hz > fmax_hz)
        gora_hz = fmax_hz;

    if (!UI_EdytujCzestotliwoscHzEx(gora_hz, dol_hz + krok_hz, fmax_hz,
                                     krok_hz,
                                     JEZYK_Wybierz("Koniec zakresu", "Range end", "Bereichsende", "Конец диапазона"),
                                     &gora_hz))
    {
        Skaner_RysujEkranGlowny();
        return;
    }

    Skaner_UstawZakresDokladny(dol_hz, gora_hz);
    /* RFSCAN10: zapisujemy od razu po zatwierdzeniu Od-Do. Dzięki temu
     * zakres nie ginie po restarcie ani po wyłączeniu urządzenia z poziomu
     * wodospadu bez wcześniejszego powrotu do menu skanera. */
    Skaner_ZapiszOstatniZakres();
    Skaner_RysujEkranGlowny();
}

void freq(void)
{
    BANDSPAN nowy_zakres = span;

    while (TOUCH_IsPressed())
        ;
    Sleep(80);

    if (PanFreq_WybierzSzerokoscEx(f1 * 1000U, &nowy_zakres, BS1000M))
    {
        span = nowy_zakres;
        skaner_zakres_dokladny = 0U;
        SetUpperLower();
        AreaSelected = 1;
        Skaner_ZapiszOstatniZakres();
    }

    Skaner_RysujEkranGlowny();
}

static const int dBs[] = {10, 0, -10, -20, -30, -40, -50, -60, -70};
//static uint32_t color;// for testing purposes
static float signal;
static int breaker;

int Delta(int binMax0, int binMax1, int binMax2)
{
    breaker = 0;
    if ((binMax0 + binMax1 + binMax2 == 0))
    {
        breaker = 9;
        return 0;
    }
    if ((binMax0 != 0) && (binMax1 != 0))
    {
        if ((binMax1 <= 16) && (binMax0 - binMax1 >= 158) && (binMax0 - binMax1 <= 162))
        {
            //color=LCD_COLOR_LIGHTMAGENTA;
            return -binMax0;
        }
        else if ((binMax1 >= 164) && (binMax1 - binMax0 >= 158) && (binMax1 - binMax0 <= 162))
        { // ????****
            //color=LCD_COLOR_YELLOW;
            return binMax0;
        }
    }
    if ((binMax1 != 0) && (binMax2 != 0))
    {
        if ((binMax2 - binMax1 >= 14) && (binMax2 - binMax1 <= 18))
        {
            //color=LCD_COLOR_GREEN;
            return binMax2 - 176;
        }
        else
        {
            if ((binMax1 - binMax2 >= 14) && (binMax1 - binMax2 <= 18))
                //color=LCD_COLOR_BLUE;
                return -(176 + binMax2);
        }
    }
    if ((binMax0 >= 32) && (binMax1 + binMax2 == 0))
    {
        //color=LCD_COLOR_RED;
        return binMax0;
    }
    if ((binMax0 != 0) && ((binMax1 != 0) || (binMax2 != 0)))
    {
        //color=LCD_COLOR_LIGHTYELLOW;
        return -binMax0;
    }

    if ((binMax0 + binMax1 == 0) && (binMax2 >= 176))
    {
        //color=LCD_COLOR_LIGHTGREEN;
        return -(binMax2 + 176);
    }

    if ((binMax0 != 0) && (binMax2 != 0) && (binMax0 + binMax2 >= 174) && (binMax0 + binMax2 <= 178))
    {
        //color=LCD_COLOR_ORANGE;
        return binMax2 - 176;
    }
    return 0;
}

void ShowSpectLine(int x)
{
    int y, h;
    h = LCD_GetHeight();

    if ((x < 480) && (x > 29) && (signal > 0))
    {
        y = (int)60 * log10f(signal);
        y = (int)(h - 23 - y);
        if (y < 0)
            y = 0;
        if (y > h - 69)
            y = h - 69;
        LCD_Line(LCD_MakePoint(x, h - 70), LCD_MakePoint(x, y), TextColor); //TextColor
        LCD_Line(LCD_MakePoint(x, y + 1), LCD_MakePoint(x, 2), BackGrColor);
    }
}

static void Skaner_WodospadResetujProbki(void)
{
    uint16_t x;
    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        skaner_wodospad_moc_db[x] = 0.0f;
        skaner_wodospad_licznik[x] = 0U;
    }
}

static void Skaner_WodospadResetujSrednia(void)
{
    uint16_t x;
    skaner_wodospad_ma_srednia = 0U;
    skaner_fm_pamiec_wazna = 0U;
    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        skaner_wodospad_srednia_db[x] = NAN;
        skaner_wodospad_nad_tlem_db[x] = 0.0f;
        skaner_fm_pamiec_db[x] = 0.0f;
    }
}

static void Skaner_WodospadZapiszMoc(uint16_t x, float moc_db)
{
    if (x >= UI_WODOSPAD_SZEROKOSC || !isfinite(moc_db))
        return;
    skaner_wodospad_moc_db[x] += moc_db;
    if (skaner_wodospad_licznik[x] < UINT16_MAX)
        skaner_wodospad_licznik[x]++;
}

/* Zgodność ze starym SpectrumExec(graph=2). RFSCAN5 nie wywołuje już tej
 * ścieżki dla wodospadu, ale pozostawiamy ją do czasu usunięcia historycznego
 * renderera widma. Nie mieszamy starego "max/średnia" z nowym detektorem mocy. */
static void Skaner_WodospadZapiszPunkt(int x, float stosunek_db)
{
    (void)x;
    (void)stosunek_db;
}

/*
 * Miernik energii w stałym oknie IF.
 *
 * Dla badanego punktu RF ustawiamy LO o rzeczywiste IF toru niżej. Sygnał z tego punktu
 * pojawia się więc w środku okna 3..15 kHz, z dala od DC i przecieku LO.
 * Zamiast wybierać największy z ~190 losowych prążków uśredniamy kwadrat
 * amplitudy prawie całego użytecznego pasma FFT. Dla szumu wariancja spada bardzo mocno, natomiast
 * szeroka stacja FM podnosi energię w sposób trwały.
 */
static float Skaner_RF_MierzMocOknaDb(uint32_t czestotliwosc_rf_hz)
{
    const int bin_dol = (int)ceilf((float)SKANER_RF_IF_DOL_HZ / SKANER_BIN_HZ);
    const int bin_gora = (int)floorf((float)SKANER_RF_IF_GORA_HZ / SKANER_BIN_HZ);
    const uint32_t if_srodek_hz = DSP_GetIF();
    uint32_t lo_hz;
    float suma_mocy = 0.0f;
    uint32_t liczba = 0U;
    int n;

    if (rfft_mags == 0)
        return NAN;

    lo_hz = (czestotliwosc_rf_hz > if_srodek_hz) ?
                (czestotliwosc_rf_hz - if_srodek_hz) :
                czestotliwosc_rf_hz;
    GEN_SetLOFreq(lo_hz);
    DSP_Sample();
    Calc_fft_audiobuf(0);

    for (n = bin_dol; n <= bin_gora && n < (NSAMPLES / 2); ++n)
    {
        const float a = fabsf(rfft_mags[n]);
        if (!isfinite(a))
            continue;
        suma_mocy += a * a;
        liczba++;
    }

    if (liczba == 0U || !(suma_mocy > 0.0f) || !isfinite(suma_mocy))
        return NAN;

    return 10.0f * log10f(suma_mocy / (float)liczba + 1e-30f);
}

static float Skaner_WodospadMedianaMocy(void)
{
    uint16_t hist[128] = {0};
    uint16_t x;
    uint32_t liczba = 0U;
    uint32_t suma = 0U;
    float vmin = INFINITY;
    float vmax = -INFINITY;

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        const float v = skaner_wodospad_srednia_db[x];
        if (!isfinite(v))
            continue;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        liczba++;
    }
    if (liczba == 0U)
        return 0.0f;
    if (!(vmax > vmin + 0.001f))
        return vmin;

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        const float v = skaner_wodospad_srednia_db[x];
        uint32_t b;
        if (!isfinite(v))
            continue;
        b = (uint32_t)(((v - vmin) * 127.0f) / (vmax - vmin));
        if (b > 127U) b = 127U;
        hist[b]++;
    }

    for (x = 0U; x < 128U; ++x)
    {
        suma += hist[x];
        if (suma * 2U >= liczba)
            return vmin + ((float)x + 0.5f) * (vmax - vmin) / 128.0f;
    }
    return (vmin + vmax) * 0.5f;
}

static float Skaner_WodospadLokalneTlo(uint16_t x, float mediana_globalna, uint16_t polokno)
{
    int32_t a = (int32_t)x - (int32_t)polokno;
    int32_t b = (int32_t)x + (int32_t)polokno;
    float suma = 0.0f;
    uint32_t liczba = 0U;
    int32_t i;

    if (a < 0) a = 0;
    if (b >= (int32_t)UI_WODOSPAD_SZEROKOSC)
        b = (int32_t)UI_WODOSPAD_SZEROKOSC - 1;

    for (i = a; i <= b; ++i)
    {
        float v = skaner_wodospad_srednia_db[i];
        if (!isfinite(v))
            continue;
        /* Silna stacja nie może sama podnieść sobie tła. Jednocześnie 6 dB
         * zapasu pozwala śledzić powolną charakterystykę toru w szerokim paśmie. */
        if (v > mediana_globalna + 6.0f)
            v = mediana_globalna + 6.0f;
        suma += v;
        liczba++;
    }
    return (liczba > 0U) ? (suma / (float)liczba) : mediana_globalna;
}

static uint8_t Skaner_RF_CzyTrybFM(void)
{
    uint64_t zakres;
    uint64_t dol_wspolny;
    uint64_t gora_wspolna;
    uint64_t pokrycie;

    /*
     * RFSCAN12: nie wymagamy już, aby CAŁY ręcznie wybrany zakres mieścił się
     * idealnie w 87,5..108,5 MHz. To powodowało bardzo nieoczywisty błąd:
     * zakres 87,0..92,0 MHz (zawierający lokalne 88,5 i 91,5 MHz) przełączał
     * skaner na detektor ogólny i wodospad stawał się prawie pusty.
     *
     * Tryb WFM włączamy, gdy co najmniej 80% aktualnego zakresu leży w paśmie
     * radiofonii. Dzięki temu niewielki margines poza 87,5/108,5 MHz jest
     * dozwolony, natomiast szerokie skany obejmujące inne pasma nadal używają
     * detektora ogólnego.
     */
    if (upper <= lower)
        return 0U;

    zakres = (uint64_t)upper - (uint64_t)lower;
    dol_wspolny = ((uint64_t)lower > (uint64_t)SKANER_FM_DOL_HZ) ?
                  (uint64_t)lower : (uint64_t)SKANER_FM_DOL_HZ;
    gora_wspolna = ((uint64_t)upper < (uint64_t)SKANER_FM_GORA_HZ) ?
                   (uint64_t)upper : (uint64_t)SKANER_FM_GORA_HZ;

    if (gora_wspolna <= dol_wspolny)
        return 0U;

    pokrycie = gora_wspolna - dol_wspolny;
    return ((pokrycie * 100ULL) >= (zakres * 80ULL)) ? 1U : 0U;
}

static float Skaner_RF_MocWzgledna(float moc_db, float odniesienie_db)
{
    float d;
    if (!isfinite(moc_db) || !isfinite(odniesienie_db))
        return NAN;
    d = moc_db - odniesienie_db;
    if (d > 30.0f) d = 30.0f;
    if (d < -30.0f) d = -30.0f;
    return powf(10.0f, d * 0.1f);
}

/*
 * Detektor kanalu WFM.
 *
 * skaner_wodospad_srednia_db[] zawiera waskopasmowa moc kolejnych punktow
 * przemiatania. Dla kazdego polozenia x liczymy srednia moc z +/-100 kHz.
 * Tlo bierzemy z dwoch bocznych obszarow 250..800 kHz od badanego srodka.
 * Probki tla sa ograniczane do +4 dB wzgledem globalnej mediany, aby sasiednia
 * stacja nie podnosila progu drugiej stacji. Wynik jest rzeczywistym stosunkiem
 * mocy kanalu do lokalnego tla, a nie maksimum pojedynczego prazka FFT.
 */
static void Skaner_WodospadPrzetworzFM(float mediana_globalna)
{
    const uint64_t zakres = (upper > lower) ? ((uint64_t)upper - (uint64_t)lower) : 1ULL;
    uint16_t pol_kanalu;
    uint16_t tlo_wew;
    uint16_t tlo_zew;
    uint16_t x;
    const float limit_tla = powf(10.0f, SKANER_FM_TLO_LIMIT_DB * 0.1f);

    /* Konwersja dB -> moc liniowa tylko raz na piksel. Wersja liczaca powf()
     * wewnatrz kazdego okna kanalu bylaby poprawna matematycznie, ale zbyt
     * kosztowna dla STM32F7 przy 480 punktach i szerokich oknach tla. */
    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
        skaner_wodospad_moc_wzgledna[x] =
            Skaner_RF_MocWzgledna(skaner_wodospad_srednia_db[x], mediana_globalna);

    pol_kanalu = (uint16_t)(((uint64_t)SKANER_FM_POL_KANALU_HZ *
                             (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
    tlo_wew = (uint16_t)(((uint64_t)SKANER_FM_TLO_WEW_HZ *
                          (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
    tlo_zew = (uint16_t)(((uint64_t)SKANER_FM_TLO_ZEW_HZ *
                          (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);

    if (pol_kanalu < 2U) pol_kanalu = 2U;
    if (tlo_wew <= pol_kanalu + 1U) tlo_wew = (uint16_t)(pol_kanalu + 2U);
    if (tlo_zew <= tlo_wew + 2U) tlo_zew = (uint16_t)(tlo_wew + 3U);
    if (tlo_zew > UI_WODOSPAD_SZEROKOSC - 1U)
        tlo_zew = UI_WODOSPAD_SZEROKOSC - 1U;

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        int32_t i;
        int32_t a = (int32_t)x - (int32_t)pol_kanalu;
        int32_t b = (int32_t)x + (int32_t)pol_kanalu;
        float suma_sygnalu = 0.0f;
        float suma_tla_lewa = 0.0f;
        float suma_tla_prawa = 0.0f;
        uint32_t n_sygnalu = 0U;
        uint32_t n_tla_lewa = 0U;
        uint32_t n_tla_prawa = 0U;

        if (a < 0) a = 0;
        if (b >= (int32_t)UI_WODOSPAD_SZEROKOSC)
            b = (int32_t)UI_WODOSPAD_SZEROKOSC - 1;

        for (i = a; i <= b; ++i)
        {
            const float p = skaner_wodospad_moc_wzgledna[i];
            if (!isfinite(p))
                continue;
            suma_sygnalu += p;
            n_sygnalu++;
        }

        /* Lewe i prawe pasmo odniesienia. Srodek +/-250 kHz jest pomijany,
         * zeby sama stacja nie stala sie wlasnym tlem. */
        for (i = (int32_t)x - (int32_t)tlo_zew;
             i <= (int32_t)x - (int32_t)tlo_wew; ++i)
        {
            float p;
            if (i < 0 || i >= (int32_t)UI_WODOSPAD_SZEROKOSC)
                continue;
            p = skaner_wodospad_moc_wzgledna[i];
            if (!isfinite(p))
                continue;
            if (p > limit_tla) p = limit_tla;
            suma_tla_lewa += p;
            n_tla_lewa++;
        }
        for (i = (int32_t)x + (int32_t)tlo_wew;
             i <= (int32_t)x + (int32_t)tlo_zew; ++i)
        {
            float p;
            if (i < 0 || i >= (int32_t)UI_WODOSPAD_SZEROKOSC)
                continue;
            p = skaner_wodospad_moc_wzgledna[i];
            if (!isfinite(p))
                continue;
            if (p > limit_tla) p = limit_tla;
            suma_tla_prawa += p;
            n_tla_prawa++;
        }

        if (n_sygnalu > 0U)
        {
            const float sygnal = suma_sygnalu / (float)n_sygnalu;
            float tlo_lewe = (n_tla_lewa > 0U) ? (suma_tla_lewa / (float)n_tla_lewa) : NAN;
            float tlo_prawe = (n_tla_prawa > 0U) ? (suma_tla_prawa / (float)n_tla_prawa) : NAN;
            float tlo = 1.0f;
            float db = 0.0f;

            /* RFSCAN9: obie strony tła mają taki sam głos niezależnie od tego,
             * ile próbek zostało po obcięciu zakresem. To jest ważne np. dla
             * 88,5 MHz przy skanie zaczynającym się od 88,0 MHz: prawa strona
             * nie może ważyć dwa razy mocniej tylko dlatego, że po lewej kończy
             * się zakres. W zatłoczonym paśmie FM wybieramy spokojniejszą ze
             * stron, ale nie pozwalamy zejść poniżej 0,75 globalnego tła. */
            if (isfinite(tlo_lewe) && isfinite(tlo_prawe))
                tlo = (tlo_lewe < tlo_prawe) ? tlo_lewe : tlo_prawe;
            else if (isfinite(tlo_lewe))
                tlo = tlo_lewe;
            else if (isfinite(tlo_prawe))
                tlo = tlo_prawe;
            if (tlo < 0.75f) tlo = 0.75f;

            if (sygnal > 0.0f && tlo > 0.0f)
                db = 10.0f * log10f(sygnal / tlo);
            skaner_wodospad_nad_tlem_db[x] = (isfinite(db) && db > 0.0f) ? db : 0.0f;
        }
        else
        {
            skaner_wodospad_nad_tlem_db[x] = 0.0f;
        }
    }
}

static uint16_t Skaner_WodospadPoziomZDb(float db)
{
    const uint8_t fm = Skaner_RF_CzyTrybFM();
    const float prog = fm ? SKANER_FM_PROG_NAD_TLEM_DB : SKANER_WODOSPAD_PROG_NAD_TLEM_DB;
    const float pelna = fm ? SKANER_FM_DB_PELNA_SKALA : SKANER_WODOSPAD_DB_PELNA_SKALA;

    if (!isfinite(db) || db <= prog)
        return 0U;
    db -= prog;
    if (db >= pelna)
        return 1023U;

    /* RFSCAN9: w FM używamy charakterystyki pierwiastkowej. Słaby, lecz
     * powtarzalny kanał 0,5..1 dB ponad tłem pozostaje widoczny jako niebieski/
     * cyjan, ale czerwony jest zarezerwowany dla naprawdę dużego odstępu od
     * tła. Poprzednia skala 2,2 dB nasycała wodospad na czerwono już przy
     * zwykłych lokalnych stacjach i tworzyła duże czerwone prostokąty. */
    if (fm)
    {
        float n = db / pelna;
        if (n < 0.0f) n = 0.0f;
        if (n > 1.0f) n = 1.0f;
        return (uint16_t)lroundf(sqrtf(n) * 1023.0f);
    }
    return (uint16_t)lroundf(db * (1023.0f / pelna));
}

static uint32_t Skaner_WodospadXNaCzestotliwosc(uint16_t x)
{
    const uint64_t zakres = (upper > lower) ? ((uint64_t)upper - (uint64_t)lower) : 0ULL;
    if (zakres == 0ULL)
        return (uint32_t)lower;
    return (uint32_t)((uint64_t)lower +
                      (zakres * (uint64_t)x) / (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U));
}

static void Skaner_WodospadDodajMarker(uint16_t x, float db)
{
    uint8_t i;
    uint8_t liczba = skaner_wodospad_liczba_markerow;
    const uint16_t p = Skaner_WodospadPoziomZDb(db);

    if (liczba < SKANER_WODOSPAD_MAX_MARKEROW)
    {
        skaner_wodospad_markery_x[liczba] = x;
        skaner_wodospad_markery_p[liczba] = p;
        skaner_wodospad_markery_db[liczba] = db;
        skaner_wodospad_liczba_markerow = (uint8_t)(liczba + 1U);
        return;
    }

    {
        uint8_t najslabszy = 0U;
        for (i = 1U; i < liczba; ++i)
            if (skaner_wodospad_markery_db[i] < skaner_wodospad_markery_db[najslabszy])
                najslabszy = i;
        if (db > skaner_wodospad_markery_db[najslabszy])
        {
            skaner_wodospad_markery_x[najslabszy] = x;
            skaner_wodospad_markery_p[najslabszy] = p;
            skaner_wodospad_markery_db[najslabszy] = db;
        }
    }
}

/*
 * Łączymy sąsiednie punkty ponad tłem w jedną stację/nośną. Dla WFM daje to
 * szeroki pionowy pas i marker w środku energetycznym zamiast losowego piku
 * modulacji. Dopuszczamy dwupikselową dziurę, bo chwilowa dewiacja FM nie może
 * rozcinać jednej stacji na kilka znaczników.
 */
static void Skaner_WodospadWyznaczMarkery(void)
{
    uint16_t x = 0U;
    const uint8_t fm = Skaner_RF_CzyTrybFM();
    const float prog_stacji = fm ? SKANER_FM_PROG_STACJI_DB : SKANER_WODOSPAD_PROG_STACJI_DB;
    const float prog_punkt = fm ? SKANER_FM_PROG_PUNKT_DB : SKANER_WODOSPAD_PROG_PUNKT_DB;
    uint16_t min_szerokosc = 2U;

    if (fm && upper > lower)
    {
        const uint64_t zakres = (uint64_t)upper - (uint64_t)lower;
        min_szerokosc = (uint16_t)(((uint64_t)SKANER_FM_MIN_SZEROKOSC_HZ *
                                    (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
        if (min_szerokosc < 2U) min_szerokosc = 2U;
        if (min_szerokosc > 24U) min_szerokosc = 24U;
    }

    skaner_wodospad_liczba_markerow = 0U;

    while (x < UI_WODOSPAD_SZEROKOSC)
    {
        uint16_t start;
        uint16_t koniec;
        uint16_t ostatni_mocny;
        uint16_t szerokosc;
        uint16_t centroid;
        uint8_t luka = 0U;
        float szczyt = -INFINITY;
        float suma_wag = 0.0f;
        float suma_x = 0.0f;

        if (!(skaner_wodospad_nad_tlem_db[x] >= prog_stacji))
        {
            x++;
            continue;
        }

        start = x;
        ostatni_mocny = x;
        while (x < UI_WODOSPAD_SZEROKOSC)
        {
            const float v = skaner_wodospad_nad_tlem_db[x];
            if (v >= prog_stacji)
            {
                const float w = v - prog_stacji + 0.1f;
                if (v > szczyt) szczyt = v;
                suma_wag += w;
                suma_x += w * (float)x;
                ostatni_mocny = x;
                luka = 0U;
            }
            else
            {
                luka++;
                if (luka > 2U)
                    break;
            }
            x++;
        }
        koniec = ostatni_mocny;
        szerokosc = (uint16_t)(koniec - start + 1U);

        if (suma_wag > 0.0f)
            centroid = (uint16_t)lroundf(suma_x / suma_wag);
        else
            centroid = (uint16_t)((start + koniec) / 2U);

        /* W trybie FM wymagamy szerokosci odpowiadajacej realnemu kanalowi,
         * chyba ze punkt jest wyjatkowo mocny. To usuwa losowe kropki szumu. */
        if (szerokosc >= min_szerokosc || szczyt >= prog_punkt)
            Skaner_WodospadDodajMarker(centroid, szczyt);
    }

    /* Wynik liczbowy skanera bierze najsilniejszy trwały klaster, a nie
     * największy prążek jednego FFT. */
    found = 0;
    skaner_wynik_energia = 1U;
    if (skaner_wodospad_liczba_markerow > 0U)
    {
        uint8_t i;
        uint8_t naj = 0U;
        for (i = 1U; i < skaner_wodospad_liczba_markerow; ++i)
            if (skaner_wodospad_markery_db[i] > skaner_wodospad_markery_db[naj])
                naj = i;
        fx = Skaner_WodospadXNaCzestotliwosc(skaner_wodospad_markery_x[naj]);
        binx = 0;
        Maxmag = 1.0f; /* znacznik ważności dla starszego UI; nie jest amplitudą */
        skaner_ostatni_stosunek_db = skaner_wodospad_markery_db[naj];
        found = 1;
    }
    else
    {
        Maxmag = 0.0f;
        skaner_ostatni_stosunek_db = -INFINITY;
    }
}

/*
 * RFSCAN11 - świadomy powrót do detektora RFSCAN7.
 *
 * Próba RFSCAN10 oceniania każdego kanału rastra przez średnią trzech punktów
 * z okna +/-60 kHz okazała się na prawdziwym torze EU1KY zbyt restrykcyjna:
 * po kilkunastu-kilkudziesięciu przebiegach potrafiła wyzerować nawet silne
 * stacje FM. Wracamy do metody sprawdzonej sprzętowo: wykryj klaster w surowej
 * mapie sygnał/tło, ustabilizuj jego środek do rastra 100 kHz, narysuj kanał i
 * zachowaj krótką pamięć między przebiegami.
 *
 * Zachowujemy późniejsze, dobre poprawki: oś i postęp przebiegu, 6-dB skalę
 * koloru bez czerwonych prostokątów, obsługę brzegu zakresu i pamiętanie Od-Do.
 */
static uint32_t Skaner_FM_ZaokraglijRaster(uint32_t hz)
{
    const uint32_t r = SKANER_FM_RASTER_HZ;
    return ((hz + r / 2U) / r) * r;
}

static uint16_t Skaner_FM_HzNaX(uint32_t hz)
{
    const uint64_t zakres = (upper > lower) ? ((uint64_t)upper - (uint64_t)lower) : 1ULL;
    if (hz <= lower) return 0U;
    if (hz >= upper) return (uint16_t)(UI_WODOSPAD_SZEROKOSC - 1U);
    return (uint16_t)(((uint64_t)(hz - lower) * (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
}

static void Skaner_FM_ZbudujMapeKanalow(void)
{
    float teraz[UI_WODOSPAD_SZEROKOSC];
    uint16_t x;
    uint8_t i;

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
        teraz[x] = 0.0f;

    for (i = 0U; i < skaner_wodospad_liczba_markerow; ++i)
    {
        uint32_t f = Skaner_WodospadXNaCzestotliwosc(skaner_wodospad_markery_x[i]);
        uint32_t fs = Skaner_FM_ZaokraglijRaster(f);
        uint32_t fa = (fs > SKANER_FM_KANAL_WYSWIETL_POL_HZ) ?
                      (fs - SKANER_FM_KANAL_WYSWIETL_POL_HZ) : lower;
        uint32_t fb = fs + SKANER_FM_KANAL_WYSWIETL_POL_HZ;
        uint16_t xa, xb;
        float db = skaner_wodospad_markery_db[i];

        if (fb < lower || fa > upper)
            continue;
        if (fa < lower) fa = lower;
        if (fb > upper) fb = upper;
        xa = Skaner_FM_HzNaX(fa);
        xb = Skaner_FM_HzNaX(fb);
        if (xb < xa) { uint16_t t = xa; xa = xb; xb = t; }

        for (x = xa; x <= xb; ++x)
        {
            /* Łagodne ramiona kanału, pełny poziom w środkowych ~100 kHz. */
            float k = 1.0f;
            const uint16_t szer = (uint16_t)(xb - xa + 1U);
            if (szer > 6U)
            {
                const uint16_t brzeg = (uint16_t)(szer / 5U);
                if (x < xa + brzeg)
                    k = 0.55f + 0.45f * (float)(x - xa) / (float)brzeg;
                else if (x > xb - brzeg)
                    k = 0.55f + 0.45f * (float)(xb - x) / (float)brzeg;
            }
            if (db * k > teraz[x])
                teraz[x] = db * k;
        }

        /* Sam marker i wynik liczbowy są stabilizowane do rastra 100 kHz. */
        skaner_wodospad_markery_x[i] = Skaner_FM_HzNaX(fs);
    }

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        float pamiec = skaner_fm_pamiec_wazna ?
                       (skaner_fm_pamiec_db[x] - SKANER_FM_PAMIEC_SPAD_DB) : 0.0f;
        if (pamiec < 0.0f) pamiec = 0.0f;
        if (teraz[x] > pamiec) pamiec = teraz[x];
        skaner_fm_pamiec_db[x] = pamiec;
        skaner_wodospad_nad_tlem_db[x] = pamiec;
    }
    skaner_fm_pamiec_wazna = 1U;

    /* Najsilniejszy marker po rasteryzacji jest również prezentowany liczbowo. */
    if (skaner_wodospad_liczba_markerow > 0U)
    {
        uint8_t naj = 0U;
        for (i = 1U; i < skaner_wodospad_liczba_markerow; ++i)
            if (skaner_wodospad_markery_db[i] > skaner_wodospad_markery_db[naj])
                naj = i;
        fx = Skaner_WodospadXNaCzestotliwosc(skaner_wodospad_markery_x[naj]);
    }
}

static void Skaner_WodospadZbudujLinie(void)
{
    uint16_t x;
    uint16_t i;
    float mediana_globalna;
    uint16_t polokno;
    uint16_t *poziomy = UI_WodospadBuforPoziomow();
    uint32_t *kolory = UI_WodospadBuforLinii();
    const uint64_t zakres = (upper > lower) ? ((uint64_t)upper - (uint64_t)lower) : 1ULL;

    /* Uśrednienie w czasie: stała stacja zostaje w tym samym miejscu, losowy
     * szum znika. Pierwsza linia jest natychmiastowa, kolejne mają EMA 35%. */
    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        float v;
        if (skaner_wodospad_licznik[x] == 0U)
            continue;
        v = skaner_wodospad_moc_db[x] / (float)skaner_wodospad_licznik[x];
        if (!isfinite(v))
            continue;
        if (!skaner_wodospad_ma_srednia || !isfinite(skaner_wodospad_srednia_db[x]))
            skaner_wodospad_srednia_db[x] = v;
        else
            skaner_wodospad_srednia_db[x] =
                (1.0f - SKANER_RF_EMA_NOWA) * skaner_wodospad_srednia_db[x] +
                SKANER_RF_EMA_NOWA * v;
    }
    skaner_wodospad_ma_srednia = 1U;

    mediana_globalna = Skaner_WodospadMedianaMocy();

    if (Skaner_RF_CzyTrybFM())
    {
        /* Dla radiofonii FM najpierw integrujemy caly kanal ~200 kHz.
         * To jest zasadnicza zmiana RFSCAN6: stacja ma byc widoczna jako
         * szeroki, stabilny pas, a nie jako pojedynczy prazek modulacji. */
        Skaner_WodospadPrzetworzFM(mediana_globalna);
    }
    else
    {
        /* Lokalna linia odniesienia ma około 2 MHz szerokości (±1 MHz), ale
         * ograniczamy ją do rozsądnego zakresu pikseli. Dzięki temu kompensuje
         * powolne nierówności toru bez zmiany zachowania pasm amatorskich. */
        polokno = (uint16_t)(((uint64_t)1000000U *
                              (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
        if (polokno < 16U) polokno = 16U;
        if (polokno > 160U) polokno = 160U;

        for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
        {
            float v = skaner_wodospad_srednia_db[x];
            float tlo = Skaner_WodospadLokalneTlo(x, mediana_globalna, polokno);
            float nad = isfinite(v) ? (v - tlo) : 0.0f;
            skaner_wodospad_nad_tlem_db[x] = (nad > 0.0f && isfinite(nad)) ? nad : 0.0f;
        }
    }

    /* Najpierw rozpoznaj klastry w surowej mapie. W FM dopiero potem
     * rasteryzujemy kanały i dodajemy krótką pamięć obecności. */
    Skaner_WodospadWyznaczMarkery();
    if (Skaner_RF_CzyTrybFM())
        Skaner_FM_ZbudujMapeKanalow();

    /* Łagodne 1-2-1 w osi częstotliwości usuwa ząbki bez rozmywania WFM. */
    for (x = 1U; x + 1U < UI_WODOSPAD_SZEROKOSC; ++x)
    {
        const float a = skaner_wodospad_nad_tlem_db[x - 1U];
        const float b = skaner_wodospad_nad_tlem_db[x];
        const float c = skaner_wodospad_nad_tlem_db[x + 1U];
        const float v = (a + 2.0f * b + c) * 0.25f;
        poziomy[x] = Skaner_WodospadPoziomZDb(v);
    }
    poziomy[0] = Skaner_WodospadPoziomZDb(skaner_wodospad_nad_tlem_db[0]);
    poziomy[UI_WODOSPAD_SZEROKOSC - 1U] =
        Skaner_WodospadPoziomZDb(skaner_wodospad_nad_tlem_db[UI_WODOSPAD_SZEROKOSC - 1U]);

    for (x = 0U; x < UI_WODOSPAD_SZEROKOSC; ++x)
        kolory[x] = UI_WodospadKolor(poziomy[x]);

    for (i = 0U; i < SKANER_WODOSPAD_GRUBOSC_WIERSZA; ++i)
        UI_WodospadHistoriaDodaj(poziomy);
}

/*
 * Jeden przebieg RFSCAN5. Liczba próbek jest co najmniej równa szerokości
 * ekranu, a dla szerokich zakresów rośnie tak, aby krok LO nie przekraczał
 * 15 kHz (do limitu czasu 2400 FFT). To usuwa dawny przypadek 2 MHz, w którym
 * faktycznie wykonywano zaledwie kilkadziesiąt punktów i na ekranie powstawały
 * przypadkowe krótkie kreski zamiast ciągłego obrazu stacji.
 */
/*
 * RFSCAN13: wodospad ma byc samodzielna funkcja. Wczesniej tor odbiorczy
 * potrafil ruszyc dopiero po recznym "Skanuj", bo ten pelny skan
 * pozostawial aktywny i ustabilizowany LO oraz swieze probki SAI.
 *
 * Tutaj wykonujemy jawne przygotowanie toru przy KAZDYM wejsciu do
 * wodospadu: inicjalizacja generatora, wylaczenie F0, ustawienie samego LO
 * na srodku wybranego zakresu i dwie probki rozruchowe. Dzieki temu
 * Wodospad nie zalezy od tego, czy uzytkownik wczesniej nacisnal Skanuj.
 */
static int Skaner_RF_PrzygotujWodospadNiezaleznie(void)
{
    uint32_t f_srodek;
    uint32_t if_hz;
    uint32_t lo_hz;

    if (rfft_mags == 0 || upper <= lower)
        return 0;

    GEN_Init();
    SetColours();

    /* F0 nie moze nadawac podczas pracy skanera. GEN_SetF0Freq(0) wylacza
     * tor pomiarowy, dlatego bezposrednio potem jawnie uruchamiamy CLK1/LO. */
    GEN_SetF0Freq(0U);

    f_srodek = (uint32_t)((uint64_t)lower +
                 ((uint64_t)upper - (uint64_t)lower) / 2ULL);
    if_hz = DSP_GetIF();
    lo_hz = (f_srodek > if_hz) ? (f_srodek - if_hz) : f_srodek;
    GEN_SetLOFreq(lo_hz);

    /* Pierwsza ramka po ponownym uruchomieniu LO moze zawierac stan
     * przejsciowy kodeka/filtru. Nie wprowadzamy jej do wodospadu. */
    Sleep(3U);
    DSP_Sample();
    DSP_Sample();

    Average = 0.0f;
    Maxmag = 0.0f;
    skaner_ostatni_szczyt_fft = 0.0f;
    skaner_ostatnie_tlo_fft = 0.0f;
    skaner_ostatni_stosunek_db = -INFINITY;
    return 1;
}

static int Skaner_RF_WykonajPrzebiegEnergii(uint8_t pokaz_postep)
{
    const uint64_t zakres = (upper > lower) ? ((uint64_t)upper - (uint64_t)lower) : 0ULL;
    uint32_t liczba_probek;
    uint32_t i;

    if (zakres == 0ULL || rfft_mags == 0)
        return 0;

    liczba_probek = (uint32_t)(zakres / SKANER_RF_KROK_MAX_HZ) + 1U;
    if (liczba_probek < UI_WODOSPAD_SZEROKOSC)
        liczba_probek = UI_WODOSPAD_SZEROKOSC;
    if (liczba_probek > SKANER_RF_MAX_PROBEK_PRZEBIEGU)
        liczba_probek = SKANER_RF_MAX_PROBEK_PRZEBIEGU;
    if (liczba_probek < 2U)
        liczba_probek = 2U;

    Skaner_WodospadResetujProbki();

    for (i = 0U; i < liczba_probek; ++i)
    {
        const uint32_t f = (uint32_t)((uint64_t)lower +
                           (zakres * (uint64_t)i) / (uint64_t)(liczba_probek - 1U));
        const uint16_t x = (uint16_t)(((uint64_t)(f - lower) *
                           (uint64_t)(UI_WODOSPAD_SZEROKOSC - 1U)) / zakres);
        const float moc_db = Skaner_RF_MierzMocOknaDb(f);

        if (TOUCH_IsPressed())
        {
            if (TOUCH_Poll(&pt))
                return -1;
        }

        Skaner_WodospadZapiszMoc(x, moc_db);

        if (pokaz_postep == 1U && (i % 48U) == 0U)
            Skaner_RysujPostepSkanowania(f);
        else if (pokaz_postep == 2U)
        {
            const uint32_t krok_rysowania = (liczba_probek >= 20U) ? (liczba_probek / 20U) : 1U;
            if ((i % krok_rysowania) == 0U || i + 1U == liczba_probek)
            {
                skaner_wodospad_postep_permille = (uint16_t)(((uint64_t)(i + 1U) * 1000ULL) /
                                                               (uint64_t)liczba_probek);
                skaner_wodospad_postep_f_hz = f;
                if (!skaner_wodospad_pelny)
                {
                    char tekst[64];
                    char ftxt[24];
                    const uint32_t tlo = UI_KolorTlaEkranu();
                    const uint32_t procent = (uint32_t)skaner_wodospad_postep_permille / 10U;
                    uint32_t szer;

                    UI_FormatujCzestotliwoscMHzKrotko(f, ftxt, sizeof(ftxt));
                    snprintf(tekst, sizeof(tekst),
                             JEZYK_Wybierz("Przebieg %lu  %lu%%  %s",
                                           "Sweep %lu  %lu%%  %s",
                                           "Durchlauf %lu  %lu%%  %s",
                                           "Проход %lu  %lu%%  %s"),
                             (unsigned long)skaner_wodospad_przebieg_biezacy,
                             (unsigned long)procent, ftxt);

                    LCD_FillRect(LCD_MakePoint(0U, 112U), LCD_MakePoint(479U, 128U), tlo);
                    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 4U, 112U, tekst);
                    /* Pasek 0..100% jest świadomie bardzo cienki, żeby nie zabierał
                     * miejsca osi częstotliwości ani historii wodospadu. */
                    LCD_FillRect(LCD_MakePoint(0U, 126U), LCD_MakePoint(479U, 128U), UI_KolorTlaPola());
                    szer = ((uint32_t)skaner_wodospad_postep_permille * 480U) / 1000U;
                    if (szer > 0U)
                        LCD_FillRect(LCD_MakePoint(0U, 126U),
                                     LCD_MakePoint((uint16_t)(szer - 1U), 128U),
                                     UI_KolorTekstu(UI_STYL_AKCENT));
                }
            }
        }
    }

    return 1;
}

static void Skaner_RysujPostepSkanowania(uint32_t czestotliwosc_hz)
{
    char ftxt[32];
    char komunikat[64];

    UI_FormatujCzestotliwoscMHz(czestotliwosc_hz, ftxt, sizeof(ftxt));
    snprintf(komunikat, sizeof(komunikat), "%s %s",
             JEZYK_Wybierz("Skanuję:", "Scanning:", "Scan:", "Сканирование:"), ftxt);
    UI_RysujPoleStatusu(150, 148, 326, 70, JEZYK_Tekst(TEKST_SKANER_RF_TYTUL),
                        komunikat, UI_STYL_AKCENT);
}

int SpectrumExec(int graph)
{

    float sum1, suma_wodospadu, flabel, MaxiMag, steps, MaxMag0, MaxMag1, MaxMag2, Faktor, testFreq, fraction;
    float stosunek0, stosunek1, stosunek2, maks_stosunek_wodospadu;
    int AggregatedPoints, x, l, w, k, delta, n;
    int yofs, LastX;
    int znaleziono_szczyt = 0;
    //uint32_t power, t;
    //unsigned long FLow;
    char s[20];
    char f[25];
    int MaxiBin = 0, binMax0, binMax1, binMax2, linediv = 0, lmod = 0;
    unsigned long MaxiFreq = 0, fmaxSIhz = CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);

    if (rfft_mags == 0 || upper <= lower)
        return 0;

    if (graph != 2)
        skaner_wynik_energia = 0U;

    if (graph == 2)
        Skaner_WodospadResetujProbki();

    Maxmag = 0.0;
    MaxiMag = 0.0;
    Average = 0.0;
    //uint32_t h=LCD_GetHeight();
    LastX = LCD_GetWidth() + 1;
    steps = (upper - lower) / 17500.0f; // in 17.5 kHz steps
    if (graph == 1)
    {
        if (Scale_Factors[span] == 8)
        {
            linediv = 11;
            lmod = 40;
        }
        else
        {
            linediv = 9;
            lmod = 50;
        }
        k = linediv * lmod;
    }
    else
        k = 450;

    Faktor = (float)((float)k / (upper - lower));
    AggregatedPoints = (steps + 225) / k; // number of measures, aggregated to 1 point (rounded)
    if (AggregatedPoints <= 1)
        AggregatedPoints = 1;

    if (graph == 1)
    {
        fraction = (float)((upper - lower) / Scale_Factors[span]) / 5000000.f;
        k = 470 - k;
        for (n = 0; n <= lmod; n++)
        { //Scale_Factors[span]
            x = k + n * linediv;
            if (n % 5 == 0)
            {
                if (n % 10 == 0)
                {
                    flabel = (float)(lower / 1000000.f + n * fraction); // ??
                    if (fraction < 0.001)
                        sprintf(f, "%.4f", flabel);
                    else if (flabel > 99.99)
                        sprintf(f, "%.2f", flabel);
                    else if (flabel > 9.99)
                        sprintf(f, "%.2f", flabel);
                    else
                        sprintf(f, "%.3f", flabel);

                    w = FONT_GetStrPixelWidth(FONT_SDIGITS, f);
                    l = x - 8 - w / 2;
                    if (l < 0)
                        l = 0;

                    if (l + w > 460)
                        l = 460 - w;
                    FONT_Write(FONT_FRAN, TextColor, BackGrColor, l, 210, f);
                    LCD_VLine(LCD_MakePoint(x - 1, 205), 5, LCD_COLOR_RED);
                    LCD_VLine(LCD_MakePoint(x + 1, 205), 5, LCD_COLOR_RED);
                }
                LCD_VLine(LCD_MakePoint(x, 205), 5, LCD_COLOR_RED);
            }
            else
            {
                LCD_VLine(LCD_MakePoint(x, 205), 5, LCD_COLOR_YELLOW);
            }
        }
        if (upper - lower < 50000)
        {
            GEN_SetLOFreq(lower);
            Faktor = (float)(upper - lower) / 40000;
            if (Faktor == 1)
            {
                if (TOUCH_IsPressed())
                {
                    if (TOUCH_Poll(&pt))
                        return -1;
                }
                DSP_Sample();
                Calc_fft_audiobuf(0);
                for (n = 0; n < 225; n++) // 0 Hz .. 20.625 kHz
                {
                    signal = rfft_mags[n];
                    x = 30 + n * 0.975f;
                    ShowSpectLine(x);
                }
                GEN_SetLOFreq(upper);
                if (TOUCH_IsPressed())
                {
                    if (TOUCH_Poll(&pt))
                        return -1;
                }
                DSP_Sample();
                Calc_fft_audiobuf(0);
                for (n = 0; n < 225; n++) // 0 Hz .. 20.625 kHz
                {
                    signal = rfft_mags[n];
                    x = 470 - n * 0.975f;
                    ShowSpectLine(x);
                }
            }
            else if (Faktor == 0.5f)
            {
                if (TOUCH_IsPressed())
                {
                    if (TOUCH_Poll(&pt))
                        return -1;
                }
                DSP_Sample();
                Calc_fft_audiobuf(0);
                for (n = 0; n < 225; n++) // 0 Hz .. 20.625 kHz
                {
                    signal = rfft_mags[n];
                    x = 30 + 2 * n * 0.975f;
                    ShowSpectLine(x);
                }
            }

            else
            {
                for (k = 0; k < 1 / Faktor; k++)
                {
                    GEN_SetLOFreq(lower + k * 97.5 * Faktor);
                    if (TOUCH_IsPressed())
                    {
                        if (TOUCH_Poll(&pt))
                            return -1;
                    }
                    DSP_Sample();
                    Calc_fft_audiobuf(0);
                    for (n = 0; n < 2 * 225 * Faktor; n++) // 0 Hz .. 20.625 kHz
                    {
                        signal = rfft_mags[n];
                        x = 30 + ((float)n / Faktor + k) * 0.975f;
                        ShowSpectLine(x);
                    }
                }
            }

            for (n = 0; n < 7; n++)
            { // paint vertical scale
                yofs = n * 27 + 16;
                sprintf(s, "%d", (int)dBs[n]);
                FONT_Write(FONT_FRAN, TextColor, BackGrColor, 5, yofs - 9, s);
                LCD_HLine(LCD_MakePoint(30, yofs), 450, LCD_COLOR_DARKGRAY);
            }
            return 2;
        }
    }
    k = 0;
    Maxmag = 0;
    Average = 0.;
    if (AggregatedPoints > 1)
    {
        for (actFreq = upper + 18000; actFreq >= lower - 18000;)
        { // 3 ms per step
            sum1 = 0.;
            suma_wodospadu = 0.0f;
            maks_stosunek_wodospadu = -INFINITY;
            for (l = 0; l < AggregatedPoints; l++)
            {
                if (TOUCH_IsPressed())
                {
                    if (TOUCH_Poll(&pt))
                        return -1;
                }
                sum1 = 0.0; //Maxmag/1000;//++++++++++++++++++++++++++++++++++++++++++
                binMax0 = CalcMaxBin(actFreq);
                if (isfinite(skaner_ostatni_stosunek_db) &&
                    (!isfinite(maks_stosunek_wodospadu) || skaner_ostatni_stosunek_db > maks_stosunek_wodospadu))
                    maks_stosunek_wodospadu = skaner_ostatni_stosunek_db;
                if (Maxmag > suma_wodospadu)
                    suma_wodospadu = Maxmag;
                if (binMax0 != 0 && Maxmag > sum1)
                    sum1 = Maxmag;
                if (binMax0 != 0 && Maxmag > MaxiMag)
                {
                    MaxiMag = Maxmag;
                    MaxiBin = binMax0;
                    MaxiFreq = actFreq;
                    znaleziono_szczyt = 1;
                }
                actFreq -= 17500ul;
            } // end inner "for"

            if (TOUCH_IsPressed())
            {
                if (TOUCH_Poll(&pt))
                    return -1;
            }
            Maxmag = 0;
            actFreq += 17500ul;
            if (actFreq > fmaxSIhz)
                sum1 *= 1.5;
            signal = (graph == 2) ? suma_wodospadu : sum1;
            x = 470 - (int)((double)(upper - actFreq) * Faktor);
            if (graph == 2)
                Skaner_WodospadZapiszPunkt(x, maks_stosunek_wodospadu);
            if (graph == 1)
            {
                if ((x > 470 - linediv * lmod) && (x != LastX))
                {
                    ShowSpectLine(x);
                    LastX = x;
                }
            }
            else if (graph == 0)
            {
                k++;
                if (k > 20)
                {
                    k = 0;
                    Skaner_RysujPostepSkanowania((uint32_t)actFreq);
                }
            }
            actFreq -= 17500ul;
            if (x < 28)
                break;
        } // end for{}
    }

    else
    {
        breaker = 0;
        LastX = LCD_GetWidth() + 1;
        for (actFreq = upper + 18000; actFreq >= lower - 18000;)
        { // 3 ms per step
            if (TOUCH_IsPressed())
            {
                if (TOUCH_Poll(&pt))
                    return -1;
            }
            Maxmag = 0;
            binMax0 = CalcMaxBin(actFreq);
            MaxMag0 = Maxmag;
            stosunek0 = skaner_ostatni_stosunek_db;
            Maxmag = 0;
            binMax1 = CalcMaxBin(actFreq - 15000);
            MaxMag1 = Maxmag;
            stosunek1 = skaner_ostatni_stosunek_db;
            Maxmag = 0;
            binMax2 = CalcMaxBin(actFreq - 16500);
            MaxMag2 = Maxmag;
            stosunek2 = skaner_ostatni_stosunek_db;
            maks_stosunek_wodospadu = stosunek0;
            if (isfinite(stosunek1) && (!isfinite(maks_stosunek_wodospadu) || stosunek1 > maks_stosunek_wodospadu))
                maks_stosunek_wodospadu = stosunek1;
            if (isfinite(stosunek2) && (!isfinite(maks_stosunek_wodospadu) || stosunek2 > maks_stosunek_wodospadu))
                maks_stosunek_wodospadu = stosunek2;
            if (MaxMag0 > MaxMag1)
                signal = MaxMag0;
            else
                signal = MaxMag1;
            if (MaxMag2 > signal)
                signal = MaxMag2;
            if (MaxMag0 < signal * 0.6)
                binMax0 = 0;
            if (MaxMag1 < signal * 0.6)
                binMax1 = 0;
            if (MaxMag2 < signal * 0.6)
                binMax2 = 0;
            delta = Delta(binMax0, binMax1, binMax2);
            if (breaker == 9)
                signal = Average;
            breaker = 0;
            if ((binMax0 != 0 || binMax1 != 0 || binMax2 != 0) && signal > MaxiMag)
            {
                MaxiMag = signal;
                MaxiBin = -delta;
                MaxiFreq = actFreq;
                znaleziono_szczyt = 1;
            }
            x = 470 - (int)((double)(upper - actFreq - delta * 93.75f) * Faktor + 0.5f);
            if (graph == 2)
                Skaner_WodospadZapiszPunkt(x, maks_stosunek_wodospadu);
            if (graph == 1)
            {
                if (actFreq > fmaxSIhz)
                    signal *= 1.5;
                if (x < LastX)
                {
                    for (n = x; n < LastX; n++)
                    {
                        if ((n < 470) && (n > 29))
                        {
                            LCD_VLine(LCD_MakePoint(n, 2), 201, BackGrColor); //delete old content
                        }
                        else
                            break;
                    }
                }
                LastX = x;
                ShowSpectLine(x);
            }
            else if (graph == 0)
            {
                k++;
                if (k > 20)
                {
                    k = 0;
                    Skaner_RysujPostepSkanowania((uint32_t)actFreq);
                }
            }
            actFreq -= 52500;
        } // end "for"
    }
    if (graph == 1)
    {
        for (n = 0; n < 7; n++)
        { // paint vertical scale
            yofs = n * 27 + 16;
            sprintf(s, "%d", (int)dBs[n]);
            FONT_Write(FONT_FRAN, TextColor, BackGrColor, 5, yofs - 9, s);
            LCD_HLine(LCD_MakePoint(30, yofs), 450, LCD_COLOR_DARKGRAY);
        }
    }
    if (!znaleziono_szczyt || MaxiFreq == 0U || !(MaxiMag > 0.0f) || !isfinite(MaxiMag))
    {
        Maxmag = 0.0f;
        found = 0;
        skaner_ostatni_stosunek_db = -INFINITY;
        return 0;
    }

    MaxMag0 = MaxiMag;

    binx = (MaxiFreq > 16000U) ? CalcMaxBin(MaxiFreq - 16000U) : 0; // doprecyzowanie
    if (binx != 0)
    {
        if (Maxmag > MaxiMag)
        {
            MaxiMag = Maxmag;
            MaxiFreq = MaxiFreq - 16000;
            MaxiBin = binx;
        }
    }
    binx = (MaxiFreq <= UINT32_MAX - 14000U) ? CalcMaxBin(MaxiFreq + 14000U) : 0; // doprecyzowanie
    if (binx != 0)
    {
        if (Maxmag > MaxiMag)
        {
            MaxiMag = Maxmag;
            MaxiFreq = MaxiFreq + 14000;
            MaxiBin = binx;
        }
    }
    binx = (MaxiFreq <= UINT32_MAX - 8000U) ? CalcMaxBin(MaxiFreq + 8000U) : 0; // doprecyzowanie
    if (binx != 0)
    {
        if (Maxmag > MaxiMag)
        {
            MaxiMag = Maxmag;
            MaxiFreq = MaxiFreq + 8000;
            MaxiBin = binx;
        }
    }
    if (MaxiFreq > fmaxSIhz)
        signal = 1.5 * MaxiMag;
    else
        signal = MaxiMag;
    fx = MaxiFreq;
    binx = MaxiBin;
    if (graph == 1)
    {
        x = 470 - (int)((double)(upper - MaxiFreq + binx * 93.75f) * Faktor + 0.5f);
        testFreq = (float)(fx - (int)(float)(binx * 93.75f + 0.05)) / 1000;
        if (testFreq < 100000)
            sprintf(f, "%.1f kHz ", (float)testFreq);
        else
            sprintf(f, "%.2f MHz ", (float)testFreq / 1000);
        if (x < 235)
            k = 235;
        else
            k = 45;
        if (signal > 164) // trigger value
            FONT_Write(FONT_FRANBIG, TextColor, BackGrColor, k, 10, f);
        ShowSpectLine(x); // correction
    }
    Maxmag = MaxiMag;
    if (Average > 1e-20f && isfinite(Average))
        skaner_ostatni_stosunek_db = 20.0f * log10f(MaxiMag / Average);
    else
        skaner_ostatni_stosunek_db = INFINITY;
    found = 1;
    return 2;
}

enum
{
    SKANER_WODOSPAD_AKCJA_WSTECZ = 1,
    SKANER_WODOSPAD_AKCJA_PRACA,
    SKANER_WODOSPAD_AKCJA_PELNY
};

static void Skaner_FormatujMHzBezJednostki(uint32_t czestotliwosc_hz, char *bufor, size_t rozmiar)
{
    const char separator = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    const uint32_t szerokosc = (upper > lower) ? (uint32_t)(upper - lower) : 0U;
    if (bufor == 0 || rozmiar == 0U)
        return;
    if (szerokosc >= 500000U)
        snprintf(bufor, rozmiar, "%lu%c%03lu",
                 (unsigned long)(czestotliwosc_hz / 1000000U), separator,
                 (unsigned long)((czestotliwosc_hz % 1000000U) / 1000U));
    else
        snprintf(bufor, rozmiar, "%lu%c%04lu",
                 (unsigned long)(czestotliwosc_hz / 1000000U), separator,
                 (unsigned long)((czestotliwosc_hz % 1000000U) / 100U));
}

static void Skaner_WodospadFormatujOs(uint32_t hz, char *bufor, size_t rozmiar)
{
    const char sep = JEZYK_CzySeparatorDziesietnyPrzecinek() ? ',' : '.';
    const uint32_t szerokosc = (upper > lower) ? (uint32_t)(upper - lower) : 0U;

    if (bufor == 0 || rozmiar == 0U)
        return;

    if (szerokosc >= 10000000U && (hz % 1000000U) == 0U)
        snprintf(bufor, rozmiar, "%lu", (unsigned long)(hz / 1000000U));
    else if (szerokosc >= 1000000U)
        snprintf(bufor, rozmiar, "%lu%c%01lu",
                 (unsigned long)(hz / 1000000U), sep,
                 (unsigned long)((hz % 1000000U) / 100000U));
    else
        snprintf(bufor, rozmiar, "%lu%c%03lu",
                 (unsigned long)(hz / 1000000U), sep,
                 (unsigned long)((hz % 1000000U) / 1000U));
}

static void Skaner_WodospadRysujSkale(uint8_t pelny)
{
    const uint16_t y0 = pelny ? SKANER_WODOSPAD_PELNY_SKALA_Y_GORA : SKANER_WODOSPAD_SKALA_Y_GORA;
    const uint16_t y1 = pelny ? SKANER_WODOSPAD_PELNY_SKALA_Y_DOL  : SKANER_WODOSPAD_SKALA_Y_DOL;
    const uint32_t tlo = pelny ? UI_WodospadKolor(0U) : UI_KolorTlaEkranu();
    const uint32_t fg = pelny ? LCD_WHITE : UI_KolorTekstu(UI_STYL_NIEAKTYWNY);
    const uint32_t znacznik = UI_KolorTekstu(UI_STYL_AKCENT);
    uint8_t i;

    LCD_FillRect(LCD_MakePoint(0U, y0), LCD_MakePoint(479U, y1), tlo);

    for (i = 0U; i < 5U; ++i)
    {
        char txt[16];
        const uint32_t f = (uint32_t)((uint64_t)lower +
                           ((uint64_t)(upper - lower) * (uint64_t)i) / 4ULL);
        const uint16_t x = (uint16_t)(((uint32_t)479U * i) / 4U);
        int16_t tx;
        int16_t w;

        Skaner_WodospadFormatujOs(f, txt, sizeof(txt));
        w = FONT_GetStrPixelWidth(FONT_FRAN, txt);
        tx = (int16_t)x - w / 2;
        if (tx < 1)
            tx = 1;
        if (tx + w > 477)
            tx = (int16_t)(477 - w);
        FONT_Write(FONT_FRAN, fg, tlo, tx, y0, txt);
        LCD_VLine(LCD_MakePoint(x, (uint16_t)(y1 - 3U)), 4U, fg);
    }

    /* Małe kreski na osi wskazują wykryte lokalne maksima ostatniego przebiegu.
     * Nie zastępują kolorowego wodospadu; pozwalają jednak natychmiast policzyć
     * stacje/nośne i odczytać ich położenie względem skali. */
    for (i = 0U; i < skaner_wodospad_liczba_markerow; ++i)
    {
        const uint16_t x = skaner_wodospad_markery_x[i];
        LCD_VLine(LCD_MakePoint(x, (uint16_t)(y1 - 7U)), 5U, znacznik);
        if (x > 0U)
            LCD_VLine(LCD_MakePoint((uint16_t)(x - 1U), (uint16_t)(y1 - 5U)), 3U, znacznik);
    }
}

static void Skaner_WodospadWypelnijAkcje(UI_AKCJA_t akcje[3])
{
    akcje[0] = (UI_AKCJA_t){SKANER_WODOSPAD_AKCJA_WSTECZ,
                            JEZYK_Wybierz("Wstecz", "Back", "Zurück", "Назад"),
                            UI_STYL_POWROT, true, false};
    akcje[1] = (UI_AKCJA_t){SKANER_WODOSPAD_AKCJA_PRACA,
                            skaner_wodospad_ciagly ?
                                JEZYK_Wybierz("Pauza", "Pause", "Pause", "Пауза") :
                                JEZYK_Wybierz("Wznów", "Resume", "Weiter", "Продолж."),
                            UI_STYL_NORMALNY, true, skaner_wodospad_ciagly != 0U};
    akcje[2] = (UI_AKCJA_t){SKANER_WODOSPAD_AKCJA_PELNY,
                            JEZYK_Wybierz("Pełny ekran", "Full screen", "Vollbild", "Весь экран"),
                            UI_STYL_AKCENT, true, false};
}

static void Skaner_WodospadRysujDane(void)
{
    SKANER_WYNIK_t wynik = Skaner_PobierzWynik();
    char czestotliwosc[28] = "---";
    char stosunek[16] = "--";
    char poziom[40];
    char zakres_dol[24];
    char zakres_gora[24];
    char zakres[56];
    const uint32_t tlo = UI_KolorTlaEkranu();

    if (wynik.wazny)
    {
        Skaner_FormatujMHzBezJednostki(wynik.czestotliwosc_hz,
                                      czestotliwosc, sizeof(czestotliwosc));
        if (isfinite(wynik.stosunek_db))
            snprintf(stosunek, sizeof(stosunek), "%.1f", (double)wynik.stosunek_db);
        else
            snprintf(stosunek, sizeof(stosunek), ">60");

        if (skaner_wynik_energia)
            snprintf(poziom, sizeof(poziom), "%s",
                     JEZYK_Wybierz("Poziom: względny", "Level: relative",
                                   "Pegel: relativ", "Уровень: относит."));
        else if (isfinite(wynik.moc_dbm))
            snprintf(poziom, sizeof(poziom),
                     JEZYK_Wybierz("Poziom: ~%.1f dBm (orient.)",
                                   "Level: ~%.1f dBm (approx.)",
                                   "Pegel: ~%.1f dBm (ca.)",
                                   "Уровень: ~%.1f dBm (прибл.)"),
                     (double)wynik.moc_dbm);
        else
            snprintf(poziom, sizeof(poziom), "%s",
                     JEZYK_Wybierz("Poziom: niewzorcowany", "Level: uncalibrated",
                                   "Pegel: unkalibriert", "Уровень: не калиброван"));
    }
    else
    {
        snprintf(poziom, sizeof(poziom), "%s",
                 JEZYK_Wybierz("Brak pewnego sygnału", "No reliable signal",
                               "Kein sicheres Signal", "Нет уверенного сигнала"));
    }

    UI_RysujPoleLiczboweGlowne(4U, 34U, 300U, 76U,
                               JEZYK_Tekst(TEKST_SKANER_RF_CZESTOTLIWOSC),
                               czestotliwosc, "MHz");
    UI_RysujPoleLiczboweGlowne(310U, 34U, 166U, 76U,
                               JEZYK_Tekst(TEKST_SKANER_RF_SZCZYT_TLO),
                               stosunek, "dB");

    UI_FormatujCzestotliwoscMHzKrotko((uint32_t)lower, zakres_dol, sizeof(zakres_dol));
    UI_FormatujCzestotliwoscMHzKrotko((uint32_t)upper, zakres_gora, sizeof(zakres_gora));
    snprintf(zakres, sizeof(zakres), "%s - %s", zakres_dol, zakres_gora);

    LCD_FillRect(LCD_MakePoint(0U, 112U), LCD_MakePoint(479U, 128U), tlo);
    if (!skaner_wodospad_ciagly && skaner_wodospad_przebiegi_gotowe > 0U)
    {
        char gotowe[48];
        snprintf(gotowe, sizeof(gotowe),
                 JEZYK_Wybierz("Pauza - gotowe przebiegi: %lu",
                               "Paused - completed sweeps: %lu",
                               "Pause - fertige Durchläufe: %lu",
                               "Пауза - готовых проходов: %lu"),
                 (unsigned long)skaner_wodospad_przebiegi_gotowe);
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 4U, 112U, gotowe);
    }
    else
    {
        FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo, 4U, 112U, poziom);
    }
    FONT_Write_RightAlign(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), tlo,
                          250U, 112U, 476U, zakres);
}

static void Skaner_WodospadRysujWidok(void)
{
    if (skaner_wodospad_pelny)
    {
        LCD_FillAll(UI_WodospadKolor(0U));
        Skaner_WodospadRysujSkale(1U);
        UI_WodospadHistoriaRysuj(0U, SKANER_WODOSPAD_PELNY_GORA,
                                 479U, SKANER_WODOSPAD_PELNY_DOL);
        return;
    }

    {
        UI_AKCJA_t akcje[3];
        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Wybierz("Skaner RF / wodospad", "RF scanner / waterfall",
                                       "HF-Scanner / Wasserfall", "ВЧ-сканер / водопад"));
        Skaner_WodospadRysujDane();
        Skaner_WodospadRysujSkale(0U);
        UI_WodospadHistoriaRysuj(0U, SKANER_WODOSPAD_Y_GORA,
                                 479U, SKANER_WODOSPAD_Y_DOL);
        Skaner_WodospadWypelnijAkcje(akcje);
        UI_RysujPasekAkcji(220U, 48U, akcje, 3U);
    }
}

static void Skaner_WodospadDodajNaEkran(void)
{
    uint8_t i;
    uint32_t *kolory = UI_WodospadBuforLinii();

    if (skaner_wodospad_pelny)
    {
        for (i = 0U; i < SKANER_WODOSPAD_GRUBOSC_WIERSZA; ++i)
            UI_WodospadDodajLinie(0U, SKANER_WODOSPAD_PELNY_GORA,
                                  479U, SKANER_WODOSPAD_PELNY_DOL, kolory);
        Skaner_WodospadRysujSkale(1U);
    }
    else
    {
        for (i = 0U; i < SKANER_WODOSPAD_GRUBOSC_WIERSZA; ++i)
            UI_WodospadDodajLinie(0U, SKANER_WODOSPAD_Y_GORA,
                                  479U, SKANER_WODOSPAD_Y_DOL, kolory);
        Skaner_WodospadRysujDane();
        Skaner_WodospadRysujSkale(0U);
    }
}

static uint8_t Skaner_WodospadObsluzDotyk(LCDPoint punkt)
{
    if (skaner_wodospad_pelny)
    {
        skaner_wodospad_pelny = 0U;
        Skaner_WodospadRysujWidok();
        return 0U;
    }

    if (punkt.y >= SKANER_WODOSPAD_Y_GORA && punkt.y <= SKANER_WODOSPAD_Y_DOL)
    {
        skaner_wodospad_pelny = 1U;
        Skaner_WodospadRysujWidok();
        return 0U;
    }

    {
        UI_AKCJA_t akcje[3];
        int16_t akcja;
        Skaner_WodospadWypelnijAkcje(akcje);
        akcja = UI_ZnajdzAkcjePaska(punkt, 220U, 48U, akcje, 3U);
        if (akcja == SKANER_WODOSPAD_AKCJA_WSTECZ)
            return 1U;
        if (akcja == SKANER_WODOSPAD_AKCJA_PRACA)
        {
            skaner_wodospad_ciagly = skaner_wodospad_ciagly ? 0U : 1U;
            Skaner_WodospadRysujWidok();
        }
        else if (akcja == SKANER_WODOSPAD_AKCJA_PELNY)
        {
            skaner_wodospad_pelny = 1U;
            Skaner_WodospadRysujWidok();
        }
    }
    return 0U;
}

void Spectrum(void)
{
    uint8_t wyjdz = 0U;
    uint8_t wlasny_bufor = 0U;

    while (TOUCH_IsPressed())
        ;
    Sleep(80);
    SetUpperLower();

    /* Spectrum ma dzialac takze bez poprzedzajacego nacisniecia "Skanuj".
     * Normalnie bufor tworzy SPECTR_FindFreq(), ale zachowujemy pelna
     * niezaleznosc tej funkcji rowniez dla przyszlych wywolan. */
    if (rfft_mags == 0)
    {
        rfft_mags = (float *)SDRH_malloc(sizeof(float) * NSAMPLES / 2U);
        if (rfft_mags == 0)
        {
            UI_RysujPoleStatusu(20, 85, 440, 100, JEZYK_Tekst(TEKST_BLAD),
                                JEZYK_Tekst(TEKST_SKANER_RF_ALOKACJA_BLAD),
                                UI_STYL_OSTRZEZENIE);
            Sleep(1200);
            Skaner_RysujEkranGlowny();
            return;
        }
        wlasny_bufor = 1U;
    }

    if (!Skaner_RF_PrzygotujWodospadNiezaleznie())
    {
        if (wlasny_bufor)
        {
            SDRH_free(rfft_mags);
            rfft_mags = 0;
        }
        Skaner_RysujEkranGlowny();
        return;
    }
    /* Zakres zatwierdzony przez użytkownika ma być już bezpiecznie zapisany
     * zanim rozpocznie się długi skan. */
    Skaner_ZapiszOstatniZakres();
    sScan_Meas = 1;
    selector = 4;
    breaker = 0;
    skaner_wodospad_pelny = 0U;
    skaner_wodospad_ciagly = 1U;
    skaner_wodospad_liczba_markerow = 0U;
    skaner_wynik_energia = 1U;
    skaner_wodospad_przebiegi_gotowe = 0U;
    skaner_wodospad_przebieg_biezacy = 0U;
    skaner_wodospad_postep_permille = 0U;
    skaner_wodospad_postep_f_hz = (uint32_t)lower;
    skaner_wodospad_w_trakcie = 0U;
    Skaner_WodospadResetujSrednia();
    UI_WodospadHistoriaResetuj();
    Skaner_WodospadRysujWidok();

    while (!wyjdz)
    {
        WEJSCIE_ZDARZENIE_t zdarzenie = WEJSCIA_PobierzZdarzenie();
        if (zdarzenie == WEJSCIE_ZDARZENIE_WSTECZ)
            break;

        if (skaner_wodospad_ciagly)
        {
            int res;
            skaner_wodospad_przebieg_biezacy = skaner_wodospad_przebiegi_gotowe + 1U;
            skaner_wodospad_postep_permille = 0U;
            skaner_wodospad_postep_f_hz = (uint32_t)lower;
            skaner_wodospad_w_trakcie = 1U;
            res = Skaner_RF_WykonajPrzebiegEnergii(2U);
            skaner_wodospad_w_trakcie = 0U;
            if (res > 0)
            {
                skaner_wodospad_przebiegi_gotowe++;
                skaner_wodospad_postep_permille = 1000U;
                Skaner_WodospadZbudujLinie();
                Skaner_WodospadDodajNaEkran();
            }
            else
            {
                /* SpectrumExec przerwał skan po dotknięciu i pozostawił punkt w pt. */
                while (TOUCH_IsPressed())
                    ;
                wyjdz = Skaner_WodospadObsluzDotyk(pt);
            }
        }
        else
        {
            LCDPoint punkt;
            if (TOUCH_Poll(&punkt))
            {
                while (TOUCH_IsPressed())
                    ;
                wyjdz = Skaner_WodospadObsluzDotyk(punkt);
            }
            else
                Sleep(20);
        }
    }

    sScan_Meas = 0;
    selector = 0;
    Skaner_RysujEkranGlowny();
    if (found)
        FX_ShowResult();

    if (wlasny_bufor)
    {
        SDRH_free(rfft_mags);
        rfft_mags = 0;
    }
}

int SPECTR_DokumentacjaWodospadRealny(uint32_t fmin_hz, uint32_t fmax_hz, uint8_t przebiegi)
{
    const unsigned long stare_lower = lower;
    const unsigned long stare_upper = upper;
    const BANDSPAN stary_span = span;
    const uint8_t stary_dokladny = skaner_zakres_dokladny;
    const int stary_area = AreaSelected;
    const uint32_t stare_f1 = f1;
    uint8_t wlasny_bufor = 0U;
    uint8_t n;
    int ok = 1;

    if (fmax_hz <= fmin_hz || przebiegi == 0U)
        return 0;

    if (rfft_mags == 0)
    {
        rfft_mags = (float *)SDRH_malloc(sizeof(float) * NSAMPLES / 2U);
        if (rfft_mags == 0)
            return 0;
        wlasny_bufor = 1U;
    }

    Skaner_UstawZakresDokladny(fmin_hz, fmax_hz);
    GEN_Init();
    SetColours();
    GEN_SetF0Freq(0U);

    skaner_wodospad_pelny = 0U;
    skaner_wodospad_ciagly = 1U;
    skaner_wodospad_liczba_markerow = 0U;
    skaner_wynik_energia = 1U;
    skaner_wodospad_przebiegi_gotowe = 0U;
    skaner_wodospad_przebieg_biezacy = 0U;
    skaner_wodospad_w_trakcie = 0U;
    Skaner_WodospadResetujSrednia();
    UI_WodospadHistoriaResetuj();

    for (n = 0U; n < przebiegi; ++n)
    {
        skaner_wodospad_przebieg_biezacy = (uint32_t)n + 1U;
        if (Skaner_RF_WykonajPrzebiegEnergii(0U) <= 0)
        {
            ok = 0;
            break;
        }
        skaner_wodospad_przebiegi_gotowe++;
        Skaner_WodospadZbudujLinie();
    }

    /* Do zrzutu pokazujemy stan „pracuje”, czyli przycisk Pauza. */
    skaner_wodospad_ciagly = 1U;
    Skaner_WodospadRysujWidok();

    GEN_SetF0Freq(0U);
    if (wlasny_bufor)
    {
        SDRH_free(rfft_mags);
        rfft_mags = 0;
    }

    lower = stare_lower;
    upper = stare_upper;
    span = stary_span;
    skaner_zakres_dokladny = stary_dokladny;
    AreaSelected = stary_area;
    f1 = stare_f1;
    return ok;
}

void SPECTR_DokumentacjaWodospadPelny(uint32_t fmin_hz, uint32_t fmax_hz)
{
    const unsigned long stare_lower = lower;
    const unsigned long stare_upper = upper;
    const uint8_t stary_pelny = skaner_wodospad_pelny;

    if (fmax_hz <= fmin_hz)
        return;

    lower = fmin_hz;
    upper = fmax_hz;
    skaner_wodospad_pelny = 1U;
    Skaner_WodospadRysujWidok();
    skaner_wodospad_pelny = stary_pelny;
    lower = stare_lower;
    upper = stare_upper;
}

static TEXTBOX_CTX_t *skaner_ctx_aktywny;

static TEXTBOX_t tb_Scan[] = {
    (TEXTBOX_t){.x0 = 4, .y0 = 88, .tekst_id = TEXTBOX_TEKST(TEKST_SKANER_RF_PASMA_KF), .font = FONT_FRAN,
                .width = 138, .height = 34, .center = 1, .border = 1, .fgcolor = LCD_WHITE, .bgcolor = LCD_BLACK,
                .cb = (void (*)(void))Skaner_WybierzPasmo, .cbparam = 1, .next = (void *)&tb_Scan[1]},
    (TEXTBOX_t){.x0 = 4, .y0 = 128, .tekst_id = TEXTBOX_TEKST(TEKST_SKANER_RF_WYBRANY_ZAKRES), .font = FONT_FRAN,
                .width = 138, .height = 34, .center = 1, .border = 1, .fgcolor = LCD_WHITE, .bgcolor = LCD_BLACK,
                .cb = (void (*)(void))OneBand, .cbparam = 1, .next = (void *)&tb_Scan[2]},
    (TEXTBOX_t){.x0 = 0, .y0 = UI_DOLNY_PASEK_Y, .tekst_id = TEXTBOX_TEKST(TEKST_WSTECZ), .rola = TEXTBOX_ROLA_WSTECZ,
                .font = FONT_FRAN, .width = UI_DOLNY_PRZYCISK_SZEROKOSC, .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1, .fgcolor = LCD_WHITE, .bgcolor = LCD_RED,
                .cb = (void (*)(void))SCExit, .cbparam = 1, .next = (void *)&tb_Scan[3]},
    (TEXTBOX_t){.x0 = 82, .y0 = UI_DOLNY_PASEK_Y, .tekst_id = TEXTBOX_TEKST(TEKST_SKANER_RF_AUTO), .font = FONT_FRAN,
                .width = 128, .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1, .fgcolor = LCD_WHITE, .bgcolor = LCD_BLACK,
                .cb = (void (*)(void))Skaner_UstawOdDo, .cbparam = 1, .next = (void *)&tb_Scan[4]},
    (TEXTBOX_t){.x0 = 216, .y0 = UI_DOLNY_PASEK_Y, .tekst_id = TEXTBOX_TEKST(TEKST_ZAKRES), .font = FONT_FRAN,
                .width = 128, .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1, .fgcolor = LCD_WHITE, .bgcolor = LCD_BLACK,
                .cb = (void (*)(void))freq, .cbparam = 1, .next = (void *)&tb_Scan[5]},
    (TEXTBOX_t){.x0 = 350, .y0 = UI_DOLNY_PASEK_Y, .tekst_id = TEXTBOX_TEKST(TEKST_SKANER_RF_WIDMO), .rola = TEXTBOX_ROLA_START_STOP,
                .font = FONT_FRAN, .width = 128, .height = UI_DOLNY_PRZYCISK_WYSOKOSC, .center = 1, .border = 1, .fgcolor = LCD_BLACK, .bgcolor = LCD_YELLOW,
                .cb = (void (*)(void))Spectrum, .cbparam = 1, .next = 0},
};

static void Skaner_RysujEkranGlowny(void)
{
    char zakres[64];
    const uint32_t tlo = UI_KolorTlaEkranu();

    UI_WyczyscEkran();
    UI_RysujNaglowek(JEZYK_Tekst(TEKST_SKANER_RF_TYTUL));
    UI_RysujPanel(4, 36, 472, 46, JEZYK_Tekst(TEKST_INFORMACJA), UI_STYL_NORMALNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NORMALNY), UI_KolorTlaPola(),
               10, 56, JEZYK_Tekst(TEKST_SKANER_RF_OPIS_1));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               10, 70, JEZYK_Tekst(TEKST_SKANER_RF_OPIS_2));

    UI_FormatujCzestotliwoscMHz(lower, zakres, sizeof(zakres));
    {
        char gora_txt[32];
        UI_FormatujCzestotliwoscMHz(upper, gora_txt, sizeof(gora_txt));
        snprintf(zakres, sizeof(zakres), "%lu.%03lu - %lu.%03lu MHz",
                 (unsigned long)(lower / 1000000U), (unsigned long)((lower % 1000000U) / 1000U),
                 (unsigned long)(upper / 1000000U), (unsigned long)((upper % 1000000U) / 1000U));
        if (JEZYK_CzySeparatorDziesietnyPrzecinek())
        {
            char *c = zakres;
            while (*c)
            {
                if (*c == '.')
                    *c = ',';
                c++;
            }
        }
        (void)gora_txt;
    }
    UI_RysujPoleWartosci(150, 88, 326, 54, JEZYK_Tekst(TEKST_ZAKRES), zakres);
    UI_RysujPanel(150, 148, 326, 70, 0, UI_STYL_NIEAKTYWNY);
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               158, 158, JEZYK_Tekst(TEKST_SKANER_RF_BRAK));
    FONT_Write(FONT_FRAN, UI_KolorTekstu(UI_STYL_NIEAKTYWNY), UI_KolorTlaPola(),
               158, 184, JEZYK_Tekst(TEKST_SKANER_RF_POZIOM_INFO));

    if (skaner_ctx_aktywny != 0)
    {
        /*
         * Pole z aktualnym zakresem jest tylko informacją. Dolny przycisk
         * służy do jego zmiany, więc nazywamy czynność wprost zamiast
         * powtarzać samo słowo „Zakres”.
         */
        TEXTBOX_SetText(skaner_ctx_aktywny, 0U,
                        JEZYK_Wybierz("Pasmo", "Band", "Band", "Диапазон"));
        TEXTBOX_SetText(skaner_ctx_aktywny, 1U,
                        JEZYK_Wybierz("Skanuj", "Scan", "Scannen", "Сканировать"));
        TEXTBOX_SetText(skaner_ctx_aktywny, 3U,
                        JEZYK_Wybierz("Od-Do", "From-To", "Von-Bis", "От-До"));
        TEXTBOX_SetText(skaner_ctx_aktywny, 4U,
                        JEZYK_Wybierz("Zakres", "Span", "Spanne", "Полоса"));
        TEXTBOX_DrawContext(skaner_ctx_aktywny);
    }

    /* Dolny rzad korzysta z jednej geometrii y=220, h=45. Nie czyscimy
     * juz linii nad przyciskami po ich narysowaniu. */
    (void)tlo;
}


int SPECTR_PomiarTlaSeriaZPostepem(uint32_t fmin_hz, uint32_t fmax_hz, uint16_t liczba_punktow,
                                   float *tlo, float *szczyt, SPECTR_POSTEP_CB_t postep)
{
    uint16_t i;
    uint8_t wlasny_bufor = 0U;
    int wykonane = 0;

    if (tlo == 0 || szczyt == 0 || liczba_punktow < 2U || fmax_hz <= fmin_hz)
        return 0;

    if (rfft_mags == 0)
    {
        rfft_mags = (float *)SDRH_malloc(sizeof(float) * NSAMPLES / 2U);
        if (rfft_mags == 0)
            return 0;
        wlasny_bufor = 1U;
    }

    GEN_Init();
    GEN_SetF0Freq(0U);
    Average = 0.0f;
    Maxmag = 0.0f;

    for (i = 0U; i < liczba_punktow; i++)
    {
        uint64_t zakres = (uint64_t)fmax_hz - (uint64_t)fmin_hz;
        uint32_t f = fmin_hz + (uint32_t)((zakres * (uint64_t)i) / (uint64_t)(liczba_punktow - 1U));

        if (TOUCH_IsPressed())
        {
            LCDPoint p;
            if (TOUCH_Poll(&p))
            {
                wykonane = -1;
                break;
            }
        }

        (void)CalcMaxBin(f);
        tlo[i] = (isfinite(skaner_ostatnie_tlo_fft) && skaner_ostatnie_tlo_fft >= 0.0f) ?
                     skaner_ostatnie_tlo_fft : 0.0f;
        szczyt[i] = (isfinite(skaner_ostatni_szczyt_fft) && skaner_ostatni_szczyt_fft >= 0.0f) ?
                        skaner_ostatni_szczyt_fft : 0.0f;
        wykonane++;
        if (postep != 0)
            postep((uint16_t)wykonane, liczba_punktow, f);
    }

    GEN_SetF0Freq(0U);
    if (wlasny_bufor)
    {
        SDRH_free(rfft_mags);
        rfft_mags = 0;
    }
    return wykonane;
}

int SPECTR_PomiarTlaSeria(uint32_t fmin_hz, uint32_t fmax_hz, uint16_t liczba_punktow,
                          float *tlo, float *szczyt)
{
    return SPECTR_PomiarTlaSeriaZPostepem(fmin_hz, fmax_hz, liczba_punktow, tlo, szczyt, 0);
}

void SPECTR_FindFreq(void)
{
    TEXTBOX_CTX_t fctx;

    AreaSelected = 1;
    rfft_mags = (float *)SDRH_malloc(sizeof(float) * NSAMPLES / 2U);
    if (rfft_mags == 0)
    {
        UI_WyczyscEkran();
        UI_RysujNaglowek(JEZYK_Tekst(TEKST_SKANER_RF_TYTUL));
        UI_RysujPoleStatusu(20, 85, 440, 100, JEZYK_Tekst(TEKST_BLAD),
                            JEZYK_Tekst(TEKST_SKANER_RF_ALOKACJA_BLAD), UI_STYL_OSTRZEZENIE);
        Sleep(1800);
        return;
    }

    /* RFSCAN8: zamiast wracać przy każdym wejściu do historycznych 3,5-3,9 MHz
     * odtwarzamy ostatni zakres użytkownika z config.bin. Stare konfiguracje
     * dostają dokładnie dawny zakres jako wartość migracyjną. */
    lower = CFG_GetParam(CFG_PARAM_SKANER_RF_FMIN_HZ);
    upper = CFG_GetParam(CFG_PARAM_SKANER_RF_FMAX_HZ);
    span = Skaner_DobierzZakresDlaPasma((uint32_t)(upper - lower));
    skaner_zakres_dokladny = 1U;
    selector = 0;
    f1 = (CFG_GetParam(CFG_PARAM_PAN_CENTER_F) == 0U) ?
         (uint32_t)(lower / 1000U) :
         (uint32_t)((lower + (upper - lower) / 2U) / 1000U);

    GEN_Init();
    SetColours();
    SetUpperLower();

    TEXTBOX_InitContext(&fctx);
    TEXTBOX_Append(&fctx, (TEXTBOX_t *)tb_Scan);
    skaner_ctx_aktywny = &fctx;

Repaint:
    GEN_SetF0Freq(0); /* skaner jest odbiornikiem; F0 ma pozostać wyłączone */
    sScan_Meas = 0;
    sRepaint = 0;
    sExit = 0;
    FreqFound = 0;
    found = 0;
    Maxmag = 0.0f;
    Average = 0.0f;
    skaner_ostatni_stosunek_db = -INFINITY;
    SetUpperLower();
    Skaner_RysujEkranGlowny();

    while (TOUCH_IsPressed())
        ;

    for (;;)
    {
        uint32_t wynik = TEXTBOX_HitTest(&fctx);
        if (wynik != 0U)
            Sleep(30);

        if (sExit == 1)
        {
            while (TOUCH_IsPressed())
                ;
            GEN_SetF0Freq(0);
            Skaner_ZapiszOstatniZakres();
            skaner_ctx_aktywny = 0;
            SDRH_free(rfft_mags);
            rfft_mags = 0;
            return;
        }

        if (sRepaint == 1)
        {
            while (TOUCH_IsPressed())
                ;
            goto Repaint;
        }

        Sleep(20);
    }
}
