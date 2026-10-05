/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    py32f403_it.c
  * @brief   This file provides code for the configuration
  *          of all used NVIC.
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

/* Includes ------------------------------------------------------------------*/
#include "py32f403_it.h"
/* USER CODE BEGIN Includes */
#include "spi.h"
#include "usart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Public variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Private */

/* USER CODE END Private */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV *//* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*          Cortex-M4 Processor Interruption and Exception Handlers           */
/******************************************************************************/

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NMI_Handler 0 */

  /* USER CODE END NMI_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_NMI_Handler 0 */

    /* USER CODE END W1_NMI_Handler 0 */
  }
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_Handler 0 */

  /* USER CODE END HardFault_Handler 0 */
  /* Go to infinite loop when Hard Fault exception occurs */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_Handler 0 */

    /* USER CODE END W1_HardFault_Handler 0 */
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemManage_Handler 0 */

  /* USER CODE END MemManage_Handler 0 */
  /* Go to infinite loop when Memory Manage exception occurs */
  while (1)
  {
    /* USER CODE BEGIN W1_MemManage_Handler 0 */

    /* USER CODE END W1_MemManage_Handler 0 */
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_Handler 0 */

  /* USER CODE END BusFault_Handler 0 */
  /* Go to infinite loop when Bus Fault exception occurs */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_Handler 0 */

    /* USER CODE END W1_BusFault_Handler 0 */
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_Handler 0 */

  /* USER CODE END UsageFault_Handler 0 */
  /* Go to infinite loop when Usage Fault exception occurs */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_Handler 0 */

    /* USER CODE END W1_UsageFault_Handler 0 */
  }
}

/**
 * @brief This function handles System service call via SWI instruction.
 */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVC_Handler 0 */

  /* USER CODE END SVC_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_SVC_Handler 0 */

    /* USER CODE END W1_SVC_Handler 0 */
  }
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMon_Handler 0 */

  /* USER CODE END DebugMon_Handler 0 */
  /* Go to infinite loop when Debug Monitor exception occurs */
  while (1)
  {
    /* USER CODE BEGIN W1_DebugMon_Handler 0 */

    /* USER CODE END W1_DebugMon_Handler 0 */
  }
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_Handler 0 */

  /* USER CODE END PendSV_Handler 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_PendSV_Handler 0 */

    /* USER CODE END W1_PendSV_Handler 0 */
  }
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_Handler 0 */

  /* USER CODE END SysTick_Handler 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_Handler 1 */

  /* USER CODE END SysTick_Handler 1 */
}


/******************************************************************************/
/* Puya Peripheral Interrupt Handlers */
/* Add here the Interrupt Handlers for the used peripherals. */
/* For the available peripheral interrupt handler names, */
/* please refer to the startup file. */
/******************************************************************************/

/**
 * @brief This function handles DMA1 Channel 1 interrupt.
 * @param None
 * @retval None
 */
void DMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Channel1_IRQn 0 */

  /* USER CODE END DMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi1_wr);
  /* USER CODE BEGIN DMA1_Channel1_IRQn 1 */

  /* USER CODE END DMA1_Channel1_IRQn 1 */
}

/**
 * @brief This function handles EXTI9_5_IRQn interrupt.
 * @param None
 * @retval None
 */
void EXTI9_5_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI9_5_IRQn 0 */

  /* USER CODE END EXTI9_5_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
  /* USER CODE BEGIN EXTI9_5_IRQn 1 */

  /* USER CODE END EXTI9_5_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/**
 * @brief  处理USB调试通道USART1的收发中断。
 * @retval 无。
 */
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&husart1);
}

/**
 * @brief  处理VG6328A蓝牙通道USART2的收发中断。
 * @retval 无。
 */
void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&husart2);
}

/* USER CODE END 1 */

extern DMA_HandleTypeDef hdma_usart1_tx,hdma_usart2_tx;
void DMA1_Channel2_IRQHandler(void){HAL_DMA_IRQHandler(&hdma_usart1_tx);}
void DMA1_Channel3_IRQHandler(void){HAL_DMA_IRQHandler(&hdma_usart2_tx);}
