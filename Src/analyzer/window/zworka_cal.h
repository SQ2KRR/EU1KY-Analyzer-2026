#ifndef ZWORKA_CAL_H_
#define ZWORKA_CAL_H_

#include <stdint.h>

typedef enum
{
    ZWORKA_CAL_STATUS_BRAK_WZORCA = 0,
    ZWORKA_CAL_STATUS_WORK,
    ZWORKA_CAL_STATUS_PODEJRZENIE,
    ZWORKA_CAL_STATUS_CAL_PEWNE,
    ZWORKA_CAL_STATUS_BLAD
} ZWORKA_CAL_STATUS_t;

typedef struct
{
    ZWORKA_CAL_STATUS_t status;
    uint32_t f_hz;
    float delta_vi_db;
    float delta_faza_deg;
    float delta_vi_db_2;
    float delta_faza_deg_2;
    uint8_t wykonano_powtorzenie;
} ZWORKA_CAL_WYNIK_t;

/*
 * Szybka, nieinwazyjna kontrola położenia CAL/WORK.
 * Wzorzec CAL pochodzi bezpośrednio z aktualnej kalibracji HW (errcorr.osl),
 * więc nie ma stałych absolutnych zależnych od konkretnego egzemplarza.
 */
ZWORKA_CAL_WYNIK_t ZWORKA_CAL_Sprawdz(void);

#endif
