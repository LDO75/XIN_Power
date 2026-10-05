/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim3.h
  * @brief   This file provides code for the configuration
  *          of all used TIM3.
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
#ifndef __TIM3_H__
#define __TIM3_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */




extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

#define LCD_LIGHT_Pin GPIO_PIN_8
#define LCD_LIGHT_Port GPIOA

/* USER CODE BEGIN defines */

/* USER CODE END defines */

void Studio_TIM1_Init(void);
void Studio_TIM2_Init(void);
void Studio_TIM3_Init(void);




/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */


#ifdef __cplusplus
}
#endif

#endif /* __TIM3_H__ */
