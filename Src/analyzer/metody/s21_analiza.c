#include "s21_analiza.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define S21_MIN_KONTRAST_TYPU_DB 6.0f
#define S21_LN2 0.6931471805599453f

static float S21_CzestotliwoscPunktu(uint16_t indeks, uint32_t f_start_hz, float krok_hz)
{
    return (float)f_start_hz + (float)indeks * krok_hz;
}

static bool S21_InterpolujPrzeciecie(float f_a, float y_a, float f_b, float y_b,
                                    float prog, float *f_przeciecia)
{
    float roznica;
    float udzial;

    if (f_przeciecia == NULL || !isfinite(f_a) || !isfinite(f_b) ||
        !isfinite(y_a) || !isfinite(y_b) || !isfinite(prog))
        return false;

    roznica = y_b - y_a;
    if (fabsf(roznica) < 1.0e-9f)
        return false;

    udzial = (prog - y_a) / roznica;
    if (udzial < 0.0f)
        udzial = 0.0f;
    if (udzial > 1.0f)
        udzial = 1.0f;

    *f_przeciecia = f_a + udzial * (f_b - f_a);
    return isfinite(*f_przeciecia);
}

static float S21_NachylenieDbNaOktawe(float f_a, float y_a, float f_b, float y_b)
{
    float ln_stosunku;

    if (!isfinite(f_a) || !isfinite(f_b) || !isfinite(y_a) || !isfinite(y_b) ||
        f_a <= 0.0f || f_b <= f_a)
        return 0.0f;

    ln_stosunku = logf(f_b / f_a);
    if (!isfinite(ln_stosunku) || fabsf(ln_stosunku) < 1.0e-9f)
        return 0.0f;

    return fabsf((y_b - y_a) * S21_LN2 / ln_stosunku);
}

static bool S21_SredniaBrzegu(const float *strata_db, const uint8_t *poprawny,
                              uint16_t liczba, bool prawy, float *srednia)
{
    uint16_t ile_docelowo;
    uint16_t znaleziono = 0U;
    uint16_t i;
    float suma = 0.0f;

    if (strata_db == NULL || poprawny == NULL || srednia == NULL || liczba < 3U)
        return false;

    ile_docelowo = (uint16_t)(liczba / 10U);
    if (ile_docelowo < 2U)
        ile_docelowo = 2U;
    if (ile_docelowo > 20U)
        ile_docelowo = 20U;

    for (i = 0U; i < liczba && znaleziono < ile_docelowo; ++i)
    {
        uint16_t idx = prawy ? (uint16_t)(liczba - 1U - i) : i;
        if (!poprawny[idx] || !isfinite(strata_db[idx]))
            continue;
        suma += strata_db[idx];
        ++znaleziono;
    }

    if (znaleziono == 0U)
        return false;
    *srednia = suma / (float)znaleziono;
    return isfinite(*srednia);
}

static bool S21_SzukajLewegoPrzeciecia(const float *strata_db, const uint8_t *poprawny,
                                       uint16_t indeks_srodka, uint32_t f_start_hz,
                                       float krok_hz, float prog, bool srodek_ponizej,
                                       float *f, float *nachylenie)
{
    uint16_t i;

    if (indeks_srodka == 0U)
        return false;

    for (i = indeks_srodka; i > 0U; --i)
    {
        uint16_t lewy = (uint16_t)(i - 1U);
        uint16_t prawy = i;
        bool przecina;
        float fl;
        float fp;

        if (!poprawny[lewy] || !poprawny[prawy] ||
            !isfinite(strata_db[lewy]) || !isfinite(strata_db[prawy]))
            break;

        if (srodek_ponizej)
            przecina = strata_db[lewy] >= prog && strata_db[prawy] <= prog;
        else
            przecina = strata_db[lewy] <= prog && strata_db[prawy] >= prog;

        if (!przecina)
            continue;

        fl = S21_CzestotliwoscPunktu(lewy, f_start_hz, krok_hz);
        fp = S21_CzestotliwoscPunktu(prawy, f_start_hz, krok_hz);
        if (!S21_InterpolujPrzeciecie(fl, strata_db[lewy], fp, strata_db[prawy], prog, f))
            return false;
        if (nachylenie != NULL)
            *nachylenie = S21_NachylenieDbNaOktawe(fl, strata_db[lewy], fp, strata_db[prawy]);
        return true;
    }
    return false;
}

