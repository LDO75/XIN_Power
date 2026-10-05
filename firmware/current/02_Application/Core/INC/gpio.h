/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   This file provides code for the configuration
  *          of all used GPIO.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the License ; You may not use this file except in compliance with the
  * License.You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */



#define LCD_CS_Pin GPIO_PIN_4
#define LCD_CS_Port GPIOA
#define LCD_DC_Pin GPIO_PIN_0
#define LCD_DC_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_1
#define LCD_RST_Port GPIOB
#define TP_INT_Pin GPIO_PIN_8
#define TP_INT_Port GPIOB
#define TP_RST_Pin GPIO_PIN_9
#define TP_RST_Port GPIOB
#define FLA_CS_Pin GPIO_PIN_12
#define FLA_CS_Port GPIOB
#define P_EN_Pin GPIO_PIN_13
#define P_EN_Port GPIOC

/* USER CODE BEGIN defines */

/* USER CODE END defines */

void Studio_GPIO_Init(void);


/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */


#ifdef __cplusplus
}
#endif

#endif /* __GPIO_H__ */
