#include "dopasowanie_l.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float complex DOPASOWANIE_Rownolegle(float complex a, float complex b)
{
    const float complex suma = a + b;
    if (cabsf(suma) < 1.0e-12f)
        return NAN + NAN * I;
    return (a * b) / suma;
}

float complex DOPASOWANIE_Zastosuj(const DOPASOWANIE_WARIANT_t *w,
                                   float complex z)
{
    if (w == 0)
        return NAN + NAN * I;

    /* Liczymy od obciazenia w strone zrodla. */
    if (w->ma_rownolegle_obciazenie)
        z = DOPASOWANIE_Rownolegle(z, I * w->x_rownolegle_obciazenie_ohm);
    if (w->ma_szereg)
        z += I * w->x_szereg_ohm;
    if (w->ma_rownolegle_zrodlo)
        z = DOPASOWANIE_Rownolegle(z, I * w->x_rownolegle_zrodlo_ohm);
    return z;
}

static void DOPASOWANIE_Dodaj(DOPASOWANIE_WYNIK_t *wynik,
                              DOPASOWANIE_TOPOLOGIA_t topologia,
                              uint8_t ma_zrodlo, float x_zrodlo,
                              uint8_t ma_szereg, float x_szereg,
                              uint8_t ma_obciazenie, float x_obciazenie)
{
    DOPASOWANIE_WARIANT_t *w;
    float complex zin;

    if (wynik->liczba_wariantow >= DOPASOWANIE_L_MAX_WARIANTOW)
        return;

    w = &wynik->wariant[wynik->liczba_wariantow];
    memset(w, 0, sizeof(*w));
    w->topologia = topologia;
    w->ma_rownolegle_zrodlo = ma_zrodlo;
    w->ma_szereg = ma_szereg;
    w->ma_rownolegle_obciazenie = ma_obciazenie;
    w->x_rownolegle_zrodlo_ohm = x_zrodlo;
    w->x_szereg_ohm = x_szereg;
    w->x_rownolegle_obciazenie_ohm = x_obciazenie;

    zin = DOPASOWANIE_Zastosuj(w, wynik->z_obciazenia);
    w->z_wejscie_teoretyczne = zin;
    w->blad_ohm = hypotf(crealf(zin) - wynik->z0_ohm, cimagf(zin));

    if (isfinite(w->blad_ohm) && w->blad_ohm < fmaxf(0.05f, wynik->z0_ohm * 0.002f))
        ++wynik->liczba_wariantow;
}

int DOPASOWANIE_Oblicz(float complex z, float z0, uint32_t f_hz,
                       DOPASOWANIE_WYNIK_t *wynik)
{
    const float R = crealf(z);
    const float X = cimagf(z);
    const float eps = 1.0e-6f;

    if (wynik == 0)
        return 0;
    memset(wynik, 0, sizeof(*wynik));
    wynik->z_obciazenia = z;
    wynik->z0_ohm = z0;
    wynik->czestotliwosc_hz = f_hz;

    if (!isfinite(R) || !isfinite(X) || R <= 0.0f || z0 <= 0.0f || f_hz == 0U)
        return 0;

    /* Topologia 1: element rownolegly po stronie zrodla, potem element szeregowy.
     * Istnieje dla R <= Z0. Najpierw dobieramy reaktancje calkowita Xt galezi
     * szeregowej tak, aby jej konduktancja byla rowna 1/Z0. */
    {
        float d = R * (z0 - R);
        if (d >= -eps)
        {
            float xt_abs;
            if (d < 0.0f)
                d = 0.0f;
            xt_abs = sqrtf(d);

            if (xt_abs < eps)
            {
                DOPASOWANIE_Dodaj(wynik, DOPASOWANIE_TOPOLOGIA_ZRODLO_ROWN,
                                  0U, 0.0f, 1U, -X, 0U, 0.0f);
            }
            else
            {
                const float xt1 = xt_abs;
                const float xt2 = -xt_abs;
                const float xsrc1 = -(R * z0) / xt1;
                const float xsrc2 = -(R * z0) / xt2;
                DOPASOWANIE_Dodaj(wynik, DOPASOWANIE_TOPOLOGIA_ZRODLO_ROWN,
                                  1U, xsrc1, 1U, xt1 - X, 0U, 0.0f);
                DOPASOWANIE_Dodaj(wynik, DOPASOWANIE_TOPOLOGIA_ZRODLO_ROWN,
                                  1U, xsrc2, 1U, xt2 - X, 0U, 0.0f);
            }
        }
    }

    /* Topologia 2: element rownolegly bezposrednio przy obciazeniu, a za nim
     * element szeregowy. Pracujemy w admitancji obciazenia. Rozwiazanie
     * istnieje, gdy po dodaniu susceptancji mozna otrzymac Re(Z)=Z0. */
    {
        const float mod2 = R * R + X * X;
        if (mod2 > eps)
        {
            const float G = R / mod2;
            const float B_load = -X / mod2;
            float d = G / z0 - G * G;
            if (d >= -eps)
            {
                float bt_abs;
                if (d < 0.0f)
                    d = 0.0f;
                bt_abs = sqrtf(d);

                for (uint8_t znak = 0U; znak < (bt_abs < eps ? 1U : 2U); ++znak)
                {
                    const float bt = (znak == 0U) ? bt_abs : -bt_abs;
                    const float b_shunt = bt - B_load;
                    const float mian = G * G + bt * bt;
                    const float xload = (fabsf(b_shunt) < eps) ? 0.0f : -1.0f / b_shunt;
                    const uint8_t ma_load = fabsf(b_shunt) >= eps;
                    const float xs = (mian > eps) ? bt / mian : 0.0f;

                    DOPASOWANIE_Dodaj(wynik, DOPASOWANIE_TOPOLOGIA_OBCIAZENIE_ROWN,
                                      0U, 0.0f, 1U, xs, ma_load, xload);
                }
            }
        }
    }

    return wynik->liczba_wariantow > 0U;
}

DOPASOWANIE_ELEMENT_WARTOSC_t DOPASOWANIE_ReaktancjaNaElement(float x,
                                                               uint32_t f_hz)
{
    DOPASOWANIE_ELEMENT_WARTOSC_t wynik = {DOPASOWANIE_ELEMENT_BRAK, 0.0f};
    const float omega = 2.0f * (float)M_PI * (float)f_hz;

    if (!isfinite(x) || f_hz == 0U || fabsf(x) < 1.0e-9f)
        return wynik;

    if (x > 0.0f)
    {
        wynik.typ = DOPASOWANIE_ELEMENT_L;
        wynik.wartosc_si = x / omega;
    }
    else
    {
        wynik.typ = DOPASOWANIE_ELEMENT_C;
        wynik.wartosc_si = -1.0f / (omega * x);
    }
    return wynik;
}
