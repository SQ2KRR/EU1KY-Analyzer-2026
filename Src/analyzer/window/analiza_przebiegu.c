#include "analiza_przebiegu.h"

#include <math.h>
#include <stddef.h>

static float ObliczSWR(float complex z, float z0_ohm)
{
    float r = crealf(z);
    float x = cimagf(z);
    float licznik;
    float mianownik;
    float gamma;

    if (!isfinite(r) || !isfinite(x) || !isfinite(z0_ohm) || z0_ohm <= 0.0f)
        return INFINITY;

    if (r < 0.0f)
        r = 0.0f;

    licznik = (r - z0_ohm) * (r - z0_ohm) + x * x;
    mianownik = (r + z0_ohm) * (r + z0_ohm) + x * x;
    if (mianownik <= 0.0f)
        return INFINITY;

    gamma = sqrtf(licznik / mianownik);
    if (gamma >= 0.999999f)
        return 1999999.0f;

    return (1.0f + gamma) / (1.0f - gamma);
}

static uint32_t InterpolujCzestotliwosc(
    uint32_t f0,
    uint32_t f1,
    float y0,
    float y1,
    float cel)
{
    float roznica = y1 - y0;
    float udzial;
    float f;

    if (!isfinite(y0) || !isfinite(y1) || fabsf(roznica) < 1.0e-12f)
        return f0;

    udzial = (cel - y0) / roznica;
    if (udzial < 0.0f)
        udzial = 0.0f;
    if (udzial > 1.0f)
        udzial = 1.0f;

    f = (float)f0 + udzial * ((float)f1 - (float)f0);
    if (f < 0.0f)
        return 0U;
    return (uint32_t)(f + 0.5f);
}

static void WyznaczPasmo(
    const float complex *probki,
    const uint8_t *maska_poprawnosci,
    uint32_t liczba_probek,
    uint32_t indeks_minimum,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm,
    float prog_swr,
    uint8_t *znalezione,
    uint32_t *f_dol_hz,
    uint32_t *f_gora_hz)
{
    uint32_t i;
    uint8_t ma_dol = 0U;
    uint8_t ma_gore = 0U;
    uint32_t dol = f_start_hz;
    uint32_t gora = f_start_hz + (liczba_probek - 1U) * krok_hz;

    *znalezione = 0U;
    *f_dol_hz = 0U;
    *f_gora_hz = 0U;

    if (maska_poprawnosci != NULL && !maska_poprawnosci[indeks_minimum])
        return;
    if (ObliczSWR(probki[indeks_minimum], z0_ohm) > prog_swr)
        return;

    for (i = indeks_minimum; i > 0U; --i)
    {
        float swr_prawy;
        float swr_lewy;
        if (maska_poprawnosci != NULL && (!maska_poprawnosci[i] || !maska_poprawnosci[i - 1U]))
            return;
        swr_prawy = ObliczSWR(probki[i], z0_ohm);
        swr_lewy = ObliczSWR(probki[i - 1U], z0_ohm);
        if (swr_prawy <= prog_swr && swr_lewy > prog_swr)
        {
            uint32_t f_lewy = f_start_hz + (i - 1U) * krok_hz;
            uint32_t f_prawy = f_start_hz + i * krok_hz;
            dol = InterpolujCzestotliwosc(f_lewy, f_prawy, swr_lewy, swr_prawy, prog_swr);
            ma_dol = 1U;
            break;
        }
    }

    for (i = indeks_minimum; i + 1U < liczba_probek; ++i)
    {
        float swr_lewy;
        float swr_prawy;
        if (maska_poprawnosci != NULL && (!maska_poprawnosci[i] || !maska_poprawnosci[i + 1U]))
            return;
        swr_lewy = ObliczSWR(probki[i], z0_ohm);
        swr_prawy = ObliczSWR(probki[i + 1U], z0_ohm);
        if (swr_lewy <= prog_swr && swr_prawy > prog_swr)
        {
            uint32_t f_lewy = f_start_hz + i * krok_hz;
            uint32_t f_prawy = f_start_hz + (i + 1U) * krok_hz;
            gora = InterpolujCzestotliwosc(f_lewy, f_prawy, swr_lewy, swr_prawy, prog_swr);
            ma_gore = 1U;
            break;
        }
    }

    /*
     * Jeżeli przebieg pozostaje poniżej progu aż do krawędzi skanu, nie znamy
     * pełnej szerokości pasma. Nie podajemy wtedy pozornie dokładnej liczby.
     */
    if (ma_dol && ma_gore)
    {
        *znalezione = 1U;
        *f_dol_hz = dol;
        *f_gora_hz = gora;
    }
}

