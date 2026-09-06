#include <complex.h>
#include <math.h>
#include <stdint.h>

#include "port_extension.h"
#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PORTEXT_PS_NA_SEKUNDE 1000000000000.0
#define PORTEXT_MIN_Z0_OHM 0.001f
#define PORTEXT_MIN_MIANOWNIK 1.0e-12f

uint32_t PORTEXT_PobierzOpoznieniePs(void)
{
    return CFG_GetParam(CFG_PARAM_PORT_EXT_PS);
}

float complex PORTEXT_KorygujImpedancje(float complex impedancja_ohm,
                                        float z0_ohm,
                                        uint32_t czestotliwosc_hz,
                                        uint32_t opoznienie_ps)
{
    float complex gamma;
    float complex mianownik;
    float complex obrot;
    double tau_s;
    double kat_rad;

    /*
     * Stan domyślny musi być dokładnie przezroczysty. Dzięki temu samo dodanie
     * funkcji nie zmienia ani jednego wyniku istniejącego firmware.
     */
    if (opoznienie_ps == 0U || czestotliwosc_hz == 0U)
        return impedancja_ohm;

    if (!isfinite(crealf(impedancja_ohm)) || !isfinite(cimagf(impedancja_ohm)) ||
        !isfinite(z0_ohm) || z0_ohm < PORTEXT_MIN_Z0_OHM)
        return impedancja_ohm;

    mianownik = impedancja_ohm + z0_ohm;
    if (cabsf(mianownik) < PORTEXT_MIN_MIANOWNIK)
        return impedancja_ohm;

    gamma = (impedancja_ohm - z0_ohm) / mianownik;

    tau_s = (double)opoznienie_ps / PORTEXT_PS_NA_SEKUNDE;
    kat_rad = 4.0 * M_PI * (double)czestotliwosc_hz * tau_s;
    obrot = (float)cos(kat_rad) + (float)sin(kat_rad) * I;
    gamma *= obrot;

    mianownik = 1.0f - gamma;
    if (cabsf(mianownik) < PORTEXT_MIN_MIANOWNIK)
    {
        /*
         * Gamma bliskie +1 odpowiada impedancji dążącej do nieskończoności.
         * Nie tworzymy tu sztucznej liczby zastępczej, bo mogłaby wyglądać jak
         * prawidłowy pomiar. Zwracamy nieskończoność zgodną z fizyką modelu.
         */
        return INFINITY + copysignf(INFINITY, cimagf(gamma)) * I;
    }

    return z0_ohm * (1.0f + gamma) / mianownik;
}

float complex PORTEXT_Zastosuj(uint32_t czestotliwosc_hz,
                               float complex impedancja_ohm,
                               float z0_ohm)
{
    return PORTEXT_KorygujImpedancje(impedancja_ohm,
                                     z0_ohm,
                                     czestotliwosc_hz,
                                     PORTEXT_PobierzOpoznieniePs());
}
