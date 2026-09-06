#include "pomiar_s11.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "rf_akwizycja.h"
#include "rf_korekcja.h"
#include "profil_kabla.h"

static float complex POMIAR_S11_BezpiecznaImpedancja(float complex gamma, float z0)
{
    const float complex mianownik = 1.0f - gamma;

    if (!isfinite(crealf(gamma)) || !isfinite(cimagf(gamma)) ||
        !isfinite(z0) || z0 <= 0.0f || cabsf(mianownik) < 1.0e-12f)
        return NAN + NAN * I;

    return z0 * (1.0f + gamma) / mianownik;
}

float complex POMIAR_S11_GammaZImpedancji(float complex impedancja_ohm, float z0_ohm)
{
    return RF_AKW_ImpedancjaNaGamma(impedancja_ohm, z0_ohm);
}

float complex POMIAR_S11_ImpedancjaZGamma(float complex gamma, float z0_ohm)
{
    return POMIAR_S11_BezpiecznaImpedancja(gamma, z0_ohm);
}

float complex POMIAR_S11_GammaNaImpedancje(float complex gamma, float z0_ohm)
{
    return POMIAR_S11_ImpedancjaZGamma(gamma, z0_ohm);
}

float complex POMIAR_S11_ImpedancjaNaGamma(float complex impedancja_ohm, float z0_ohm)
{
    return POMIAR_S11_GammaZImpedancji(impedancja_ohm, z0_ohm);
}

static POMIAR_S11_USTAWIENIA_t POMIAR_S11_UstawieniaDomyslne(void)
{
    POMIAR_S11_USTAWIENIA_t ustawienia;
    ustawienia.tor = POMIAR_S11_TOR_STANDARD;
    ustawienia.liczba_usrednien = 1U;
    ustawienia.korekcja_hw = true;
    ustawienia.korekcja_osl = true;
    ustawienia.kompensacja_portu = false;
    ustawienia.kompensacja_kabla = false;
    ustawienia.automatyczna_osl_pasmowa = false;
    ustawienia.zakres_pomiaru_od_hz = 0U;
    ustawienia.zakres_pomiaru_do_hz = 0U;
    return ustawienia;
}

static POMIAR_S11_KOREKCJA_t POMIAR_S11_MapujModel(RF_KOR_MODEL_t model)
{
    switch (model)
    {
    case RF_KOR_MODEL_OSL_KLASYCZNY:
        return POMIAR_S11_KOREKCJA_OSL_KLASYCZNY;
    case RF_KOR_MODEL_OSL_LC:
        return POMIAR_S11_KOREKCJA_OSL_LC;
    case RF_KOR_MODEL_OSL_70CM:
        return POMIAR_S11_KOREKCJA_OSL_70CM;
    default:
        return POMIAR_S11_KOREKCJA_BRAK;
    }
}

