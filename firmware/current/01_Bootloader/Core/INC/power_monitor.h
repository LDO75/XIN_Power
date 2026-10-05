/**
  ******************************************************************************
  * @file    power_monitor.h
  * @brief   输入、输出双INA226采样服务接口。
  ******************************************************************************
  */

#ifndef __POWER_MONITOR_H__
#define __POWER_MONITOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  /* 经过软件低通滤波的数据，供UI显示和电量累计使用。 */
  uint16_t input_voltage_mv;
  int32_t input_current_ua;
  uint32_t input_power_mw;
  uint16_t output_voltage_mv;
  int32_t output_current_ua;
  uint32_t output_power_mw;
  uint32_t capacity_uah;
  uint32_t energy_uwh;
  uint32_t input_error_count;
  uint32_t output_error_count;
  /* 未经过显示滤波的快速数据，供保护与软件恒流环使用。 */
  uint16_t input_voltage_raw_mv;
  int32_t input_current_raw_ua;
  uint32_t input_power_raw_mw;
  uint16_t output_voltage_raw_mv;
  int32_t output_current_raw_ua;
  uint32_t output_power_raw_mw;
  /* INA226原始换算值，不经过用户校准，供校准向导取样。 */
  uint16_t output_voltage_uncalibrated_mv;
  int32_t output_current_uncalibrated_ua;
  uint32_t sample_sequence;
  uint32_t last_valid_sample_time_ms;
  bool input_online;
  bool output_online;
  bool data_valid;
} PowerMonitor_Snapshot_t;

/**
 * @brief  初始化输入端和输出端两个INA226。
 * @retval 两个器件都初始化成功时返回true。
 * @note   单个器件失败不会阻塞系统，后台任务会继续自动重试。
 */
bool PowerMonitor_Init(void);

/**
 * @brief  周期执行双INA226采样、自动重连、电量累计和串口诊断。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void PowerMonitor_Process(uint32_t system_time_ms);

/**
 * @brief  获取最近一次完整的双路监测快照。
 * @param  snapshot 用于接收监测数据的结构体。
 * @retval 自上次读取后存在新测量数据时返回true。
 */
bool PowerMonitor_GetSnapshot(PowerMonitor_Snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_MONITOR_H__ */
