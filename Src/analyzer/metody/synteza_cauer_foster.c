#include "synteza_cauer_foster.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYNTEZA_CF_EPS_ADMITANCJA 1.0e-18
#define SYNTEZA_CF_EPS_WZGLEDNY 1.0e-12

static void SYNTEZA_CF_UstawStatus(SYNTEZA_CF_STATUS_t *status, SYNTEZA_CF_STATUS_t wartosc)
{
    if (status != NULL)
        *status = wartosc;
}

static bool SYNTEZA_CF_Dodatnia(double wartosc)
{
    /*
     * Nie stosujemy jednego progu bezwzględnego do H, F i wielkości
     * pośrednich. Po przekształceniu równoważnym poprawny element może mieć
     * bardzo małą wartość. Dla pasywności liczy się dodatni znak i skończona
     * reprezentacja; miejsca dzielenia pilnują zera osobno.
     */
    return isfinite(wartosc) && wartosc > 0.0;
}

static bool SYNTEZA_CF_SprawdzCauer(const SYNTEZA_CF_CAUER_I_t *cauer)
{
    uint8_t i;

    if (cauer == NULL ||
        (cauer->liczba_elementow != 1U && cauer->liczba_elementow != 3U &&
         cauer->liczba_elementow != 5U))
        return false;

    for (i = 0U; i < cauer->liczba_elementow; ++i)
    {
        if (!SYNTEZA_CF_Dodatnia(cauer->wartosc[i]))
            return false;
    }
    return true;
}

static bool SYNTEZA_CF_ParametryGalezi(const SYNTEZA_CF_GALAZ_FOSTER_t *galaz,
                                        double *k, double *a)
{
    if (galaz == NULL || k == NULL || a == NULL ||
        !SYNTEZA_CF_Dodatnia(galaz->l_h) || !SYNTEZA_CF_Dodatnia(galaz->c_f))
        return false;

    *k = 1.0 / galaz->c_f;
    *a = 1.0 / (galaz->l_h * galaz->c_f);
    return SYNTEZA_CF_Dodatnia(*k) && SYNTEZA_CF_Dodatnia(*a);
}

