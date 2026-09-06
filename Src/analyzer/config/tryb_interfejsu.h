#ifndef _TRYB_INTERFEJSU_H_
#define _TRYB_INTERFEJSU_H_

#include <stdbool.h>

typedef enum
{
    TRYB_INTERFEJSU_ZAAWANSOWANY = 0,
    TRYB_INTERFEJSU_PODSTAWOWY = 1
} TRYB_INTERFEJSU_t;

bool TRYB_CzyZaawansowany(void);
bool TRYB_CzyPodstawowy(void);
void TRYB_Ustaw(TRYB_INTERFEJSU_t tryb, bool zapisz);

#endif
