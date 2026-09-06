#include "q_okrag.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define Q_OKRAG_EPS 1.0e-12
#define Q_OKRAG_KOSZT_NIEPOPRAWNY 1.0e100
#define Q_OKRAG_KROKI_FAZY 121U
#define Q_OKRAG_ITERACJE_ZLOTY_PODZIAL 36U
#define Q_OKRAG_PS_NA_SEKUNDE 1000000000000.0

/* Bufory są statyczne, bo metoda ma pracować także na urządzeniu bez
 * dynamicznej alokacji. Moduł nie jest wywoływany z przerwania. */
static double q_okrag_f[Q_OKRAG_MAX_PUNKTOW];
static double q_okrag_x[Q_OKRAG_MAX_PUNKTOW];
static double q_okrag_y[Q_OKRAG_MAX_PUNKTOW];
static double q_okrag_faza[Q_OKRAG_MAX_PUNKTOW];

static bool Q_OKRAG_Rozwiaz3x3(double a[3][3], double b[3], double x[3])
{
    double m[3][4];
    int i, j, k;

    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 3; ++j)
            m[i][j] = a[i][j];
        m[i][3] = b[i];
    }

    for (i = 0; i < 3; ++i)
    {
        int pivot = i;
        double maksimum = fabs(m[i][i]);
        for (j = i + 1; j < 3; ++j)
        {
            const double v = fabs(m[j][i]);
            if (v > maksimum)
            {
                maksimum = v;
                pivot = j;
            }
        }
        if (!(maksimum > 1.0e-24) || !isfinite(maksimum))
            return false;
        if (pivot != i)
        {
            for (k = i; k < 4; ++k)
            {
                const double t = m[i][k];
                m[i][k] = m[pivot][k];
                m[pivot][k] = t;
            }
        }

        {
            const double d = m[i][i];
            for (k = i; k < 4; ++k)
                m[i][k] /= d;
        }
        for (j = 0; j < 3; ++j)
        {
            double mnoznik;
            if (j == i)
                continue;
            mnoznik = m[j][i];
            for (k = i; k < 4; ++k)
                m[j][k] -= mnoznik * m[i][k];
        }
    }

    for (i = 0; i < 3; ++i)
    {
        x[i] = m[i][3];
        if (!isfinite(x[i]))
            return false;
    }
    return true;
}

static void Q_OKRAG_WyczyscWynik(Q_OKRAG_WYNIK_t *wynik)
{
    memset(wynik, 0, sizeof(*wynik));
    wynik->f0_hz = NAN;
    wynik->q_obciazone = NAN;
    wynik->srodek = NAN + I * NAN;
    wynik->promien = NAN;
    wynik->luk_rad = NAN;
    wynik->blad_okregu_rms = NAN;
    wynik->blad_okregu_wzgledny = NAN;
    wynik->blad_fazy_rms_rad = NAN;
    wynik->faza_rezonansu_rad = NAN;
}

static uint16_t Q_OKRAG_PobierzPunkty(const SERIA_S11_t *seria, int32_t opoznienie_ps, uint32_t *jakosc)
{
    uint16_t i;
    uint16_t n = 0U;
    double poprzednia_f = -1.0;

    if (seria == NULL || seria->punkty == NULL)
        return 0U;

    for (i = 0U; i < seria->liczba && n < Q_OKRAG_MAX_PUNKTOW; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double x = (double)crealf(p->gamma);
        double y = (double)cimagf(p->gamma);
        const double f = (double)p->czestotliwosc_hz;

        if ((p->flagi & POMIAR_S11_FLAGA_POPRAWNY) == 0U ||
            !(f > 0.0) || !isfinite(x) || !isfinite(y))
            continue;

        if (opoznienie_ps != 0)
        {
            const double tau_s = (double)opoznienie_ps / Q_OKRAG_PS_NA_SEKUNDE;
            const double kat = 4.0 * M_PI * f * tau_s;
            const double c = cos(kat);
            const double si = sin(kat);
            const double xr = x * c - y * si;
            const double yr = x * si + y * c;
            x = xr;
            y = yr;
        }

        if (n > 0U && !(f > poprzednia_f))
        {
            if (jakosc != NULL)
                *jakosc |= Q_OKRAG_JAKOSC_ZLA_OS_CZESTOTLIWOSCI;
            return 0U;
        }

        q_okrag_f[n] = f;
        q_okrag_x[n] = x;
        q_okrag_y[n] = y;
        poprzednia_f = f;
        ++n;
    }
    return n;
}