bool SYNTEZA_CF_CauerNaFoster(const SYNTEZA_CF_CAUER_I_t *cauer,
                              SYNTEZA_CF_FOSTER_I_t *foster,
                              SYNTEZA_CF_STATUS_t *status)
{
    SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_ARGUMENTU);
    if (foster == NULL)
        return false;

    memset(foster, 0, sizeof(*foster));
    if (!SYNTEZA_CF_SprawdzCauer(cauer))
    {
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_TOPOLOGII);
        return false;
    }

    foster->l_szeregowa_h = cauer->wartosc[0];
    if (cauer->liczba_elementow == 1U)
    {
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
        return true;
    }

    if (cauer->liczba_elementow == 3U)
    {
        /* L1 + (L3 || C2) jest już jedną gałęzią postaci Foster I. */
        foster->liczba_galezi = 1U;
        foster->galaz[0].l_h = cauer->wartosc[2];
        foster->galaz[0].c_f = cauer->wartosc[1];
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
        return true;
    }

    {
        const double c2 = cauer->wartosc[1];
        const double l3 = cauer->wartosc[2];
        const double c4 = cauer->wartosc[3];
        const double l5 = cauer->wartosc[4];
        const double a_wsp = c4 * l5 + c2 * (l3 + l5);
        const double b_wsp = c2 * l3 * c4 * l5;
        const double a_wsp2 = a_wsp * a_wsp;
        double wyroznik = a_wsp2 - 4.0 * b_wsp;
        double qnum;
        double a_niskie;
        double a_wysokie;
        double suma_k;
        double suma_ka;
        double roznica_a;
        double n_przez_delta;
        double iloczyn_k;
        double wyroznik_k;
        double k_duze;
        double k_male;
        double k_niskie;
        double k_wysokie;
        double blad_a;
        double blad_b;

        if (!SYNTEZA_CF_Dodatnia(a_wsp) || !SYNTEZA_CF_Dodatnia(b_wsp) ||
            !isfinite(a_wsp2) || !isfinite(wyroznik))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }

        if (wyroznik < 0.0 && fabs(wyroznik) <= SYNTEZA_CF_EPS_WZGLEDNY * a_wsp2)
            wyroznik = 0.0;
        if (!(wyroznik > SYNTEZA_CF_EPS_WZGLEDNY * a_wsp2))
        {
            /* Zlane bieguny nie tworzą dwóch rozróżnialnych gałęzi Foster. */
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_TOPOLOGII);
            return false;
        }

        /* Stabilna postać wzoru kwadratowego dla B*a^2 - A*a + 1 = 0. */
        qnum = 0.5 * (a_wsp + sqrt(wyroznik));
        if (!SYNTEZA_CF_Dodatnia(qnum))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }
        a_wysokie = qnum / b_wsp;
        a_niskie = 1.0 / qnum;
        if (!SYNTEZA_CF_Dodatnia(a_niskie) || !SYNTEZA_CF_Dodatnia(a_wysokie) ||
            !(a_wysokie > a_niskie))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }

        /*
         * K = k1+k2 = 1/C2, N = k1*a1+k2*a2 = K^2/L3.
         * Iloczyn k1*k2 wyznaczamy z C4 bez odejmowania dużych liczb.
         */
        suma_k = 1.0 / c2;
        suma_ka = (suma_k / l3) * suma_k;
        roznica_a = a_wysokie - a_niskie;
        n_przez_delta = suma_ka / roznica_a;
        iloczyn_k = (n_przez_delta * n_przez_delta) / (suma_k * c4);
        if (!SYNTEZA_CF_Dodatnia(suma_k) || !SYNTEZA_CF_Dodatnia(suma_ka) ||
            !SYNTEZA_CF_Dodatnia(roznica_a) || !SYNTEZA_CF_Dodatnia(iloczyn_k))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }

        wyroznik_k = suma_k * suma_k - 4.0 * iloczyn_k;
        if (wyroznik_k < 0.0 &&
            fabs(wyroznik_k) <= SYNTEZA_CF_EPS_WZGLEDNY * suma_k * suma_k)
            wyroznik_k = 0.0;
        if (wyroznik_k < 0.0 || !isfinite(wyroznik_k))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NIEPASYWNA);
            return false;
        }

        k_duze = 0.5 * (suma_k + sqrt(wyroznik_k));
        if (!SYNTEZA_CF_Dodatnia(k_duze))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }
        k_male = iloczyn_k / k_duze;
        if (!SYNTEZA_CF_Dodatnia(k_male))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }

        /*
         * Mamy dwie możliwe pary (k,a). Wybieramy tę, która odtwarza N.
         * Porównanie służy tylko przypisaniu gałęzi; nie wyznacza małej
         * wartości przez odejmowanie prawie równych liczb.
         */
        blad_a = fabs((k_duze * a_niskie + k_male * a_wysokie) - suma_ka);
        blad_b = fabs((k_male * a_niskie + k_duze * a_wysokie) - suma_ka);
        if (blad_a <= blad_b)
        {
            k_niskie = k_duze;
            k_wysokie = k_male;
        }
        else
        {
            k_niskie = k_male;
            k_wysokie = k_duze;
        }

        foster->liczba_galezi = 2U;
        foster->galaz[0].c_f = 1.0 / k_niskie;
        foster->galaz[0].l_h = k_niskie / a_niskie;
        foster->galaz[1].c_f = 1.0 / k_wysokie;
        foster->galaz[1].l_h = k_wysokie / a_wysokie;

        if (!SYNTEZA_CF_Dodatnia(foster->galaz[0].c_f) ||
            !SYNTEZA_CF_Dodatnia(foster->galaz[0].l_h) ||
            !SYNTEZA_CF_Dodatnia(foster->galaz[1].c_f) ||
            !SYNTEZA_CF_Dodatnia(foster->galaz[1].l_h))
        {
            memset(foster, 0, sizeof(*foster));
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }
    }

    SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
    return true;
}

