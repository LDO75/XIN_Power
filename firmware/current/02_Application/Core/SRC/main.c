/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   This file provides code for the configuration
  *          of all used MAIN.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Mcu Studio under BSD 3-Clause license,
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
#include "main.h"
#include "rcc.h"
#include "gpio.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "i2c.h"
#include "adc.h"
#include "usart.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cst836u.h"
#include "calibration.h"
#include "debug_console.h"
#include "fan_control.h"
#include "power_config.h"
#include "power_control.h"
#include "power_monitor.h"
#include "temperature_sensor.h"
#include "user_settings.h"
#include "lcd.h"
#include "ota_updater.h"
#include "splash_logo.h"
#include "ui_page.h"
#include <limits.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_TOUCH_RECOVERY_PERIOD_MS       1000U
#define APP_NTC_DEBUG_PERIOD_MS            1000U
#define APP_CURRENT_PLAUSIBLE_MAX_UA      14000000L
#define APP_CURRENT_PLAUSIBLE_MIN_UA      (-2000000L)
#define APP_INPUT_PLAUSIBLE_MAX_MV          30000U
#define APP_OUTPUT_PLAUSIBLE_MAX_MV         34000U
#define APP_LOCAL_CALIBRATION_SAMPLE_COUNT      64U
#define APP_LOCAL_CALIBRATION_TIMEOUT_MS       3000U
#define APP_LOCAL_CALIBRATION_MAX_ZERO_UA     20000L
#define APP_LOCAL_CALIBRATION_MAX_SPAN_UA      8000L
#define APP_UI_CURRENT_ZERO_DEADBAND_UA         1000L

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static bool s_touch_available = false;
static PowerMonitor_Snapshot_t s_power_snapshot;
static PowerControl_Status_t s_power_status;
static UI_ControlRequest_t s_control_request;
static PowerControl_Request_t s_power_request;
static TemperatureSensor_Snapshot_t s_temperature_snapshot;
static uint32_t s_last_touch_recovery_time_ms;
static uint32_t s_last_ntc_debug_time_ms;
static int64_t s_local_calibration_sum_ua;
static uint32_t s_local_calibration_start_time_ms;
static uint32_t s_local_calibration_last_sample_ms;
static uint32_t s_local_calibration_last_sequence;
static int32_t s_local_calibration_min_ua;
static int32_t s_local_calibration_max_ua;
static uint8_t s_local_calibration_sample_count;
static bool s_local_calibration_active;
static uint32_t s_reset_csr;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint16_t App_CurrentUaToMa(int32_t current_ua);
static bool App_IsMeasurementPlausible(
  const PowerMonitor_Snapshot_t *measurement);
static void App_PrintResetCause(void);
static void App_ProcessLocalCalibration(uint32_t system_time_ms,
                                        bool measurement_updated);

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */


/* USER CODE BEGIN 0 */

/**
 * @brief  将INA226有符号微安值转换为UI使用的非负毫安值。
 * @param  current_ua INA226电流值，单位为微安。
 * @retval 四舍五入并限制范围后的毫安值。
 */
static uint16_t App_CurrentUaToMa(int32_t current_ua)
{
  uint32_t current_ma;

  if ((current_ua <= APP_UI_CURRENT_ZERO_DEADBAND_UA) ||
      (current_ua > APP_CURRENT_PLAUSIBLE_MAX_UA))
  {
    return 0U;
  }

  current_ma = ((uint32_t)current_ua + 500U) / 1000U;
  return (current_ma > UINT16_MAX) ? UINT16_MAX : (uint16_t)current_ma;
}

/** @brief 打印本次启动前由RCC保存的复位来源。 */
static void App_PrintResetCause(void)
{
  char text[192];

  (void)snprintf(text, sizeof(text),
                 "[RESET] csr=%08lX pin=%u power=%u soft=%u iwdg=%u wwdg=%u lowpower=%u option=%u\r\n",
                 (unsigned long)s_reset_csr,
                 (s_reset_csr & RCC_CSR_PINRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_PWRRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_SFTRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_IWDGRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_WWDGRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_LPWRRSTF) ? 1U : 0U,
                 (s_reset_csr & RCC_CSR_OBLRSTF) ? 1U : 0U);
  DebugConsole_Write(text);
}

/**
 * @brief  检查送往UI的数据是否处于本机硬件可能产生的范围。
 * @param  measurement 双路INA226测量快照。
 * @retval 数据完整且物理范围合理时返回true。
 * @note   该检查用于阻止损坏或错位数据绘制成58A等不可能数值，
 *         真正的输出保护仍由PowerControl完成。
 */
