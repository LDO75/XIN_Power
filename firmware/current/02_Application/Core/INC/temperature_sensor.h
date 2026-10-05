/**
  ******************************************************************************
  * @file    temperature_sensor.h
  * @brief   功率板NTC温度采样、滤波与故障诊断接口。
  ******************************************************************************
  */

#ifndef __TEMPERATURE_SENSOR_H__
#define __TEMPERATURE_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  TEMPERATURE_SENSOR_FAULT_NONE = 0,
  TEMPERATURE_SENSOR_FAULT_ADC,
  TEMPERATURE_SENSOR_FAULT_SHORT,
  TEMPERATURE_SENSOR_FAULT_OPEN
} TemperatureSensor_Fault_t;

typedef struct
{
  uint16_t raw_adc;
  int16_t temperature_decic;
  TemperatureSensor_Fault_t fault;
  uint32_t sample_sequence;
  uint32_t last_update_time_ms;
  bool valid;
  bool warning;
} TemperatureSensor_Snapshot_t;

/**
 * @brief  初始化NTC采样状态并立即完成第一次测量。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 第一次温度数据有效时返回true。
 */
bool TemperatureSensor_Init(uint32_t system_time_ms);

/**
 * @brief  周期采集PA0上的NTC分压并更新滤波温度。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 本次调用产生新样本时返回true。
 */
bool TemperatureSensor_Process(uint32_t system_time_ms);

/**
 * @brief  获取最近一次NTC测量快照。
 * @param  snapshot 用于接收快照的结构体指针。
 * @retval 参数有效时返回true。
 */
bool TemperatureSensor_GetSnapshot(TemperatureSensor_Snapshot_t *snapshot);

/**
 * @brief  将12位ADC码值换算为0.1摄氏度，供测试和校准使用。
 * @param  adc_code 12位ADC码值。
 * @param  temperature_decic 用于接收温度的指针。
 * @retval ADC码值处于有效NTC范围时返回true。
 */
bool TemperatureSensor_AdcToDecic(uint16_t adc_code,
                                  int16_t *temperature_decic);

/**
 * @brief  返回便于串口诊断的NTC故障名称。
 * @param  fault NTC故障枚举。
 * @retval 始终有效的常量字符串。
 */
const char *TemperatureSensor_GetFaultName(TemperatureSensor_Fault_t fault);

#ifdef __cplusplus
}
#endif

#endif /* __TEMPERATURE_SENSOR_H__ */
