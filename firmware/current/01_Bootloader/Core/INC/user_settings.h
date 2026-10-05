/**
  ******************************************************************************
  * @file    user_settings.h
  * @brief   用户电压/电流设定的W25Q256掉电保存接口。
  ******************************************************************************
  */

#ifndef __USER_SETTINGS_H__
#define __USER_SETTINGS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint16_t voltage_setpoint_mv;
  uint16_t current_limit_ma;
  bool loaded_from_flash;
} UserSettings_Data_t;

/** @brief 从独立双备份扇区装载最后一次输出设定。 */
void UserSettings_Init(void);

/** @brief 读取当前已装载或已保存的设定。 */
void UserSettings_Get(UserSettings_Data_t *data);

/**
 * @brief 观察最新设定，并在停止调整1秒且输出关闭后保存。
 * @param voltage_mv 当前电压设定，单位毫伏。
 * @param current_ma 当前电流设定，单位毫安。
 * @param system_time_ms 当前HAL毫秒计数。
 * @param allow_write 只有真实输出关闭时传入true。
 * @retval 本轮成功写入新记录时返回true。
 */
bool UserSettings_Process(uint16_t voltage_mv,
                          uint16_t current_ma,
                          uint32_t system_time_ms,
                          bool allow_write);

#ifdef __cplusplus
}
#endif

#endif /* __USER_SETTINGS_H__ */
