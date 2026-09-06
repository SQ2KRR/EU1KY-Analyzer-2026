#include "metody_eksperymentalne.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool MET_InterpolujPrzeciecie(float f_a, float y_a, float f_b, float y_b,
                                    float poziom, float *f_wynik)
{
    const float dy = y_b - y_a;
    float udzial;

    if (f_wynik == NULL || !isfinite(f_a) || !isfinite(f_b) ||
        !isfinite(y_a) || !isfinite(y_b) || fabsf(dy) < 1.0e-9f)
        return false;

    udzial = (poziom - y_a) / dy;
    if (udzial < 0.0f || udzial > 1.0f)
        return false;

    *f_wynik = f_a + udzial * (f_b - f_a);
    return isfinite(*f_wynik);
}

bool MET_QMetoda3dB(const float *f_hz, const float *poziom_db, uint32_t liczba,
                    bool rezonans_jako_maksimum, MET_Q_3DB_WYNIK_t *wynik)
{
    uint32_t i;
    uint32_t indeks_szczytu = 0U;
    float szczyt;
    float prog;
    float f1 = NAN;
    float f2 = NAN;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->f0_hz = NAN;
    wynik->f1_hz = NAN;
    wynik->f2_hz = NAN;
    wynik->q = NAN;
    wynik->poziom_szczytu_db = NAN;

    if (f_hz == NULL || poziom_db == NULL || liczba < 5U)
        return false;

    szczyt = poziom_db[0];
    for (i = 1U; i < liczba; ++i)
    {
        if (!isfinite(f_hz[i]) || !isfinite(poziom_db[i]))
            continue;
        if ((rezonans_jako_maksimum && poziom_db[i] > szczyt) ||
            (!rezonans_jako_maksimum && poziom_db[i] < szczyt))
        {
            szczyt = poziom_db[i];
            indeks_szczytu = i;
        }
    }

    if (indeks_szczytu == 0U || indeks_szczytu + 1U >= liczba ||
        !isfinite(f_hz[indeks_szczytu]) || !isfinite(szczyt))
        return false;

    prog = szczyt + (rezonans_jako_maksimum ? -3.0f : 3.0f);

    for (i = indeks_szczytu; i > 0U; --i)
    {
        const float a = poziom_db[i - 1U];
        const float b = poziom_db[i];
        if ((rezonans_jako_maksimum && a <= prog && b >= prog) ||
            (!rezonans_jako_maksimum && a >= prog && b <= prog))
        {
            if (MET_InterpolujPrzeciecie(f_hz[i - 1U], a, f_hz[i], b, prog, &f1))
                break;
        }
    }

    for (i = indeks_szczytu; i + 1U < liczba; ++i)
    {
        const float a = poziom_db[i];
        const float b = poziom_db[i + 1U];
        if ((rezonans_jako_maksimum && a >= prog && b <= prog) ||
            (!rezonans_jako_maksimum && a <= prog && b >= prog))
        {
            if (MET_InterpolujPrzeciecie(f_hz[i], a, f_hz[i + 1U], b, prog, &f2))
                break;
        }
    }

    if (!isfinite(f1) || !isfinite(f2) || f2 <= f1 || f_hz[indeks_szczytu] <= 0.0f)
        return false;

    wynik->f0_hz = f_hz[indeks_szczytu];
    wynik->f1_hz = f1;
    wynik->f2_hz = f2;
    wynik->q = wynik->f0_hz / (f2 - f1);
    wynik->poziom_szczytu_db = szczyt;
    wynik->poprawny = isfinite(wynik->q) && wynik->q > 0.0f;
    return wynik->poprawny;
}


