#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build_timestamp.h"
#include "config.h"
#include "dsp.h"
#include "gen.h"
#include "oslfile.h"
#include "shell.h"

/*
 * Warstwa zgodności z prostym protokołem konsoli NanoVNA.
 *
 * Założenia EU1KY-PL 2026:
 * - udostępniamy wyłącznie rzeczywiście mierzone S11,
 * - nie tworzymy fikcyjnego S21,
 * - zakres pochodzi z bieżącej konfiguracji i planu harmonicznych,
 * - siatkę częstotliwości liczymy całkowitoliczbowo, bez utraty pojedynczych Hz,
 * - kompensacja kabla (Port Extension) pozostaje funkcją lokalnego interfejsu;
 *   PC dostaje S11 w płaszczyźnie kalibracji OSL i może wykonać własne
 *   przesunięcie płaszczyzny odniesienia.
 *
 * Celowo nie deklarujemy komendy "scan". Program PC wykrywa dostępne komendy
 * przez "help"; dopóki nie zaimplementujemy dokładnie semantyki nowego scan,
 * bezpieczniej pozostać przy historycznych poleceniach sweep/frequencies/data.
 */

#define NANOVNA_MAKS_PUNKTOW 101
#define NANOVNA_MIN_PUNKTOW 2

#define frequency0 current_props._frequency0
#define frequency1 current_props._frequency1
#define sweep_points current_props._sweep_points
#define frequencies current_props._frequencies

enum
{
    ST_START,
    ST_STOP,
    ST_CENTER,
    ST_SPAN,
    ST_CW
};

typedef struct
{
    uint32_t _frequency0;
    uint32_t _frequency1;
    int16_t _sweep_points;
    uint32_t _frequencies[NANOVNA_MAKS_PUNKTOW];
} properties_t;

properties_t current_props __attribute__((section(".user_sdram")));
properties_t *active_props = &current_props;

static bool _is_initialized = false;
static float measured[2][NANOVNA_MAKS_PUNKTOW][2] __attribute__((section(".user_sdram")));

static uint32_t NANOVNA_ZakresMinHz(void)
{
    uint32_t fmin = CFG_GetParam(CFG_PARAM_BAND_FMIN);

    if (fmin < BAND_FMIN)
        fmin = BAND_FMIN;
    return fmin;
}

static uint32_t NANOVNA_ZakresMaxHz(void)
{
    uint32_t fmax = CFG_GetParam(CFG_PARAM_BAND_FMAX);
    const uint32_t generator_max = GEN_MaksCzestotliwoscEfektywna();

    if (fmax > MAX_BAND_FREQ)
        fmax = MAX_BAND_FREQ;
    if (fmax > generator_max)
        fmax = generator_max;
    if (fmax < NANOVNA_ZakresMinHz())
        fmax = NANOVNA_ZakresMinHz();
    return fmax;
}

static uint32_t NANOVNA_OgraniczCzestotliwosc(uint32_t freq)
{
    const uint32_t fmin = NANOVNA_ZakresMinHz();
    const uint32_t fmax = NANOVNA_ZakresMaxHz();

    if (freq < fmin)
        return fmin;
    if (freq > fmax)
        return fmax;
    return freq;
}

static void NANOVNA_UstawSiatke(uint32_t start, uint32_t stop, int16_t points)
{
    int i;
    uint64_t span;

    if (points < NANOVNA_MIN_PUNKTOW)
        points = NANOVNA_MIN_PUNKTOW;
    if (points > NANOVNA_MAKS_PUNKTOW)
        points = NANOVNA_MAKS_PUNKTOW;

    start = NANOVNA_OgraniczCzestotliwosc(start);
    stop = NANOVNA_OgraniczCzestotliwosc(stop);
    if (stop < start)
    {
        const uint32_t tmp = start;
        start = stop;
        stop = tmp;
    }

    sweep_points = points;
    frequency0 = start;
    frequency1 = stop;
    span = (uint64_t)stop - (uint64_t)start;

    /*
     * Float traci rozdzielczość pojedynczych Hz już przy dziesiątkach MHz.
     * Arytmetyka 64-bitowa daje dokładnie tę samą siatkę przy każdym buildzie.
     */
    for (i = 0; i < points; ++i)
    {
        const uint64_t offset =
            (span * (uint64_t)i + (uint64_t)(points - 1) / 2ULL) /
            (uint64_t)(points - 1);
        frequencies[i] = start + (uint32_t)offset;
    }

    for (; i < NANOVNA_MAKS_PUNKTOW; ++i)
        frequencies[i] = 0U;
}

