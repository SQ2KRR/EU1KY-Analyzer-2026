/*
 *   (c) Yury Kuchura
 *   kuchura@gmail.com
 *
 *   Modified by KD8CEC and EU1KY-PL 2026.
 *   This code can be used on terms of WTFPL Version 2 (http://www.wtfpl.net/).
 */
#include "si5351.h"
#include "adf4350.h"
#include "adf4351.h"
#include "si5338a.h"
#include "gen.h"
#include "dsp.h"
#include "config.h"
#include "gpio_control.h"
#include "crash.h"
#include <limits.h>

static uint32_t lastSetFreq = 14000000ul;
extern void Sleep(uint32_t);

typedef struct
{
    void (*Init)(void);
    void (*Off)(void);
    void (*OffMeasurement)(void);
    void (*OffTX)(void);
    void (*SetF0)(uint32_t);
    void (*SetLO)(uint32_t);
    void (*SetTX)(uint32_t);
    uint32_t min_hz;
    uint32_t max_hz;
    uint8_t ma_harmoniczne;
    uint8_t ma_wyjscie_tx;
    const char *nazwa;
} GenDrv_t;

static GenDrv_t gen = {0};

static void GEN_WybierzSterownik(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);

    gen = (GenDrv_t){0};

    if (typ == CFG_SYNTH_SI5351)
    {
        gen.Init = si5351_Init;
        gen.Off = si5351_Off;
        gen.OffMeasurement = si5351_OffMeasurement;
        gen.OffTX = si5351_OffF2;
        gen.SetF0 = si5351_SetF0;
        gen.SetLO = si5351_SetLO;
        gen.SetTX = si5351_SetF2;
        gen.min_hz = CFG_GetParam(CFG_PARAM_BAND_FMIN);
        gen.max_hz = UINT32_MAX;
        gen.ma_harmoniczne = 1U;
        gen.ma_wyjscie_tx = 1U;
        gen.nazwa = "Si5351";
    }
    else if (typ == CFG_SYNTH_ADF4350)
    {
        gen.Init = adf4350_Init;
        gen.Off = adf4350_Off;
        gen.OffMeasurement = adf4350_Off;
        gen.SetF0 = adf4350_SetF0;
        gen.SetLO = adf4350_SetLO;
        gen.min_hz = adf4350_MinFreq();
        gen.max_hz = adf4350_MaxFreq();
        gen.ma_harmoniczne = 0U;
        gen.ma_wyjscie_tx = 0U;
        gen.nazwa = "2x ADF4350";
    }
    else if (typ == CFG_SYNTH_ADF4351)
    {
        gen.Init = ADF4351_Init;
        gen.Off = ADF4351_Off;
        gen.OffMeasurement = ADF4351_Off;
        gen.SetF0 = ADF4351_SetF0;
        gen.SetLO = ADF4351_SetLO;
        gen.min_hz = ADF4351_MinFreq();
        gen.max_hz = ADF4351_MaxFreq();
        gen.ma_harmoniczne = 0U;
        gen.ma_wyjscie_tx = 0U;
        gen.nazwa = "2x ADF4351";
    }
    else if (typ == CFG_SYNTH_SI5338A)
    {
        gen.Init = SI5338A_Init;
        gen.Off = SI5338A_Off;
        gen.OffMeasurement = SI5338A_Off;
        gen.SetF0 = SI5338A_SetF0;
        gen.SetLO = SI5338A_SetLO;
        gen.min_hz = SI5338A_MinFreq();
        gen.max_hz = SI5338A_MaxFreq();
        gen.ma_harmoniczne = 0U;
        gen.ma_wyjscie_tx = 0U;
        gen.nazwa = "Si5338A";
    }
    else
    {
        CRASH("Unsupported frequency synthesizer type");
    }
}

void GEN_Init(void)
{
    GEN_WybierzSterownik();
    gen.Init();
    SET_LED_STANU_RF(0U);
}

