#ifndef _RF_KOREKCJA_H_
#define _RF_KOREKCJA_H_

#include <stdbool.h>
#include <complex.h>
#include <stdint.h>

typedef enum
{
    RF_KOR_MODEL_BRAK = 0,
    RF_KOR_MODEL_OSL_KLASYCZNY = 1,
    RF_KOR_MODEL_OSL_LC = 3,
    RF_KOR_MODEL_OSL_70CM = 4
} RF_KOR_MODEL_t;

typedef enum
{
    RF_KOR_TOR_STANDARD = 0,
    RF_KOR_TOR_LC = 1
} RF_KOR_TOR_t;

bool RF_KOR_SkorygujOSL(uint32_t czestotliwosc_hz,
                        RF_KOR_TOR_t tor,
                        float complex impedancja_przed_osl,
                        bool pozwol_osl_pasmowa,
                        uint32_t zakres_od_hz,
                        uint32_t zakres_do_hz,
                        float complex *impedancja_po_osl,
                        RF_KOR_MODEL_t *model);

bool RF_KOR_ZastosujPortExtension(uint32_t czestotliwosc_hz,
                                  float z0_ohm,
                                  float complex impedancja_we,
                                  float complex *impedancja_wy,
                                  uint32_t *opoznienie_ps);

#endif
