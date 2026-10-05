/**
  ******************************************************************************
  * @file    debug_console.h
  * @brief   USB串口与VG6328A蓝牙串口共用的调试控制台接口。
  ******************************************************************************
  */

#ifndef __DEBUG_CONSOLE_H__
#define __DEBUG_CONSOLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "power_control.h"
#include "power_monitor.h"
#include "temperature_sensor.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化USART1和USART2的中断收发缓冲区。
 * @retval 两路接收中断都启动成功时返回true。
 * @note   USART1连接USB转串口，USART2连接VG6328A蓝牙透传模块。
 */
bool DebugConsole_Init(void);

/**
 * @brief  处理两路收到的文本命令并周期发送结构化遥测。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @param  measurement 双路INA226最新快照。
 * @param  power_status 电源状态机最新状态。
 * @param  temperature NTC最新快照。
 * @retval 无。
 */
void DebugConsole_Process(uint32_t system_time_ms,
                          const PowerMonitor_Snapshot_t *measurement,
                          const PowerControl_Status_t *power_status,
                          const TemperatureSensor_Snapshot_t *temperature);

/**
 * @brief  将原有诊断日志广播到USB和蓝牙两个通道。
 * @param  text 以零结尾的日志字符串。
 * @retval 无。
 * @note   发送使用中断环形缓冲，不阻塞功率控制主循环。
 */
void DebugConsole_Write(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_CONSOLE_H__ */
