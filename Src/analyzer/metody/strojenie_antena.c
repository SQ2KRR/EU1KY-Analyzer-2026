#include "strojenie_antena.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static float STROJENIE_ObliczSWR(float complex z, float z0_ohm)
{
    const float r = crealf(z);
    const float x = cimagf(z);
    const float licznik = (r - z0_ohm) * (r - z0_ohm) + x * x;
    const float mianownik = (r + z0_ohm) * (r + z0_ohm) + x * x;
    float gamma;

    if (!isfinite(r) || !isfinite(x) || !isfinite(z0_ohm) || z0_ohm <= 0.0f || r < 0.0f)
        return INFINITY;
    if (mianownik <= 0.0f)
        return INFINITY;

    gamma = sqrtf(licznik / mianownik);
    if (!isfinite(gamma) || gamma >= 0.999999f)
        return INFINITY;
    return (1.0f + gamma) / (1.0f - gamma);
}

static uint32_t STROJENIE_InterpolujF(uint32_t f0, uint32_t f1, float y0, float y1, float cel)
{
    const float roznica = y1 - y0;
    float udzial;
    float f;

    if (!isfinite(y0) || !isfinite(y1) || fabsf(roznica) < 1.0e-12f)
        return f0;

    udzial = (cel - y0) / roznica;
    if (udzial < 0.0f) udzial = 0.0f;
    if (udzial > 1.0f) udzial = 1.0f;
    f = (float)f0 + udzial * ((float)f1 - (float)f0);
    if (f < 0.0f)
        return 0U;
    return (uint32_t)(f + 0.5f);
}

static void STROJENIE_WyznaczPasmo(
    const float complex *probki,
    const uint8_t *maska,
    uint32_t liczba,
    uint32_t indeks_minimum,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm,
    float prog,
    uint8_t *znalezione,
    uint32_t *f_dol_hz,
    uint32_t *f_gora_hz)
{
    uint32_t i;
    uint8_t ma_dol = 0U;
    uint8_t ma_gore = 0U;

    *znalezione = 0U;
    *f_dol_hz = 0U;
    *f_gora_hz = 0U;

    if (indeks_minimum >= liczba || (maska != NULL && !maska[indeks_minimum]))
        return;
    if (STROJENIE_ObliczSWR(probki[indeks_minimum], z0_ohm) > prog)
        return;

    for (i = indeks_minimum; i > 0U; --i)
    {
        float swr0;
        float swr1;
        if (maska != NULL && (!maska[i - 1U] || !maska[i]))
            return;
        swr0 = STROJENIE_ObliczSWR(probki[i - 1U], z0_ohm);
        swr1 = STROJENIE_ObliczSWR(probki[i], z0_ohm);
        if (swr0 > prog && swr1 <= prog)
        {
            *f_dol_hz = STROJENIE_InterpolujF(
                f_start_hz + (i - 1U) * krok_hz,
                f_start_hz + i * krok_hz,
                swr0, swr1, prog);
            ma_dol = 1U;
            break;
        }
    }

    for (i = indeks_minimum; i + 1U < liczba; ++i)
    {
        float swr0;
        float swr1;
        if (maska != NULL && (!maska[i] || !maska[i + 1U]))
            return;
        swr0 = STROJENIE_ObliczSWR(probki[i], z0_ohm);
        swr1 = STROJENIE_ObliczSWR(probki[i + 1U], z0_ohm);
        if (swr0 <= prog && swr1 > prog)
        {
            *f_gora_hz = STROJENIE_InterpolujF(
                f_start_hz + i * krok_hz,
                f_start_hz + (i + 1U) * krok_hz,
                swr0, swr1, prog);
            ma_gore = 1U;
            break;
        }
    }

    if (ma_dol && ma_gore)
        *znalezione = 1U;
}

