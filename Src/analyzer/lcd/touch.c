#include "touch.h"
#include "stm32746g_discovery_ts.h"
#include "stm32746g_discovery_lcd.h"
#include "lcd.h"
#include "config.h"

void TOUCH_Init(void)
{
    BSP_TS_Init(FT5336_MAX_WIDTH, FT5336_MAX_HEIGHT);
}

static uint32_t wakeup_touch(void)
{
    extern volatile uint32_t autosleep_timer;
    autosleep_timer = CFG_GetParam(CFG_PARAM_LOWPWR_TIME);
    if (LCD_IsOff())
    {
        BSP_LCD_DisplayOn();
        TS_StateTypeDef ts = {0};
        do
        {
            BSP_TS_GetState(&ts);
        } while (ts.touchDetected);
        return 1;
    }
    return 0;
}

uint8_t TOUCH_Poll(LCDPoint *pCoord)
{
    TS_StateTypeDef ts = {0};
    BSP_TS_GetState(&ts);
    if (ts.touchDetected)
    {
        if (wakeup_touch())
            return 0;

        if (LCD_Get_Orientation() == 1)
        {
            pCoord->x = 479 - ts.touchX[0];
            pCoord->y = 271 - ts.touchY[0];
        }
        else
        {
            pCoord->x = ts.touchX[0];
            pCoord->y = ts.touchY[0];
        }
    }
    return ts.touchDetected;
}

uint8_t TOUCH_IsPressed(void)
{
    TS_StateTypeDef ts = {0};
    BSP_TS_GetState(&ts);
    if (ts.touchDetected)
    {
        if (wakeup_touch())
            return 0;
    }
    return ts.touchDetected;
}

void TOUCH_CzekajNaPuszczenie(uint32_t stabilnie_ms)
{
    uint32_t poczatek_spokoju = 0U;

    if (stabilnie_ms == 0U)
        stabilnie_ms = 30U;

    for (;;)
    {
        if (TOUCH_IsPressed())
        {
            /* Każdy ponowny kontakt zeruje czas stabilnego puszczenia. */
            poczatek_spokoju = 0U;
        }
        else if (poczatek_spokoju == 0U)
        {
            poczatek_spokoju = HAL_GetTick();
        }
        else if ((uint32_t)(HAL_GetTick() - poczatek_spokoju) >= stabilnie_ms)
        {
            return;
        }

        HAL_Delay(2U);
    }
}