static bool S21_SzukajPrawegoPrzeciecia(const float *strata_db, const uint8_t *poprawny,
                                        uint16_t liczba, uint16_t indeks_srodka,
                                        uint32_t f_start_hz, float krok_hz, float prog,
                                        bool srodek_ponizej, float *f, float *nachylenie)
{
    uint16_t i;

    for (i = indeks_srodka; i + 1U < liczba; ++i)
    {
        uint16_t lewy = i;
        uint16_t prawy = (uint16_t)(i + 1U);
        bool przecina;
        float fl;
        float fp;

        if (!poprawny[lewy] || !poprawny[prawy] ||
            !isfinite(strata_db[lewy]) || !isfinite(strata_db[prawy]))
            break;

        if (srodek_ponizej)
            przecina = strata_db[lewy] <= prog && strata_db[prawy] >= prog;
        else
            przecina = strata_db[lewy] >= prog && strata_db[prawy] <= prog;

        if (!przecina)
            continue;

        fl = S21_CzestotliwoscPunktu(lewy, f_start_hz, krok_hz);
        fp = S21_CzestotliwoscPunktu(prawy, f_start_hz, krok_hz);
        if (!S21_InterpolujPrzeciecie(fl, strata_db[lewy], fp, strata_db[prawy], prog, f))
            return false;
        if (nachylenie != NULL)
            *nachylenie = S21_NachylenieDbNaOktawe(fl, strata_db[lewy], fp, strata_db[prawy]);
        return true;
    }
    return false;
}

const char *S21_NazwaTypu(S21_TYP_FILTRU_t typ)
{
    switch (typ)
    {
    case S21_TYP_LPF: return "LPF";
    case S21_TYP_HPF: return "HPF";
    case S21_TYP_BPF: return "BPF";
    case S21_TYP_NOTCH: return "NOTCH";
    default: return "S21";
    }
}

