/**
  ******************************************************************************
  * @file    power_control.h
  * @brief   SC8701输出调压、软件恒流和安全保护业务接口。
  ******************************************************************************
  */

#ifndef __POWER_CONTROL_H__
#define __POWER_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "power_monitor.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  POWER_CONTROL_FAULT_NONE = 0,
  POWER_CONTROL_FAULT_MONITOR_OFFLINE,
  POWER_CONTROL_FAULT_DAC_OFFLINE,
  POWER_CONTROL_FAULT_INPUT_OVERVOLTAGE,
  POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE,
  POWER_CONTROL_FAULT_INPUT_OVERCURRENT,
  POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT,
  POWER_CONTROL_FAULT_OVERPOWER,
  POWER_CONTROL_FAULT_CC_OUT_OF_RANGE,
  POWER_CONTROL_FAULT_TEMPERATURE_SENSOR,
  POWER_CONTROL_FAULT_OVERTEMPERATURE
} PowerControl_Fault_t;

typedef struct
{
  uint16_t voltage_setpoint_mv;
  uint16_t current_limit_ma;
  int16_t temperature_decic;
  bool temperature_valid;
  bool output_requested;
} PowerControl_Request_t;

typedef struct
{
  uint16_t effective_current_limit_ma;
  uint16_t dac_code;
  PowerControl_Fault_t fault;
  bool output_enabled;
  bool constant_current;
} PowerControl_Status_t;

/**
 * @brief  初始化MCP4725并让SC8701保持安全关闭状态。
 * @retval MCP4725应答且安全码值写入成功时返回true。
 */
bool PowerControl_Init(void);

/**
 * @brief  执行输出状态机、调压、软件恒流和超限保护。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @param  request 电压、电流和输出请求。
 * @param  measurement 双INA226最新测量快照。
 * @retval 无。
 */
void PowerControl_Process(uint32_t system_time_ms,
                          const PowerControl_Request_t *request,
                          const PowerMonitor_Snapshot_t *measurement);

/**
 * @brief  读取当前输出状态、有效限流值和故障原因。
 * @param  status 用于接收状态的结构体。
 * @retval 无。
 */
void PowerControl_GetStatus(PowerControl_Status_t *status);

/**
 * @brief  在输出保持关闭时确认并清除已锁存的故障。
 * @retval DAC控制链可用且故障已清除时返回true。
 */
bool PowerControl_ClearFault(void);

/**
 * @brief  将目标输出电压换算为MCP4725理论码值。
 * @param  voltage_mv 目标输出电压，单位毫伏。
 * @retval 钳位到有效范围后的十二位DAC码值。
 */
uint16_t PowerControl_VoltageToDacCode(uint16_t voltage_mv);

/**
 * @brief  返回便于串口诊断的故障名称。
 * @param  fault 故障枚举。
 * @retval 始终有效的常量字符串。
 */
const char *PowerControl_GetFaultName(PowerControl_Fault_t fault);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_CONTROL_H__ */
