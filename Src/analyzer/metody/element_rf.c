#include "element_rf.h"

#include <complex.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#define ELEMENT_RF_PI 3.14159265358979323846f
#define ELEMENT_RF_MIN_PUNKTOW 5U
#define ELEMENT_RF_EPS 1.0e-20f
#define METROLOGIA_MIN_AUTOMAT_HZ 500000U

static bool PunktPoprawny(const POMIAR_S11_t *p)
{
    if (p == NULL || (p->flagi & POMIAR_S11_FLAGA_POPRAWNY) == 0U || p->czestotliwosc_hz == 0U)
        return false;
    if (p->czestotliwosc_hz < METROLOGIA_MIN_AUTOMAT_HZ)
        return false;
    return isfinite(crealf(p->impedancja_ohm)) && isfinite(cimagf(p->impedancja_ohm));
}

static float InterpolujPrzejsciePrzezZero(const SERIA_S11_t *seria, ELEMENT_RF_TYP_t typ)
{
    uint16_t i;
    bool znaleziono = false;
    float najlepsza_f = NAN;
    float najlepsza_miara = typ == ELEMENT_RF_CEWKA ? -INFINITY : INFINITY;

    if (seria == NULL || seria->punkty == NULL)
        return NAN;

    /*
     * Punkty kampanii i niektórych skanów nie muszą leżeć w tablicy rosnąco
     * po częstotliwości. Dla każdego poprawnego punktu po właściwej stronie
     * rezonansu znajdujemy więc najbliższy poprawny punkt o wyższej
     * częstotliwości. To daje pary sąsiednie w sensie częstotliwości bez
     * kopiowania i sortowania całej serii.
     */
    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p0 = &seria->punkty[i];
        const POMIAR_S11_t *p1 = NULL;
        uint16_t j;
        float f0, f1, x0, x1;
        float modul0, modul1, miara, f_przejscia;

        if (!PunktPoprawny(p0))
            continue;
        f0 = (float)p0->czestotliwosc_hz;
        x0 = cimagf(p0->impedancja_ohm);
        if ((typ == ELEMENT_RF_CEWKA && !(x0 > 0.0f)) ||
            (typ == ELEMENT_RF_KONDENSATOR && !(x0 < 0.0f)))
            continue;

        for (j = 0U; j < seria->liczba; ++j)
        {
            const POMIAR_S11_t *kandydat = &seria->punkty[j];
            if (!PunktPoprawny(kandydat) || kandydat->czestotliwosc_hz <= p0->czestotliwosc_hz)
                continue;
            if (p1 == NULL || kandydat->czestotliwosc_hz < p1->czestotliwosc_hz)
                p1 = kandydat;
        }
        if (p1 == NULL)
            continue;

        f1 = (float)p1->czestotliwosc_hz;
        x1 = cimagf(p1->impedancja_ohm);
        if ((typ == ELEMENT_RF_CEWKA && !(x1 <= 0.0f)) ||
            (typ == ELEMENT_RF_KONDENSATOR && !(x1 >= 0.0f)))
            continue;

        modul0 = cabsf(p0->impedancja_ohm);
        modul1 = cabsf(p1->impedancja_ohm);
        if (!isfinite(modul0) || !isfinite(modul1))
            continue;
        if (fabsf(crealf(p0->impedancja_ohm)) + fabsf(crealf(p1->impedancja_ohm)) < 1.0e-9f)
            continue;

        /* Cewka z pojemnością pasożytniczą ma przy rezonansie równoległym
         * maksimum |Z|, a kondensator z ESL przy rezonansie szeregowym ma
         * minimum |Z|. Dzięki temu nie wybieramy pierwszego przypadkowego
         * przejścia X przez zero na dole pasma. */
        miara = 0.5f * (modul0 + modul1);
        if (znaleziono &&
            ((typ == ELEMENT_RF_CEWKA && miara <= najlepsza_miara) ||
             (typ == ELEMENT_RF_KONDENSATOR && miara >= najlepsza_miara)))
            continue;

        if (fabsf(x1 - x0) > 1.0e-12f)
            f_przejscia = f0 + (f1 - f0) * (-x0) / (x1 - x0);
        else
            f_przejscia = 0.5f * (f0 + f1);

        if (!isfinite(f_przejscia) || f_przejscia < (float)METROLOGIA_MIN_AUTOMAT_HZ)
            continue;

        najlepsza_f = f_przejscia;
        najlepsza_miara = miara;
        znaleziono = true;
    }

    return znaleziono ? najlepsza_f : NAN;
}

