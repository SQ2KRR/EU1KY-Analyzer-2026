#ifndef _PROJEKTOWANIE_ANTEN_H_
#define _PROJEKTOWANIE_ANTEN_H_

#include <stdint.h>

void ANTENY_Otworz(void);

uint32_t ANTENY_DokumentacjaLiczbaStron(void);
const char *ANTENY_DokumentacjaNazwaStrony(uint32_t strona);
void ANTENY_DokumentacjaRysujStrone(uint32_t strona);

#endif
