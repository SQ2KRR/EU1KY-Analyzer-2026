#ifndef _KOMUNIKATY_H_
#define _KOMUNIKATY_H_

#include "jezyk.h"

void KOMUNIKAT_Pokaz(TEKST_ID_t tytul, TEKST_ID_t tresc);
void KOMUNIKAT_PokazTekst(const char *tytul, const char *tresc);
void KOMUNIKAT_PokazDwa(TEKST_ID_t tytul, TEKST_ID_t tresc1, TEKST_ID_t tresc2);

#endif
