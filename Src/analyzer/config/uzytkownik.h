#ifndef _UZYTKOWNIK_H_
#define _UZYTKOWNIK_H_

#include <stdint.h>

#define UZYTKOWNIK_ZNAK_MAX 15U

void UZYTKOWNIK_PobierzZnak(char *bufor, uint32_t rozmiar_bufora);
void UZYTKOWNIK_UstawZnak(const char *znak);

#endif
