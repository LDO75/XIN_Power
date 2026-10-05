#include "fan_control.h"

#include "power_config.h"
#include "tim.h"

#define FAN_DUTY_MAX_PERCENT 100U
#define FAN_START_DECIC ((int16_t)(POWER_CFG_FAN_START_DEFAULT_C * 10U))
#define FAN_STOP_DECIC ((int16_t)(POWER_CFG_FAN_STOP_C * 10U))

static uint8_t s_duty_percent;
static bool s_pwm_ready;
static bool s_fan_running;

bool FanControl_Init(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
  s_duty_percent = 0U;
  s_fan_running = false;
  s_pwm_ready = (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) == HAL_OK);
  return s_pwm_ready;
}

uint8_t FanControl_CalculateDuty(int16_t temperature_decic,
                                 bool temperature_valid)
{
  /* NTC无有效读数时风扇继续转动；不控制电源输出开关。 */
  if (!temperature_valid)
  {
    s_fan_running = true;
    return FAN_DUTY_MAX_PERCENT;
  }
  /* 40C开启、35C关闭；中间区间保留状态，避免阈值附近反复启停。 */
  if (temperature_decic >= FAN_START_DECIC)
  {
    s_fan_running = true;
  }
  else if (temperature_decic <= FAN_STOP_DECIC)
  {
    s_fan_running = false;
  }
  return s_fan_running ? FAN_DUTY_MAX_PERCENT : 0U;
}

void FanControl_Update(int16_t temperature_decic,
                       bool temperature_valid)
{
  uint8_t duty = FanControl_CalculateDuty(temperature_decic,
                                          temperature_valid);
  uint32_t period;
  uint32_t compare;

  if (!s_pwm_ready || (duty == s_duty_percent))
  {
    return;
  }

  period = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1U;
  compare = (period * duty) / 100U;
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, compare);
  s_duty_percent = duty;
}

uint8_t FanControl_GetDutyPercent(void)
{
  return s_duty_percent;
}
