#include "dopasowanie_cauer_foster.h"

#include <complex.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DOPASOWANIE_CF_EPS 1.0e-30
#define DOPASOWANIE_CF_ITERACJE 30U
#define DOPASOWANIE_CF_KROK_START 0.50
#define DOPASOWANIE_CF_KROK_STOP 0.002
#define METROLOGIA_MIN_AUTOMAT_HZ 500000U

static bool PunktPoprawny(const POMIAR_S11_t *punkt)
{
    if (punkt == NULL || punkt->czestotliwosc_hz == 0U ||
        (punkt->flagi & POMIAR_S11_FLAGA_POPRAWNY) == 0U)
        return false;

    if (punkt->czestotliwosc_hz < METROLOGIA_MIN_AUTOMAT_HZ)
        return false;
    return isfinite(crealf(punkt->impedancja_ohm)) &&
           isfinite(cimagf(punkt->impedancja_ohm));
}

static uint16_t PoliczPunkty(const SERIA_S11_t *seria, double *f_max_hz,
                             double *rms_y_h, double *rms_x_ohm,
                             double *rms_r_ohm, double *rms_z_ohm)
{
    uint16_t i;
    uint16_t n = 0U;
    double suma_y2 = 0.0;
    double suma_x2 = 0.0;
    double suma_r2 = 0.0;
    double suma_z2 = 0.0;
    double f_max = 0.0;

    if (seria == NULL || seria->punkty == NULL)
        return 0U;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double f;
        double omega;
        double r;
        double x;
        double y;

        if (!PunktPoprawny(p))
            continue;

        f = (double)p->czestotliwosc_hz;
        omega = 2.0 * M_PI * f;
        r = (double)crealf(p->impedancja_ohm);
        x = (double)cimagf(p->impedancja_ohm);
        y = x / omega;
        if (!isfinite(y))
            continue;

        if (f > f_max)
            f_max = f;
        suma_y2 += y * y;
        suma_x2 += x * x;
        suma_r2 += r * r;
        suma_z2 += r * r + x * x;
        ++n;
    }

    if (n == 0U)
        return 0U;

    if (f_max_hz != NULL)
        *f_max_hz = f_max;
    if (rms_y_h != NULL)
        *rms_y_h = sqrt(suma_y2 / (double)n);
    if (rms_x_ohm != NULL)
        *rms_x_ohm = sqrt(suma_x2 / (double)n);
    if (rms_r_ohm != NULL)
        *rms_r_ohm = sqrt(suma_r2 / (double)n);
    if (rms_z_ohm != NULL)
        *rms_z_ohm = sqrt(suma_z2 / (double)n);
    return n;
}

static bool Rozwiaz3x3(double a[3][3], double b[3], double x[3])
{
    uint8_t kol;

    for (kol = 0U; kol < 3U; ++kol)
    {
        uint8_t wiersz;
        uint8_t pivot = kol;
        double maksimum = fabs(a[kol][kol]);

        for (wiersz = (uint8_t)(kol + 1U); wiersz < 3U; ++wiersz)
        {
            const double kandydat = fabs(a[wiersz][kol]);
            if (kandydat > maksimum)
            {
                maksimum = kandydat;
                pivot = wiersz;
            }
        }
        if (!(maksimum > 1.0e-14) || !isfinite(maksimum))
            return false;

        if (pivot != kol)
        {
            uint8_t j;
            for (j = kol; j < 3U; ++j)
            {
                const double tmp = a[kol][j];
                a[kol][j] = a[pivot][j];
                a[pivot][j] = tmp;
            }
            {
                const double tmp = b[kol];
                b[kol] = b[pivot];
                b[pivot] = tmp;
            }
        }

        for (wiersz = (uint8_t)(kol + 1U); wiersz < 3U; ++wiersz)
        {
            uint8_t j;
            const double mnoznik = a[wiersz][kol] / a[kol][kol];
            for (j = kol; j < 3U; ++j)
                a[wiersz][j] -= mnoznik * a[kol][j];
            b[wiersz] -= mnoznik * b[kol];
        }
    }

    for (int i = 2; i >= 0; --i)
    {
        double suma = b[i];
        for (int j = i + 1; j < 3; ++j)
            suma -= a[i][j] * x[j];
        if (!(fabs(a[i][i]) > 1.0e-14) || !isfinite(a[i][i]))
            return false;
        x[i] = suma / a[i][i];
        if (!isfinite(x[i]))
            return false;
    }
    return true;
}

