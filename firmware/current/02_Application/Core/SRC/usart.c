/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of all used USART.
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
#include "usart.h"
/* USER CODE BEGIN Includes */

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
UART_HandleTypeDef husart1;
UART_HandleTypeDef husart2;
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

/* USER CODE END EV */
/***************************************
 * @brief USART1 Init
 **************************************/
void Studio_USART1_Init(void)
{
  /* USER CODE BEGIN Studio_USART1_Init 0 */

  /* USER CODE END Studio_USART1_Init 0 */

  husart1.Instance = USART1;
  husart1.Init.BaudRate = 115200;
  husart1.Init.WordLength = UART_WORDLENGTH_8B;
  husart1.Init.Parity = UART_PARITY_NONE;
  husart1.Init.StopBits = UART_STOPBITS_1;
  husart1.Init.Mode = UART_MODE_TX_RX;
  husart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  husart1.Init.OverSampling = UART_OVERSAMPLING_16;
  husart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&husart1) != HAL_OK)
  {
    Error_Handler();
  }
  
  /* USER CODE BEGIN Studio_USART1_Init 1 */

  /* USER CODE END Studio_USART1_Init 1 */
}
/***************************************
 * @brief USART2 Init
 **************************************/
void Studio_USART2_Init(void)
{
  /* USER CODE BEGIN Studio_USART2_Init 0 */

  /* USER CODE END Studio_USART2_Init 0 */

  husart2.Instance = USART2;
  husart2.Init.BaudRate = 115200;
  husart2.Init.WordLength = UART_WORDLENGTH_8B;
  husart2.Init.Parity = UART_PARITY_NONE;
  husart2.Init.StopBits = UART_STOPBITS_1;
  husart2.Init.Mode = UART_MODE_TX_RX;
  husart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  husart2.Init.OverSampling = UART_OVERSAMPLING_16;
  husart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&husart2) != HAL_OK)
  {
    Error_Handler();
  }
  
  /* USER CODE BEGIN Studio_USART2_Init 1 */

  /* USER CODE END Studio_USART2_Init 1 */
}

DMA_HandleTypeDef hdma_usart1_tx,hdma_usart2_tx;
static void USART_TxDma(UART_HandleTypeDef *uart,DMA_HandleTypeDef *dma,DMA_Channel_TypeDef *channel,uint32_t map)
{
  dma->Instance=channel;dma->Init.Direction=DMA_MEMORY_TO_PERIPH;
  dma->Init.PeriphInc=DMA_PINC_DISABLE;dma->Init.MemInc=DMA_MINC_ENABLE;
  dma->Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;dma->Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
  dma->Init.Mode=DMA_NORMAL;dma->Init.Priority=DMA_PRIORITY_LOW;
  HAL_DMA_ChannelMap(dma,map);
  if(HAL_DMA_Init(dma)!=HAL_OK){Error_Handler();}
  __HAL_LINKDMA(uart,hdmatx,*dma);
}

void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (huart->Instance == USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    *PA9     ------> USART1_TX
    *PA10     ------> USART1_RX
    */

    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    USART_TxDma(huart,&hdma_usart1_tx,DMA1_Channel2,DMA_CHANNEL_MAP_USART1_WR);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
  if (huart->Instance == USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    *PA2     ------> USART2_TX
    *PA3     ------> USART2_RX
    */

    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    USART_TxDma(huart,&hdma_usart2_tx,DMA1_Channel3,DMA_CHANNEL_MAP_USART2_WR);
  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
  if (huart->Instance == USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    __HAL_RCC_USART1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);
    HAL_DMA_DeInit(&hdma_usart1_tx);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
  if (huart->Instance == USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    __HAL_RCC_USART2_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);
    HAL_DMA_DeInit(&hdma_usart2_tx);
  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
