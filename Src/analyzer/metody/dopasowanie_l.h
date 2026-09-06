#ifndef DOPASOWANIE_L_H_
#define DOPASOWANIE_L_H_

#include <complex.h>
#include <stdint.h>

#define DOPASOWANIE_L_MAX_WARIANTOW 4U

typedef enum
{
    DOPASOWANIE_TOPOLOGIA_ZRODLO_ROWN = 0,
    DOPASOWANIE_TOPOLOGIA_OBCIAZENIE_ROWN = 1
} DOPASOWANIE_TOPOLOGIA_t;

typedef struct
{
    DOPASOWANIE_TOPOLOGIA_t topologia;
    uint8_t ma_rownolegle_zrodlo;
    uint8_t ma_szereg;
    uint8_t ma_rownolegle_obciazenie;
    float x_rownolegle_zrodlo_ohm;
    float x_szereg_ohm;
    float x_rownolegle_obciazenie_ohm;
    float complex z_wejscie_teoretyczne;
    float blad_ohm;
} DOPASOWANIE_WARIANT_t;

typedef struct
{
    DOPASOWANIE_WARIANT_t wariant[DOPASOWANIE_L_MAX_WARIANTOW];
    uint8_t liczba_wariantow;
    float complex z_obciazenia;
    float z0_ohm;
    uint32_t czestotliwosc_hz;
} DOPASOWANIE_WYNIK_t;

typedef enum
{
    DOPASOWANIE_ELEMENT_BRAK = 0,
    DOPASOWANIE_ELEMENT_L,
    DOPASOWANIE_ELEMENT_C
} DOPASOWANIE_ELEMENT_t;

typedef struct
{
    DOPASOWANIE_ELEMENT_t typ;
    float wartosc_si;
} DOPASOWANIE_ELEMENT_WARTOSC_t;

int DOPASOWANIE_Oblicz(float complex z_obciazenia, float z0_ohm,
                       uint32_t czestotliwosc_hz, DOPASOWANIE_WYNIK_t *wynik);
DOPASOWANIE_ELEMENT_WARTOSC_t DOPASOWANIE_ReaktancjaNaElement(float x_ohm,
                                                               uint32_t czestotliwosc_hz);
float complex DOPASOWANIE_Zastosuj(const DOPASOWANIE_WARIANT_t *wariant,
                                   float complex z_obciazenia);

#endif