uint8_t GEN_MaksHarmoniczna(void)
{
    uint32_t wartosc;

    if (CFG_GetParam(CFG_PARAM_SYNTH_TYPE) != CFG_SYNTH_SI5351)
        return 1U;

    wartosc = CFG_GetParam(CFG_PARAM_HARMONICZNA_MAX);
    if (wartosc == 1U || wartosc == 3U || wartosc == 5U || wartosc == 7U)
        return (uint8_t)wartosc;
    return 3U;
}

/*
 * Klasa sposobu programowania MultiSynth Si5351. Te granice sa istotne
 * metrologicznie, bo po obu stronach zmienia sie sposob wyznaczania PLL/MS.
 * Nie wolno interpolowac wspolczynnikow kalibracji tak, jakby tor byl ten sam.
 */
static uint8_t GEN_KlasaMultiSynthSi5351(uint32_t hz)
{
    if (hz >= SI5351_MULTISYNTH_DIVBY4_FREQ)
        return 2U; /* dzielnik 4 */
    if (hz >= 112500000U)
        return 1U; /* dzielnik 6 */
    return 0U;     /* klasyczny fractional MultiSynth */
}

uint32_t GEN_RezimPomiarowy(uint32_t czestotliwosc_hz)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);

    if (czestotliwosc_hz == 0U)
        return 0U;

    if (typ != CFG_SYNTH_SI5351)
        return 0x80000000U | (typ & 0xFFU);

    {
        const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(czestotliwosc_hz);
        const uint32_t if_hz = DSP_GetIF();
        uint32_t f0_hz;
        uint32_t lo_hz;
        uint8_t klasa_f0;
        uint8_t klasa_lo;

        if (harmoniczna == 0U || czestotliwosc_hz <= if_hz)
            return 0U;

        f0_hz = czestotliwosc_hz / (uint32_t)harmoniczna;
        lo_hz = (czestotliwosc_hz - if_hz) / (uint32_t)harmoniczna;
        klasa_f0 = GEN_KlasaMultiSynthSi5351(f0_hz);
        klasa_lo = GEN_KlasaMultiSynthSi5351(lo_hz);

        return ((uint32_t)harmoniczna << 16) |
               ((uint32_t)klasa_f0 << 8) |
               (uint32_t)klasa_lo;
    }
}

int GEN_CzyTenSamRezimPomiarowy(uint32_t a_hz, uint32_t b_hz)
{
    const uint32_t a = GEN_RezimPomiarowy(a_hz);
    const uint32_t b = GEN_RezimPomiarowy(b_hz);
    return a != 0U && a == b;
}

uint8_t GEN_WyznaczHarmoniczna(uint32_t czestotliwosc_hz)
{
    uint64_t maksimum;
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);

    if (czestotliwosc_hz == 0U)
        return 0U;

    if (typ == CFG_SYNTH_ADF4350)
        return (czestotliwosc_hz >= adf4350_MinFreq() && czestotliwosc_hz <= adf4350_MaxFreq()) ? 1U : 0U;
    if (typ == CFG_SYNTH_ADF4351)
        return (czestotliwosc_hz >= ADF4351_MinFreq() && czestotliwosc_hz <= ADF4351_MaxFreq()) ? 1U : 0U;
    if (typ == CFG_SYNTH_SI5338A)
        return SI5338A_CanSet(czestotliwosc_hz) ? 1U : 0U;
    if (typ != CFG_SYNTH_SI5351)
        return 0U;

    maksimum = (uint64_t)CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    if (maksimum == 0ULL)
        return 0U;
    if ((uint64_t)czestotliwosc_hz <= maksimum)
        return 1U;
    if (GEN_MaksHarmoniczna() >= 3U && (uint64_t)czestotliwosc_hz <= 3ULL * maksimum)
        return 3U;
    if (GEN_MaksHarmoniczna() >= 5U && (uint64_t)czestotliwosc_hz <= 5ULL * maksimum)
        return 5U;
    if (GEN_MaksHarmoniczna() >= 7U && (uint64_t)czestotliwosc_hz <= 7ULL * maksimum)
        return 7U;
    return 0U;
}