static bool MET_Rozwiaz4x4(double a[4][4], double b[4], double x[4])
{
    int i, j, k, pivot;
    double m[4][5];

    for (i = 0; i < 4; ++i)
    {
        for (j = 0; j < 4; ++j)
            m[i][j] = a[i][j];
        m[i][4] = b[i];
    }

    for (i = 0; i < 4; ++i)
    {
        double max_abs = fabs(m[i][i]);
        pivot = i;
        for (j = i + 1; j < 4; ++j)
        {
            const double v = fabs(m[j][i]);
            if (v > max_abs)
            {
                max_abs = v;
                pivot = j;
            }
        }
        if (max_abs < 1.0e-24)
            return false;
        if (pivot != i)
        {
            for (k = i; k < 5; ++k)
            {
                const double tmp = m[i][k];
                m[i][k] = m[pivot][k];
                m[pivot][k] = tmp;
            }
        }

        {
            const double d = m[i][i];
            for (k = i; k < 5; ++k)
                m[i][k] /= d;
        }
        for (j = 0; j < 4; ++j)
        {
            double f;
            if (j == i)
                continue;
            f = m[j][i];
            for (k = i; k < 5; ++k)
                m[j][k] -= f * m[i][k];
        }
    }

    for (i = 0; i < 4; ++i)
    {
        x[i] = m[i][4];
        if (!isfinite(x[i]))
            return false;
    }
    return true;
}

