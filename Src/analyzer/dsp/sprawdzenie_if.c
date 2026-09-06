#include "sprawdzenie_if.h"

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <complex.h>

#include "arm_math.h"
#include "stm32746g_discovery_audio.h"
#include "config.h"
#include "dsp.h"
#include "gen.h"
#include "si5351.h"

/*
 * 2048 próbek daje 23,4375 Hz/bin przy Fs=48 kHz, czyli duży zapas wobec
 * tolerancji testu 2%. Nie ma potrzeby trzymać tablic RFFT 4096 w Flash.
 */
#define SPRAWDZENIE_FFT_N 2048U
#define SPRAWDZENIE_KANALY 2U
#define SPRAWDZENIE_F_POMIAROWA_HZ 14000000U
#define SPRAWDZENIE_F_MIN_HZ 7000.0f
#define SPRAWDZENIE_F_MAX_HZ 13000.0f
#define SPRAWDZENIE_MIN_JAKOSC_DB 12.0f
#define SPRAWDZENIE_TOLERANCJA_NOMINAL_PROC 2.0f

/*
 * Bufory są w SDRAM, aby nie zużywać cennego wewnętrznego SRAM. Sprawdzenie jest
 * uruchamiany ręcznie, więc nie obciąża normalnego toru pomiarowego.
 */
static int16_t __attribute__((section(".user_sdram"))) __attribute__((used))
    sprawdzenie_audio[SPRAWDZENIE_FFT_N * SPRAWDZENIE_KANALY];
static float __attribute__((section(".user_sdram"))) __attribute__((used))
    sprawdzenie_fft_wejscie[SPRAWDZENIE_FFT_N];
static float __attribute__((section(".user_sdram"))) __attribute__((used))
    sprawdzenie_fft_wyjscie[SPRAWDZENIE_FFT_N];


static SPRAWDZENIE_IF_WYNIK_t ostatni_wynik;
static uint8_t ostatni_wynik_wazny;

int SPRAWDZENIE_IF_PobierzOstatni(SPRAWDZENIE_IF_WYNIK_t *wynik)
{
    if (!ostatni_wynik_wazny || wynik == NULL)
        return 0;
    *wynik = ostatni_wynik;
    return 1;
}

static float SPRAWDZENIE_IF_ObliczKanal(uint8_t kanal, float *jakosc_db)
{
    arm_rfft_fast_instance_f32 fft;
    const float bin_hz = (float)FSAMPLE / (float)SPRAWDZENIE_FFT_N;
    const uint32_t bin_min = (uint32_t)(SPRAWDZENIE_F_MIN_HZ / bin_hz);
    const uint32_t bin_max = (uint32_t)(SPRAWDZENIE_F_MAX_HZ / bin_hz);
    const float complex *widmo = (const float complex *)sprawdzenie_fft_wyjscie;
    uint32_t i;
    uint32_t bin_peak = bin_min;
    float peak = 0.0f;
    float suma_tla = 0.0f;
    uint32_t liczba_tla = 0U;
    float przesuniecie = 0.0f;

    if (kanal >= SPRAWDZENIE_KANALY)
        return NAN;

    for (i = 0U; i < SPRAWDZENIE_FFT_N; ++i)
    {
        const float x = (float)sprawdzenie_audio[i * SPRAWDZENIE_KANALY + kanal];
        const float a = (2.0f * (float)M_PI * (float)i) / (float)(SPRAWDZENIE_FFT_N - 1U);
        /* Blackman: dobre tłumienie prążków bocznych przy silnym tonie IF. */
        const float okno = 0.42f - 0.5f * cosf(a) + 0.08f * cosf(2.0f * a);
        sprawdzenie_fft_wejscie[i] = x * okno;
    }

    if (arm_rfft_fast_init_f32(&fft, SPRAWDZENIE_FFT_N) != ARM_MATH_SUCCESS)
        return NAN;
    arm_rfft_fast_f32(&fft, sprawdzenie_fft_wejscie, sprawdzenie_fft_wyjscie, 0);

    for (i = bin_min; i <= bin_max; ++i)
    {
        const float re = crealf(widmo[i]);
        const float im = cimagf(widmo[i]);
        const float p = re * re + im * im;
        if (p > peak)
        {
            peak = p;
            bin_peak = i;
        }
    }

    for (i = bin_min; i <= bin_max; ++i)
    {
        const float re = crealf(widmo[i]);
        const float im = cimagf(widmo[i]);
        const float p = re * re + im * im;
        if (i + 3U < bin_peak || i > bin_peak + 3U)
        {
            suma_tla += p;
            ++liczba_tla;
        }
    }

    if (peak <= 1.0f || liczba_tla == 0U)
    {
        if (jakosc_db != NULL)
            *jakosc_db = -100.0f;
        return NAN;
    }

    if (jakosc_db != NULL)
    {
        const float tlo = suma_tla / (float)liczba_tla;
        *jakosc_db = 10.0f * log10f(peak / fmaxf(tlo, 1.0f));
    }

    if (bin_peak > bin_min && bin_peak < bin_max)
    {
        const float p1 = crealf(widmo[bin_peak - 1U]) * crealf(widmo[bin_peak - 1U]) +
                         cimagf(widmo[bin_peak - 1U]) * cimagf(widmo[bin_peak - 1U]);
        const float p2 = peak;
        const float p3 = crealf(widmo[bin_peak + 1U]) * crealf(widmo[bin_peak + 1U]) +
                         cimagf(widmo[bin_peak + 1U]) * cimagf(widmo[bin_peak + 1U]);
        const float mianownik = p1 - 2.0f * p2 + p3;
        if (fabsf(mianownik) > 1e-20f)
        {
            przesuniecie = 0.5f * (p1 - p3) / mianownik;
            if (przesuniecie > 0.5f)
                przesuniecie = 0.5f;
            else if (przesuniecie < -0.5f)
                przesuniecie = -0.5f;
        }
    }

    return ((float)bin_peak + przesuniecie) * bin_hz;
}