uint32_t GEN_CzestotliwoscBazowa(uint32_t czestotliwosc_hz)
{
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(czestotliwosc_hz);
    return harmoniczna == 0U ? 0U : czestotliwosc_hz / (uint32_t)harmoniczna;
}

uint32_t GEN_MinCzestotliwoscEfektywna(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    if (typ == CFG_SYNTH_ADF4350)
        return adf4350_MinFreq();
    if (typ == CFG_SYNTH_ADF4351)
        return ADF4351_MinFreq();
    if (typ == CFG_SYNTH_SI5338A)
        return SI5338A_MinFreq();
    return CFG_GetParam(CFG_PARAM_BAND_FMIN);
}

uint32_t GEN_MaksCzestotliwoscEfektywna(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    uint64_t maksimum;

    if (typ == CFG_SYNTH_ADF4350)
        return adf4350_MaxFreq();
    if (typ == CFG_SYNTH_ADF4351)
        return ADF4351_MaxFreq();
    if (typ == CFG_SYNTH_SI5338A)
        return SI5338A_MaxFreq();
    if (typ != CFG_SYNTH_SI5351)
        return 0U;

    maksimum = (uint64_t)GEN_MaksHarmoniczna() *
               (uint64_t)CFG_GetParam(CFG_PARAM_SI5351_MAX_FREQ);
    return maksimum > UINT32_MAX ? UINT32_MAX : (uint32_t)maksimum;
}

int GEN_CzyCzestotliwoscObslugiwana(uint32_t czestotliwosc_hz)
{
    const uint32_t IF = DSP_GetIF();
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);

    if (czestotliwosc_hz == 0U || czestotliwosc_hz <= IF)
        return 0;

    if (typ == CFG_SYNTH_ADF4350)
        return czestotliwosc_hz >= adf4350_MinFreq() &&
               czestotliwosc_hz <= adf4350_MaxFreq() &&
               (czestotliwosc_hz - IF) >= adf4350_MinFreq();
    if (typ == CFG_SYNTH_ADF4351)
        return czestotliwosc_hz >= ADF4351_MinFreq() &&
               czestotliwosc_hz <= ADF4351_MaxFreq() &&
               (czestotliwosc_hz - IF) >= ADF4351_MinFreq();
    if (typ == CFG_SYNTH_SI5338A)
        return SI5338A_CanSet(czestotliwosc_hz) && SI5338A_CanSet(czestotliwosc_hz - IF);

    if (typ == CFG_SYNTH_SI5351)
    {
        const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(czestotliwosc_hz);
        uint32_t f0_hz;
        uint32_t lo_hz;

        if (harmoniczna == 0U)
            return 0;

        f0_hz = czestotliwosc_hz / (uint32_t)harmoniczna;
        lo_hz = (czestotliwosc_hz - IF) / (uint32_t)harmoniczna;

        /*
         * To jest ograniczenie calego toru pomiarowego, nie samego Si5351.
         * Dla pomiaru potrzebujemy jednoczesnie sygnalu F0 i heterodyny LO.
         * Przy f=10 kHz LO bylaby ujemna, bo p.cz. wynosi ok. 10,031 kHz.
         * Ponadto oba wyjscia Si5351 musza pozostac >= 8 kHz. W praktyce
         * najnizsza poprawna czestotliwosc RF wypada nieco powyzej 18 kHz.
         */
        if (f0_hz < SI5351_CLKOUT_MIN_FREQ || lo_hz < SI5351_CLKOUT_MIN_FREQ)
            return 0;

        return 1;
    }

    return 0;
}

int GEN_CzyWyjscieDodatkoweObslugiwane(void)
{
    return CFG_GetParam(CFG_PARAM_SYNTH_TYPE) == CFG_SYNTH_SI5351;
}

