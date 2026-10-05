/**
  ******************************************************************************
  * @file    power_control.h
  * @brief   SC8701校准区间DAC调压、直接DAC校准和基本保护关断接口。
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
  POWER_CONTROL_FAULT_MONITOR_OFFLINE = 1,
  POWER_CONTROL_FAULT_INPUT_UNDERVOLTAGE = 2,
  POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE = 4,
  POWER_CONTROL_FAULT_OVERTEMPERATURE = 10,
  POWER_CONTROL_FAULT_INPUT_OVERCURRENT = 5,
  POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT = 6
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
  bool pid_enabled;
  int32_t pid_error_mv;
  int16_t pid_delta;
  uint16_t pid_feedback_mv;
  uint32_t pid_updates;
  uint32_t pid_revision;
  uint32_t pid_interval_ms;
  uint32_t dac_write_count;
  uint32_t dac_write_time_ms;
  bool ce_gpio_high; /* MCU pin reading, not an analogue measurement at SC8701. */
} PowerControl_Status_t;

/**
 * @brief  初始化MCP4725并让SC8701保持安全关闭状态。
 * @retval MCP4725应答且安全码值写入成功时返回true。
 */
bool PowerControl_Init(void);

/**
 * @brief  正常输出DAC映射与小误差PI微调；手动校准固定DAC；保护锁存关断。
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
/* Cooperative foreground protection pass during LCD drawing. Never enables
 * output, changes setpoints, clears a fault, or adjusts a working DAC. */
void PowerControl_ServiceProtections(uint32_t now,
  const PowerControl_Request_t *request, const PowerMonitor_Snapshot_t *measurement);

void PowerControl_GetStatus(PowerControl_Status_t *status);

/**
 * @brief  在输出保持关闭时确认并清除已锁存的故障。
 * @retval 故障已清除时返回true。
 */
bool PowerControl_ClearFault(void);

/**
 * @brief  将目标输出电压换算为MCP4725理论码值。
 * @param  voltage_mv DAC映射电压，单位毫伏；用户范围为3.2-32V。
 * @retval 钳位到有效范围后的十二位DAC码值。
 */
uint16_t PowerControl_VoltageToDacCode(uint16_t voltage_mv);
/* Fresh-sample voltage trim, called only after current/protection services. */
void PowerControl_ServiceVoltageTrim(uint32_t now,const PowerControl_Request_t *request,
  const PowerMonitor_Snapshot_t *measurement);
uint16_t PowerControl_GetRippleDacCode(void);
/* Legacy compatibility no-op: calibration is never learned by the PI loop. */
void PowerControl_ResetVoltageLearning(void);
/* Manual DAC calibration. ready only acknowledges the applied DAC code. */
bool PowerControl_IsDacCalibrationActive(void);
bool PowerControl_BeginDacCalibration(uint16_t target_mv);
bool PowerControl_DacCalibrationReady(const PowerMonitor_Snapshot_t *measurement);
bool PowerControl_StepDacCalibration(int16_t delta,
                                     const PowerMonitor_Snapshot_t *measurement);
void PowerControl_EndDacCalibration(void);

/**
 * @brief  返回便于串口诊断的故障名称。
 * @param  fault 故障枚举。
 * @retval 始终有效的常量字符串。
 */
const char *PowerControl_GetFaultName(PowerControl_Fault_t fault);
/* Frozen trigger snapshot, not the voltage after CE has already shut off. */
const char *PowerControl_GetFaultContext(void);

void PowerControl_ServiceCurrentSample(uint32_t now, bool output,
  int32_t calibrated_ua, int32_t uncalibrated_ua, uint16_t voltage_mv,
  const PowerControl_Request_t *request, const PowerMonitor_Snapshot_t *measurement);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_CONTROL_H__ */