static double KosztX(const SERIA_S11_t *seria, const SYNTEZA_CF_CAUER_I_t *cauer,
                     double *rms_ohm, uint16_t *liczba)
{
    uint16_t i;
    uint16_t n = 0U;
    double suma_norm = 0.0;
    double suma_ohm = 0.0;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double complex z_model;
        double x;
        double dx;
        double skala;

        if (!PunktPoprawny(p))
            continue;
        z_model = SYNTEZA_CF_ImpedancjaCauer(cauer, (double)p->czestotliwosc_hz);
        if (!isfinite(creal(z_model)) || !isfinite(cimag(z_model)))
            return INFINITY;

        x = (double)cimagf(p->impedancja_ohm);
        dx = cimag(z_model) - x;
        skala = fmax(1.0, fabs(x));
        suma_norm += (dx / skala) * (dx / skala);
        suma_ohm += dx * dx;
        ++n;
    }

    if (liczba != NULL)
        *liczba = n;
    if (rms_ohm != NULL)
        *rms_ohm = n ? sqrt(suma_ohm / (double)n) : NAN;
    return n ? suma_norm / (double)n : INFINITY;
}

static bool InicjalizujL(const SERIA_S11_t *seria, SYNTEZA_CF_CAUER_I_t *cauer)
{
    uint16_t i;
    double licznik = 0.0;
    double mianownik = 0.0;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double omega;
        double x;
        if (!PunktPoprawny(p))
            continue;
        omega = 2.0 * M_PI * (double)p->czestotliwosc_hz;
        x = (double)cimagf(p->impedancja_ohm);
        licznik += omega * x;
        mianownik += omega * omega;
    }

    if (!(mianownik > DOPASOWANIE_CF_EPS))
        return false;

    memset(cauer, 0, sizeof(*cauer));
    cauer->liczba_elementow = 1U;
    cauer->wartosc[0] = licznik / mianownik;
    return isfinite(cauer->wartosc[0]) && cauer->wartosc[0] > 0.0;
}

static bool InicjalizujLCL(const SERIA_S11_t *seria, double f_ref_hz,
                           double y_skala_h, SYNTEZA_CF_CAUER_I_t *cauer)
{
    uint16_t i;
    double ata[3][3] = {{0.0}};
    double aty[3] = {0.0, 0.0, 0.0};
    double p[3] = {0.0, 0.0, 0.0};
    const double omega_ref = 2.0 * M_PI * f_ref_hz;

    if (!(f_ref_hz > 0.0) || !(y_skala_h > DOPASOWANIE_CF_EPS))
        return false;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *punkt = &seria->punkty[i];
        double f;
        double omega;
        double u;
        double v;
        double a[3];
        uint8_t r;
        uint8_t k;

        if (!PunktPoprawny(punkt))
            continue;
        f = (double)punkt->czestotliwosc_hz;
        omega = 2.0 * M_PI * f;
        u = (f / f_ref_hz) * (f / f_ref_hz);
        v = ((double)cimagf(punkt->impedancja_ohm) / omega) / y_skala_h;
        if (!isfinite(u) || !isfinite(v))
            continue;

        a[0] = 1.0;
        a[1] = u;
        a[2] = u * v;
        for (r = 0U; r < 3U; ++r)
        {
            aty[r] += a[r] * v;
            for (k = 0U; k < 3U; ++k)
                ata[r][k] += a[r] * a[k];
        }
    }

    if (!Rozwiaz3x3(ata, aty, p) || !(p[2] > 1.0e-12))
        return false;

    memset(cauer, 0, sizeof(*cauer));
    cauer->liczba_elementow = 3U;
    cauer->wartosc[0] = -p[1] * y_skala_h / p[2];
    cauer->wartosc[2] = p[0] * y_skala_h - cauer->wartosc[0];
    cauer->wartosc[1] = p[2] / (omega_ref * omega_ref * cauer->wartosc[2]);

    return isfinite(cauer->wartosc[0]) && cauer->wartosc[0] > 0.0 &&
           isfinite(cauer->wartosc[1]) && cauer->wartosc[1] > 0.0 &&
           isfinite(cauer->wartosc[2]) && cauer->wartosc[2] > 0.0;
}

