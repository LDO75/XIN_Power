#ifndef __FAN_CONTROL_H__
#define __FAN_CONTROL_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief 启动PB4/TIM3_CH1风扇PWM，初始占空比为零。 */
bool FanControl_Init(void);

/** @brief 高于40°C时全速，其他有效温度停转；NTC故障时全速。 */
uint8_t FanControl_CalculateDuty(int16_t temperature_decic,
                                 bool temperature_valid);

/** @brief 更新风扇输出；温度传感器故障时强制全速。 */
void FanControl_Update(int16_t temperature_decic,
                       bool temperature_valid);

uint8_t FanControl_GetDutyPercent(void);

#endif /* __FAN_CONTROL_H__ */