static void NANOVNA_UstawParametrZakresu(int typ, uint32_t freq)
{
    const uint32_t zakres_min = NANOVNA_ZakresMinHz();
    const uint32_t zakres_max = NANOVNA_ZakresMaxHz();
    uint32_t start = frequency0;
    uint32_t stop = frequency1;

    if (stop < start)
    {
        const uint32_t tmp = start;
        start = stop;
        stop = tmp;
    }

    switch (typ)
    {
    case ST_START:
        start = NANOVNA_OgraniczCzestotliwosc(freq);
        if (stop < start)
            stop = start;
        break;

    case ST_STOP:
        stop = NANOVNA_OgraniczCzestotliwosc(freq);
        if (start > stop)
            start = stop;
        break;

    case ST_CENTER:
    {
        const uint32_t span = stop - start;
        const uint32_t lewa = span / 2U;
        const uint32_t prawa = span - lewa;
        uint32_t center = NANOVNA_OgraniczCzestotliwosc(freq);

        /* Przy bardzo szerokim span najpierw ograniczamy środek do obszaru,
         * w którym cały zakres nadal mieści się w możliwościach przyrządu. */
        if (lewa > zakres_max - zakres_min || prawa > zakres_max - zakres_min)
        {
            start = zakres_min;
            stop = zakres_max;
            break;
        }
        if (center < zakres_min + lewa)
            center = zakres_min + lewa;
        if (center > zakres_max - prawa)
            center = zakres_max - prawa;
        start = center - lewa;
        stop = center + prawa;
        break;
    }

    case ST_SPAN:
    {
        const uint32_t maks_span = zakres_max - zakres_min;
        uint32_t span = freq;
        uint32_t center = start + (stop - start) / 2U;
        uint32_t lewa;
        uint32_t prawa;

        if (span > maks_span)
            span = maks_span;
        lewa = span / 2U;
        prawa = span - lewa;

        if (center < zakres_min + lewa)
            center = zakres_min + lewa;
        if (center > zakres_max - prawa)
            center = zakres_max - prawa;
        start = center - lewa;
        stop = center + prawa;
        break;
    }

    case ST_CW:
        start = NANOVNA_OgraniczCzestotliwosc(freq);
        stop = start;
        break;

    default:
        return;
    }

    NANOVNA_UstawSiatke(start, stop, sweep_points);
}

static int NANOVNA_ParsujSweep(int argc, char *const argv[])
{
    if (argc == 0)
    {
        printf("%lu %lu %d\r\n",
               (unsigned long)frequency0,
               (unsigned long)frequency1,
               (int)sweep_points);
        return 0;
    }

    if (argc == 2)
    {
        const uint32_t value = (uint32_t)strtoul(argv[1], NULL, 10);

        if (strcmp(argv[0], "start") == 0)
        {
            NANOVNA_UstawParametrZakresu(ST_START, value);
            return 1;
        }
        if (strcmp(argv[0], "stop") == 0)
        {
            NANOVNA_UstawParametrZakresu(ST_STOP, value);
            return 1;
        }
        if (strcmp(argv[0], "center") == 0)
        {
            NANOVNA_UstawParametrZakresu(ST_CENTER, value);
            return 1;
        }
        if (strcmp(argv[0], "span") == 0)
        {
            NANOVNA_UstawParametrZakresu(ST_SPAN, value);
            return 1;
        }
        if (strcmp(argv[0], "cw") == 0)
        {
            NANOVNA_UstawParametrZakresu(ST_CW, value);
            return 1;
        }
    }

    if (argc >= 1 && argc <= 3)
    {
        const uint32_t start = (uint32_t)strtoul(argv[0], NULL, 10);
        const uint32_t stop = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : frequency1;
        int16_t points = sweep_points;

        if (start == 0U)
            goto usage;
        if (argc == 3)
        {
            const long requested = strtol(argv[2], NULL, 10);
            if (requested < NANOVNA_MIN_PUNKTOW || requested > NANOVNA_MAKS_PUNKTOW)
                goto usage;
            points = (int16_t)requested;
        }

        NANOVNA_UstawSiatke(start, stop, points);
        return 1;
    }

usage:
    printf("usage: sweep {start(Hz)} [stop(Hz)] [points 2..101]\r\n");
    printf("\tsweep {start|stop|center|span|cw} {freq(Hz)}\r\n");
    return 0;
}