static bool ModelCewki(float r, float l, float cp, float f, float *zr, float *zx)
{
    const float w = 2.0f * ELEMENT_RF_PI * f;
    const float complex z_rl = r + I * (w * l);
    float complex y;
    float complex z;

    if (!(r > 0.0f) || !(l > 0.0f) || !(cp > 0.0f) || !(f > 0.0f))
        return false;
    if (cabsf(z_rl) < 1.0e-12f)
        return false;
    y = 1.0f / z_rl + I * (w * cp);
    if (cabsf(y) < 1.0e-12f)
        return false;
    z = 1.0f / y;
    if (!isfinite(crealf(z)) || !isfinite(cimagf(z)))
        return false;
    *zr = crealf(z);
    *zx = cimagf(z);
    return true;
}

static bool ModelKondensatora(float r, float c, float esl, float f, float *zr, float *zx)
{
    const float w = 2.0f * ELEMENT_RF_PI * f;
    float x;
    if (!(r >= 0.0f) || !(c > 0.0f) || !(esl >= 0.0f) || !(f > 0.0f))
        return false;
    x = w * esl - 1.0f / (w * c);
    if (!isfinite(x))
        return false;
    *zr = r;
    *zx = x;
    return true;
}

bool ELEMENT_RF_ModelImpedancji(const ELEMENT_RF_WYNIK_t *model, float f_hz,
                                float *r_ohm, float *x_ohm)
{
    if (model == NULL || r_ohm == NULL || x_ohm == NULL || !model->poprawny)
        return false;
    if (model->typ == ELEMENT_RF_CEWKA)
        return ModelCewki(model->r_strat_ohm, model->l_h, model->pasozyt_f_lub_h,
                          f_hz, r_ohm, x_ohm);
    return ModelKondensatora(model->r_strat_ohm, model->c_f, model->pasozyt_f_lub_h,
                             f_hz, r_ohm, x_ohm);
}

static float KosztCewki(const SERIA_S11_t *seria, float r, float l, float cp,
                        float *rms_ohm, uint16_t *liczba)
{
    uint16_t i;
    uint16_t n = 0U;
    double suma_norm = 0.0;
    double suma_ohm = 0.0;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        float mr, mx;
        float dr, dx;
        float skala2;
        if (!PunktPoprawny(p) || !ModelCewki(r, l, cp, (float)p->czestotliwosc_hz, &mr, &mx))
            continue;
        dr = mr - crealf(p->impedancja_ohm);
        dx = mx - cimagf(p->impedancja_ohm);
        skala2 = fmaxf(1.0f, crealf(p->impedancja_ohm) * crealf(p->impedancja_ohm) +
                              cimagf(p->impedancja_ohm) * cimagf(p->impedancja_ohm));
        suma_norm += ((double)dr * dr + (double)dx * dx) / skala2;
        suma_ohm += (double)dr * dr + (double)dx * dx;
        ++n;
    }

    if (liczba != NULL)
        *liczba = n;
    if (rms_ohm != NULL)
        *rms_ohm = n ? sqrtf((float)(suma_ohm / n)) : NAN;
    return n ? (float)(suma_norm / n) : INFINITY;
}

