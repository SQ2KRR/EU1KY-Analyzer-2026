#ifndef LACZNOSC_H_
#define LACZNOSC_H_
#include <stdint.h>
void LACZNOSC_Otworz(void);
uint32_t LACZNOSC_DokumentacjaLiczbaStron(void);
const char *LACZNOSC_DokumentacjaNazwaStrony(uint32_t strona);
void LACZNOSC_DokumentacjaRysujStrone(uint32_t strona);
#endif
