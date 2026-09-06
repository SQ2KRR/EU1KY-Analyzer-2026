//========================================================================================
//GPIO CONTROL for Version 0.4 ~ 1.0
//by KD8CEC
//04/15/2019
//
//PTT i zewnetrzna dioda stanu RF
//----------------------------------------------------------------------------------------
//GPIO for PTT Control by KD8CEC
//B4

#include "gpio_control.h"

uint8_t ptt_status = 0;
static uint8_t led_stanu_rf = 0U;
void SET_PTT(uint8_t isPTTOn)
{
    ptt_status = isPTTOn;
    HAL_GPIO_WritePin(PTT_TX_GPIO, PTT_TX_PIN, ptt_status);
}

void GPIO_PTT_Setup(void)
{
    GPIO_InitTypeDef gpioInitStructure;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpioInitStructure.Pin = PTT_TX_PIN;
    gpioInitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInitStructure.Pull = GPIO_NOPULL; //
    gpioInitStructure.Speed = GPIO_SPEED_MEDIUM;
    HAL_GPIO_Init(PTT_TX_GPIO, &gpioInitStructure);
}

void SET_LED_STANU_RF(uint8_t wlaczona)
{
    led_stanu_rf = wlaczona ? 1U : 0U;
    HAL_GPIO_WritePin(LED_STANU_RF_GPIO, LED_STANU_RF_PIN,
                      led_stanu_rf ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t GET_LED_STANU_RF(void)
{
    return led_stanu_rf;
}

void GPIO_LED_STANU_RF_Setup(void)
{
    GPIO_InitTypeDef gpioInitStructure = {0};

    __HAL_RCC_GPIOI_CLK_ENABLE();
    gpioInitStructure.Pin = LED_STANU_RF_PIN;
    gpioInitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInitStructure.Pull = GPIO_NOPULL;
    gpioInitStructure.Speed = GPIO_SPEED_LOW;
    HAL_GPIO_Init(LED_STANU_RF_GPIO, &gpioInitStructure);

    /* Bezpieczny stan po uruchomieniu: dioda zgaszona, RF nieaktywne. */
    SET_LED_STANU_RF(0U);
}

/* PG6 i PG7 sa teraz obslugiwane przez wejscia_uzytkownika.c jako enkoder. */
