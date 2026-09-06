#include "porownanie_modeli_elementu.h"

#include <complex.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "dopasowanie_cauer_foster.h"
#include "element_rf.h"
#include "synteza_cauer_foster.h"

#define POR_MODELI_PI 3.14159265358979323846
#define POR_MODELI_EPS_RSS 1.0e-30
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

static void InicjalizujModel(POR_MODELI_WYNIK_t *model, POR_MODELI_ID_t id,
                             uint8_t liczba_parametrow)
{
    memset(model, 0, sizeof(*model));
    model->id = id;
    model->liczba_parametrow = liczba_parametrow;
    model->blad_rms_ohm = INFINITY;
    model->blad_wzgledny = INFINITY;
    model->aicc = INFINITY;
    model->delta_aicc = INFINITY;
}

static double ObliczAICc(double rss, uint32_t liczba_obserwacji,
                         uint8_t liczba_parametrow)
{
    const double n = (double)liczba_obserwacji;
    const double k = (double)liczba_parametrow;
    double aic;

    if (liczba_obserwacji <= (uint32_t)liczba_parametrow + 1U ||
        !isfinite(rss) || rss < 0.0)
        return INFINITY;

    /*
     * Wartość bezwzględna AICc zależy od jednostki reszty. W tym module
     * wszystkie modele są jednak liczone na dokładnie tych samych R i X,
     * dlatego do wyboru ma znaczenie wyłącznie różnica AICc między modelami.
     */
    rss = fmax(rss, POR_MODELI_EPS_RSS);
    aic = n * log(rss / n) + 2.0 * k;
    return aic + (2.0 * k * (k + 1.0)) / (n - k - 1.0);
}

static bool ZakonczOcene(POR_MODELI_WYNIK_t *model, double rss,
                         double suma_z2, uint16_t liczba_punktow)
{
    if (model == NULL || liczba_punktow == 0U || !(suma_z2 > 0.0) ||
        !isfinite(rss) || rss < 0.0)
    {
        if (model != NULL)
            model->jakosc |= POR_MODELI_JAKOSC_BLAD_NUMERYCZNY;
        return false;
    }

    model->liczba_punktow = liczba_punktow;
    model->blad_rms_ohm = sqrt(rss / (double)liczba_punktow);
    model->blad_wzgledny = sqrt(rss / suma_z2);
    {
        const double prog2 = POR_MODELI_MIN_BLAD_WZGLEDNY_AICC *
                             POR_MODELI_MIN_BLAD_WZGLEDNY_AICC;
        const double rss_do_aicc = fmax(rss, suma_z2 * prog2);
        model->aicc = ObliczAICc(rss_do_aicc,
                                 2U * (uint32_t)liczba_punktow,
                                 model->liczba_parametrow);
    }
    model->obliczony = isfinite(model->blad_rms_ohm) &&
                       isfinite(model->blad_wzgledny) &&
                       isfinite(model->aicc);
    if (!model->obliczony)
    {
        model->jakosc |= POR_MODELI_JAKOSC_BLAD_NUMERYCZNY;
        return false;
    }

    model->zaakceptowany = model->blad_wzgledny <= POR_MODELI_MAX_BLAD_WZGLEDNY;
    if (!model->zaakceptowany)
        model->jakosc |= POR_MODELI_JAKOSC_SLABE_DOPASOWANIE;
    return true;
}

static bool OcenIdealny(const SERIA_S11_t *seria, METODA_ELEMENT_TYP_t typ,
                        uint16_t indeks_reprezentatywny, POR_MODELI_WYNIK_t *model)
{
    METODA_ELEMENT_DANE_t dane;
    METODA_ELEMENT_WYNIK_t parametry;
    uint16_t i;
    uint16_t n = 0U;
    double rss = 0.0;
    double suma_z2 = 0.0;

    InicjalizujModel(model, POR_MODELI_IDEALNY, 2U);
    dane.seria = seria;
    dane.indeks_reprezentatywny = indeks_reprezentatywny;
    dane.typ = typ;
    if (!METODA_ObliczElement(METODA_ELEMENT_IDEALNA, &dane, &parametry))
        return false;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double w, zr, zx, dr, dx, r, x;
        if (!PunktPoprawny(p))
            continue;
        w = 2.0 * POR_MODELI_PI * (double)p->czestotliwosc_hz;
        zr = (double)parametry.r_ohm;
        if (typ == METODA_ELEMENT_CEWKA)
            zx = w * (double)parametry.l_h;
        else
            zx = -1.0 / (w * (double)parametry.c_f);
        r = (double)crealf(p->impedancja_ohm);
        x = (double)cimagf(p->impedancja_ohm);
        dr = r - zr;
        dx = x - zx;
        rss += dr * dr + dx * dx;
        suma_z2 += r * r + x * x;
        ++n;
    }
    return ZakonczOcene(model, rss, suma_z2, n);
}

