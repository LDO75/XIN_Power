/**
  ******************************************************************************
  * @file    buzzer.c
  * @brief   PB3/TIM2_CH2无源蜂鸣器非阻塞驱动。
  ******************************************************************************
  */

#include "buzzer.h"
#include "tim.h"

#define BUZZER_PWM_FREQUENCY_HZ           4000U
#define BUZZER_COUNTER_CLOCK_HZ         1000000U
#define BUZZER_DUTY_PERCENT                50U

static bool s_buzzer_enabled = true;
static bool s_buzzer_active = false;
static uint32_t s_buzzer_stop_time_ms = 0U;

/**
 * @brief  将TIM2_CH2比较值设为0，使PB3停止输出声音。
 * @retval 无。
 */
static void Buzzer_StopOutput(void)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);
  s_buzzer_active = false;
}

/**
 * @brief  初始化TIM2_CH2蜂鸣器PWM，默认不发声。
 * @retval PWM启动成功时返回true。
 */
bool Buzzer_Init(void)
{
  uint32_t timer_clock_hz;
  uint32_t prescaler;
  uint32_t counter_clock_hz;
  uint32_t period;

  /*
   * 当前工程APB1分频为1，TIM2时钟与PCLK1相同。
   * 先将计数频率降到约1 MHz，再产生4 kHz方波。
   */
  timer_clock_hz = HAL_RCC_GetPCLK1Freq();
  if (timer_clock_hz == 0U)
  {
    return false;
  }

  prescaler = timer_clock_hz / BUZZER_COUNTER_CLOCK_HZ;
  if (prescaler == 0U)
  {
    prescaler = 1U;
  }
  prescaler -= 1U;
  counter_clock_hz = timer_clock_hz / (prescaler + 1U);
  period = counter_clock_hz / BUZZER_PWM_FREQUENCY_HZ;
  if (period < 2U)
  {
    period = 2U;
  }
  period -= 1U;

  (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_2);
  __HAL_TIM_SET_PRESCALER(&htim2, prescaler);
  __HAL_TIM_SET_AUTORELOAD(&htim2, period);
  __HAL_TIM_SET_COUNTER(&htim2, 0U);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0U);

  s_buzzer_enabled = true;
  s_buzzer_active = false;
  s_buzzer_stop_time_ms = 0U;
  return (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) == HAL_OK);
}

/**
 * @brief  设置蜂鸣器是否允许发声。
 * @param  enabled true为允许，false为禁止并立即停止当前声音。
 * @retval 无。
 */
void Buzzer_SetEnabled(bool enabled)
{
  s_buzzer_enabled = enabled;
  if (!enabled)
  {
    Buzzer_StopOutput();
  }
}

/**
 * @brief  以4 kHz频率启动一次非阻塞蜂鸣。
 * @param  duration_ms 蜂鸣持续时间，单位为毫秒。
 * @retval 无。
 */
void Buzzer_Beep(uint16_t duration_ms)
{
  uint32_t period;
  uint32_t compare;

  if ((!s_buzzer_enabled) || (duration_ms == 0U))
  {
    return;
  }

  period = __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;
  compare = (period * BUZZER_DUTY_PERCENT) / 100U;
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, compare);
  s_buzzer_stop_time_ms = HAL_GetTick() + duration_ms;
  s_buzzer_active = true;
}

/**
 * @brief  处理蜂鸣器自动停止计时。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void Buzzer_Process(uint32_t system_time_ms)
{
  if (s_buzzer_active &&
      ((int32_t)(system_time_ms - s_buzzer_stop_time_ms) >= 0))
  {
    Buzzer_StopOutput();
  }
}
