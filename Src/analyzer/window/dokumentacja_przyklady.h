#ifndef DOKUMENTACJA_PRZYKLADY_H_
#define DOKUMENTACJA_PRZYKLADY_H_

#include <stdint.h>

/*
 * Przykłady do instrukcji nie wykonują pomiaru. Rysują deterministyczne
 * stany demonstracyjne, aby instrukcja mogła pokazać wygląd poprawnych
 * wyników, różnice ustawień i typowe przebiegi bez podłączonego RF.
 * Diagnostyka jest wyjątkiem: nie wolno jej syntetyzować, bo stan ma wynikać
 * z rzeczywistego sprzętu i rzeczywistych zapisów kalibracji.
 */
uint32_t DOK_PRZYKLADY_LiczbaStron(void);
const char *DOK_PRZYKLADY_NazwaStrony(uint32_t strona);
void DOK_PRZYKLADY_RysujStrone(uint32_t strona);

#endif
