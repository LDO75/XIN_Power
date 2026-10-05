/**
  ******************************************************************************
  * @file    buzzer.h
  * @brief   PB3/TIM2_CH2无源蜂鸣器非阻塞驱动接口。
  ******************************************************************************
  */

#ifndef __BUZZER_H__
#define __BUZZER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化TIM2_CH2蜂鸣器PWM，默认不发声。
 * @retval PWM启动成功时返回true。
 */
bool Buzzer_Init(void);

/**
 * @brief  设置蜂鸣器是否允许发声。
 * @param  enabled true为允许，false为禁止并立即停止当前声音。
 * @retval 无。
 */
void Buzzer_SetEnabled(bool enabled);

/**
 * @brief  以4 kHz频率启动一次非阻塞蜂鸣。
 * @param  duration_ms 蜂鸣持续时间，单位为毫秒。
 * @retval 无。
 */
void Buzzer_Beep(uint16_t duration_ms);

/**
 * @brief  处理蜂鸣器自动停止计时。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void Buzzer_Process(uint32_t system_time_ms);

#ifdef __cplusplus
}
#endif

#endif /* __BUZZER_H__ */
