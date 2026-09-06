#include "uzytkownik.h"
#include "config.h"
#include <ctype.h>
#include <string.h>

static uint8_t UZYTKOWNIK_CzyDozwolonyZnak(char znak)
{
    unsigned char c = (unsigned char)znak;
    return (uint8_t)(isalnum(c) || znak == '/' || znak == '-');
}

void UZYTKOWNIK_PobierzZnak(char *bufor, uint32_t rozmiar_bufora)
{
    uint32_t dane[4];
    char zapisany[16];
    uint32_t i;

    if (bufor == 0 || rozmiar_bufora == 0)
        return;

    dane[0] = CFG_GetParam(CFG_PARAM_ZNAK_0);
    dane[1] = CFG_GetParam(CFG_PARAM_ZNAK_1);
    dane[2] = CFG_GetParam(CFG_PARAM_ZNAK_2);
    dane[3] = CFG_GetParam(CFG_PARAM_ZNAK_3);
    memcpy(zapisany, dane, sizeof(zapisany));
    zapisany[sizeof(zapisany) - 1] = '\0';

    for (i = 0; i + 1 < rozmiar_bufora && zapisany[i] != '\0'; ++i)
        bufor[i] = zapisany[i];
    bufor[i] = '\0';
}

void UZYTKOWNIK_UstawZnak(const char *znak)
{
    char zapisany[16] = {0};
    uint32_t dane[4] = {0};
    uint32_t i = 0;

    if (znak != 0)
    {
        while (*znak != '\0' && i < UZYTKOWNIK_ZNAK_MAX)
        {
            char c = (char)toupper((unsigned char)*znak++);
            if (UZYTKOWNIK_CzyDozwolonyZnak(c))
                zapisany[i++] = c;
        }
    }

    memcpy(dane, zapisany, sizeof(zapisany));
    CFG_SetParam(CFG_PARAM_ZNAK_0, dane[0]);
    CFG_SetParam(CFG_PARAM_ZNAK_1, dane[1]);
    CFG_SetParam(CFG_PARAM_ZNAK_2, dane[2]);
    CFG_SetParam(CFG_PARAM_ZNAK_3, dane[3]);
    CFG_Flush();
}
