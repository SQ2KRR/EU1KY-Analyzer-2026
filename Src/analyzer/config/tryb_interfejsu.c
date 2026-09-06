#include "tryb_interfejsu.h"

#include "config.h"

bool TRYB_CzyZaawansowany(void)
{
    return CFG_GetParam(CFG_PARAM_TRYB_INTERFEJSU) == TRYB_INTERFEJSU_ZAAWANSOWANY;
}

bool TRYB_CzyPodstawowy(void)
{
    return !TRYB_CzyZaawansowany();
}

void TRYB_Ustaw(TRYB_INTERFEJSU_t tryb, bool zapisz)
{
    CFG_SetParam(CFG_PARAM_TRYB_INTERFEJSU,
                 tryb == TRYB_INTERFEJSU_ZAAWANSOWANY
                     ? TRYB_INTERFEJSU_ZAAWANSOWANY
                     : TRYB_INTERFEJSU_PODSTAWOWY);
    if (zapisz)
        CFG_Flush();
}
