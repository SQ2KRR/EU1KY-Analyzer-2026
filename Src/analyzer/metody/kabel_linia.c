#include <math.h>
#include <complex.h>
#include <string.h>
#include "kabel_linia.h"

#define KABEL_EPS 1.0e-10f

static bool KABEL_CzySkonczone(float complex z)
{
    return isfinite(crealf(z)) && isfinite(cimagf(z));
}

static bool KABEL_CzyMianownikPoprawny(float complex mianownik, float skala)
{
    const float prog = KABEL_EPS * fmaxf(1.0f, skala);
    return KABEL_CzySkonczone(mianownik) && cabsf(mianownik) > prog;
}

bool KABEL_LINIA_ZOpenShort(float complex z_open_ohm,
                            float complex z_short_ohm,
                            KABEL_LINIA_PUNKT_t *wynik)
{
    float complex zc;
    float complex t;

    if (wynik == 0)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->status = KABEL_LINIA_BRAK_DANYCH;

    if (!KABEL_CzySkonczone(z_open_ohm) || !KABEL_CzySkonczone(z_short_ohm))
        return false;
    if (cabsf(z_open_ohm) < KABEL_EPS || cabsf(z_short_ohm) < KABEL_EPS)
    {
        wynik->status = KABEL_LINIA_OSOBLIWOSC;
        return false;
    }

    /* Zopen*Zshort = Zc^2 dla jednorodnej linii, także stratnej. */
    zc = csqrtf(z_open_ohm * z_short_ohm);
    if (!KABEL_CzySkonczone(zc) || cabsf(zc) < KABEL_EPS)
    {
        wynik->status = KABEL_LINIA_OSOBLIWOSC;
        return false;
    }

    /*
     * Pierwiastek ma dwa znaki. Wybieramy reprezentację z dodatnią częścią
     * rzeczywistą Zc; przy Re=0 wybieramy dodatnią część urojoną.
     */
    if (crealf(zc) < 0.0f || (fabsf(crealf(zc)) < KABEL_EPS && cimagf(zc) < 0.0f))
        zc = -zc;

    t = z_short_ohm / zc;
    if (!KABEL_CzySkonczone(t))
    {
        wynik->status = KABEL_LINIA_OSOBLIWOSC;
        return false;
    }

    /* Dodatnia część rzeczywista Zc jest minimalnym warunkiem pasywnej linii. */
    if (crealf(zc) <= 0.0f)
    {
        wynik->status = KABEL_LINIA_NIEFIZYCZNY;
        return false;
    }

    wynik->zc_ohm = zc;
    wynik->tanh_gamma_l = t;
    wynik->status = KABEL_LINIA_OK;
    return true;
}

bool KABEL_LINIA_TransformujDoWejscia(const KABEL_LINIA_PUNKT_t *kabel,
                                      float complex z_obciazenia_ohm,
                                      float complex *z_wejscia_ohm)
{
    float complex licznik;
    float complex mianownik;
    float skala;

    if (kabel == 0 || z_wejscia_ohm == 0 || kabel->status != KABEL_LINIA_OK ||
        !KABEL_CzySkonczone(z_obciazenia_ohm))
        return false;

    licznik = z_obciazenia_ohm + kabel->zc_ohm * kabel->tanh_gamma_l;
    mianownik = kabel->zc_ohm + z_obciazenia_ohm * kabel->tanh_gamma_l;
    skala = cabsf(kabel->zc_ohm) + cabsf(z_obciazenia_ohm * kabel->tanh_gamma_l);
    if (!KABEL_CzyMianownikPoprawny(mianownik, skala))
        return false;

    *z_wejscia_ohm = kabel->zc_ohm * licznik / mianownik;
    return KABEL_CzySkonczone(*z_wejscia_ohm);
}

bool KABEL_LINIA_Deembeduj(const KABEL_LINIA_PUNKT_t *kabel,
                           float complex z_wejscia_ohm,
                           float complex *z_obciazenia_ohm)
{
    float complex licznik;
    float complex mianownik;
    float skala;

    if (kabel == 0 || z_obciazenia_ohm == 0 || kabel->status != KABEL_LINIA_OK ||
        !KABEL_CzySkonczone(z_wejscia_ohm))
        return false;

    licznik = z_wejscia_ohm - kabel->zc_ohm * kabel->tanh_gamma_l;
    mianownik = kabel->zc_ohm - z_wejscia_ohm * kabel->tanh_gamma_l;
    skala = cabsf(kabel->zc_ohm) + cabsf(z_wejscia_ohm * kabel->tanh_gamma_l);
    if (!KABEL_CzyMianownikPoprawny(mianownik, skala))
        return false;

    *z_obciazenia_ohm = kabel->zc_ohm * licznik / mianownik;
    return KABEL_CzySkonczone(*z_obciazenia_ohm);
}

bool KABEL_LINIA_SprawdzLoad(const KABEL_LINIA_PUNKT_t *kabel,
                             float complex z_load_zmierzony_ohm,
                             float complex z_load_wzorcowy_ohm,
                             float *blad_ohm,
                             float *blad_proc)
{
    float complex z_po;
    float blad;
    float skala;

    if (blad_ohm == 0 || blad_proc == 0 ||
        !KABEL_LINIA_Deembeduj(kabel, z_load_zmierzony_ohm, &z_po) ||
        !KABEL_CzySkonczone(z_load_wzorcowy_ohm))
        return false;

    blad = cabsf(z_po - z_load_wzorcowy_ohm);
    skala = fmaxf(cabsf(z_load_wzorcowy_ohm), 1.0e-6f);
    *blad_ohm = blad;
    *blad_proc = 100.0f * blad / skala;
    return isfinite(*blad_ohm) && isfinite(*blad_proc);
}