bool MET_QDopasujLorentza(const float *f_hz, const float *amplituda, uint32_t liczba,
                           MET_Q_LORENTZ_WYNIK_t *wynik)
{
    uint32_t i;
    uint32_t indeks_max = 0U;
    float y_min = INFINITY;
    float y_max = -INFINITY;
    float f1 = NAN, f2 = NAN;
    double p[4]; /* tlo, amplituda, f0, gamma */
    double lambda = 1.0e-3;
    double poprzedni_blad = DBL_MAX;
    int iter;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->f0_hz = NAN;
    wynik->szerokosc_3db_hz = NAN;
    wynik->q = NAN;
    wynik->blad_rms = NAN;

    if (f_hz == NULL || amplituda == NULL || liczba < 7U)
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        if (!isfinite(f_hz[i]) || !isfinite(amplituda[i]) || amplituda[i] < 0.0f)
            continue;
        if (amplituda[i] < y_min)
            y_min = amplituda[i];
        if (amplituda[i] > y_max)
        {
            y_max = amplituda[i];
            indeks_max = i;
        }
    }
    if (!isfinite(y_min) || !isfinite(y_max) || !(y_max > y_min) ||
        indeks_max == 0U || indeks_max + 1U >= liczba)
        return false;

    {
        const float polowa = y_min + 0.5f * (y_max - y_min);
        for (i = indeks_max; i > 0U; --i)
        {
            if (amplituda[i - 1U] <= polowa && amplituda[i] >= polowa &&
                MET_InterpolujPrzeciecie(f_hz[i - 1U], amplituda[i - 1U],
                                         f_hz[i], amplituda[i], polowa, &f1))
                break;
        }
        for (i = indeks_max; i + 1U < liczba; ++i)
        {
            if (amplituda[i] >= polowa && amplituda[i + 1U] <= polowa &&
                MET_InterpolujPrzeciecie(f_hz[i], amplituda[i],
                                         f_hz[i + 1U], amplituda[i + 1U], polowa, &f2))
                break;
        }
    }

    p[0] = y_min;
    p[1] = y_max - y_min;
    p[2] = f_hz[indeks_max];
    if (isfinite(f1) && isfinite(f2) && f2 > f1)
        p[3] = 0.5 * ((double)f2 - (double)f1);
    else
        p[3] = fmax(1.0, ((double)f_hz[liczba - 1U] - (double)f_hz[0]) / 10.0);

    for (iter = 0; iter < 24; ++iter)
    {
        double h[4][4] = {{0}};
        double g[4] = {0};
        double blad = 0.0;
        uint32_t n = 0U;
        double delta[4];
        double kandydat[4];
        double blad_kand = 0.0;
        uint32_t nk = 0U;

        for (i = 0U; i < liczba; ++i)
        {
            double f, y, t0, den, model, r;
            double j[4];
            int a, b;
            if (!isfinite(f_hz[i]) || !isfinite(amplituda[i]) || amplituda[i] < 0.0f)
                continue;
            f = f_hz[i];
            y = amplituda[i];
            t0 = (f - p[2]) / p[3];
            den = 1.0 + t0 * t0;
            model = p[0] + p[1] / den;
            r = y - model;
            j[0] = 1.0;
            j[1] = 1.0 / den;
            j[2] = p[1] * (2.0 * t0) / (p[3] * den * den);
            j[3] = p[1] * (2.0 * t0 * t0) / (p[3] * den * den);
            blad += r * r;
            ++n;
            for (a = 0; a < 4; ++a)
            {
                g[a] += j[a] * r;
                for (b = 0; b < 4; ++b)
                    h[a][b] += j[a] * j[b];
            }
        }
        if (n < 7U)
            return false;
        for (i = 0U; i < 4U; ++i)
            h[i][i] *= (1.0 + lambda);
        if (!MET_Rozwiaz4x4(h, g, delta))
            return false;

        for (i = 0U; i < 4U; ++i)
            kandydat[i] = p[i] + delta[i];
        if (!(kandydat[1] > 0.0) || !(kandydat[3] > 0.0) ||
            !isfinite(kandydat[2]))
        {
            lambda *= 10.0;
            continue;
        }

        for (i = 0U; i < liczba; ++i)
        {
            double tt, den, model, r;
            if (!isfinite(f_hz[i]) || !isfinite(amplituda[i]) || amplituda[i] < 0.0f)
                continue;
            tt = ((double)f_hz[i] - kandydat[2]) / kandydat[3];
            den = 1.0 + tt * tt;
            model = kandydat[0] + kandydat[1] / den;
            r = (double)amplituda[i] - model;
            blad_kand += r * r;
            ++nk;
        }
        if (nk == 0U)
            return false;

        if (blad_kand < blad)
        {
            memcpy(p, kandydat, sizeof(p));
            lambda *= 0.35;
            if (lambda < 1.0e-9)
                lambda = 1.0e-9;
            if (fabs(poprzedni_blad - blad_kand) < 1.0e-12 * (1.0 + blad_kand))
                break;
            poprzedni_blad = blad_kand;
        }
        else
        {
            lambda *= 8.0;
            if (lambda > 1.0e12)
                break;
        }
    }

    if (!(p[1] > 0.0) || !(p[2] > 0.0) || !(p[3] > 0.0))
        return false;

    {
        double blad = 0.0;
        uint32_t n = 0U;
        for (i = 0U; i < liczba; ++i)
        {
            double tt, model, r;
            if (!isfinite(f_hz[i]) || !isfinite(amplituda[i]) || amplituda[i] < 0.0f)
                continue;
            tt = ((double)f_hz[i] - p[2]) / p[3];
            model = p[0] + p[1] / (1.0 + tt * tt);
            r = (double)amplituda[i] - model;
            blad += r * r;
            ++n;
        }
        if (n == 0U)
            return false;
        wynik->blad_rms = (float)sqrt(blad / (double)n);
    }

    wynik->f0_hz = (float)p[2];
    wynik->szerokosc_3db_hz = (float)(2.0 * p[3]);
    wynik->q = wynik->f0_hz / wynik->szerokosc_3db_hz;
    wynik->amplituda = (float)p[1];
    wynik->tlo = (float)p[0];
    wynik->poprawny = isfinite(wynik->q) && wynik->q > 0.0f;
    return wynik->poprawny;
}

static bool MET_Rozwiaz2x2(double aa, double ab, double bb, double ay, double by,
                           double *x0, double *x1)
{
    const double det = aa * bb - ab * ab;
    if (x0 == NULL || x1 == NULL || fabs(det) < 1.0e-30)
        return false;
    *x0 = (ay * bb - by * ab) / det;
    *x1 = (aa * by - ab * ay) / det;
    return isfinite(*x0) && isfinite(*x1);
}

