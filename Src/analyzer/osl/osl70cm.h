#ifndef OSL70CM_H_
#define OSL70CM_H_

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Lokalna kalibracja OSL dla pasma 70 cm.
 *
 * Zakres jest celowo staly. Dzięki temu plik kalibracji ma jednoznaczne
 * znaczenie, a program nigdy nie dopasowuje go na sile do innego pasma.
 */
#define OSL70_FMIN_HZ 420000000UL
#define OSL70_FMAX_HZ 450000000UL
#define OSL70_KROK_HZ    50000UL
#define OSL70_LICZBA_PUNKTOW (((OSL70_FMAX_HZ - OSL70_FMIN_HZ) / OSL70_KROK_HZ) + 1UL)

/*
 * Automatyczny wybor jest dozwolony tylko wtedy, gdy CALY zadany pomiar
 * miesci sie w 420..450 MHz. Zapobiega to skokowi wspolczynnikow w srodku
 * szerokiego wykresu.
 */
bool OSL70_CzyUzycDlaZakresu(uint32_t od_hz, uint32_t do_hz);

bool OSL70_IsValid(void);
int32_t OSL70_Reload(void);
const char *OSL70_Nazwa(void);

int32_t OSL70_ScanShort(void (*progresscb)(uint32_t));
int32_t OSL70_ScanLoad(void (*progresscb)(uint32_t));
int32_t OSL70_ScanOpen(void (*progresscb)(uint32_t));
int32_t OSL70_Calculate(void);

float complex OSL70_CorrectZ(uint32_t czestotliwosc_hz,
                             float complex impedancja_przed_osl);

/* Dane diagnostyczne do UI i raportow. */
uint32_t OSL70_PobierzFminHz(void);
uint32_t OSL70_PobierzFmaxHz(void);
uint32_t OSL70_PobierzKrokHz(void);
uint8_t OSL70_PobierzMetodeAkwizycji(void);

#endif /* OSL70CM_H_ */
