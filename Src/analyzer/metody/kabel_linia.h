#ifndef KABEL_LINIA_H
#define KABEL_LINIA_H

#include <stdbool.h>
#include <stdint.h>
#include <complex.h>

/*
 * Model jednorodnej linii transmisyjnej zapisany bezpośrednio w postaci,
 * której potrzebuje de-embedding:
 *   Zc  - zespolona impedancja charakterystyczna,
 *   t   - tanh(gamma*l), gdzie gamma zawiera straty i fazę.
 *
 * Dzięki temu do usunięcia wpływu kabla nie trzeba wybierać gałęzi fazy
 * gamma*l. OPEN i SHORT wyznaczają Zc oraz t niezależnie dla każdego punktu f.
 */
typedef enum
{
    KABEL_LINIA_OK = 0,
    KABEL_LINIA_BRAK_DANYCH,
    KABEL_LINIA_OSOBLIWOSC,
    KABEL_LINIA_NIEFIZYCZNY
} KABEL_LINIA_STATUS_t;

typedef struct
{
    float complex zc_ohm;
    float complex tanh_gamma_l;
    KABEL_LINIA_STATUS_t status;
} KABEL_LINIA_PUNKT_t;

bool KABEL_LINIA_ZOpenShort(float complex z_open_ohm,
                            float complex z_short_ohm,
                            KABEL_LINIA_PUNKT_t *wynik);

bool KABEL_LINIA_TransformujDoWejscia(const KABEL_LINIA_PUNKT_t *kabel,
                                      float complex z_obciazenia_ohm,
                                      float complex *z_wejscia_ohm);

bool KABEL_LINIA_Deembeduj(const KABEL_LINIA_PUNKT_t *kabel,
                           float complex z_wejscia_ohm,
                           float complex *z_obciazenia_ohm);

bool KABEL_LINIA_SprawdzLoad(const KABEL_LINIA_PUNKT_t *kabel,
                             float complex z_load_zmierzony_ohm,
                             float complex z_load_wzorcowy_ohm,
                             float *blad_ohm,
                             float *blad_proc);

#endif