static void NANOVNA_WypiszCzestotliwosci(void)
{
    int i;

    for (i = 0; i < sweep_points; ++i)
    {
        if (frequencies[i] != 0U)
            printf("%lu\r\n", (unsigned long)frequencies[i]);
    }
}

static void NANOVNA_WypiszDane(int argc, char *const argv[])
{
    int i;
    int wybor = 0;

    if (argc == 1)
        wybor = atoi(argv[0]);

    if (wybor == 0 || wybor == 1)
    {
        for (i = 0; i < sweep_points; ++i)
        {
            if (frequencies[i] != 0U)
                printf("%f %f\r\n", measured[wybor][i][0], measured[wybor][i][1]);
        }
        return;
    }

    /* Historyczne tablice kalibracyjne NanoVNA nie istnieją w EU1KY.
     * Zwracamy zera dla zachowania zgodności parsera, bez udawania pomiaru. */
    if (wybor >= 2 && wybor < 7)
    {
        for (i = 0; i < sweep_points; ++i)
        {
            if (frequencies[i] != 0U)
                printf("0.000000 0.000000\r\n");
        }
        return;
    }

    printf("usage: data [array]\r\n");
}

static void NANOVNA_Inicjalizuj(void)
{
    if (_is_initialized)
        return;

    memset(measured, 0, sizeof(measured));
    memset(&current_props, 0, sizeof(current_props));

    current_props._frequency0 = NANOVNA_ZakresMinHz();
    current_props._frequency1 = current_props._frequency0 + 10000000U;
    if (current_props._frequency1 > NANOVNA_ZakresMaxHz())
        current_props._frequency1 = NANOVNA_ZakresMaxHz();
    current_props._sweep_points = NANOVNA_MAKS_PUNKTOW;

    NANOVNA_UstawSiatke(current_props._frequency0,
                        current_props._frequency1,
                        current_props._sweep_points);
    _is_initialized = true;
}

static void NANOVNA_WykonajPomiar(void)
{
    uint32_t i;
    int nscans = (int)CFG_GetParam(CFG_PARAM_PAN_NSCANS);
    const uint32_t port_extension_ps = CFG_GetParam(CFG_PARAM_PORT_EXT_PS);

    if (nscans < 1)
        nscans = 1;
    if (nscans > 20)
        nscans = 20;

    /* PC powinien otrzymywać wynik w płaszczyźnie OSL. Lokalna kompensacja
     * przewodu nie jest zapisywana na SD i nie może po cichu zmieniać danych. */
    if (port_extension_ps != 0U)
        CFG_SetParam(CFG_PARAM_PORT_EXT_PS, 0U);

    for (i = 0U; i < (uint32_t)sweep_points; ++i)
    {
        float complex gamma;

        if (frequencies[i] == 0U)
            break;

        DSP_Measure(frequencies[i], 1, 1, nscans);
        if (!DSP_CzyOstatniPomiarPoprawny() ||
            !isfinite(crealf(DSP_MeasuredZ())) ||
            !isfinite(cimagf(DSP_MeasuredZ())))
        {
            measured[0][i][0] = NAN;
            measured[0][i][1] = NAN;
            measured[1][i][0] = NAN;
            measured[1][i][1] = NAN;
            continue;
        }

        gamma = OSL_GFromZ(DSP_MeasuredZ(), (float)CFG_GetParam(CFG_PARAM_R0));
        measured[0][i][0] = crealf(gamma);
        measured[0][i][1] = cimagf(gamma);

        /* EU1KY w tej ścieżce mierzy S11. NAN jest uczciwsze niż fikcyjne S21=0. */
        measured[1][i][0] = NAN;
        measured[1][i][1] = NAN;
    }

    if (port_extension_ps != 0U)
        CFG_SetParam(CFG_PARAM_PORT_EXT_PS, port_extension_ps);
    GEN_SetMeasurementFreq(0);
}