static bool ImpedancjaRLC(const METODA_ELEMENT_WYNIK_t *parametry, double f_hz,
                          double *zr, double *zx)
{
    const double w = 2.0 * POR_MODELI_PI * f_hz;
    const double r = (double)parametry->r_ohm;
    const double l = (double)parametry->l_h;
    const double c = (double)parametry->c_f;

    if (!(f_hz > 0.0) || !(r > 0.0) || !(l > 0.0) || !(c > 0.0))
        return false;

    if (parametry->model_rlc == MET_MODEL_RLC_SZEREGOWY)
    {
        *zr = r;
        *zx = w * l - 1.0 / (w * c);
    }
    else
    {
        const double g = 1.0 / r;
        const double b = w * c - 1.0 / (w * l);
        const double d = g * g + b * b;
        if (!(d > DBL_MIN))
            return false;
        *zr = g / d;
        *zx = -b / d;
    }
    return isfinite(*zr) && isfinite(*zx);
}

static bool OcenRLC(const SERIA_S11_t *seria, METODA_ELEMENT_TYP_t typ,
                    uint16_t indeks_reprezentatywny, POR_MODELI_WYNIK_t *model)
{
    METODA_ELEMENT_DANE_t dane;
    METODA_ELEMENT_WYNIK_t parametry;
    uint16_t i;
    uint16_t n = 0U;
    double rss = 0.0;
    double suma_z2 = 0.0;

    InicjalizujModel(model, POR_MODELI_RLC, 3U);
    dane.seria = seria;
    dane.indeks_reprezentatywny = indeks_reprezentatywny;
    dane.typ = typ;
    if (!METODA_ObliczElement(METODA_ELEMENT_RLC, &dane, &parametry))
        return false;
    if (parametry.jakosc != METODA_JAKOSC_OK)
        model->jakosc |= POR_MODELI_JAKOSC_ZRODLO_OSTRZEGA;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double zr, zx, r, x, dr, dx;
        if (!PunktPoprawny(p) ||
            !ImpedancjaRLC(&parametry, (double)p->czestotliwosc_hz, &zr, &zx))
            continue;
        r = (double)crealf(p->impedancja_ohm);
        x = (double)cimagf(p->impedancja_ohm);
        dr = r - zr;
        dx = x - zx;
        rss += dr * dr + dx * dx;
        suma_z2 += r * r + x * x;
        ++n;
    }
    return ZakonczOcene(model, rss, suma_z2, n);
}

static bool OcenRF(const SERIA_S11_t *seria, METODA_ELEMENT_TYP_t typ,
                   uint16_t indeks_reprezentatywny, POR_MODELI_WYNIK_t *model)
{
    ELEMENT_RF_WYNIK_t parametry;
    uint16_t i;
    uint16_t n = 0U;
    double rss = 0.0;
    double suma_z2 = 0.0;
    float fref;

    InicjalizujModel(model, POR_MODELI_RF_PASOZYTNICZY, 3U);
    if (seria == NULL || seria->punkty == NULL || indeks_reprezentatywny >= seria->liczba)
        return false;
    fref = (float)seria->punkty[indeks_reprezentatywny].czestotliwosc_hz;
    if (!ELEMENT_RF_Analizuj(seria,
                             typ == METODA_ELEMENT_CEWKA ? ELEMENT_RF_CEWKA : ELEMENT_RF_KONDENSATOR,
                             fref, &parametry))
        return false;
    if (parametry.uwagi != ELEMENT_RF_UWAGA_BRAK)
        model->jakosc |= POR_MODELI_JAKOSC_ZRODLO_OSTRZEGA;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        float mr, mx;
        double r, x, dr, dx;
        if (!PunktPoprawny(p) ||
            !ELEMENT_RF_ModelImpedancji(&parametry, (float)p->czestotliwosc_hz, &mr, &mx))
            continue;
        r = (double)crealf(p->impedancja_ohm);
        x = (double)cimagf(p->impedancja_ohm);
        dr = r - (double)mr;
        dx = x - (double)mx;
        rss += dr * dr + dx * dx;
        suma_z2 += r * r + x * x;
        ++n;
    }
    return ZakonczOcene(model, rss, suma_z2, n);
}