bool POMIAR_S11_PobierzPunkt(uint32_t czestotliwosc_hz,
                             const POMIAR_S11_USTAWIENIA_t *ustawienia_we,
                             POMIAR_S11_t *wynik)
{
    POMIAR_S11_USTAWIENIA_t ustawienia;
    RF_AKW_PUNKT_t surowy;
    RF_KOR_MODEL_t model = RF_KOR_MODEL_BRAK;
    float complex z;
    uint32_t opoznienie_ps = 0U;

    if (wynik == NULL)
        return false;

    memset(wynik, 0, sizeof(*wynik));
    wynik->czestotliwosc_hz = czestotliwosc_hz;
    wynik->gamma_przed_osl = NAN + NAN * I;
    wynik->impedancja_przed_osl_ohm = NAN + NAN * I;
    wynik->gamma = NAN + NAN * I;
    wynik->impedancja_ohm = NAN + NAN * I;

    ustawienia = ustawienia_we != NULL ? *ustawienia_we : POMIAR_S11_UstawieniaDomyslne();
    if (ustawienia.liczba_usrednien == 0U)
        ustawienia.liczba_usrednien = 1U;

    if (!RF_AKW_PobierzPunkt(czestotliwosc_hz,
                             ustawienia.tor == POMIAR_S11_TOR_LC ? RF_AKW_TOR_LC : RF_AKW_TOR_STANDARD,
                             ustawienia.korekcja_hw,
                             ustawienia.liczba_usrednien,
                             &surowy))
        return false;

    wynik->z0_ohm = surowy.z0_ohm;
    wynik->gamma_przed_osl = surowy.gamma_przed_osl;
    wynik->impedancja_przed_osl_ohm = surowy.impedancja_przed_osl_ohm;
    wynik->napiecie_v_mv = surowy.napiecie_v_mv;
    wynik->napiecie_i_mv = surowy.napiecie_i_mv;
    wynik->stosunek_amplitud = surowy.stosunek_amplitud;
    wynik->stosunek_db = (surowy.stosunek_amplitud > 0.0f) ? 20.0f * log10f(surowy.stosunek_amplitud) : NAN;
    wynik->faza_stopnie = surowy.faza_stopnie;
    wynik->spojnosc_fazy = surowy.spojnosc_fazy;
    wynik->rozrzut_v_proc = surowy.rozrzut_v_proc;
    wynik->rozrzut_i_proc = surowy.rozrzut_i_proc;

    if (ustawienia.korekcja_hw)
        wynik->flagi |= POMIAR_S11_FLAGA_KOREKCJA_HW;
    if (ustawienia.tor == POMIAR_S11_TOR_LC)
        wynik->flagi |= POMIAR_S11_FLAGA_TOR_LC;

    z = surowy.impedancja_przed_osl_ohm;

    if (ustawienia.korekcja_osl)
    {
        uint32_t zakres_od = ustawienia.zakres_pomiaru_od_hz;
        uint32_t zakres_do = ustawienia.zakres_pomiaru_do_hz;

        if (zakres_od == 0U || zakres_do == 0U)
            zakres_od = zakres_do = czestotliwosc_hz;

        if (!RF_KOR_SkorygujOSL(czestotliwosc_hz,
                                ustawienia.tor == POMIAR_S11_TOR_LC ? RF_KOR_TOR_LC : RF_KOR_TOR_STANDARD,
                                z,
                                ustawienia.automatyczna_osl_pasmowa,
                                zakres_od, zakres_do,
                                &z, &model))
            return false;
        wynik->flagi |= POMIAR_S11_FLAGA_KOREKCJA_OSL;
        wynik->model_korekcji = POMIAR_S11_MapujModel(model);
        if (model == RF_KOR_MODEL_OSL_70CM)
            wynik->flagi |= POMIAR_S11_FLAGA_OSL_70CM;
    }

    if (ustawienia.kompensacja_kabla && ustawienia.tor == POMIAR_S11_TOR_STANDARD)
    {
        /*
         * Profil kabla jest pełniejszym przesunięciem płaszczyzny odniesienia
         * niż Port Extension: zawiera Zc oraz straty. Nie stosujemy obu naraz,
         * żeby nie skompensować tej samej linii dwukrotnie.
         */
        if (!ustawienia.korekcja_osl)
            return false;
        if (!PROFIL_KABLA_DeembedujAktywny(czestotliwosc_hz, z, &z))
            return false;
        wynik->flagi |= POMIAR_S11_FLAGA_KABEL_DEEMBED;
    }
    else if (ustawienia.kompensacja_portu && ustawienia.tor == POMIAR_S11_TOR_STANDARD)
    {
        /* Port Extension ma sens dopiero po OSL. */
        if (!ustawienia.korekcja_osl)
            return false;
        if (!RF_KOR_ZastosujPortExtension(czestotliwosc_hz, wynik->z0_ohm, z, &z, &opoznienie_ps))
            return false;
        if (opoznienie_ps != 0U)
            wynik->flagi |= POMIAR_S11_FLAGA_PORT_EXTENSION;
        wynik->port_extension_ps = opoznienie_ps;
    }

    if (!isfinite(crealf(z)) || !isfinite(cimagf(z)))
        return false;

    wynik->impedancja_ohm = z;
    wynik->gamma = POMIAR_S11_GammaZImpedancji(z, wynik->z0_ohm);
    if (!isfinite(crealf(wynik->gamma)) || !isfinite(cimagf(wynik->gamma)))
        return false;

    wynik->flagi |= POMIAR_S11_FLAGA_POPRAWNY;
    return true;
}

bool POMIAR_S11_PobierzSerie(const uint32_t *czestotliwosci_hz, uint16_t liczba,
                             const POMIAR_S11_USTAWIENIA_t *ustawienia_we,
                             SERIA_S11_t *seria)
{
    uint16_t i;
    bool wszystko_poprawne = true;
    POMIAR_S11_USTAWIENIA_t ustawienia;

    if (czestotliwosci_hz == NULL || seria == NULL || seria->punkty == NULL ||
        liczba == 0U || seria->liczba < liczba)
        return false;

    ustawienia = ustawienia_we != NULL ? *ustawienia_we : POMIAR_S11_UstawieniaDomyslne();

    /*
     * Seria zna swoje granice, wiec moze bezpiecznie zdecydowac o lokalnej OSL
     * raz dla calego zestawu. Nie ma wtedy skoku kalibracji pomiedzy punktami.
     */
    if (ustawienia.automatyczna_osl_pasmowa &&
        (ustawienia.zakres_pomiaru_od_hz == 0U || ustawienia.zakres_pomiaru_do_hz == 0U))
    {
        uint32_t fmin = czestotliwosci_hz[0];
        uint32_t fmax = czestotliwosci_hz[0];
        for (i = 1U; i < liczba; ++i)
        {
            if (czestotliwosci_hz[i] < fmin) fmin = czestotliwosci_hz[i];
            if (czestotliwosci_hz[i] > fmax) fmax = czestotliwosci_hz[i];
        }
        ustawienia.zakres_pomiaru_od_hz = fmin;
        ustawienia.zakres_pomiaru_do_hz = fmax;
    }

    for (i = 0U; i < liczba; ++i)
    {
        if (!POMIAR_S11_PobierzPunkt(czestotliwosci_hz[i], &ustawienia, &seria->punkty[i]))
            wszystko_poprawne = false;
    }
    seria->liczba = liczba;
    return wszystko_poprawne;
}
