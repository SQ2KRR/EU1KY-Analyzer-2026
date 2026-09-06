#include "kwarc_metrologia.h"

#include <float.h>
#include <math.h>

#define KWARC_DWA_PI 6.28318530717958647692f
#define KWARC_MIN_REAKTANCJA_OHM 0.05f

static bool KWARC_PunktPoprawny(float complex z, uint8_t maska)
{
    return maska != 0U && isfinite(crealf(z)) && isfinite(cimagf(z));
}

static bool KWARC_InterpolujZero(float y0, float y1, float *udzial)
{
    float mianownik;
    float t;

    if (!isfinite(y0) || !isfinite(y1) || udzial == NULL)
        return false;

    mianownik = y1 - y0;
    if (fabsf(mianownik) < FLT_EPSILON)
        return false;

    t = -y0 / mianownik;
    if (t < 0.0f || t > 1.0f)
        return false;

    *udzial = t;
    return true;
}

bool KWARC_ObliczPojemnoscRownolegla(float complex z, float czestotliwosc_hz,
                                     float *pojemnosc_f)
{
    const float r = crealf(z);
    const float x = cimagf(z);
    float xp;
    float c;

    if (pojemnosc_f == NULL || !isfinite(r) || !isfinite(x) ||
        !isfinite(czestotliwosc_hz) || czestotliwosc_hz <= 0.0f)
        return false;

    /* Dla pojemnosci X musi byc ujemne. Zbyt male |X| powodowaloby
       numerycznie niestabilne przeliczenie szeregowym->rownoleglym. */
    if (x >= -KWARC_MIN_REAKTANCJA_OHM)
        return false;

    xp = x + (r * r) / x;
    if (!isfinite(xp) || xp >= -KWARC_MIN_REAKTANCJA_OHM)
        return false;

    c = -1.0f / (KWARC_DWA_PI * czestotliwosc_hz * xp);
    if (!isfinite(c) || c <= 0.0f)
        return false;

    /* Zakres jest szeroki celowo. Chroni przed ewidentnym bledem bez
       narzucania parametrow konkretnego uchwytu lub typu kwarcu. */
    if (c < 1.0e-15f || c > 1.0e-9f)
        return false;

    *pojemnosc_f = c;
    return true;
}

static bool KWARC_ImpedancjaMotionalna(float complex z_calkowite, float f_hz,
                                         float c0_f, float complex *z_motionalne)
{
    float complex y;
    float complex y_m;

    if (z_motionalne == NULL || !isfinite(crealf(z_calkowite)) ||
        !isfinite(cimagf(z_calkowite)) || !isfinite(f_hz) || f_hz <= 0.0f ||
        !isfinite(c0_f) || c0_f < 0.0f || cabsf(z_calkowite) < 1.0e-12f)
        return false;

    y = 1.0f / z_calkowite;
    y_m = y - I * (KWARC_DWA_PI * f_hz * c0_f);
    if (!isfinite(crealf(y_m)) || !isfinite(cimagf(y_m)) || cabsf(y_m) < 1.0e-12f)
        return false;

    *z_motionalne = 1.0f / y_m;
    return isfinite(crealf(*z_motionalne)) && isfinite(cimagf(*z_motionalne));
}

bool KWARC_ZnajdzRezonansSzeregowy(const float complex *z, const uint8_t *maska,
                                    size_t liczba_punktow, float f_start_hz,
                                    float krok_hz, float f_oczekiwana_hz, float c0_f,
                                    float *fs_hz, float *rm_ohm)
{
    size_t i;
    float najlepsza_odleglosc = FLT_MAX;
    float najlepsze_fs = 0.0f;
    float najlepsze_rm = 0.0f;
    bool znaleziono = false;

    if (z == NULL || maska == NULL || fs_hz == NULL || rm_ohm == NULL ||
        liczba_punktow < 2U || krok_hz <= 0.0f)
        return false;

    for (i = 0U; i + 1U < liczba_punktow; ++i)
    {
        float x0;
        float x1;
        float t;
        float f;
        float r;
        float odleglosc;

        if (!KWARC_PunktPoprawny(z[i], maska[i]) ||
            !KWARC_PunktPoprawny(z[i + 1U], maska[i + 1U]))
            continue;

        {
            float complex zm0;
            float complex zm1;
            const float f0 = f_start_hz + (float)i * krok_hz;
            const float f1 = f0 + krok_hz;

            if (!KWARC_ImpedancjaMotionalna(z[i], f0, c0_f, &zm0) ||
                !KWARC_ImpedancjaMotionalna(z[i + 1U], f1, c0_f, &zm1))
                continue;

            x0 = cimagf(zm0);
            x1 = cimagf(zm1);
            if (!(x0 <= 0.0f && x1 >= 0.0f))
                continue;

            if (!KWARC_InterpolujZero(x0, x1, &t))
                continue;

            f = f0 + t * krok_hz;
            r = crealf(zm0) + t * (crealf(zm1) - crealf(zm0));
        }
        if (!isfinite(r) || r <= 0.0f)
            continue;

        odleglosc = fabsf(f - f_oczekiwana_hz);
        if (!znaleziono || odleglosc < najlepsza_odleglosc)
        {
            najlepsza_odleglosc = odleglosc;
            najlepsze_fs = f;
            najlepsze_rm = r;
            znaleziono = true;
        }
    }

    if (!znaleziono)
        return false;

    *fs_hz = najlepsze_fs;
    *rm_ohm = najlepsze_rm;
    return true;
}

