#ifndef _Q_POROWNANIE_H_
#define _Q_POROWNANIE_H_

#include <stdbool.h>
#include <stdint.h>

#include "pomiar_s11.h"
#include "rejestr_metod.h"

typedef enum
{
    Q_POROWNANIE_TOPOLOGIA_SZEREGOWA = 0,
    Q_POROWNANIE_TOPOLOGIA_ROWNOLEGLA = 1
} Q_POROWNANIE_TOPOLOGIA_t;

typedef enum
{
    Q_POROWNANIE_ZGODNOSC_BRAK = 0,
    Q_POROWNANIE_ZGODNOSC_DOBRA,
    Q_POROWNANIE_ZGODNOSC_UMIARKOWANA,
    Q_POROWNANIE_ZGODNOSC_SLABA
} Q_POROWNANIE_ZGODNOSC_t;

typedef struct
{
    METODA_Q_WYNIK_t metody[METODA_Q_LICZBA];
    uint8_t dostepne[METODA_Q_LICZBA];
    uint8_t liczba_poprawnych;
    uint8_t metoda_referencyjna;
    Q_POROWNANIE_TOPOLOGIA_t topologia;
    float q_min;
    float q_max;
    float q_srednie;
    float rozrzut_proc;
    Q_POROWNANIE_ZGODNOSC_t zgodnosc;
} Q_POROWNANIE_WYNIK_t;

/*
 * Porownuje wszystkie zarejestrowane metody dobroci Q na DOKLADNIE tej
 * samej serii skorygowanych punktow S11. Dla rezonansu szeregowego krzywa
 * -3 dB/Lorentza powstaje z |Y|, a dla rownoleglego z |Z|. Model RLC
 * korzysta bezposrednio z zespolonej impedancji z tej samej serii.
 */
#define Q_POROWNANIE_MASKA(metoda) (1UL << (uint32_t)(metoda))
#define Q_POROWNANIE_MASKA_WSZYSTKIE ((1UL << (uint32_t)METODA_Q_LICZBA) - 1UL)

bool Q_POROWNANIE_AnalizujMaska(const SERIA_S11_t *seria, uint32_t maska_metod,
                                 Q_POROWNANIE_WYNIK_t *wynik);

/* Zgodność z dotychczasowym API: uruchom wszystkie zarejestrowane metody. */
bool Q_POROWNANIE_Analizuj(const SERIA_S11_t *seria, Q_POROWNANIE_WYNIK_t *wynik);

const char *Q_POROWNANIE_TekstZgodnosci(Q_POROWNANIE_ZGODNOSC_t zgodnosc);
const char *Q_POROWNANIE_TekstTopologii(Q_POROWNANIE_TOPOLOGIA_t topologia);

#endif