static bool OcenCauer(const SERIA_S11_t *seria, uint8_t liczba_elementow,
                      POR_MODELI_WYNIK_t *model)
{
    DOPASOWANIE_CF_WYNIK_t parametry;
    uint16_t i;
    uint16_t n = 0U;
    double rss = 0.0;
    double suma_z2 = 0.0;
    POR_MODELI_ID_t id = liczba_elementow == 1U ? POR_MODELI_CAUER_1 : POR_MODELI_CAUER_3;

    InicjalizujModel(model, id, liczba_elementow);
    if (!DOPASOWANIE_CF_Dopasuj(seria, liczba_elementow, &parametry) || !parametry.obliczony)
        return false;
    if (!parametry.zaakceptowany)
        model->jakosc |= POR_MODELI_JAKOSC_ZRODLO_OSTRZEGA;

    for (i = 0U; i < seria->liczba; ++i)
    {
        const POMIAR_S11_t *p = &seria->punkty[i];
        double complex z;
        double r, x, zr, zx, dr, dx;
        if (!PunktPoprawny(p))
            continue;
        z = SYNTEZA_CF_ImpedancjaCauer(&parametry.cauer, (double)p->czestotliwosc_hz);
        zr = creal(z);
        zx = cimag(z);
        if (!isfinite(zr) || !isfinite(zx))
            continue;
        r = (double)crealf(p->impedancja_ohm);
        x = (double)cimagf(p->impedancja_ohm);
        dr = r - zr;
        dx = x - zx;
        rss += dr * dr + dx * dx;
        suma_z2 += r * r + x * x;
        ++n;
    }
    return ZakonczOcene(model, rss, suma_z2, n);
}

static bool ModelJestKandydatem(const POR_MODELI_WYNIK_t *model,
                                bool dopusc_ostrzezone)
{
    if (model == NULL || !model->obliczony || !model->zaakceptowany ||
        !isfinite(model->aicc))
        return false;

    if (!dopusc_ostrzezone &&
        (model->jakosc & POR_MODELI_JAKOSC_ZRODLO_OSTRZEGA) != 0U)
        return false;

    return true;
}

static bool ZnajdzMinimumAICc(const POR_MODELI_POROWNANIE_t *wynik,
                              bool dopusc_ostrzezone,
                              double *minimum)
{
    uint8_t i;
    double wartosc = INFINITY;
    bool znaleziono = false;

    if (wynik == NULL || minimum == NULL)
        return false;

    for (i = 0U; i < POR_MODELI_LICZBA; ++i)
    {
        const POR_MODELI_WYNIK_t *model = &wynik->model[i];
        if (ModelJestKandydatem(model, dopusc_ostrzezone) && model->aicc < wartosc)
        {
            wartosc = model->aicc;
            znaleziono = true;
        }
    }

    *minimum = wartosc;
    return znaleziono;
}

static void WybierzNajlepszy(POR_MODELI_POROWNANIE_t *wynik)
{
    uint8_t i;
    double minimum_aicc = INFINITY;
    bool dopusc_ostrzezone = false;
    uint8_t najmniej_parametrow = UINT8_MAX;
    double aicc_wybranego = INFINITY;

    wynik->ma_najlepszy = false;
    wynik->najlepszy = POR_MODELI_IDEALNY;

    /*
     * Najpierw szukamy wśród modeli, których własny algorytm nie zgłasza
     * zastrzeżeń. Model ostrzeżony może zostać użyty dopiero wtedy, gdy nie ma
     * żadnego zaakceptowanego modelu czystego. Dzięki temu niski błąd liczbowy
     * nie przykrywa np. rezonansu na brzegu skanu albo słabego dopasowania.
     */
    if (!ZnajdzMinimumAICc(wynik, false, &minimum_aicc))
    {
        dopusc_ostrzezone = true;
        if (!ZnajdzMinimumAICc(wynik, true, &minimum_aicc))
            return;
    }

    for (i = 0U; i < POR_MODELI_LICZBA; ++i)
    {
        POR_MODELI_WYNIK_t *model = &wynik->model[i];
        if (!ModelJestKandydatem(model, dopusc_ostrzezone))
            continue;

        model->delta_aicc = model->aicc - minimum_aicc;
        if (model->delta_aicc < 0.0 && model->delta_aicc > -1.0e-9)
            model->delta_aicc = 0.0;

        /*
         * Gdy kilka modeli ma praktycznie podobne poparcie w danych (ΔAICc<=2),
         * wybieramy model z mniejszą liczbą parametrów. Dopiero remis złożoności
         * rozstrzyga niższe AICc. To chroni przed nadmiernym dopasowaniem.
         */
        if (model->delta_aicc <= POR_MODELI_DELTA_AICC_PODOBNE &&
            (model->liczba_parametrow < najmniej_parametrow ||
             (model->liczba_parametrow == najmniej_parametrow &&
              model->aicc < aicc_wybranego)))
        {
            najmniej_parametrow = model->liczba_parametrow;
            aicc_wybranego = model->aicc;
            wynik->najlepszy = (POR_MODELI_ID_t)i;
            wynik->ma_najlepszy = true;
        }
    }
}