static bool InicjalizujCewke(const SERIA_S11_t *seria, float srf_obs,
                             float *r, float *l, float *cp, uint16_t *n_poprawnych)
{
    uint16_t i;
    uint16_t n = 0U;
    uint16_t n_niskie = 0U;
    float f_min = INFINITY;
    float f_max = 0.0f;
    double suma_r = 0.0;
    double suma_l = 0.0;
    double suma_cp = 0.0;
    uint16_t n_cp = 0U;
    float prog;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        if (!PunktPoprawny(p))
            continue;
        if ((float)p->czestotliwosc_hz < f_min) f_min = (float)p->czestotliwosc_hz;
        if ((float)p->czestotliwosc_hz > f_max) f_max = (float)p->czestotliwosc_hz;
        ++n;
    }
    if (n < ELEMENT_RF_MIN_PUNKTOW || !isfinite(f_min) || !(f_max > f_min))
        return false;

    prog = f_min + 0.25f * (f_max - f_min);
    if (isfinite(srf_obs) && srf_obs > f_min)
        prog = fminf(prog, f_min + 0.30f * (srf_obs - f_min));

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        const float f = (float)p->czestotliwosc_hz;
        const float x = cimagf(p->impedancja_ohm);
        const float rr = crealf(p->impedancja_ohm);
        const float w = 2.0f * ELEMENT_RF_PI * f;
        if (!PunktPoprawny(p) || f > prog || !(x > 0.0f) || !(rr >= 0.0f))
            continue;
        suma_r += rr;
        suma_l += x / w;
        ++n_niskie;
    }
    if (n_niskie < 2U)
        return false;

    *r = fmaxf((float)(suma_r / n_niskie), 1.0e-4f);
    *l = (float)(suma_l / n_niskie);
    if (!(*l > 0.0f) || !isfinite(*l))
        return false;

    if (isfinite(srf_obs) && srf_obs > 0.0f)
    {
        const float w0 = 2.0f * ELEMENT_RF_PI * srf_obs;
        const float skladnik_r = (*r / *l) * (*r / *l);
        *cp = 1.0f / (*l * (w0 * w0 + skladnik_r));
    }
    else
    {
        /* Bez zaobserwowanego rezonansu Cp estymujemy z susceptancji.
         * Taki wynik oznaczamy później jako SRF poza zakresem skanu. */
        const float prog_gora = f_min + 0.55f * (f_max - f_min);
        for (i = 0U; i < seria->liczba; ++i)
        {
            const POMIAR_S11_t *p = &seria->punkty[i];
            const float f = (float)p->czestotliwosc_hz;
            const float w = 2.0f * ELEMENT_RF_PI * f;
            const float complex z = p->impedancja_ohm;
            float complex y;
            float kandydat;
            float mianownik;
            if (!PunktPoprawny(p) || f < prog_gora || cabsf(z) < 1.0e-9f)
                continue;
            y = 1.0f / z;
            mianownik = (*r) * (*r) + (w * *l) * (w * *l);
            kandydat = (cimagf(y) + w * *l / mianownik) / w;
            if (isfinite(kandydat) && kandydat > 0.0f && kandydat < 1.0e-6f)
            {
                suma_cp += kandydat;
                ++n_cp;
            }
        }
        *cp = n_cp ? (float)(suma_cp / n_cp) : 1.0e-12f;
    }

    if (!(*cp > 0.0f) || !isfinite(*cp))
        *cp = 1.0e-12f;
    *n_poprawnych = n;
    return true;
}

