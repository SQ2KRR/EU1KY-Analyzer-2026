#ifndef SPRAWDZENIE_IF_H_
#define SPRAWDZENIE_IF_H_

#include <stdint.h>

typedef enum
{
    SPRAWDZENIE_IF_OK = 0,
    SPRAWDZENIE_IF_BRAK_SI5351,
    SPRAWDZENIE_IF_BRAK_SYGNALU,
    SPRAWDZENIE_IF_BLAD_PROBKI,
} SPRAWDZENIE_IF_STATUS_t;

typedef struct
{
    SPRAWDZENIE_IF_STATUS_t status;
    uint32_t czestotliwosc_pomiarowa_hz;
    float oczekiwana_if_hz;
    float zmierzona_if_hz;
    float odchylenie_if_hz;
    float odchylenie_proc;
    float jakosc_db;
    uint32_t xtal_ustawiony_hz;
    uint32_t xtal_oszacowany_hz;
    uint32_t sugerowany_nominal_hz;
    uint8_t mozna_sugerowac_nominal;
} SPRAWDZENIE_IF_WYNIK_t;

/*
 * Sprawdzenie wykorzystuje dwa niezależne wzorce czasu obecne w przyrządzie:
 * Si5351 tworzy dwa tony RF oddalone o częstotliwość IF, a kodek audio mierzy
 * powstały w torze analogowym ton różnicowy zegarem SAI wyprowadzonym z HSE
 * mikrokontrolera. Dzięki temu błąd ustawienia 25/27 MHz nie znosi się sam.
 *
 * Sprawdzenie potwierdza poprawność toru generator/IF; nie jest kalibracją częstotliwości w ppm.
 * Nie zapisuje ustawień i po zakończeniu wyłącza generator pomiarowy.
 */
void SPRAWDZENIE_IF_Wykonaj(SPRAWDZENIE_IF_WYNIK_t *wynik);
int SPRAWDZENIE_IF_PobierzOstatni(SPRAWDZENIE_IF_WYNIK_t *wynik);

#endif /* SPRAWDZENIE_IF_H_ */