bool SYNTEZA_CF_FosterNaCauer(const SYNTEZA_CF_FOSTER_I_t *foster,
                              SYNTEZA_CF_CAUER_I_t *cauer,
                              SYNTEZA_CF_STATUS_t *status)
{
    double k1;
    double a1;

    SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_ARGUMENTU);
    if (foster == NULL || cauer == NULL)
        return false;

    memset(cauer, 0, sizeof(*cauer));
    if (!SYNTEZA_CF_Dodatnia(foster->l_szeregowa_h) ||
        foster->liczba_galezi > SYNTEZA_CF_MAX_GALEZI_FOSTER)
    {
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_TOPOLOGII);
        return false;
    }

    cauer->wartosc[0] = foster->l_szeregowa_h;
    if (foster->liczba_galezi == 0U)
    {
        cauer->liczba_elementow = 1U;
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
        return true;
    }

    if (!SYNTEZA_CF_ParametryGalezi(&foster->galaz[0], &k1, &a1))
    {
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NIEPASYWNA);
        return false;
    }

    if (foster->liczba_galezi == 1U)
    {
        cauer->wartosc[1] = 1.0 / k1;
        cauer->wartosc[2] = k1 / a1;
        if (!SYNTEZA_CF_Dodatnia(cauer->wartosc[1]) ||
            !SYNTEZA_CF_Dodatnia(cauer->wartosc[2]))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }
        cauer->liczba_elementow = 3U;
        SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
        return true;
    }

    {
        double k2;
        double a2;
        double suma_k;
        double suma_ka;
        double roznica_a;
        double iloczyn_k;
        double n_przez_delta;

        if (!SYNTEZA_CF_ParametryGalezi(&foster->galaz[1], &k2, &a2))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NIEPASYWNA);
            return false;
        }

        suma_k = k1 + k2;
        suma_ka = k1 * a1 + k2 * a2;
        roznica_a = a2 - a1;
        iloczyn_k = k1 * k2;

        if (!SYNTEZA_CF_Dodatnia(suma_k) || !SYNTEZA_CF_Dodatnia(suma_ka) ||
            !SYNTEZA_CF_Dodatnia(iloczyn_k) || !isfinite(roznica_a) ||
            fabs(roznica_a) <= SYNTEZA_CF_EPS_WZGLEDNY * fmax(fabs(a1), fabs(a2)))
        {
            /* Identyczne bieguny redukują się do jednej gałęzi Foster. */
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_TOPOLOGII);
            return false;
        }

        n_przez_delta = suma_ka / roznica_a;
        cauer->wartosc[1] = 1.0 / suma_k;
        cauer->wartosc[2] = (suma_k / suma_ka) * suma_k;
        cauer->wartosc[3] = (n_przez_delta * n_przez_delta) /
                           (suma_k * iloczyn_k);
        cauer->wartosc[4] = (iloczyn_k / suma_ka) *
                           (roznica_a / a1) * (roznica_a / a2);

        if (!SYNTEZA_CF_Dodatnia(cauer->wartosc[1]) ||
            !SYNTEZA_CF_Dodatnia(cauer->wartosc[2]) ||
            !SYNTEZA_CF_Dodatnia(cauer->wartosc[3]) ||
            !SYNTEZA_CF_Dodatnia(cauer->wartosc[4]))
        {
            SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_BLAD_NUMERYCZNY);
            return false;
        }
    }

    cauer->liczba_elementow = 5U;
    SYNTEZA_CF_UstawStatus(status, SYNTEZA_CF_OK);
    return true;
}

double complex SYNTEZA_CF_ImpedancjaCauer(const SYNTEZA_CF_CAUER_I_t *cauer,
                                          double f_hz)
{
    int i;
    const double omega = 2.0 * M_PI * f_hz;
    double complex z;

    if (!SYNTEZA_CF_SprawdzCauer(cauer) || !isfinite(f_hz) || f_hz <= 0.0)
        return NAN + NAN * I;

    z = I * omega * cauer->wartosc[cauer->liczba_elementow - 1U];
    for (i = (int)cauer->liczba_elementow - 2; i >= 0; --i)
    {
        if ((i & 1) != 0)
        {
            const double complex y = I * omega * cauer->wartosc[i] + 1.0 / z;
            if (cabs(y) <= SYNTEZA_CF_EPS_ADMITANCJA)
                return INFINITY + I * INFINITY;
            z = 1.0 / y;
        }
        else
        {
            z += I * omega * cauer->wartosc[i];
        }
    }
    return z;
}

double complex SYNTEZA_CF_ImpedancjaFoster(const SYNTEZA_CF_FOSTER_I_t *foster,
                                           double f_hz)
{
    uint8_t i;
    const double omega = 2.0 * M_PI * f_hz;
    double complex z;

    if (foster == NULL || !isfinite(f_hz) || f_hz <= 0.0 ||
        foster->liczba_galezi > SYNTEZA_CF_MAX_GALEZI_FOSTER ||
        !SYNTEZA_CF_Dodatnia(foster->l_szeregowa_h))
        return NAN + NAN * I;

    z = I * omega * foster->l_szeregowa_h;
    for (i = 0U; i < foster->liczba_galezi; ++i)
    {
        const SYNTEZA_CF_GALAZ_FOSTER_t *g = &foster->galaz[i];
        double complex y;
        if (!SYNTEZA_CF_Dodatnia(g->l_h) || !SYNTEZA_CF_Dodatnia(g->c_f))
            return NAN + NAN * I;
        y = 1.0 / (I * omega * g->l_h) + I * omega * g->c_f;
        if (cabs(y) <= SYNTEZA_CF_EPS_ADMITANCJA)
            return INFINITY + I * INFINITY;
        z += 1.0 / y;
    }
    return z;
}