static bool DopasujCewke(const SERIA_S11_t *seria, float srf_obs, ELEMENT_RF_WYNIK_t *wynik)
{
    float r, l, cp;
    float koszt;
    float rms = NAN;
    float krok = 0.55f; /* krok w przestrzeni logarytmicznej */
    uint16_t n = 0U;
    uint8_t iter;

    if (!InicjalizujCewke(seria, srf_obs, &r, &l, &cp, &n))
        return false;
    koszt = KosztCewki(seria, r, l, cp, &rms, NULL);

    /* Mały deterministyczny coordinate-search. Nie wymaga macierzy ani sterty,
     * a dla trzech dodatnich parametrów jest stabilniejszy numerycznie od
     * wpychania pełnego Levenberga-Marquardta do warstwy embedded. */
    for (iter = 0U; iter < 22U; ++iter)
    {
        uint8_t p;
        bool poprawa = false;
        for (p = 0U; p < 3U; ++p)
        {
            int znak;
            for (znak = -1; znak <= 1; znak += 2)
            {
                const float mnoznik = expf((float)znak * krok);
                float rr = r, ll = l, cc = cp;
                float nowy;
                if (p == 0U) rr *= mnoznik;
                else if (p == 1U) ll *= mnoznik;
                else cc *= mnoznik;
                nowy = KosztCewki(seria, rr, ll, cc, NULL, NULL);
                if (nowy < koszt)
                {
                    r = rr; l = ll; cp = cc; koszt = nowy;
                    poprawa = true;
                }
            }
        }
        if (!poprawa)
            krok *= 0.55f;
        if (krok < 0.004f)
            break;
    }

    (void)KosztCewki(seria, r, l, cp, &rms, &n);
    wynik->r_strat_ohm = r;
    wynik->l_h = l;
    wynik->c_f = NAN;
    wynik->pasozyt_f_lub_h = cp;
    wynik->blad_rms_ohm = rms;
    wynik->liczba_punktow = n;

    {
        const float skladnik = 1.0f / (l * cp) - (r / l) * (r / l);
        wynik->srf_hz = skladnik > 0.0f ? sqrtf(skladnik) / (2.0f * ELEMENT_RF_PI) : NAN;
    }
    return isfinite(r) && isfinite(l) && isfinite(cp) && r > 0.0f && l > 0.0f && cp > 0.0f;
}

static bool DopasujKondensator(const SERIA_S11_t *seria, ELEMENT_RF_WYNIK_t *wynik)
{
    uint16_t i;
    uint16_t n = 0U;
    double stt = 0.0, suu = 0.0, stu = 0.0, sty = 0.0, suy = 0.0;
    double suma_r = 0.0;
    float f_ref_skali = 0.0f;
    float r, c, esl;
    double wyznacznik;
    double a, b;
    double suma_bledu = 0.0;

    for (i = 0U; i < seria->liczba; ++i)
        if (PunktPoprawny(&seria->punkty[i]))
        {
            f_ref_skali = (float)seria->punkty[i].czestotliwosc_hz;
            break;
        }
    if (!(f_ref_skali > 0.0f))
        return false;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        float t, u, x, rr;
        if (!PunktPoprawny(p))
            continue;
        t = (float)p->czestotliwosc_hz / f_ref_skali;
        u = 1.0f / t;
        x = cimagf(p->impedancja_ohm);
        rr = crealf(p->impedancja_ohm);
        if (!(rr >= 0.0f) || !isfinite(x))
            continue;
        stt += (double)t * t;
        suu += (double)u * u;
        stu += (double)t * u;
        sty += (double)t * x;
        suy += (double)u * x;
        suma_r += rr;
        ++n;
    }
    if (n < ELEMENT_RF_MIN_PUNKTOW)
        return false;

    wyznacznik = stt * suu - stu * stu;
    if (fabs(wyznacznik) < 1.0e-12)
        return false;
    a = (sty * suu - suy * stu) / wyznacznik;
    b = (suy * stt - sty * stu) / wyznacznik;

    /* x = A*(f/fref) + B*(fref/f), więc:
     * ESL = A/(2*pi*fref), C = -1/(B*2*pi*fref). */
    esl = (float)(a / (2.0 * ELEMENT_RF_PI * f_ref_skali));
    c = (float)(-1.0 / (b * 2.0 * ELEMENT_RF_PI * f_ref_skali));
    r = (float)(suma_r / n);
    if (!(c > 0.0f) || !(esl >= 0.0f) || !(r >= 0.0f) || !isfinite(c) || !isfinite(esl))
        return false;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        float mr, mx, dr, dx;
        if (!PunktPoprawny(p) || !ModelKondensatora(r, c, esl, (float)p->czestotliwosc_hz, &mr, &mx))
            continue;
        dr = mr - crealf(p->impedancja_ohm);
        dx = mx - cimagf(p->impedancja_ohm);
        suma_bledu += (double)dr * dr + (double)dx * dx;
    }

    wynik->r_strat_ohm = r;
    wynik->c_f = c;
    wynik->l_h = NAN;
    wynik->pasozyt_f_lub_h = esl;
    wynik->srf_hz = esl > 0.0f ? 1.0f / (2.0f * ELEMENT_RF_PI * sqrtf(esl * c)) : INFINITY;
    wynik->blad_rms_ohm = sqrtf((float)(suma_bledu / n));
    wynik->liczba_punktow = n;
    return true;
}