bool MET_DopasujRLC(const float *f_hz, const float *r_ohm, const float *x_ohm,
                    const uint8_t *poprawny, uint32_t liczba,
                    MET_MODEL_RLC_t model, MET_RLC_WYNIK_t *wynik)
{
    uint32_t i;
    uint32_t n = 0U;
    double suma_r = 0.0;
    double aa = 0.0, ab = 0.0, bb = 0.0, ay = 0.0, by = 0.0;
    double p0 = 0.0, p1 = 0.0;
    double r_model;
    double l_h;
    double c_f;
    double f0;
    double q;
    double blad2 = 0.0;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->model = model;

    if (f_hz == NULL || r_ohm == NULL || x_ohm == NULL || liczba < 3U)
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        double f, w, a, b, y;
        double r = r_ohm[i];
        double x = x_ohm[i];
        if (poprawny != NULL && !poprawny[i])
            continue;
        if (!isfinite(f_hz[i]) || !isfinite(r_ohm[i]) || !isfinite(x_ohm[i]) || f_hz[i] <= 0.0f)
            continue;

        f = f_hz[i];
        w = 2.0 * M_PI * f;
        if (model == MET_MODEL_RLC_SZEREGOWY)
        {
            suma_r += r;
            a = w;
            b = -1.0 / w;
            y = x;
        }
        else
        {
            const double d = r * r + x * x;
            double g, susceptancja;
            if (d <= DBL_MIN)
                continue;
            g = r / d;
            susceptancja = -x / d;
            suma_r += g;
            a = w;
            b = -1.0 / w;
            y = susceptancja;
        }
        aa += a * a;
        ab += a * b;
        bb += b * b;
        ay += a * y;
        by += b * y;
        ++n;
    }

    if (n < 3U || !MET_Rozwiaz2x2(aa, ab, bb, ay, by, &p0, &p1))
        return false;

    if (model == MET_MODEL_RLC_SZEREGOWY)
    {
        r_model = suma_r / (double)n;
        l_h = p0;
        c_f = (p1 > 0.0) ? 1.0 / p1 : NAN;
    }
    else
    {
        const double g = suma_r / (double)n;
        r_model = (g > 0.0) ? 1.0 / g : NAN;
        c_f = p0;
        l_h = (p1 > 0.0) ? 1.0 / p1 : NAN;
    }

    if (!(r_model > 0.0) || !(l_h > 0.0) || !(c_f > 0.0) ||
        !isfinite(r_model) || !isfinite(l_h) || !isfinite(c_f))
        return false;

    f0 = 1.0 / (2.0 * M_PI * sqrt(l_h * c_f));
    if (model == MET_MODEL_RLC_SZEREGOWY)
        q = 2.0 * M_PI * f0 * l_h / r_model;
    else
        q = 2.0 * M_PI * f0 * r_model * c_f;

    for (i = 0U; i < liczba; ++i)
    {
        double f, w, zr, zx, dr, dx;
        if (poprawny != NULL && !poprawny[i])
            continue;
        if (!isfinite(f_hz[i]) || !isfinite(r_ohm[i]) || !isfinite(x_ohm[i]) || f_hz[i] <= 0.0f)
            continue;
        f = f_hz[i];
        w = 2.0 * M_PI * f;
        if (model == MET_MODEL_RLC_SZEREGOWY)
        {
            zr = r_model;
            zx = w * l_h - 1.0 / (w * c_f);
        }
        else
        {
            const double g = 1.0 / r_model;
            const double b = w * c_f - 1.0 / (w * l_h);
            const double d = g * g + b * b;
            zr = g / d;
            zx = -b / d;
        }
        dr = (double)r_ohm[i] - zr;
        dx = (double)x_ohm[i] - zx;
        blad2 += dr * dr + dx * dx;
    }

    wynik->r_ohm = (float)r_model;
    wynik->l_h = (float)l_h;
    wynik->c_f = (float)c_f;
    wynik->f0_hz = (float)f0;
    wynik->q = (float)q;
    wynik->blad_rms_ohm = (float)sqrt(blad2 / (double)n);
    wynik->poprawny = isfinite(wynik->q) && wynik->q > 0.0f && isfinite(wynik->blad_rms_ohm);
    return wynik->poprawny;
}