static void STROJENIE_DodajRezonans(
    STROJENIE_ANTENA_WYNIK_t *wynik,
    uint32_t f_hz,
    float r_ohm,
    float z0_ohm)
{
    STROJENIE_ANTENA_REZONANS_t rezonans;

    if (wynik == NULL || f_hz == 0U || !isfinite(r_ohm) || r_ohm < 0.0f)
        return;

    rezonans.czestotliwosc_hz = f_hz;
    rezonans.r_ohm = r_ohm;
    rezonans.swr = STROJENIE_ObliczSWR(r_ohm + 0.0f * I, z0_ohm);

    /* Punkt X=0 może wystąpić dokładnie na próbce i pojawić się w dwóch parach. */
    if (wynik->liczba_rezonansow > 0U)
    {
        const STROJENIE_ANTENA_REZONANS_t *poprzedni =
            &wynik->rezonanse[wynik->liczba_rezonansow - 1U];
        if (poprzedni->czestotliwosc_hz == f_hz)
            return;
    }

    if (wynik->liczba_rezonansow < STROJENIE_ANTENA_MAX_REZONANSOW)
    {
        wynik->rezonanse[wynik->liczba_rezonansow] = rezonans;
        wynik->liczba_rezonansow++;
    }
}

STROJENIE_ANTENA_WYNIK_t STROJENIE_ANTENA_Analizuj(
    const float complex *probki,
    const uint8_t *maska,
    uint32_t liczba,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm,
    uint32_t cel_hz)
{
    STROJENIE_ANTENA_WYNIK_t wynik;
    uint32_t i;
    uint32_t indeks_minimum = 0U;
    float min_swr = INFINITY;
    uint8_t znaleziono_najblizszy = 0U;
    uint64_t najlepsza_odleglosc = UINT64_MAX;
    uint64_t najlepsza_odleglosc_celu = UINT64_MAX;
    uint32_t indeks_celu = 0U;
    uint32_t najlepszy_f = 0U;
    float najlepszy_r = 0.0f;
    float najlepszy_swr = INFINITY;

    memset(&wynik, 0, sizeof(wynik));
    wynik.czestotliwosc_docelowa_hz = cel_hz;

    if (probki == NULL || liczba < 2U || krok_hz == 0U || z0_ohm <= 0.0f || cel_hz == 0U)
        return wynik;

    for (i = 0U; i < liczba; ++i)
    {
        const uint8_t poprawny = maska == NULL ? 1U : maska[i];
        float swr;
        if (!poprawny || !isfinite(crealf(probki[i])) || !isfinite(cimagf(probki[i])))
        {
            wynik.liczba_punktow_odrzuconych++;
            continue;
        }

        wynik.liczba_punktow_poprawnych++;
        {
            const uint32_t f_hz = f_start_hz + i * krok_hz;
            const uint64_t odleglosc_celu = f_hz >= cel_hz ? (uint64_t)(f_hz - cel_hz) : (uint64_t)(cel_hz - f_hz);
            if (odleglosc_celu < najlepsza_odleglosc_celu)
            {
                najlepsza_odleglosc_celu = odleglosc_celu;
                indeks_celu = i;
                wynik.punkt_docelowy_znaleziony = 1U;
            }
        }
        swr = STROJENIE_ObliczSWR(probki[i], z0_ohm);
        if (isfinite(swr) && swr >= 1.0f && swr < min_swr)
        {
            min_swr = swr;
            indeks_minimum = i;
        }
    }

    if (!isfinite(min_swr))
        return wynik;

    wynik.poprawny = 1U;
    if (wynik.punkt_docelowy_znaleziony)
    {
        wynik.f_punkt_docelowy_hz = f_start_hz + indeks_celu * krok_hz;
        wynik.r_docelowy_ohm = crealf(probki[indeks_celu]);
        wynik.x_docelowy_ohm = cimagf(probki[indeks_celu]);
        wynik.swr_docelowy = STROJENIE_ObliczSWR(probki[indeks_celu], z0_ohm);
    }
    wynik.min_swr = min_swr;
    wynik.f_min_swr_hz = f_start_hz + indeks_minimum * krok_hz;
    wynik.r_min_ohm = crealf(probki[indeks_minimum]);
    wynik.x_min_ohm = cimagf(probki[indeks_minimum]);

    for (i = 0U; i + 1U < liczba; ++i)
    {
        float x0;
        float x1;
        float r0;
        float r1;
        float udzial = 0.0f;
        uint32_t f0;
        uint32_t f1;
        uint32_t f_zero;
        float r_zero;
        uint64_t odleglosc;
        float swr_zero;

        if (maska != NULL && (!maska[i] || !maska[i + 1U]))
            continue;
        x0 = cimagf(probki[i]);
        x1 = cimagf(probki[i + 1U]);
        if (!isfinite(x0) || !isfinite(x1))
            continue;

        /* Rezonans = przejście reaktancji X przez zero. */
        if (!(x0 == 0.0f || ((x0 < 0.0f) && (x1 > 0.0f)) || ((x0 > 0.0f) && (x1 < 0.0f))))
            continue;

        f0 = f_start_hz + i * krok_hz;
        f1 = f_start_hz + (i + 1U) * krok_hz;
        f_zero = x0 == 0.0f ? f0 : STROJENIE_InterpolujF(f0, f1, x0, x1, 0.0f);

        r0 = crealf(probki[i]);
        r1 = crealf(probki[i + 1U]);
        if (x1 != x0)
            udzial = -x0 / (x1 - x0);
        if (udzial < 0.0f) udzial = 0.0f;
        if (udzial > 1.0f) udzial = 1.0f;
        r_zero = r0 + udzial * (r1 - r0);
        swr_zero = STROJENIE_ObliczSWR(r_zero + 0.0f * I, z0_ohm);

        STROJENIE_DodajRezonans(&wynik, f_zero, r_zero, z0_ohm);

        odleglosc = f_zero >= cel_hz ? (uint64_t)(f_zero - cel_hz) : (uint64_t)(cel_hz - f_zero);
        if (!znaleziono_najblizszy || odleglosc < najlepsza_odleglosc)
        {
            znaleziono_najblizszy = 1U;
            najlepsza_odleglosc = odleglosc;
            najlepszy_f = f_zero;
            najlepszy_r = r_zero;
            najlepszy_swr = swr_zero;
        }
    }

    if (znaleziono_najblizszy)
    {
        uint32_t tolerancja_hz = cel_hz / 1000U; /* 0,1% częstotliwości celu. */
        const uint32_t min_tolerancja = krok_hz * 2U;
        int64_t roznica = (int64_t)najlepszy_f - (int64_t)cel_hz;

        if (tolerancja_hz < min_tolerancja)
            tolerancja_hz = min_tolerancja;
        if (tolerancja_hz < 1000U)
            tolerancja_hz = 1000U;

        wynik.rezonans_znaleziony = 1U;
        wynik.f_rezonans_hz = najlepszy_f;
        wynik.r_rezonans_ohm = najlepszy_r;
        wynik.swr_rezonans = najlepszy_swr;
        if (roznica > INT32_MAX) roznica = INT32_MAX;
        if (roznica < INT32_MIN) roznica = INT32_MIN;
        wynik.odchylenie_rezonansu_hz = (int32_t)roznica;
        wynik.korekta_dlugosci_procent =
            (((float)najlepszy_f / (float)cel_hz) - 1.0f) * 100.0f;

        if (najlepsza_odleglosc <= tolerancja_hz)
            wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_W_CELU;
        else if (najlepszy_f < cel_hz)
            wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_SKROC;
        else
            wynik.kierunek = STROJENIE_ANTENA_KIERUNEK_WYDLUZ;
    }

    STROJENIE_WyznaczPasmo(probki, maska, liczba, indeks_minimum, f_start_hz, krok_hz,
                           z0_ohm, 1.5f, &wynik.pasmo_swr_15_znalezione,
                           &wynik.f_swr_15_dol_hz, &wynik.f_swr_15_gora_hz);
    STROJENIE_WyznaczPasmo(probki, maska, liczba, indeks_minimum, f_start_hz, krok_hz,
                           z0_ohm, 2.0f, &wynik.pasmo_swr_20_znalezione,
                           &wynik.f_swr_20_dol_hz, &wynik.f_swr_20_gora_hz);

    return wynik;
}
