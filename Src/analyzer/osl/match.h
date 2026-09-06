#ifndef _MATCH_H_
#define _MATCH_H_

#include <complex.h>
#include <stdint.h>

void MATCH_Calc(int X0, int Y0, float complex ZL);
void MATCH_OtworzAsystenta(float complex ZL, uint32_t f_hz, float z0_ohm);

#endif