bool KWARC_ZnajdzRezonansRownolegly(const float complex *z, const uint8_t *maska,
                                    size_t liczba_punktow, float f_start_hz,
                                    float krok_hz, float f_oczekiwana_hz,
                                    float *fp_hz)
{
    size_t i;
    float najlepsza_odleglosc = FLT_MAX;
    float najlepsze_fp = 0.0f;
    bool znaleziono = false;

    if (z == NULL || maska == NULL || fp_hz == NULL || liczba_punktow < 2U ||
        krok_hz <= 0.0f)
        return false;

    for (i = 0U; i + 1U < liczba_punktow; ++i)
    {
        float complex y0;
        float complex y1;
        float b0;
        float b1;
        float t;
        float f;
        float odleglosc;

        if (!KWARC_PunktPoprawny(z[i], maska[i]) ||
            !KWARC_PunktPoprawny(z[i + 1U], maska[i + 1U]))
            continue;

        if (cabsf(z[i]) < 1.0e-9f || cabsf(z[i + 1U]) < 1.0e-9f)
            continue;

        y0 = 1.0f / z[i];
        y1 = 1.0f / z[i + 1U];
        b0 = cimagf(y0);
        b1 = cimagf(y1);

        /* Po rezonansie szeregowym susceptancja przechodzi najpierw
           na wartosci ujemne, a w antyrezonansie wraca przez zero. */
        if (!(b0 <= 0.0f && b1 >= 0.0f))
            continue;

        if (!KWARC_InterpolujZero(b0, b1, &t))
            continue;

        f = f_start_hz + ((float)i + t) * krok_hz;
        odleglosc = fabsf(f - f_oczekiwana_hz);
        if (!znaleziono || odleglosc < najlepsza_odleglosc)
        {
            najlepsza_odleglosc = odleglosc;
            najlepsze_fp = f;
            znaleziono = true;
        }
    }

    if (!znaleziono)
        return false;

    *fp_hz = najlepsze_fp;
    return true;
}

bool KWARC_ObliczModel(float fs_hz, float fp_hz, float rm_ohm, float c0_f,
                       KWARC_WYNIK_t *wynik)
{
    float stosunek;
    float cm;
    float lm;
    float q;

    if (wynik == NULL || !isfinite(fs_hz) || !isfinite(fp_hz) ||
        !isfinite(rm_ohm) || !isfinite(c0_f) || fs_hz <= 0.0f ||
        fp_hz <= fs_hz || rm_ohm <= 0.0f || c0_f <= 0.0f)
        return false;

    stosunek = fp_hz / fs_hz;
    cm = c0_f * (stosunek * stosunek - 1.0f);
    if (!isfinite(cm) || cm <= 0.0f)
        return false;

    lm = 1.0f / (KWARC_DWA_PI * KWARC_DWA_PI * fs_hz * fs_hz * cm);
    if (!isfinite(lm) || lm <= 0.0f)
        return false;

    q = KWARC_DWA_PI * fs_hz * lm / rm_ohm;
    if (!isfinite(q) || q <= 0.0f)
        return false;

    wynik->fs_hz = fs_hz;
    wynik->fp_hz = fp_hz;
    wynik->rm_ohm = rm_ohm;
    wynik->c0_f = c0_f;
    wynik->cm_f = cm;
    wynik->lm_h = lm;
    wynik->q = q;
    return true;
}
