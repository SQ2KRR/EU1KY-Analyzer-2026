#include "kwarc_seria.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool KWARC_SERIA_WynikPoprawny(const KWARC_WYNIK_t *wynik)
{
    return wynik != NULL &&
           isfinite(wynik->fs_hz) && wynik->fs_hz > 0.0f &&
           isfinite(wynik->rm_ohm) && wynik->rm_ohm > 0.0f &&
           isfinite(wynik->q) && wynik->q > 0.0f;
}

void KWARC_SERIA_Inicjalizuj(KWARC_SERIA_t *seria)
{
    if (seria == NULL)
        return;
    memset(seria, 0, sizeof(*seria));
}

bool KWARC_SERIA_Dodaj(KWARC_SERIA_t *seria, const KWARC_WYNIK_t *wynik)
{
    KWARC_REKORD_t *rekord;

    if (seria == NULL || !KWARC_SERIA_WynikPoprawny(wynik) ||
        seria->liczba >= KWARC_SERIA_MAKS)
        return false;

    rekord = &seria->rekordy[seria->liczba];
    rekord->numer = (uint8_t)(seria->liczba + 1U);
    rekord->wynik = *wynik;
    ++seria->liczba;
    return true;
}

static void KWARC_SERIA_OcenKombinacje(const KWARC_SERIA_t *seria,
                                       const uint8_t *indeksy, uint8_t liczba,
                                       float *rozrzut_fs_hz,
                                       float *srednia_fs_hz,
                                       float *rozrzut_rm_proc,
                                       float *sredni_rm_ohm,
                                       float *q_min,
                                       float *q_srednie)
{
    uint8_t i;
    float fs_min = FLT_MAX;
    float fs_max = -FLT_MAX;
    float fs_suma = 0.0f;
    float rm_min = FLT_MAX;
    float rm_max = -FLT_MAX;
    float rm_suma = 0.0f;
    float q_najgorsze = FLT_MAX;
    float q_suma = 0.0f;

    for (i = 0U; i < liczba; ++i)
    {
        const KWARC_WYNIK_t *w = &seria->rekordy[indeksy[i]].wynik;
        fs_min = fminf(fs_min, w->fs_hz);
        fs_max = fmaxf(fs_max, w->fs_hz);
        fs_suma += w->fs_hz;
        rm_min = fminf(rm_min, w->rm_ohm);
        rm_max = fmaxf(rm_max, w->rm_ohm);
        rm_suma += w->rm_ohm;
        q_najgorsze = fminf(q_najgorsze, w->q);
        q_suma += w->q;
    }

    *rozrzut_fs_hz = fs_max - fs_min;
    *srednia_fs_hz = fs_suma / (float)liczba;
    *sredni_rm_ohm = rm_suma / (float)liczba;
    *rozrzut_rm_proc = (*sredni_rm_ohm > 0.0f) ?
        100.0f * (rm_max - rm_min) / *sredni_rm_ohm : FLT_MAX;
    *q_min = q_najgorsze;
    *q_srednie = q_suma / (float)liczba;
}

static bool KWARC_SERIA_Lepszy(float fs, float rm, float q_min,
                               const KWARC_DOPASOWANIE_t *najlepszy)
{
    const float eps_fs = 0.01f;
    const float eps_rm = 0.001f;

    if (!najlepszy->poprawny)
        return true;
    if (fs < najlepszy->rozrzut_fs_hz - eps_fs)
        return true;
    if (fabsf(fs - najlepszy->rozrzut_fs_hz) <= eps_fs)
    {
        if (rm < najlepszy->rozrzut_rm_proc - eps_rm)
            return true;
        if (fabsf(rm - najlepszy->rozrzut_rm_proc) <= eps_rm && q_min > najlepszy->q_min)
            return true;
    }
    return false;
}

bool KWARC_SERIA_Dobierz(const KWARC_SERIA_t *seria, uint8_t liczba_w_zestawie,
                         KWARC_DOPASOWANIE_t *dopasowanie)
{
    uint8_t wybor[KWARC_ZESTAW_MAKS];
    uint8_t i;

    if (dopasowanie == NULL)
        return false;
    memset(dopasowanie, 0, sizeof(*dopasowanie));
    dopasowanie->rozrzut_fs_hz = NAN;
    dopasowanie->rozrzut_rm_proc = NAN;
    dopasowanie->q_min = NAN;

    if (seria == NULL || liczba_w_zestawie < 2U ||
        liczba_w_zestawie > KWARC_ZESTAW_MAKS ||
        seria->liczba < liczba_w_zestawie)
        return false;

    for (i = 0U; i < liczba_w_zestawie; ++i)
        wybor[i] = i;

    for (;;)
    {
        float rozrzut_fs;
        float srednia_fs;
        float rozrzut_rm;
        float sredni_rm;
        float q_min;
        float q_srednie;
        int8_t pozycja;

        KWARC_SERIA_OcenKombinacje(seria, wybor, liczba_w_zestawie,
                                   &rozrzut_fs, &srednia_fs,
                                   &rozrzut_rm, &sredni_rm,
                                   &q_min, &q_srednie);

        if (KWARC_SERIA_Lepszy(rozrzut_fs, rozrzut_rm, q_min, dopasowanie))
        {
            dopasowanie->poprawny = true;
            dopasowanie->liczba = liczba_w_zestawie;
            memcpy(dopasowanie->indeksy, wybor, liczba_w_zestawie);
            dopasowanie->srednia_fs_hz = srednia_fs;
            dopasowanie->rozrzut_fs_hz = rozrzut_fs;
            dopasowanie->sredni_rm_ohm = sredni_rm;
            dopasowanie->rozrzut_rm_proc = rozrzut_rm;
            dopasowanie->q_min = q_min;
            dopasowanie->q_srednie = q_srednie;
        }

        pozycja = (int8_t)liczba_w_zestawie - 1;
        while (pozycja >= 0 &&
               wybor[(uint8_t)pozycja] ==
                   (uint8_t)(seria->liczba - liczba_w_zestawie + (uint8_t)pozycja))
            --pozycja;

        if (pozycja < 0)
            break;

        ++wybor[(uint8_t)pozycja];
        for (i = (uint8_t)pozycja + 1U; i < liczba_w_zestawie; ++i)
            wybor[i] = (uint8_t)(wybor[i - 1U] + 1U);
    }

    return dopasowanie->poprawny;
}

bool KWARC_SERIA_CzyWybrany(const KWARC_DOPASOWANIE_t *dopasowanie, uint8_t indeks)
{
    uint8_t i;

    if (dopasowanie == NULL || !dopasowanie->poprawny)
        return false;
    for (i = 0U; i < dopasowanie->liczba; ++i)
    {
        if (dopasowanie->indeksy[i] == indeks)
            return true;
    }
    return false;
}