static bool App_IsMeasurementPlausible(
  const PowerMonitor_Snapshot_t *measurement)
{
  if ((measurement == NULL) || !measurement->data_valid)
  {
    return false;
  }

  return ((measurement->input_voltage_mv <= APP_INPUT_PLAUSIBLE_MAX_MV) &&
          (measurement->output_voltage_mv <= APP_OUTPUT_PLAUSIBLE_MAX_MV) &&
          (measurement->input_current_ua >= APP_CURRENT_PLAUSIBLE_MIN_UA) &&
          (measurement->input_current_ua <= APP_CURRENT_PLAUSIBLE_MAX_UA) &&
          (measurement->output_current_ua >= APP_CURRENT_PLAUSIBLE_MIN_UA) &&
          (measurement->output_current_ua <= APP_CURRENT_PLAUSIBLE_MAX_UA));
}

/**
 * @brief  每秒输出一次NTC原始值和换算温度，便于首板校准。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */


/**
 * @brief  执行本地输出电流零点自动校准。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @param  measurement_updated 本轮双INA226快照是否更新。
 * @retval 无。
 * @note   只在输出请求和真实输出均关闭时采集64帧未校准电流。
 */
static void App_ProcessLocalCalibration(uint32_t system_time_ms,
                                        bool measurement_updated)
{
  if (UI_TakeCurrentZeroCalibrationRequest())
  {
    if (s_control_request.output_requested || s_power_status.output_enabled ||
        UserSettings_IsSaving() || OtaUpdater_IsBusy() ||
        !s_power_snapshot.data_valid || !s_power_snapshot.output_online)
    {
      UI_SetCalibrationStatus(UI_CALIBRATION_FAILED);
    }
    else
    {
      s_local_calibration_sum_ua = 0;
      s_local_calibration_sample_count = 0U;
      s_local_calibration_min_ua = INT32_MAX;
      s_local_calibration_max_ua = INT32_MIN;
      s_local_calibration_last_sequence = s_power_snapshot.sample_sequence;
      s_local_calibration_start_time_ms = system_time_ms;
      s_local_calibration_last_sample_ms = system_time_ms;
      s_local_calibration_active = true;
      UI_SetCalibrationStatus(UI_CALIBRATION_RUNNING);
      DebugConsole_Write("[CAL] local current-zero sampling started\r\n");
    }
  }

  if (!s_local_calibration_active)
  {
    return;
  }
  if (s_control_request.output_requested || s_power_status.output_enabled ||
      ((system_time_ms - s_local_calibration_start_time_ms) >=
       APP_LOCAL_CALIBRATION_TIMEOUT_MS))
  {
    s_local_calibration_active = false;
    UI_SetCalibrationStatus(UI_CALIBRATION_FAILED);
    DebugConsole_Write("[CAL] local current-zero aborted\r\n");
    return;
  }

  if (measurement_updated && s_power_snapshot.data_valid &&
      (system_time_ms - s_local_calibration_last_sample_ms >= 20U) &&
      (s_power_snapshot.sample_sequence != s_local_calibration_last_sequence))
  {
    int32_t raw_zero_ua = s_power_snapshot.output_current_uncalibrated_ua;
    int32_t absolute_zero_ua = (raw_zero_ua < 0) ? -raw_zero_ua : raw_zero_ua;

    s_local_calibration_last_sequence = s_power_snapshot.sample_sequence;
    s_local_calibration_last_sample_ms = system_time_ms;
    if (absolute_zero_ua > APP_LOCAL_CALIBRATION_MAX_ZERO_UA)
    {
      s_local_calibration_active = false;
      UI_SetCalibrationStatus(UI_CALIBRATION_FAILED);
      DebugConsole_Write("[CAL] local current-zero rejected: load detected\r\n");
      return;
    }
    if (raw_zero_ua < s_local_calibration_min_ua)
    {
      s_local_calibration_min_ua = raw_zero_ua;
    }
    if (raw_zero_ua > s_local_calibration_max_ua)
    {
      s_local_calibration_max_ua = raw_zero_ua;
    }
    s_local_calibration_sum_ua += raw_zero_ua;
    s_local_calibration_sample_count++;
    if (s_local_calibration_sample_count >=
        APP_LOCAL_CALIBRATION_SAMPLE_COUNT)
    {
      int32_t average_zero_ua = (int32_t)(
        s_local_calibration_sum_ua / APP_LOCAL_CALIBRATION_SAMPLE_COUNT);
      int32_t sample_span_ua = s_local_calibration_max_ua -
                               s_local_calibration_min_ua;
      bool saved = (sample_span_ua <= APP_LOCAL_CALIBRATION_MAX_SPAN_UA) &&
                   Calibration_SaveCurrentZero(average_zero_ua);

      s_local_calibration_active = false;
      UI_SetCalibrationStatus(saved ? UI_CALIBRATION_SAVED :
                                        UI_CALIBRATION_FAILED);
      DebugConsole_Write(saved ?
        "[CAL] local current-zero saved to W25Q256\r\n" :
        "[CAL] local current-zero save failed\r\n");
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
static bool s_power_service_ready, s_power_service_busy, s_fast_measurement_pending;

/* Foreground only: LCD yields between bounded transfers and canvas rows.
 * No I2C/formatting/UI work is added to interrupt handlers. */
void LCD_BackgroundService(void)
{
  if (!s_power_service_ready || s_power_service_busy) { return; }
  s_power_service_busy = true;
  PowerControl_Status_t ripple_status;
  PowerControl_GetStatus(&ripple_status);
  PowerMonitor_SetRippleContext(ripple_status.output_enabled,
                                PowerControl_GetRippleDacCode(), HAL_GetTick());
  PowerMonitor_Process(HAL_GetTick());
  s_fast_measurement_pending |= PowerMonitor_GetSnapshot(&s_power_snapshot);
  PowerControl_ServiceProtections(HAL_GetTick(), &s_power_request, &s_power_snapshot);
  PowerControl_ServiceVoltageTrim(HAL_GetTick(), &s_power_request, &s_power_snapshot);
  s_power_service_busy = false;
}

void PowerMonitor_OnCurrentSample(bool output, int32_t calibrated_ua,
  int32_t uncalibrated_ua, uint16_t voltage_mv, uint32_t now)
{
  if (!s_power_service_ready) { return; }
  PowerControl_ServiceCurrentSample(now,output,calibrated_ua,uncalibrated_ua,
    voltage_mv,&s_power_request,&s_power_snapshot);
}

int main(void)
{
  /* USER CODE BEGIN 1 */
  s_reset_csr = RCC->CSR;
  __HAL_RCC_CLEAR_RESET_FLAGS();

  /* USER CODE END 1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  Studio_GPIO_Init();
  /* Fail-safe output gate is asserted before any peripheral probing. */
  HAL_GPIO_WritePin(P_EN_Port, P_EN_Pin, GPIO_PIN_SET);
  Studio_DMA_Init();
  Studio_SPI1_Init();
  Studio_TIM1_Init();
  Studio_I2C1_Init();
  Studio_I2C2_Init();
  Studio_ADC1_Init();
  Studio_TIM2_Init();
  Studio_TIM3_Init();
  Studio_SPI2_Init();
  Studio_USART1_Init();
  Studio_USART2_Init();
  /* USER CODE BEGIN 2 */
  /* 先从W25Q256双备份区装载校准参数，再启动采样和DAC控制。 */
  Calibration_Init();
  UserSettings_Init();
  /* 先启动双串口接收，后续初始化日志即可同时从USB和蓝牙查看。 */
  (void)DebugConsole_Init();
  App_PrintResetCause();
  if (!FanControl_Init())
  {
    DebugConsole_Write("[FAN] PWM start failed\r\n");
  }
  /* 所有监测与DAC初始化完成前，始终保持真实功率输出关闭。 */
  HAL_GPIO_WritePin(P_EN_Port, P_EN_Pin, GPIO_PIN_SET);

  if (!LCD_Init())
  {
    Error_Handler();
  }

  s_touch_available = CST836U_Init();
  (void)PowerMonitor_Init();
  (void)TemperatureSensor_Init(HAL_GetTick());
  (void)TemperatureSensor_GetSnapshot(&s_temperature_snapshot);
  (void)PowerControl_Init();
  if (!s_touch_available)
  {
    /* 初次探测失败时在总线初始化完成后立即补探一次。 */
    s_touch_available = CST836U_TryRecover();
  }
  /* The supplied logo is shown for one second after all normal probing. */
  SplashLogo_Draw();
  HAL_Delay(1000U);
  /* 耗时的I2C扫描结束后再启动UI防误触计时，避免保护窗口提前耗尽。 */
  UI_Init(s_touch_available);
  {
    UserSettings_Data_t saved_settings;
    UserSettings_Get(&saved_settings);
    if (saved_settings.loaded_from_flash)
    {
      (void)UI_SetVoltageSetpoint(saved_settings.voltage_setpoint_mv);
      (void)UI_SetCurrentLimit(saved_settings.current_limit_ma);
      (void)UI_SetScreenSleepSeconds(saved_settings.screen_sleep_seconds);
      DebugConsole_Write("[SETTINGS] restored voltage/current setpoints\r\n");
    }
  }
  /* Start the unchanged OTA trial/confirmation flow. */
  OtaUpdater_StartTrial(HAL_GetTick());
  s_last_touch_recovery_time_ms = HAL_GetTick();
  s_last_ntc_debug_time_ms = HAL_GetTick();
  s_local_calibration_active = false;
  s_power_service_ready = true;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while(1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    uint32_t system_time_ms = HAL_GetTick();
    uint16_t output_current_ma;
    bool measurement_updated;
    bool touch_ready;

    LCD_BackgroundService();
    DebugConsole_Service(system_time_ms);

    /* 触摸初始化偶发失败或运行中掉线时，每秒低频探测一次并同步UI。 */
    touch_ready = CST836U_IsReady();
    if (!touch_ready &&
        ((system_time_ms - s_last_touch_recovery_time_ms) >=
         APP_TOUCH_RECOVERY_PERIOD_MS))
    {
      s_last_touch_recovery_time_ms = system_time_ms;
      touch_ready = CST836U_TryRecover();
    }
    if (touch_ready != s_touch_available)
    {
      s_touch_available = touch_ready;
      UI_SetTouchAvailable(s_touch_available);
    }

    /* Sampling/current shutdown have already run before touch on shared I2C1. */
    UI_ProcessInput(system_time_ms);
    if (OtaUpdater_IsBusy())
    {
      UI_ForceOutputOff();
    }
    /* 页面重绘使用同步SPI，后续控制周期必须取实际时间而非绘制前时间。 */
    system_time_ms = HAL_GetTick();
    (void)TemperatureSensor_Process(system_time_ms);
    (void)TemperatureSensor_GetSnapshot(&s_temperature_snapshot);
    LCD_BackgroundService();
    measurement_updated = s_fast_measurement_pending;
    s_fast_measurement_pending = false;
    /* Apply remote OUT/SET requests in this control pass, not after drawing. */
    DebugConsole_Process(HAL_GetTick(), &s_power_snapshot,
                         &s_power_status, &s_temperature_snapshot);
    system_time_ms = HAL_GetTick();
    UI_GetControlRequest(&s_control_request);
    FanControl_Update(s_temperature_snapshot.temperature_decic,
                      s_temperature_snapshot.valid);
    if (UI_TakeFaultClearRequest())
    {
      (void)PowerControl_ClearFault();
      UI_SetPowerFault(false);
    }
    s_power_request.voltage_setpoint_mv = s_control_request.voltage_setpoint_mv;
    s_power_request.current_limit_ma = s_control_request.current_limit_ma;
    s_power_request.temperature_decic =
      s_temperature_snapshot.temperature_decic;
    s_power_request.temperature_valid = s_temperature_snapshot.valid;
    s_power_request.output_requested = s_control_request.output_requested &&
                                       !OtaUpdater_IsBusy();
    PowerControl_Process(system_time_ms,
                         &s_power_request,
                         &s_power_snapshot);
    PowerControl_GetStatus(&s_power_status);
    UI_SetPowerFault(s_power_status.fault != POWER_CONTROL_FAULT_NONE);
    App_ProcessLocalCalibration(system_time_ms, measurement_updated);

    (void)UserSettings_SetScreenSleepSeconds(s_control_request.screen_sleep_seconds,
                                            system_time_ms);
    if (UserSettings_Process(s_control_request.voltage_setpoint_mv,
                             s_control_request.current_limit_ma,
                             system_time_ms,
                             !s_control_request.output_requested &&
                             !s_power_status.output_enabled &&
                             !s_local_calibration_active && !OtaUpdater_IsBusy()))
    {
      DebugConsole_Write("[SETTINGS] voltage/current setpoints saved\r\n");
    }

    if (s_power_status.fault != POWER_CONTROL_FAULT_NONE)
    {
      UI_ForceOutputOff();
    }

    if (measurement_updated &&
        App_IsMeasurementPlausible(&s_power_snapshot))
    {
      output_current_ma = App_CurrentUaToMa(s_power_snapshot.output_current_ua);

      UI_SetMeasurements(s_power_snapshot.input_voltage_mv,
                         s_power_snapshot.output_voltage_mv,
                         output_current_ma,
                         s_power_snapshot.output_power_mw,
                         s_temperature_snapshot.temperature_decic,
                         s_power_status.constant_current);
      UI_SetRipple(s_power_snapshot.ripple_vpp_mv,s_power_snapshot.ripple_valid);
    }


    if (!UserSettings_IsSaving())
    {
    OtaUpdater_Process(HAL_GetTick(),
                       (s_power_status.fault == POWER_CONTROL_FAULT_NONE));
    }
    UI_Render(HAL_GetTick());
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief  HAL GPIO外部中断回调函数。
 * @param  GPIO_Pin HAL EXTI处理程序上报的引脚掩码。
 * @retval 无。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == TP_INT_Pin)
  {
    CST836U_NotifyInterrupt();
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
