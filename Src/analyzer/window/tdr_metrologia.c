#include "tdr_metrologia.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

bool TDRM_ObliczKrokCzasuNs(uint32_t krok_hz, uint32_t liczba_probek_czasu,
                            float *krok_ns)
{
    double dt_ns;

    if (krok_ns == NULL || krok_hz == 0U || liczba_probek_czasu == 0U)
        return false;

    dt_ns = 1.0e9 / ((double)krok_hz * (double)liczba_probek_czasu);
    if (!isfinite(dt_ns) || dt_ns <= 0.0 || dt_ns > (double)FLT_MAX)
        return false;

    *krok_ns = (float)dt_ns;
    return true;
}

bool TDRM_ObliczCzasNs(float indeks_probki, uint32_t krok_hz,
                       uint32_t liczba_probek_czasu, float *czas_ns)
{
    float krok_ns;
    float czas;

    if (czas_ns == NULL || !isfinite(indeks_probki) || indeks_probki < 0.0f)
        return false;

    if (!TDRM_ObliczKrokCzasuNs(krok_hz, liczba_probek_czasu, &krok_ns))
        return false;

    czas = indeks_probki * krok_ns;
    if (!isfinite(czas) || czas < 0.0f)
        return false;

    *czas_ns = czas;
    return true;
}

bool TDRM_ObliczOdlegloscM(float czas_ns, float vf, float *odleglosc_m)
{
    float odleglosc;

    if (odleglosc_m == NULL || !isfinite(czas_ns) || !isfinite(vf) ||
        czas_ns < 0.0f || vf <= 0.0f || vf > 1.0f)
        return false;

    odleglosc = TDRM_PREDKOSC_SWIATLA_M_NA_NS * vf * czas_ns * 0.5f;
    if (!isfinite(odleglosc) || odleglosc < 0.0f)
        return false;

    *odleglosc_m = odleglosc;
    return true;
}

bool TDRM_WyznaczVf(float czas_ns, float znana_dlugosc_m, float *vf)
{
    float wynik;
    float mianownik;

    if (vf == NULL || !isfinite(czas_ns) || !isfinite(znana_dlugosc_m) ||
        czas_ns <= 0.0f || znana_dlugosc_m <= 0.0f)
        return false;

    mianownik = TDRM_PREDKOSC_SWIATLA_M_NA_NS * czas_ns;
    if (!isfinite(mianownik) || mianownik <= 0.0f)
        return false;

    wynik = 2.0f * znana_dlugosc_m / mianownik;

    /* Vf > 1 nie ma sensu fizycznego dla biernej linii transmisyjnej.
       Dolna granica 0,01 odrzuca rowniez oczywiste pomylki markera/czasu. */
    if (!isfinite(wynik) || wynik < 0.01f || wynik > 1.0f)
        return false;

    *vf = wynik;
    return true;
}

bool TDRM_WyznaczKalibracjeDwuPunktowa(float czas1_ns, float dlugosc1_m,
                                       float czas2_ns, float dlugosc2_m,
                                       TDRM_KALIBRACJA_DWU_PUNKTOWA_t *wynik)
{
    float delta_t;
    float delta_l;
    float nachylenie_m_na_ns;
    float vf;
    float offset;

    if (wynik == NULL || !isfinite(czas1_ns) || !isfinite(czas2_ns) ||
        !isfinite(dlugosc1_m) || !isfinite(dlugosc2_m) ||
        czas1_ns < 0.0f || czas2_ns <= czas1_ns ||
        dlugosc1_m < 0.0f || dlugosc2_m <= dlugosc1_m)
        return false;

    delta_t = czas2_ns - czas1_ns;
    delta_l = dlugosc2_m - dlugosc1_m;
    nachylenie_m_na_ns = delta_l / delta_t;
    vf = 2.0f * nachylenie_m_na_ns / TDRM_PREDKOSC_SWIATLA_M_NA_NS;
    offset = dlugosc1_m - nachylenie_m_na_ns * czas1_ns;

    if (!isfinite(vf) || !isfinite(offset) || vf < 0.01f || vf > 1.0f)
        return false;

    wynik->vf = vf;
    wynik->offset_m = offset;
    return true;
}

bool TDRM_ObliczOdlegloscSkorygowanaM(float czas_ns,
                                      const TDRM_KALIBRACJA_DWU_PUNKTOWA_t *kalibracja,
                                      float *odleglosc_m)
{
    float odleglosc;

    if (kalibracja == NULL || odleglosc_m == NULL || !isfinite(czas_ns) ||
        czas_ns < 0.0f || !isfinite(kalibracja->vf) ||
        kalibracja->vf <= 0.0f || kalibracja->vf > 1.0f ||
        !isfinite(kalibracja->offset_m))
        return false;

    odleglosc = TDRM_PREDKOSC_SWIATLA_M_NA_NS * kalibracja->vf * czas_ns * 0.5f +
                kalibracja->offset_m;
    if (!isfinite(odleglosc))
        return false;

    *odleglosc_m = odleglosc;
    return true;
}

bool TDRM_ObliczImpedancjeZGamma(float z0_ohm, float gamma, float *z_ohm)
{
    float mianownik;
    float z;

    if (z_ohm == NULL || !isfinite(z0_ohm) || !isfinite(gamma) ||
        z0_ohm <= 0.0f || gamma <= -1.0f || gamma >= 1.0f)
        return false;

    mianownik = 1.0f - gamma;
    if (fabsf(mianownik) < 1.0e-7f)
        return false;

    z = z0_ohm * (1.0f + gamma) / mianownik;
    if (!isfinite(z) || z < 0.0f)
        return false;

    *z_ohm = z;
    return true;
}
