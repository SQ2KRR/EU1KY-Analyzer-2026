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
        const uint16_t idx = prawy ? (uint16_t)(liczba - 1U - i) : i;
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
        const uint16_t lewy = (uint16_t)(i - 1U);
        const uint16_t prawy = i;
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
        const uint16_t lewy = i;
        const uint16_t prawy = (uint16_t)(i + 1U);
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

static bool S21_WyznaczPasmoBPF(const float *strata_db, const uint8_t *poprawny,
                                uint16_t liczba, uint16_t indeks_srodka,
                                uint32_t f_start_hz, float krok_hz,
                                float prog_db, float *f1_hz, float *f2_hz,
                                float *pasmo_hz)
{
    float f1;
    float f2;

    if (!S21_SzukajLewegoPrzeciecia(strata_db, poprawny, indeks_srodka,
                                    f_start_hz, krok_hz, prog_db, true,
                                    &f1, NULL))
        return false;
    if (!S21_SzukajPrawegoPrzeciecia(strata_db, poprawny, liczba, indeks_srodka,
                                     f_start_hz, krok_hz, prog_db, true,
                                     &f2, NULL))
        return false;
    if (!(f2 > f1))
        return false;

    if (f1_hz != NULL)
        *f1_hz = f1;
    if (f2_hz != NULL)
        *f2_hz = f2;
    if (pasmo_hz != NULL)
        *pasmo_hz = f2 - f1;
    return true;
}

static uint16_t S21_PoliczPunkty(const uint8_t *maska, const uint8_t *poprawny,
                                uint16_t liczba, uint32_t f_start_hz, float krok_hz,
                                float f_od_hz, float f_do_hz)
{
    uint16_t i;
    uint16_t ile = 0U;

    if (poprawny == NULL)
        return 0U;

    for (i = 0U; i < liczba; ++i)
    {
        const float f = S21_CzestotliwoscPunktu(i, f_start_hz, krok_hz);
        if (!poprawny[i] || (maska != NULL && !maska[i]))
            continue;
        if (f >= f_od_hz && f <= f_do_hz)
            ++ile;
    }
    return ile;
}

static uint8_t S21_OcenProbkowanie(uint16_t punkty_bw3)
{
    /*
     * Ocena jest wskaźnikiem użytkowym, a nie niepewnością metrologiczną.
     * Poprzednia wersja miała skok 45% -> 65% pomiędzy 15 i 16 punktami,
     * przez co dwa praktycznie identyczne pomiary dostawały zupełnie inną
     * ocenę. Stosujemy łagodną funkcję odcinkowo-liniową.
     *
     *  2 pkt  ~15%  - wynik tylko orientacyjny
     *  8 pkt  ~52%  - wynik roboczy
     * 15 pkt  ~71%  - wynik użyteczny
     * 24 pkt  ~83%  - dobry
     * 32 pkt  ~92%  - bardzo dobry
     */
    if (punkty_bw3 >= 64U)
        return 100U;
    if (punkty_bw3 >= 32U)
        return (uint8_t)(92U + ((uint32_t)(punkty_bw3 - 32U) * 8U) / 32U);
    if (punkty_bw3 >= 16U)
        return (uint8_t)(74U + ((uint32_t)(punkty_bw3 - 16U) * 18U) / 16U);
    if (punkty_bw3 >= 8U)
        return (uint8_t)(52U + ((uint32_t)(punkty_bw3 - 8U) * 22U) / 8U);
    if (punkty_bw3 >= 4U)
        return (uint8_t)(30U + ((uint32_t)(punkty_bw3 - 4U) * 22U) / 4U);
    if (punkty_bw3 >= 2U)
        return (uint8_t)(15U + ((uint32_t)(punkty_bw3 - 2U) * 15U) / 2U);
    if (punkty_bw3 == 1U)
        return 8U;
    return 0U;
}