static uint32_t SPRAWDZENIE_IF_NajblizszyNominal(uint32_t oszacowany_hz, uint8_t *zgodny)
{
    static const uint32_t nominaly[] = {25000000U, 27000000U};
    uint32_t najlepszy = nominaly[0];
    uint32_t najlepsza_roznica = UINT32_MAX;
    size_t i;

    if (zgodny != NULL)
        *zgodny = 0U;

    for (i = 0U; i < sizeof(nominaly) / sizeof(nominaly[0]); ++i)
    {
        const uint32_t n = nominaly[i];
        const uint32_t roznica = (oszacowany_hz > n) ? (oszacowany_hz - n) : (n - oszacowany_hz);
        if (roznica < najlepsza_roznica)
        {
            najlepsza_roznica = roznica;
            najlepszy = n;
        }
    }

    if (zgodny != NULL)
    {
        const float proc = 100.0f * (float)najlepsza_roznica / (float)najlepszy;
        *zgodny = (proc <= SPRAWDZENIE_TOLERANCJA_NOMINAL_PROC) ? 1U : 0U;
    }
    return najlepszy;
}

void SPRAWDZENIE_IF_Wykonaj(SPRAWDZENIE_IF_WYNIK_t *wynik)
{
    extern SAI_HandleTypeDef haudio_in_sai;
    HAL_StatusTypeDef stan_probki;
    float f0;
    float f1;
    float j0;
    float j1;
    float f;
    float jakosc;
    const float oczekiwana_if = (float)DSP_GetIF();
    uint8_t nominal_zgodny = 0U;

    if (wynik == NULL)
        return;
    ostatni_wynik_wazny = 0U;
    memset(wynik, 0, sizeof(*wynik));

    wynik->status = SPRAWDZENIE_IF_BLAD_PROBKI;
    wynik->czestotliwosc_pomiarowa_hz = SPRAWDZENIE_F_POMIAROWA_HZ;
    wynik->oczekiwana_if_hz = oczekiwana_if;
    wynik->xtal_ustawiony_hz = CFG_GetParam(CFG_PARAM_SI5351_XTAL_FREQ);

    if (!si5351_IsPresent())
    {
        wynik->status = SPRAWDZENIE_IF_BRAK_SI5351;
        ostatni_wynik = *wynik;
        ostatni_wynik_wazny = 1U;
        return;
    }

    /* H1 przy 14 MHz ogranicza liczbę dodatkowych niewiadomych w teście. */
    GEN_SetMeasurementFreq(SPRAWDZENIE_F_POMIAROWA_HZ);
    HAL_Delay(20U);

    stan_probki = HAL_SAI_Receive(&haudio_in_sai, (uint8_t *)sprawdzenie_audio,
                                  (uint16_t)(SPRAWDZENIE_FFT_N * SPRAWDZENIE_KANALY), 1000U);
    GEN_SetMeasurementFreq(0U);

    if (stan_probki != HAL_OK)
    {
        wynik->status = SPRAWDZENIE_IF_BLAD_PROBKI;
        ostatni_wynik = *wynik;
        ostatni_wynik_wazny = 1U;
        return;
    }

    f0 = SPRAWDZENIE_IF_ObliczKanal(0U, &j0);
    f1 = SPRAWDZENIE_IF_ObliczKanal(1U, &j1);

    if (isfinite(f0) && (!isfinite(f1) || j0 >= j1))
    {
        f = f0;
        jakosc = j0;
    }
    else
    {
        f = f1;
        jakosc = j1;
    }

    wynik->jakosc_db = jakosc;
    if (!isfinite(f) || jakosc < SPRAWDZENIE_MIN_JAKOSC_DB)
    {
        wynik->status = SPRAWDZENIE_IF_BRAK_SYGNALU;
        ostatni_wynik = *wynik;
        ostatni_wynik_wazny = 1U;
        return;
    }

    wynik->zmierzona_if_hz = f;
    wynik->odchylenie_if_hz = f - oczekiwana_if;
    wynik->odchylenie_proc = 100.0f * wynik->odchylenie_if_hz / oczekiwana_if;

    if (wynik->xtal_ustawiony_hz > 0U)
    {
        const double oszacowany = (double)wynik->xtal_ustawiony_hz * (double)f / (double)oczekiwana_if;
        if (oszacowany > 1000000.0 && oszacowany < 100000000.0)
            wynik->xtal_oszacowany_hz = (uint32_t)(oszacowany + 0.5);
    }

    wynik->sugerowany_nominal_hz = SPRAWDZENIE_IF_NajblizszyNominal(wynik->xtal_oszacowany_hz,
                                                                         &nominal_zgodny);
    wynik->mozna_sugerowac_nominal = nominal_zgodny;
    wynik->status = SPRAWDZENIE_IF_OK;
    ostatni_wynik = *wynik;
    ostatni_wynik_wazny = 1U;
}