static bool Q_OKRAG_DopasujGeometrie(uint16_t n, double *cx, double *cy,
                                     double *promien, double *blad_rms)
{
    double a[3][3] = {{0.0}};
    double b[3] = {0.0, 0.0, 0.0};
    double rozwiazanie[3];
    double suma_bledu2 = 0.0;
    uint16_t i;

    for (i = 0U; i < n; ++i)
    {
        const double x = q_okrag_x[i];
        const double y = q_okrag_y[i];
        const double r2 = x * x + y * y;

        a[0][0] += x * x;
        a[0][1] += x * y;
        a[0][2] += x;
        a[1][0] += x * y;
        a[1][1] += y * y;
        a[1][2] += y;
        a[2][0] += x;
        a[2][1] += y;
        a[2][2] += 1.0;

        b[0] -= x * r2;
        b[1] -= y * r2;
        b[2] -= r2;
    }

    if (!Q_OKRAG_Rozwiaz3x3(a, b, rozwiazanie))
        return false;

    *cx = -0.5 * rozwiazanie[0];
    *cy = -0.5 * rozwiazanie[1];
    {
        const double r2 = (*cx) * (*cx) + (*cy) * (*cy) - rozwiazanie[2];
        if (!(r2 > Q_OKRAG_EPS) || !isfinite(r2))
            return false;
        *promien = sqrt(r2);
    }

    for (i = 0U; i < n; ++i)
    {
        const double dx = q_okrag_x[i] - *cx;
        const double dy = q_okrag_y[i] - *cy;
        const double odleglosc = hypot(dx, dy);
        const double blad = odleglosc - *promien;
        suma_bledu2 += blad * blad;
    }
    *blad_rms = sqrt(suma_bledu2 / (double)n);
    return isfinite(*blad_rms);
}

static bool Q_OKRAG_PrzygotujFaze(uint16_t n, double cx, double cy,
                                  double *luk, int *kierunek)
{
    uint16_t i;
    double poprzednia_surowa;

    if (n < 2U || luk == NULL || kierunek == NULL)
        return false;

    poprzednia_surowa = atan2(q_okrag_y[0] - cy, q_okrag_x[0] - cx);
    q_okrag_faza[0] = poprzednia_surowa;

    for (i = 1U; i < n; ++i)
    {
        const double surowa = atan2(q_okrag_y[i] - cy, q_okrag_x[i] - cx);
        double delta = surowa - poprzednia_surowa;
        while (delta > M_PI)
            delta -= 2.0 * M_PI;
        while (delta < -M_PI)
            delta += 2.0 * M_PI;
        q_okrag_faza[i] = q_okrag_faza[i - 1U] + delta;
        poprzednia_surowa = surowa;
    }

    *kierunek = q_okrag_faza[n - 1U] >= q_okrag_faza[0] ? 1 : -1;
    if (*kierunek < 0)
    {
        for (i = 0U; i < n; ++i)
            q_okrag_faza[i] = -q_okrag_faza[i];
    }

    *luk = q_okrag_faza[n - 1U] - q_okrag_faza[0];
    return isfinite(*luk) && *luk > 0.0;
}

typedef struct
{
    bool poprawny;
    double koszt;
    double f0;
    double q;
    double theta0;
    double nachylenie;
} Q_OKRAG_FIT_FAZY_t;