const char *GEN_PobierzNazweSyntezera(void)
{
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    if (typ == CFG_SYNTH_ADF4350)
        return "2x ADF4350";
    if (typ == CFG_SYNTH_ADF4351)
        return "2x ADF4351";
    if (typ == CFG_SYNTH_SI5351)
        return "Si5351";
    if (typ == CFG_SYNTH_SI5338A)
        return "Si5338A";
    return "nieobslugiwany";
}

void GEN_SetMeasurementFreq(uint32_t fhz)
{
    const int32_t if_nominalny = (int32_t)DSP_GetIF();
    const int32_t if_zadany = if_nominalny;
    const uint32_t IF = (uint32_t)(if_zadany > 1000 ? if_zadany : 1000);
    const uint32_t typ = CFG_GetParam(CFG_PARAM_SYNTH_TYPE);
    uint8_t harmoniczna;

    if (fhz == 0U)
    {
        if (gen.Off != 0)
            gen.Off();
        SET_LED_STANU_RF(0U);
        return;
    }

    if (!GEN_CzyCzestotliwoscObslugiwana(fhz))
    {
        GEN_WylaczTorPomiarowy();
        SET_LED_STANU_RF(0U);
        return;
    }

    if (typ == CFG_SYNTH_SI5351)
    {
        harmoniczna = GEN_WyznaczHarmoniczna(fhz);
        gen.SetF0(fhz / harmoniczna);
        gen.SetLO((fhz - IF) / harmoniczna);
    }
    else
    {
        gen.SetF0(fhz);
        gen.SetLO(fhz - IF);
    }

    lastSetFreq = fhz;
    SET_LED_STANU_RF(1U);
    Sleep(0U);
}

void GEN_SetTXFreq(uint32_t fhz)
{
    const uint32_t IF = DSP_GetIF();
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(fhz);

    if (fhz == 0U)
    {
        GEN_WylaczClk2();
        SET_LED_STANU_RF(0U);
        return;
    }

    if (!GEN_CzyWyjscieDodatkoweObslugiwane() || gen.SetTX == 0 ||
        harmoniczna == 0U || fhz <= IF)
    {
        GEN_WylaczClk2();
        return;
    }

    gen.SetTX(fhz / harmoniczna);
    gen.SetLO((fhz - IF) / harmoniczna);
    lastSetFreq = fhz;
    SET_LED_STANU_RF(1U);
}

void GEN_SetLOFreq(uint32_t frqu1)
{
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(frqu1);
    if (harmoniczna == 0U || gen.SetLO == 0)
    {
        GEN_WylaczTorPomiarowy();
        return;
    }
    gen.SetLO(frqu1 / harmoniczna);
}

void GEN_SetF0Freq(uint32_t frqu1)
{
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(frqu1);
    if (frqu1 == 0U || harmoniczna == 0U || gen.SetF0 == 0)
    {
        GEN_WylaczTorPomiarowy();
        SET_LED_STANU_RF(0U);
        return;
    }
    gen.SetF0(frqu1 / harmoniczna);
    SET_LED_STANU_RF(1U);
}

void GEN_WylaczTorPomiarowy(void)
{
    if (gen.OffMeasurement != 0)
        gen.OffMeasurement();
    else if (gen.Off != 0)
        gen.Off();
}

void GEN_WylaczClk2(void)
{
    if (!GEN_CzyWyjscieDodatkoweObslugiwane())
        return;
    if (gen.OffTX != 0)
        gen.OffTX();
}

void GEN_SetClk2Freq(uint32_t frqu1)
{
    const uint8_t harmoniczna = GEN_WyznaczHarmoniczna(frqu1);

    if (frqu1 == 0U)
    {
        GEN_WylaczClk2();
        SET_LED_STANU_RF(0U);
        return;
    }

    if (!GEN_CzyWyjscieDodatkoweObslugiwane() || gen.SetTX == 0 || harmoniczna == 0U)
        return;

    gen.SetTX(frqu1 / harmoniczna);
    SET_LED_STANU_RF(1U);
}

uint32_t GEN_GetLastFreq(void)
{
    return lastSetFreq;
}