bool POR_MODELI_PorownajMaska(const SERIA_S11_t *seria,
                              METODA_ELEMENT_TYP_t typ,
                              uint16_t indeks_reprezentatywny,
                              uint32_t maska_modeli,
                              POR_MODELI_POROWNANIE_t *wynik)
{
    uint8_t i;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));
    maska_modeli &= POR_MODELI_MASKA_WSZYSTKIE;

    for (i = 0U; i < POR_MODELI_LICZBA; ++i)
    {
        InicjalizujModel(&wynik->model[i], (POR_MODELI_ID_t)i,
                         i == POR_MODELI_IDEALNY ? 2U :
                         i == POR_MODELI_CAUER_1 ? 1U : 3U);
        if ((maska_modeli & POR_MODELI_MASKA(i)) == 0U)
            wynik->model[i].jakosc |= POR_MODELI_JAKOSC_NIEOBSLUGIWANY;
    }

    if (maska_modeli == 0U)
        return false;

    if (seria == NULL || seria->punkty == NULL || seria->liczba < 3U ||
        indeks_reprezentatywny >= seria->liczba)
    {
        for (i = 0U; i < POR_MODELI_LICZBA; ++i)
            if ((maska_modeli & POR_MODELI_MASKA(i)) != 0U)
                wynik->model[i].jakosc |= POR_MODELI_JAKOSC_MALO_PUNKTOW;
        return false;
    }

    if ((maska_modeli & POR_MODELI_MASKA(POR_MODELI_IDEALNY)) != 0U)
        (void)OcenIdealny(seria, typ, indeks_reprezentatywny, &wynik->model[POR_MODELI_IDEALNY]);
    if ((maska_modeli & POR_MODELI_MASKA(POR_MODELI_RLC)) != 0U)
        (void)OcenRLC(seria, typ, indeks_reprezentatywny, &wynik->model[POR_MODELI_RLC]);
    if ((maska_modeli & POR_MODELI_MASKA(POR_MODELI_RF_PASOZYTNICZY)) != 0U)
        (void)OcenRF(seria, typ, indeks_reprezentatywny, &wynik->model[POR_MODELI_RF_PASOZYTNICZY]);

    if (typ == METODA_ELEMENT_CEWKA)
    {
        if ((maska_modeli & POR_MODELI_MASKA(POR_MODELI_CAUER_1)) != 0U)
            (void)OcenCauer(seria, 1U, &wynik->model[POR_MODELI_CAUER_1]);
        if ((maska_modeli & POR_MODELI_MASKA(POR_MODELI_CAUER_3)) != 0U)
            (void)OcenCauer(seria, 3U, &wynik->model[POR_MODELI_CAUER_3]);
    }
    else
    {
        wynik->model[POR_MODELI_CAUER_1].jakosc |= POR_MODELI_JAKOSC_NIEOBSLUGIWANY;
        wynik->model[POR_MODELI_CAUER_3].jakosc |= POR_MODELI_JAKOSC_NIEOBSLUGIWANY;
    }

    WybierzNajlepszy(wynik);
    wynik->poprawny = wynik->ma_najlepszy;
    return wynik->poprawny;
}

bool POR_MODELI_Porownaj(const SERIA_S11_t *seria,
                         METODA_ELEMENT_TYP_t typ,
                         uint16_t indeks_reprezentatywny,
                         POR_MODELI_POROWNANIE_t *wynik)
{
    return POR_MODELI_PorownajMaska(seria, typ, indeks_reprezentatywny,
                                    POR_MODELI_MASKA_WSZYSTKIE, wynik);
}

const char *POR_MODELI_Nazwa(POR_MODELI_ID_t id)
{
    switch (id)
    {
    case POR_MODELI_IDEALNY: return "Idealny L/C";
    case POR_MODELI_RLC: return "Model RLC";
    case POR_MODELI_RF_PASOZYTNICZY: return "Model RF pasozytniczy";
    case POR_MODELI_CAUER_1: return "Cauer I - 1 element";
    case POR_MODELI_CAUER_3: return "Cauer I - 3 elementy";
    default: return "Nieznany model";
    }
}