static Q_OKRAG_FIT_FAZY_t Q_OKRAG_OcenTheta0(uint16_t n, double theta0)
{
    Q_OKRAG_FIT_FAZY_t w;
    double srednia_f = 0.0;
    double srednia_t = 0.0;
    double sff = 0.0;
    double sft = 0.0;
    double suma_bledu2 = 0.0;
    uint16_t i;

    memset(&w, 0, sizeof(w));
    w.koszt = Q_OKRAG_KOSZT_NIEPOPRAWNY;

    for (i = 0U; i < n; ++i)
        srednia_f += q_okrag_f[i];
    srednia_f /= (double)n;

    for (i = 0U; i < n; ++i)
    {
        const double polowa = 0.5 * (q_okrag_faza[i] - theta0);
        const double c = cos(polowa);
        double t;
        if (fabs(c) < 1.0e-6)
            return w;
        t = tan(polowa);
        if (!isfinite(t))
            return w;
        srednia_t += t;
    }
    srednia_t /= (double)n;

    for (i = 0U; i < n; ++i)
    {
        const double polowa = 0.5 * (q_okrag_faza[i] - theta0);
        const double t = tan(polowa);
        const double df = q_okrag_f[i] - srednia_f;
        sff += df * df;
        sft += df * (t - srednia_t);
    }
    if (!(sff > Q_OKRAG_EPS))
        return w;

    w.nachylenie = sft / sff;
    if (!(w.nachylenie > 0.0) || !isfinite(w.nachylenie))
        return w;

    w.f0 = srednia_f - srednia_t / w.nachylenie;
    if (!(w.f0 > q_okrag_f[0] && w.f0 < q_okrag_f[n - 1U]))
        return w;

    w.q = 0.5 * w.nachylenie * w.f0;
    if (!(w.q > 0.0) || !isfinite(w.q))
        return w;

    for (i = 0U; i < n; ++i)
    {
        const double przewidywana = theta0 +
            2.0 * atan(w.nachylenie * (q_okrag_f[i] - w.f0));
        const double blad = q_okrag_faza[i] - przewidywana;
        suma_bledu2 += blad * blad;
    }

    w.koszt = sqrt(suma_bledu2 / (double)n);
    w.theta0 = theta0;
    w.poprawny = isfinite(w.koszt);
    return w;
}

static Q_OKRAG_FIT_FAZY_t Q_OKRAG_DopasujFaze(uint16_t n)
{
    Q_OKRAG_FIT_FAZY_t najlepszy;
    const double zloty = 0.6180339887498948482;
    const double poczatek = q_okrag_faza[0];
    const double koniec = q_okrag_faza[n - 1U];
    const double luk = koniec - poczatek;
    double zakres_a = poczatek + 0.01 * luk;
    double zakres_b = koniec - 0.01 * luk;
    double krok;
    uint16_t indeks_najlepszy = 0U;
    uint16_t j;

    memset(&najlepszy, 0, sizeof(najlepszy));
    najlepszy.koszt = Q_OKRAG_KOSZT_NIEPOPRAWNY;
    if (!(zakres_b > zakres_a))
        return najlepszy;

    krok = (zakres_b - zakres_a) / (double)(Q_OKRAG_KROKI_FAZY - 1U);
    for (j = 0U; j < Q_OKRAG_KROKI_FAZY; ++j)
    {
        const double theta0 = zakres_a + (double)j * krok;
        const Q_OKRAG_FIT_FAZY_t kandydat = Q_OKRAG_OcenTheta0(n, theta0);
        if (kandydat.poprawny && kandydat.koszt < najlepszy.koszt)
        {
            najlepszy = kandydat;
            indeks_najlepszy = j;
        }
    }
    if (!najlepszy.poprawny)
        return najlepszy;

    {
        double a = zakres_a + (double)(indeks_najlepszy > 0U ? indeks_najlepszy - 1U : 0U) * krok;
        double b = zakres_a + (double)(indeks_najlepszy + 1U < Q_OKRAG_KROKI_FAZY ? indeks_najlepszy + 1U : Q_OKRAG_KROKI_FAZY - 1U) * krok;
        double x1 = b - zloty * (b - a);
        double x2 = a + zloty * (b - a);
        Q_OKRAG_FIT_FAZY_t w1 = Q_OKRAG_OcenTheta0(n, x1);
        Q_OKRAG_FIT_FAZY_t w2 = Q_OKRAG_OcenTheta0(n, x2);
        uint16_t iter;

        for (iter = 0U; iter < Q_OKRAG_ITERACJE_ZLOTY_PODZIAL; ++iter)
        {
            const double k1 = w1.poprawny ? w1.koszt : Q_OKRAG_KOSZT_NIEPOPRAWNY;
            const double k2 = w2.poprawny ? w2.koszt : Q_OKRAG_KOSZT_NIEPOPRAWNY;
            if (k1 < k2)
            {
                b = x2;
                x2 = x1;
                w2 = w1;
                x1 = b - zloty * (b - a);
                w1 = Q_OKRAG_OcenTheta0(n, x1);
            }
            else
            {
                a = x1;
                x1 = x2;
                w1 = w2;
                x2 = a + zloty * (b - a);
                w2 = Q_OKRAG_OcenTheta0(n, x2);
            }
        }
        if (w1.poprawny && w1.koszt < najlepszy.koszt)
            najlepszy = w1;
        if (w2.poprawny && w2.koszt < najlepszy.koszt)
            najlepszy = w2;
    }
    return najlepszy;
}