/* =====================================================================================
 * Polecenia konsoli
 */

static bool _nanovna_cmd_exit(uint32_t argc, char *const argv[])
{
    (void)argc;
    (void)argv;
    printf("ChibiOS/RT Shell\r\n");
    return false;
}
SHELL_CMD(exit, "exits and then restarts console mode", _nanovna_cmd_exit);

static bool _nanovna_cmd_info(uint32_t argc, char *const argv[])
{
    (void)argc;
    (void)argv;
    printf("Kernel:       none\r\n");
    printf("Compiler:     arm-none-eabi-gcc 12.3.1\r\n");
    printf("Architecture: ARMv7-M\r\n");
    printf("Core Variant: Cortex-M7\r\n");
    printf("Platform:     STM32F746G DISCO\r\n");
    printf("Board:        NanoVNA compatible / EU1KY-PL 2026\r\n");
    printf("Firmware:     %s\r\n", PROJEKT_WERSJA);
    printf("S-parameters: S11 only\r\n");
    printf("Build time:   %s - %s\r\n", __DATE__, __TIME__);
    return false;
}
SHELL_CMD(info, "prints firmware info on console", _nanovna_cmd_info);

static bool _nanovna_cmd_freq(uint32_t argc, char *const argv[])
{
    uint32_t freq = 0U;

    if (argc != 2U)
    {
        printf("Usage: freq {frequency (Hz)}\r\n");
        return false;
    }

    shell_str2num(argv[1], &freq);
    if (freq < NANOVNA_ZakresMinHz() || freq > NANOVNA_ZakresMaxHz() ||
        !GEN_CzyCzestotliwoscObslugiwana(freq))
    {
        GEN_SetMeasurementFreq(0);
        printf("error: frequency out of configured range\r\n");
        return false;
    }

    GEN_SetMeasurementFreq(freq);
    return false;
}
SHELL_CMD(freq, "set the CW frequency", _nanovna_cmd_freq);

static bool _nanovna_cmd_data(uint32_t argc, char *const argv[])
{
    NANOVNA_Inicjalizuj();
    NANOVNA_WypiszDane((int)argc - 1, &argv[1]);
    return false;
}
SHELL_CMD(data, "data [array]", _nanovna_cmd_data);

static bool _nanovna_cmd_sweep(uint32_t argc, char *const argv[])
{
    int wykonaj;

    NANOVNA_Inicjalizuj();
    wykonaj = NANOVNA_ParsujSweep((int)argc - 1, &argv[1]);
    if (wykonaj)
        NANOVNA_WykonajPomiar();
    return false;
}
SHELL_CMD(sweep, "sweep", _nanovna_cmd_sweep);

static bool _nanovna_cmd_frequencies(uint32_t argc, char *const argv[])
{
    (void)argc;
    (void)argv;
    NANOVNA_Inicjalizuj();
    NANOVNA_WypiszCzestotliwosci();
    return false;
}
SHELL_CMD(frequencies, "list frequencies", _nanovna_cmd_frequencies);

static bool _nanovna_cmd_version(uint32_t argc, char *const argv[])
{
    (void)argc;
    (void)argv;

    /* Zachowujemy historyczny format wersji oczekiwany przez klienty NanoVNA.
     * Dostępne możliwości należy wykrywać przez help, a nie sam numer wersji. */
    printf("0.1.0-1-1-00000000\r\n");
    return false;
}
SHELL_CMD(version, "", _nanovna_cmd_version);