bool ELEMENT_RF_Analizuj(const SERIA_S11_t *seria, ELEMENT_RF_TYP_t typ,
                         float fref_hz, ELEMENT_RF_WYNIK_t *wynik)
{
    uint16_t i;
    float fmin = INFINITY;
    float fmax = 0.0f;
    float srf_obs;
    bool ok;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    wynik->typ = typ;
    wynik->l_h = NAN;
    wynik->c_f = NAN;
    wynik->r_strat_ohm = NAN;
    wynik->pasozyt_f_lub_h = NAN;
    wynik->srf_hz = NAN;
    wynik->srf_obserwowane_hz = NAN;
    wynik->q_przy_fref = NAN;
    wynik->fref_hz = fref_hz;
    wynik->blad_rms_ohm = NAN;

    if (seria == NULL || seria->punkty == NULL || seria->liczba < ELEMENT_RF_MIN_PUNKTOW)
    {
        wynik->uwagi |= ELEMENT_RF_UWAGA_MALO_PUNKTOW;
        return false;
    }

    for (i = 0U; i < seria->liczba; ++i)
        if (PunktPoprawny(&seria->punkty[i]))
        {
            const float f = (float)seria->punkty[i].czestotliwosc_hz;
            if (f < fmin) fmin = f;
            if (f > fmax) fmax = f;
        }
    if (!isfinite(fmin) || !(fmax > fmin))
    {
        wynik->uwagi |= ELEMENT_RF_UWAGA_MALO_PUNKTOW;
        return false;
    }

    srf_obs = InterpolujPrzejsciePrzezZero(seria, typ);
    wynik->srf_obserwowane_hz = srf_obs;
    ok = typ == ELEMENT_RF_CEWKA ? DopasujCewke(seria, srf_obs, wynik)
                                 : DopasujKondensator(seria, wynik);
    if (!ok)
    {
        wynik->uwagi |= ELEMENT_RF_UWAGA_SLABE_DOPASOWANIE;
        return false;
    }

    if (!isfinite(srf_obs))
        wynik->uwagi |= ELEMENT_RF_UWAGA_SRF_POZA_SKANEM;
    else if (srf_obs <= fmin + 0.05f * (fmax - fmin) ||
             srf_obs >= fmax - 0.05f * (fmax - fmin))
        wynik->uwagi |= ELEMENT_RF_UWAGA_SRF_NA_BRZEGU;

    if (isfinite(wynik->blad_rms_ohm) &&
        wynik->blad_rms_ohm > fmaxf(2.0f, 0.25f * fmaxf(wynik->r_strat_ohm, 1.0f)))
        wynik->uwagi |= ELEMENT_RF_UWAGA_SLABE_DOPASOWANIE;

    if (wynik->r_strat_ohm < 0.02f)
        wynik->uwagi |= ELEMENT_RF_UWAGA_REZYSTANCJA_NIEPEWNA;

    if (!(fref_hz > 0.0f))
        fref_hz = sqrtf(fmin * fmax);
    wynik->fref_hz = fref_hz;
    if (wynik->r_strat_ohm > 1.0e-6f)
    {
        const float w = 2.0f * ELEMENT_RF_PI * fref_hz;
        if (typ == ELEMENT_RF_CEWKA)
            wynik->q_przy_fref = w * wynik->l_h / wynik->r_strat_ohm;
        else
            wynik->q_przy_fref = 1.0f / (w * wynik->c_f * wynik->r_strat_ohm);
    }

    wynik->poprawny = true;
    return true;
}