static void UlepszLCL(const SERIA_S11_t *seria, SYNTEZA_CF_CAUER_I_t *cauer)
{
    double koszt = KosztX(seria, cauer, NULL, NULL);
    double krok = DOPASOWANIE_CF_KROK_START;
    uint8_t iter;

    for (iter = 0U; iter < DOPASOWANIE_CF_ITERACJE; ++iter)
    {
        uint8_t parametr;
        bool poprawa = false;

        for (parametr = 0U; parametr < 3U; ++parametr)
        {
            const uint8_t indeks = parametr;
            int znak;
            for (znak = -1; znak <= 1; znak += 2)
            {
                SYNTEZA_CF_CAUER_I_t kandydat = *cauer;
                const double mnoznik = exp((double)znak * krok);
                double nowy_koszt;

                kandydat.wartosc[indeks] *= mnoznik;
                nowy_koszt = KosztX(seria, &kandydat, NULL, NULL);
                if (nowy_koszt < koszt)
                {
                    *cauer = kandydat;
                    koszt = nowy_koszt;
                    poprawa = true;
                }
            }
        }

        if (!poprawa)
            krok *= 0.55;
        if (krok < DOPASOWANIE_CF_KROK_STOP)
            break;
    }
}

bool DOPASOWANIE_CF_Dopasuj(const SERIA_S11_t *seria,
                            uint8_t liczba_elementow_cauer,
                            DOPASOWANIE_CF_WYNIK_t *wynik)
{
    double f_max_hz = 0.0;
    double rms_y_h = 0.0;
    double rms_x_pomiar_ohm = 0.0;
    double rms_z_ohm = 0.0;
    SYNTEZA_CF_STATUS_t status;
    uint16_t minimum;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));

    if (liczba_elementow_cauer != 1U && liczba_elementow_cauer != 3U)
    {
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_NIEIDENTYFIKOWALNY;
        return false;
    }

    wynik->liczba_punktow = PoliczPunkty(seria, &f_max_hz, &rms_y_h,
                                         &rms_x_pomiar_ohm, &wynik->rms_r_ohm,
                                         &rms_z_ohm);
    minimum = (liczba_elementow_cauer == 1U) ? DOPASOWANIE_CF_MIN_PUNKTOW_L
                                             : DOPASOWANIE_CF_MIN_PUNKTOW_LCL;
    if (wynik->liczba_punktow < minimum)
    {
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_MALO_PUNKTOW;
        return false;
    }

    if (liczba_elementow_cauer == 1U)
    {
        if (!InicjalizujL(seria, &wynik->cauer))
        {
            wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_NIEIDENTYFIKOWALNY;
            return false;
        }
    }
    else
    {
        if (!InicjalizujLCL(seria, f_max_hz, rms_y_h, &wynik->cauer))
        {
            wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_NIEIDENTYFIKOWALNY;
            return false;
        }
        UlepszLCL(seria, &wynik->cauer);
    }

    if (!SYNTEZA_CF_CauerNaFoster(&wynik->cauer, &wynik->foster, &status) ||
        status != SYNTEZA_CF_OK)
    {
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_BLAD_NUMERYCZNY;
        return false;
    }

    (void)KosztX(seria, &wynik->cauer, &wynik->blad_rms_x_ohm, NULL);
    wynik->blad_wzgledny_x = wynik->blad_rms_x_ohm /
                             fmax(rms_x_pomiar_ohm, DOPASOWANIE_CF_EPS);
    wynik->udzial_strat = wynik->rms_r_ohm /
                          fmax(rms_z_ohm, DOPASOWANIE_CF_EPS);
    if (!isfinite(wynik->blad_wzgledny_x) || !isfinite(wynik->udzial_strat))
    {
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_BLAD_NUMERYCZNY;
        return false;
    }

    if (wynik->blad_wzgledny_x > DOPASOWANIE_CF_MAX_BLAD_WZGLEDNY_X)
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_SLABE_DOPASOWANIE;
    if (wynik->udzial_strat > DOPASOWANIE_CF_MAX_UDZIAL_STRAT)
        wynik->jakosc |= DOPASOWANIE_CF_JAKOSC_STRATY_ISTOTNE;

    wynik->obliczony = true;
    wynik->zaakceptowany = (wynik->jakosc == DOPASOWANIE_CF_JAKOSC_OK);
    return true;
}