bool Q_OKRAG_DopasujZOpoznieniem(const SERIA_S11_t *seria, int32_t opoznienie_ps,
                                  Q_OKRAG_WYNIK_t *wynik)
{
    uint16_t n;
    double cx, cy, promien, blad_okregu;
    double luk;
    int kierunek;
    Q_OKRAG_FIT_FAZY_t faza;
    const double margines = Q_OKRAG_MARGINES_REZONANSU;
    double szerokosc;

    if (wynik == NULL)
        return false;
    Q_OKRAG_WyczyscWynik(wynik);

    wynik->opoznienie_ps = opoznienie_ps;
    n = Q_OKRAG_PobierzPunkty(seria, opoznienie_ps, &wynik->jakosc);
    wynik->liczba_punktow = n;
    if (n < Q_OKRAG_MIN_PUNKTOW)
    {
        wynik->jakosc |= Q_OKRAG_JAKOSC_MALO_PUNKTOW;
        return false;
    }

    if (!Q_OKRAG_DopasujGeometrie(n, &cx, &cy, &promien, &blad_okregu))
    {
        wynik->jakosc |= Q_OKRAG_JAKOSC_ZDEGENEROWANY_OKRAG;
        return false;
    }

    wynik->srodek = (float)cx + I * (float)cy;
    wynik->promien = (float)promien;
    wynik->blad_okregu_rms = (float)blad_okregu;
    wynik->blad_okregu_wzgledny = (float)(blad_okregu / promien);
    if (wynik->blad_okregu_wzgledny > Q_OKRAG_MAX_BLAD_OKREGU_WZGL)
        wynik->jakosc |= Q_OKRAG_JAKOSC_BLAD_OKREGU;

    if (!Q_OKRAG_PrzygotujFaze(n, cx, cy, &luk, &kierunek))
    {
        wynik->jakosc |= Q_OKRAG_JAKOSC_ZDEGENEROWANY_OKRAG;
        return false;
    }
    wynik->luk_rad = (float)luk;
    if (luk < (double)Q_OKRAG_MIN_LUK_RAD)
        wynik->jakosc |= Q_OKRAG_JAKOSC_MALY_LUK;

    faza = Q_OKRAG_DopasujFaze(n);
    if (!faza.poprawny)
    {
        wynik->jakosc |= Q_OKRAG_JAKOSC_BLAD_FAZY;
        return false;
    }

    wynik->f0_hz = (float)faza.f0;
    wynik->q_obciazone = (float)faza.q;
    wynik->blad_fazy_rms_rad = (float)faza.koszt;
    wynik->faza_rezonansu_rad = (float)(kierunek > 0 ? faza.theta0 : -faza.theta0);
    if (wynik->blad_fazy_rms_rad > Q_OKRAG_MAX_BLAD_FAZY_RMS_RAD)
        wynik->jakosc |= Q_OKRAG_JAKOSC_BLAD_FAZY;

    szerokosc = q_okrag_f[n - 1U] - q_okrag_f[0];
    if (!(szerokosc > 0.0))
    {
        wynik->jakosc |= Q_OKRAG_JAKOSC_ZLA_OS_CZESTOTLIWOSCI;
        return false;
    }
    if (faza.f0 <= q_okrag_f[0] + margines * szerokosc ||
        faza.f0 >= q_okrag_f[n - 1U] - margines * szerokosc)
        wynik->jakosc |= Q_OKRAG_JAKOSC_REZONANS_PRZY_BRZEGU;

    wynik->obliczony = true;
    wynik->wiarygodny = wynik->jakosc == Q_OKRAG_JAKOSC_OK;
    return true;
}


bool Q_OKRAG_Dopasuj(const SERIA_S11_t *seria, Q_OKRAG_WYNIK_t *wynik)
{
    return Q_OKRAG_DopasujZOpoznieniem(seria, 0, wynik);
}