static ANALIZA_PRZEBIEGU_WYNIK_t ObliczWewnetrznie(
    const float complex *probki,
    const uint8_t *maska_poprawnosci,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm)
{
    ANALIZA_PRZEBIEGU_WYNIK_t wynik = {0};
    uint32_t indeks_minimum = 0U;
    float min_swr = INFINITY;
    uint32_t i;
    float najlepsze_abs_x = INFINITY;
    uint32_t najlepszy_rezonans_hz = 0U;
    float najlepszy_r_rezonans = 0.0f;
    uint8_t znaleziono_przejscie_x = 0U;

    if (probki == NULL || liczba_probek < 2U || krok_hz == 0U || z0_ohm <= 0.0f)
        return wynik;

    for (i = 0U; i < liczba_probek; ++i)
    {
        float swr;
        const uint8_t punkt_poprawny = maska_poprawnosci == NULL ? 1U : maska_poprawnosci[i];
        if (!punkt_poprawny)
        {
            wynik.liczba_punktow_odrzuconych++;
            continue;
        }
        wynik.liczba_punktow_poprawnych++;
        swr = ObliczSWR(probki[i], z0_ohm);
        if (isfinite(swr) && swr >= 1.0f && swr < min_swr)
        {
            min_swr = swr;
            indeks_minimum = i;
        }
    }

    if (!isfinite(min_swr))
        return wynik;

    wynik.poprawna = 1U;
    wynik.min_swr = min_swr;
    wynik.f_min_swr_hz = f_start_hz + indeks_minimum * krok_hz;
    wynik.r_min_ohm = crealf(probki[indeks_minimum]);
    wynik.x_min_ohm = cimagf(probki[indeks_minimum]);

    /*
     * Rezonans definiujemy jako przejście X przez zero. Gdy jest ich kilka,
     * wybieramy to najbliższe minimum SWR, bo zwykle właśnie ono interesuje
     * podczas strojenia anteny.
     */
    for (i = 0U; i + 1U < liczba_probek; ++i)
    {
        float x0;
        float x1;
        if (maska_poprawnosci != NULL && (!maska_poprawnosci[i] || !maska_poprawnosci[i + 1U]))
            continue;
        x0 = cimagf(probki[i]);
        x1 = cimagf(probki[i + 1U]);
        if (!isfinite(x0) || !isfinite(x1))
            continue;

        if (x0 == 0.0f || x1 == 0.0f || ((x0 < 0.0f) && (x1 > 0.0f)) || ((x0 > 0.0f) && (x1 < 0.0f)))
        {
            uint32_t f0 = f_start_hz + i * krok_hz;
            uint32_t f1 = f_start_hz + (i + 1U) * krok_hz;
            uint32_t f_zero;
            float odleglosc;
            float udzial = 0.0f;
            float r0 = crealf(probki[i]);
            float r1 = crealf(probki[i + 1U]);
            float r_zero;

            if (x0 == 0.0f)
                f_zero = f0;
            else if (x1 == 0.0f)
                f_zero = f1;
            else
                f_zero = InterpolujCzestotliwosc(f0, f1, x0, x1, 0.0f);

            if (x1 != x0)
                udzial = -x0 / (x1 - x0);
            if (udzial < 0.0f)
                udzial = 0.0f;
            if (udzial > 1.0f)
                udzial = 1.0f;
            r_zero = r0 + udzial * (r1 - r0);

            odleglosc = fabsf((float)f_zero - (float)wynik.f_min_swr_hz);
            if (!znaleziono_przejscie_x || odleglosc < najlepsze_abs_x)
            {
                znaleziono_przejscie_x = 1U;
                najlepsze_abs_x = odleglosc;
                najlepszy_rezonans_hz = f_zero;
                najlepszy_r_rezonans = r_zero;
            }
        }
    }

    if (znaleziono_przejscie_x)
    {
        wynik.rezonans_znaleziony = 1U;
        wynik.f_rezonans_hz = najlepszy_rezonans_hz;
        wynik.r_rezonans_ohm = najlepszy_r_rezonans;
    }

    WyznaczPasmo(probki, maska_poprawnosci, liczba_probek, indeks_minimum, f_start_hz, krok_hz,
                 z0_ohm, 1.5f, &wynik.pasmo_swr_15_znalezione,
                 &wynik.f_swr_15_dol_hz, &wynik.f_swr_15_gora_hz);
    WyznaczPasmo(probki, maska_poprawnosci, liczba_probek, indeks_minimum, f_start_hz, krok_hz,
                 z0_ohm, 2.0f, &wynik.pasmo_swr_20_znalezione,
                 &wynik.f_swr_20_dol_hz, &wynik.f_swr_20_gora_hz);

    return wynik;
}
ANALIZA_PRZEBIEGU_WYNIK_t ANALIZA_PRZEBIEGU_Oblicz(
    const float complex *probki,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm)
{
    return ObliczWewnetrznie(probki, NULL, liczba_probek, f_start_hz, krok_hz, z0_ohm);
}

ANALIZA_PRZEBIEGU_WYNIK_t ANALIZA_PRZEBIEGU_ObliczZMaska(
    const float complex *probki,
    const uint8_t *maska_poprawnosci,
    uint32_t liczba_probek,
    uint32_t f_start_hz,
    uint32_t krok_hz,
    float z0_ohm)
{
    return ObliczWewnetrznie(probki, maska_poprawnosci, liczba_probek, f_start_hz, krok_hz, z0_ohm);
}

