#include "rf_korekcja.h"

#include <math.h>
#include <stddef.h>

#include "oslfile.h"
#include "osl70cm.h"
#include "port_extension.h"

static bool RF_KOR_CzySkonczone(float complex z)
{
    return isfinite(crealf(z)) && isfinite(cimagf(z));
}

bool RF_KOR_SkorygujOSL(uint32_t czestotliwosc_hz,
                        RF_KOR_TOR_t tor,
                        float complex impedancja_przed_osl,
                        bool pozwol_osl_pasmowa,
                        uint32_t zakres_od_hz,
                        uint32_t zakres_do_hz,
                        float complex *impedancja_po_osl,
                        RF_KOR_MODEL_t *model)
{
    float complex z;
    RF_KOR_MODEL_t wybrany_model;

    if (impedancja_po_osl == NULL || !RF_KOR_CzySkonczone(impedancja_przed_osl))
        return false;

    if (tor == RF_KOR_TOR_LC)
    {
        /*
         * V2.1-LCUX1: L/C korzysta z tej samej OSL co pozostale pomiary S11.
         * Fizyczny tor mostka jest ten sam jak w pozostalych pomiarach S11.
         * Osobna OSL L/C byla drugim kompletem wspolczynnikow dla tej
         * samej plaszczyzny odniesienia i prowadzila do niejasnego stanu
         * "Profil A: BRAK" w mierniku elementow.
         *
         * Jesli uzytkownik chce skompensowac dodatkowy uchwyt, nalezy
         * wykonac zwykla OSL na jego plaszczyznie DUT. Nie tworzymy drugiego
         * algorytmu korekcji tylko dla L/C.
         */
        z = OSL_CorrectZ(czestotliwosc_hz, impedancja_przed_osl);
        wybrany_model = RF_KOR_MODEL_OSL_KLASYCZNY;
    }
    else if (pozwol_osl_pasmowa &&
             OSL70_CzyUzycDlaZakresu(zakres_od_hz, zakres_do_hz))
    {
        /*
         * Lokalna OSL 70 cm ma pierwszenstwo tylko wtedy, gdy CALY zadany
         * pomiar miesci sie w jej zakresie. Nie przelaczamy wspolczynnikow
         * punkt po punkcie w srodku szerokiego wykresu.
         */
        z = OSL70_CorrectZ(czestotliwosc_hz, impedancja_przed_osl);
        wybrany_model = RF_KOR_MODEL_OSL_70CM;
    }
    else
    {
        z = OSL_CorrectZ(czestotliwosc_hz, impedancja_przed_osl);
        wybrany_model = RF_KOR_MODEL_OSL_KLASYCZNY;
    }

    if (!RF_KOR_CzySkonczone(z))
        return false;

    *impedancja_po_osl = z;
    if (model != NULL)
        *model = wybrany_model;
    return true;
}

bool RF_KOR_ZastosujPortExtension(uint32_t czestotliwosc_hz,
                                  float z0_ohm,
                                  float complex impedancja_we,
                                  float complex *impedancja_wy,
                                  uint32_t *opoznienie_ps)
{
    uint32_t ps;
    float complex z;

    if (impedancja_wy == NULL || !RF_KOR_CzySkonczone(impedancja_we))
        return false;

    ps = PORTEXT_PobierzOpoznieniePs();
    if (ps == 0U)
    {
        *impedancja_wy = impedancja_we;
        if (opoznienie_ps != NULL)
            *opoznienie_ps = 0U;
        return true;
    }

    z = PORTEXT_KorygujImpedancje(impedancja_we, z0_ohm, czestotliwosc_hz, ps);
    if (!RF_KOR_CzySkonczone(z))
        return false;

    *impedancja_wy = z;
    if (opoznienie_ps != NULL)
        *opoznienie_ps = ps;
    return true;
}