static float S21_ZafalowanieSrodka(const float *strata_db, const uint8_t *poprawny,
                                  uint16_t liczba, uint32_t f_start_hz, float krok_hz,
                                  float f1_3db_hz, float f2_3db_hz)
{
    const float szerokosc = f2_3db_hz - f1_3db_hz;
    const float f_od = f1_3db_hz + 0.20f * szerokosc;
    const float f_do = f2_3db_hz - 0.20f * szerokosc;
    bool ma = false;
    float min_v = 0.0f;
    float max_v = 0.0f;
    uint16_t i;

    if (!(szerokosc > 0.0f))
        return 0.0f;

    for (i = 0U; i < liczba; ++i)
    {
        const float f = S21_CzestotliwoscPunktu(i, f_start_hz, krok_hz);
        const float v = strata_db[i];
        if (!poprawny[i] || !isfinite(v) || f < f_od || f > f_do)
            continue;
        if (!ma)
        {
            min_v = max_v = v;
            ma = true;
        }
        else
        {
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
        }
    }

    return ma ? (max_v - min_v) : 0.0f;
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
                       const uint8_t *zmierzony, uint16_t liczba,
                       uint32_t f_start_hz, float krok_hz,
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
    /*
     * Do rozpoznania BPF wystarczy, aby maksimum transmisji nie leżało tuż
     * przy krawędzi skanu. Poprzedni margines 20% był zbyt duży: szeroki
     * skan filtru testowego z pasmem w dolnej części zakresu był mylnie
     * klasyfikowany jako LPF i tracił szerokości -6/-10/-20/-40 dB.
     * Pięć procent nadal chroni przed uznaniem zwykłego zbocza LPF/HPF za BPF.
     */
    const uint16_t margines = (uint16_t)((liczba / 20U) > 3U ? (liczba / 20U) : 3U);
    bool minimum_wewnatrz;
    bool maksimum_wewnatrz;

    if (wynik == NULL)
        return false;
    memset(wynik, 0, sizeof(*wynik));

    if (strata_db == NULL || poprawny == NULL || liczba < 3U ||
        !isfinite(krok_hz) || krok_hz <= 0.0f)
        return false;

    wynik->krok_hz = krok_hz;

    for (i = 0U; i < liczba; ++i)
    {
        if (!poprawny[i] || !isfinite(strata_db[i]))
            continue;
        ++wynik->punkty_poprawne;
        if (zmierzony == NULL || zmierzony[i])
            ++wynik->punkty_zmierzone;

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
        /*
         * Dla BPF/LPF/HPF ekstremum przepuszczania jest minimum straty,
         * natomiast dla NOTCH środkiem zapadki jest maksimum straty.
         * Poprzednia wersja używała f_min również dla NOTCH, przez co BW3
         * było poprawne, ale asymetria zapadki mogła być bezsensowna.
         */
        const float f_ekstremum = wynik->typ == S21_TYP_NOTCH
            ? wynik->f_max_hz : wynik->f_min_hz;
        const float lewa_polowa = f_ekstremum - wynik->f1_3db_hz;
        const float prawa_polowa = wynik->f2_3db_hz - f_ekstremum;
        const float suma_polow = fabsf(lewa_polowa) + fabsf(prawa_polowa);

        wynik->pasmo_3db_hz = wynik->f2_3db_hz - wynik->f1_3db_hz;
        wynik->f_srodek_3db_hz = 0.5f * (wynik->f1_3db_hz + wynik->f2_3db_hz);
        if (wynik->typ == S21_TYP_BPF && wynik->pasmo_3db_hz > 0.0f)
            wynik->q_3db = wynik->f_srodek_3db_hz / wynik->pasmo_3db_hz;
        if (suma_polow > 1.0f)
            wynik->asymetria_3db_proc = 100.0f * fabsf(lewa_polowa - prawa_polowa) / suma_polow;
        if (wynik->typ == S21_TYP_BPF)
            wynik->przesuniecie_piku_od_srodka_hz = wynik->f_min_hz - wynik->f_srodek_3db_hz;

        /*
         * Dwa przecięcia są interpolowane pomiędzy próbkami. Nie nazywamy
         * tego formalną niepewnością, bo ta wymagałaby także modelu szumu,
         * kalibracji i nachylenia zboczy. Jako bezpieczną informację UI
         * podaje rozdzielczość siatki: około jeden krok dla całego BW3.
         */
        wynik->rozdzielczosc_bw3_hz = krok_hz;

        wynik->punkty_bw3 = S21_PoliczPunkty(NULL, poprawny, liczba, f_start_hz,
                                             krok_hz, wynik->f1_3db_hz,
                                             wynik->f2_3db_hz);
        wynik->punkty_zmierzone_bw3 = S21_PoliczPunkty(zmierzony, poprawny, liczba,
                                                       f_start_hz, krok_hz,
                                                       wynik->f1_3db_hz,
                                                       wynik->f2_3db_hz);
        wynik->jakosc_probkowania_proc = S21_OcenProbkowanie(wynik->punkty_zmierzone_bw3);
        wynik->zafalowanie_srodka_db = S21_ZafalowanieSrodka(
            strata_db, poprawny, liczba, f_start_hz, krok_hz,
            wynik->f1_3db_hz, wynik->f2_3db_hz);
    }
    else
    {
        wynik->jakosc_probkowania_proc = 0U;
    }

    /*
     * Szerokości progowe liczymy dla każdej charakterystyki z maksimum
     * transmisji, z wyjątkiem NOTCH. Jeżeli jedna strona nie ma przecięcia
     * (typowy LPF/HPF), funkcja po prostu zwróci false. Dzięki temu BPF
     * położony blisko jednej krawędzi szerokiego skanu nie traci danych tylko
     * dlatego, że klasyfikator typu był ostrożny.
     */
    if (wynik->typ != S21_TYP_NOTCH)
    {
        wynik->ma_6db = S21_WyznaczPasmoBPF(
            strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
            minimum + 6.0f, &wynik->f1_6db_hz, &wynik->f2_6db_hz,
            &wynik->pasmo_6db_hz);
        wynik->ma_10db = S21_WyznaczPasmoBPF(
            strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
            minimum + 10.0f, &wynik->f1_10db_hz, &wynik->f2_10db_hz,
            &wynik->pasmo_10db_hz);
        wynik->ma_20db = S21_WyznaczPasmoBPF(
            strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
            minimum + 20.0f, &wynik->f1_20db_hz, &wynik->f2_20db_hz,
            &wynik->pasmo_20db_hz);
        wynik->ma_40db = S21_WyznaczPasmoBPF(
            strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
            minimum + 40.0f, &wynik->f1_40db_hz, &wynik->f2_40db_hz,
            &wynik->pasmo_40db_hz);
        wynik->ma_60db = S21_WyznaczPasmoBPF(
            strata_db, poprawny, liczba, indeks_minimum, f_start_hz, krok_hz,
            minimum + 60.0f, &wynik->f1_60db_hz, &wynik->f2_60db_hz,
            &wynik->pasmo_60db_hz);

        if (wynik->pasmo_3db_hz > 0.0f)
        {
            if (wynik->ma_20db)
                wynik->shape_20_3 = wynik->pasmo_20db_hz / wynik->pasmo_3db_hz;
            if (wynik->ma_40db)
                wynik->shape_40_3 = wynik->pasmo_40db_hz / wynik->pasmo_3db_hz;
            if (wynik->ma_60db)
                wynik->shape_60_3 = wynik->pasmo_60db_hz / wynik->pasmo_3db_hz;
        }
        if (wynik->ma_6db && wynik->pasmo_6db_hz > 0.0f)
        {
            if (wynik->ma_40db)
                wynik->shape_40_6 = wynik->pasmo_40db_hz / wynik->pasmo_6db_hz;
            if (wynik->ma_60db)
                wynik->shape_60_6 = wynik->pasmo_60db_hz / wynik->pasmo_6db_hz;
        }
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
