#include "rf_akwizycja.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "config.h"
#include "dsp.h"

static float complex RF_AKW_BezpiecznyGamma(float complex impedancja, float z0)
{
    const float complex mianownik = impedancja + z0;

    if (!isfinite(crealf(impedancja)) || !isfinite(cimagf(impedancja)) ||
        !isfinite(z0) || z0 <= 0.0f || cabsf(mianownik) < 1.0e-12f)
        return NAN + NAN * I;

    return (impedancja - z0) / mianownik;
}

float complex RF_AKW_ImpedancjaNaGamma(float complex impedancja_ohm, float z0_ohm)
{
    return RF_AKW_BezpiecznyGamma(impedancja_ohm, z0_ohm);
}

bool RF_AKW_PobierzPunkt(uint32_t czestotliwosc_hz,
                         RF_AKW_TOR_t tor,
                         bool korekcja_hw,
                         uint8_t liczba_usrednien,
                         RF_AKW_PUNKT_t *wynik)
{
    DSP_DANE_METROLOGICZNE_t dane;
    float complex z;
    int poprawny;

    if (wynik == NULL)
        return false;

    memset(wynik, 0, sizeof(*wynik));
    wynik->czestotliwosc_hz = czestotliwosc_hz;
    wynik->z0_ohm = (float)CFG_GetParam(CFG_PARAM_R0);
    wynik->impedancja_przed_osl_ohm = NAN + NAN * I;
    wynik->gamma_przed_osl = NAN + NAN * I;

    if (liczba_usrednien == 0U)
        liczba_usrednien = 1U;
    wynik->liczba_usrednien = liczba_usrednien;

    /*
     * V2.1-LCUX1: jeden produkcyjny tor DSP dla wszystkich pomiarow S11.
     * Historyczny DSP_MeasureLC wykonywal ten sam pomiar mostka osobna
     * funkcja i utrzymywal drugi stan poprawnosci. Po ujednoliceniu filtracji
     * amplitudy/fazy nie ma juz fizycznego powodu, aby L/C omijalo sprawdzony
     * DSP_Measure(). Typ L/C pozostaje tylko informacja dla warstwy analizy/UI.
     */
    DSP_Measure(czestotliwosc_hz, korekcja_hw ? 1 : 0, 0, liczba_usrednien);
    poprawny = DSP_CzyOstatniPomiarPoprawny();
    if (tor == RF_AKW_TOR_LC)
        wynik->flagi |= RF_AKW_FLAGA_TOR_LC;

    if (korekcja_hw)
        wynik->flagi |= RF_AKW_FLAGA_KOREKCJA_HW;

    z = DSP_MeasuredZ();
    DSP_PobierzDaneMetrologiczne(&dane);

    wynik->napiecie_v_mv = dane.napiecie_v_mv;
    wynik->napiecie_i_mv = dane.napiecie_i_mv;
    wynik->stosunek_amplitud = dane.stosunek_amplitud;
    wynik->faza_stopnie = dane.faza_stopnie;
    wynik->spojnosc_fazy = dane.spojnosc_fazy;
    wynik->rozrzut_v_proc = dane.rozrzut_v_proc;
    wynik->rozrzut_i_proc = dane.rozrzut_i_proc;

    if (!poprawny || !isfinite(crealf(z)) || !isfinite(cimagf(z)))
        return false;

    wynik->impedancja_przed_osl_ohm = z;
    wynik->gamma_przed_osl = RF_AKW_BezpiecznyGamma(z, wynik->z0_ohm);
    if (!isfinite(crealf(wynik->gamma_przed_osl)) || !isfinite(cimagf(wynik->gamma_przed_osl)))
        return false;

    wynik->flagi |= RF_AKW_FLAGA_POPRAWNY;
    return true;
}
