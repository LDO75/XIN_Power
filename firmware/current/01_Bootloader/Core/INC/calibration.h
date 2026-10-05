/**
  ******************************************************************************
  * @file    calibration.h
  * @brief   输出测量与DAC两点校准、W25Q256持久化接口。
  ******************************************************************************
  */

#ifndef __CALIBRATION_H__
#define __CALIBRATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  int32_t voltage_gain_ppm;
  int32_t voltage_offset_mv;
  int32_t current_gain_ppm;
  int32_t current_offset_ua;
  uint16_t dac_voltage_1_mv;
  uint16_t dac_code_1;
  uint16_t dac_voltage_2_mv;
  uint16_t dac_code_2;
  bool voltage_valid;
  bool current_valid;
  bool measurement_valid;
  bool dac_valid;
  bool loaded_from_flash;
} Calibration_Data_t;

void Calibration_Init(void);
void Calibration_Get(Calibration_Data_t *data);
void Calibration_ResetDefaults(void);
bool Calibration_SetVoltage(int32_t gain_ppm, int32_t offset_mv);
bool Calibration_SetCurrent(int32_t gain_ppm, int32_t offset_ua);
/**
 * @brief  使用输出关闭时的INA226原始电流自动修正零点，并保留现有增益。
 * @param  raw_zero_ua 多帧平均后的未校准电流值，单位微安。
 * @retval 计算结果处于允许范围时返回true。
 */
bool Calibration_SetCurrentZero(int32_t raw_zero_ua);
/** @brief 一键零点校准并保存；写入失败时恢复原有运行参数。 */
bool Calibration_SaveCurrentZero(int32_t raw_zero_ua);
bool Calibration_SetDac(uint16_t voltage_1_mv,
                        uint16_t code_1,
                        uint16_t voltage_2_mv,
                        uint16_t code_2);
bool Calibration_Save(void);
uint16_t Calibration_ApplyVoltage(uint16_t raw_mv);
int32_t Calibration_ApplyCurrent(int32_t raw_ua);
bool Calibration_MapDac(uint16_t voltage_mv, uint16_t *code);

#ifdef __cplusplus
}
#endif

#endif /* __CALIBRATION_H__ */