bool S21_AnalizujFiltr(const float *strata_db, const uint8_t *poprawny,
                       uint16_t liczba, uint32_t f_start_hz, float krok_hz,
                       S21_ANALIZA_t *wynik)
{
    uint16_t i;
    uint16_t indeks_minimum = 0U;
    uint16_t indeks_maksimum = 0U;
    bool znaleziono = false;
    float minimum = 0.0f;
    float maksimum = 0.0f;
    float lewy_brzeg;
    float prawy_brzeg;
    const uint16_t margines = (uint16_t)(liczba / 5U);
    bool minimum_wewnatrz;
    bool maksimum_wewnatrz;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));

    if (strata_db == NULL || poprawny == NULL || liczba < 3U ||
        !isfinite(krok_hz) || krok_hz <= 0.0f)
        return false;

    for (i = 0U; i < liczba; ++i)
    {
        if (!poprawny[i] || !isfinite(strata_db[i]))
            continue;
        if (!znaleziono)
        {
            minimum = maksimum = strata_db[i];
            indeks_minimum = indeks_maksimum = i;
            znaleziono = true;
            continue;
        }
        if (strata_db[i] < minimum)
        {
            minimum = strata_db[i];
            indeks_minimum = i;
        }
        if (strata_db[i] > maksimum)
        {
            maksimum = strata_db[i];
            indeks_maksimum = i;
        }
    }

    if (!znaleziono ||
        !S21_SredniaBrzegu(strata_db, poprawny, liczba, false, &lewy_brzeg) ||
        !S21_SredniaBrzegu(strata_db, poprawny, liczba, true, &prawy_brzeg))
        return false;

    wynik->poprawny = true;
    wynik->indeks_minimum = indeks_minimum;
    wynik->indeks_maksimum = indeks_maksimum;
    wynik->strata_min_db = minimum;
    wynik->strata_max_db = maksimum;
    wynik->f_min_hz = S21_CzestotliwoscPunktu(indeks_minimum, f_start_hz, krok_hz);
    wynik->f_max_hz = S21_CzestotliwoscPunktu(indeks_maksimum, f_start_hz, krok_hz);
    wynik->strata_lewego_brzegu_db = lewy_brzeg;
    wynik->strata_prawego_brzegu_db = prawy_brzeg;

    minimum_wewnatrz = indeks_minimum > margines && indeks_minimum + margines < liczba;
    maksimum_wewnatrz = indeks_maksimum > margines && indeks_maksimum + margines < liczba;

    /*
     * Klasyfikacja jest celowo konserwatywna. Minimalny kontrast 6 dB chroni
     * przed nazywaniem niemal plaskiej charakterystyki filtrem tylko z powodu
     * szumu lub niewielkiego przechylu kalibracji.
     */
    if (minimum_wewnatrz &&
        lewy_brzeg - minimum >= S21_MIN_KONTRAST_TYPU_DB &&
        prawy_brzeg - minimum >= S21_MIN_KONTRAST_TYPU_DB)
    {
        wynik->typ = S21_TYP_BPF;
    }
    else if (maksimum_wewnatrz &&
             maksimum - lewy_brzeg >= S21_MIN_KONTRAST_TYPU_DB &&
             maksimum - prawy_brzeg >= S21_MIN_KONTRAST_TYPU_DB)
    {
        wynik->typ = S21_TYP_NOTCH;
    }
    else if (prawy_brzeg - lewy_brzeg >= S21_MIN_KONTRAST_TYPU_DB)
    {
        wynik->typ = S21_TYP_LPF;
    }
    else if (lewy_brzeg - prawy_brzeg >= S21_MIN_KONTRAST_TYPU_DB)
    {
        wynik->typ = S21_TYP_HPF;
    }
    else
    {
        wynik->typ = S21_TYP_NIEZNANY;
    }

    if (wynik->typ == S21_TYP_NOTCH)
    {
        const float odniesienie = 0.5f * (lewy_brzeg + prawy_brzeg);
        wynik->glebokosc_notch_db = maksimum - odniesienie;
        wynik->prog_3db_db = odniesienie + 3.0f;
        wynik->ma_lewy_3db = S21_SzukajLewegoPrzeciecia(
            strata_db, poprawny, indeks_maksimum, f_start_hz, krok_hz,
            wynik->prog_3db_db, false, &wynik->f1_3db_hz,
            &wynik->nachylenie_lewe_db_na_oktawe);
        wynik->ma_prawy_3db = S21_SzukajPrawegoPrzeciecia(
            strata_db, poprawny, liczba, indeks_maksimum, f_start_hz, krok_hz,
            wynik->prog_3db_db, false, &wynik->f2_3db_hz,
            &wynik->nachylenie_prawe_db_na_oktawe);
    }
    else
    {
        wynik->prog_3db_db = minimum + 3.0f;

        if (wynik->typ == S21_TYP_BPF || wynik->typ == S21_TYP_HPF ||
            wynik->typ == S21_TYP_NIEZNANY)
        {
            wynik->ma_lewy_3db = S21_SzukajLewegoPrzeciecia(
                strata_db, poprawny, indeks_minimum, f_start_hz, krok_hz,
                wynik->prog_3db_db, true, &wynik->f1_3db_hz,
                &wynik->nachylenie_lewe_db_na_oktawe);
        }

        if (wynik->typ == S21_TYP_BPF || wynik->typ == S21_TYP_LPF ||
            wynik->typ == S21_TYP_NIEZNANY)
        {
            wynik->ma_prawy_3db = S21_SzukajPrawegoPrzeciecia(
                strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
                wynik->prog_3db_db, true, &wynik->f2_3db_hz,
                &wynik->nachylenie_prawe_db_na_oktawe);
        }
    }

    if (wynik->ma_lewy_3db && wynik->ma_prawy_3db &&
        wynik->f2_3db_hz > wynik->f1_3db_hz)
    {
        wynik->pasmo_3db_hz = wynik->f2_3db_hz - wynik->f1_3db_hz;
        if (wynik->typ == S21_TYP_BPF && wynik->pasmo_3db_hz > 0.0f)
            wynik->q_3db = wynik->f_min_hz / wynik->pasmo_3db_hz;
    }

    switch (wynik->typ)
    {
    case S21_TYP_LPF:
        wynik->tlumienie_stop_db = prawy_brzeg - minimum;
        break;
    case S21_TYP_HPF:
        wynik->tlumienie_stop_db = lewy_brzeg - minimum;
        break;
    case S21_TYP_BPF:
        wynik->tlumienie_stop_db = fminf(lewy_brzeg, prawy_brzeg) - minimum;
        break;
    case S21_TYP_NOTCH:
        wynik->tlumienie_stop_db = wynik->glebokosc_notch_db;
        break;
    default:
        wynik->tlumienie_stop_db = maksimum - minimum;
        break;
    }

    if (wynik->tlumienie_stop_db < 0.0f)
        wynik->tlumienie_stop_db = 0.0f;

    return true;
}
