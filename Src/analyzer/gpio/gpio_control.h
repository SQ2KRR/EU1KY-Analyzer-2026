//========================================================================================
//GPIO CONTROL for Version 0.4 ~ 1.0
//by KD8CEC
//04/15/2019
//
//PTT i zewnetrzna dioda stanu RF
//----------------------------------------------------------------------------------------

#ifndef GPIO_CONTROL_H_
#define GPIO_CONTROL_H_

#include <stdio.h>
#include <stdint.h>

#include "stm32f7xx.h"
#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_def.h"
#include "stm32746g_discovery.h"
#include "stm32f7xx_hal_adc.h"

//GPIO for PTT Control by KD8CEC
//B4
#define PTT_TX_PIN GPIO_PIN_4
#define PTT_TX_GPIO GPIOB
extern uint8_t ptt_status;
void SET_PTT(uint8_t isPTTOn);
void GPIO_PTT_Setup(void);

/* Zewnetrzna dioda stanu RF: Arduino D7 = PI3. */
#define LED_STANU_RF_PIN GPIO_PIN_3
#define LED_STANU_RF_GPIO GPIOI
void SET_LED_STANU_RF(uint8_t wlaczona);
uint8_t GET_LED_STANU_RF(void);
void GPIO_LED_STANU_RF_Setup(void);


#endif