void MET_SavitzkyGolay5(const float *wejscie, float *wyjscie, uint32_t liczba)
{
    uint32_t i;
    if (wejscie == NULL || wyjscie == NULL || liczba == 0U)
        return;
    if (liczba < 5U)
    {
        for (i = 0U; i < liczba; ++i)
            wyjscie[i] = wejscie[i];
        return;
    }

    wyjscie[0] = wejscie[0];
    wyjscie[1] = wejscie[1];
    for (i = 2U; i + 2U < liczba; ++i)
    {
        if (!isfinite(wejscie[i - 2U]) || !isfinite(wejscie[i - 1U]) ||
            !isfinite(wejscie[i]) || !isfinite(wejscie[i + 1U]) || !isfinite(wejscie[i + 2U]))
        {
            wyjscie[i] = wejscie[i];
            continue;
        }
        wyjscie[i] = (-3.0f * wejscie[i - 2U] + 12.0f * wejscie[i - 1U] +
                       17.0f * wejscie[i] + 12.0f * wejscie[i + 1U] -
                       3.0f * wejscie[i + 2U]) / 35.0f;
    }
    wyjscie[liczba - 2U] = wejscie[liczba - 2U];
    wyjscie[liczba - 1U] = wejscie[liczba - 1U];
}

static float MET_BesselI0(float x)
{
    float suma = 1.0f;
    float wyraz = 1.0f;
    float polowa = x * 0.5f;
    uint32_t k;

    for (k = 1U; k <= 12U; ++k)
    {
        const float d = polowa / (float)k;
        wyraz *= d * d;
        suma += wyraz;
        if (wyraz < 1.0e-7f * suma)
            break;
    }
    return suma;
}

float MET_WspolczynnikOkna(MET_OKNO_t okno, uint32_t indeks, uint32_t liczba, float beta)
{
    float faza;
    if (liczba <= 1U || indeks >= liczba)
        return 0.0f;

    faza = 2.0f * (float)M_PI * (float)indeks / (float)(liczba - 1U);
    switch (okno)
    {
    case MET_OKNO_HANN:
        return 0.5f - 0.5f * cosf(faza);
    case MET_OKNO_HAMMING:
        return 0.54f - 0.46f * cosf(faza);
    case MET_OKNO_BLACKMAN_HARRIS:
        return 0.35875f - 0.48829f * cosf(faza) +
               0.14128f * cosf(2.0f * faza) - 0.01168f * cosf(3.0f * faza);
    case MET_OKNO_KAISER:
    {
        const float n = 2.0f * (float)indeks / (float)(liczba - 1U) - 1.0f;
        const float pod_pierwiastkiem = fmaxf(0.0f, 1.0f - n * n);
        if (beta < 0.0f)
            beta = 0.0f;
        return MET_BesselI0(beta * sqrtf(pod_pierwiastkiem)) / MET_BesselI0(beta);
    }
    case MET_OKNO_PROSTOKATNE:
    default:
        return 1.0f;
    }
}

void MET_Kalman1DInit(MET_KALMAN_1D_t *filtr, float szum_procesu, float szum_pomiaru)
{
    if (filtr == NULL)
        return;
    memset(filtr, 0, sizeof(*filtr));
    filtr->wariancja = 1.0f;
    filtr->szum_procesu = fmaxf(szum_procesu, 1.0e-12f);
    filtr->szum_pomiaru = fmaxf(szum_pomiaru, 1.0e-12f);
}

float MET_Kalman1DAktualizuj(MET_KALMAN_1D_t *filtr, float pomiar)
{
    float wzmocnienie;
    if (filtr == NULL || !isfinite(pomiar))
        return pomiar;

    if (!filtr->zainicjalizowany)
    {
        filtr->stan = pomiar;
        filtr->zainicjalizowany = true;
        return filtr->stan;
    }

    filtr->wariancja += filtr->szum_procesu;
    wzmocnienie = filtr->wariancja / (filtr->wariancja + filtr->szum_pomiaru);
    filtr->stan += wzmocnienie * (pomiar - filtr->stan);
    filtr->wariancja *= (1.0f - wzmocnienie);
    return filtr->stan;
}
