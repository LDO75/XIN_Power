/**
  ******************************************************************************
  * @file    ina226.h
  * @brief   INA226电压、电流与功率监测芯片驱动接口。
  ******************************************************************************
  */

#ifndef __INA226_H__
#define __INA226_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

#define INA226_INPUT_ADDRESS_7BIT         0x40U
#define INA226_OUTPUT_ADDRESS_7BIT        0x45U
#define INA226_INPUT_CURRENT_LSB_UA         200U
#define INA226_INPUT_POWER_LSB_MW             5U
#define INA226_INPUT_CALIBRATION_VALUE     0x1400U
#define INA226_OUTPUT_CURRENT_LSB_UA        400U
#define INA226_OUTPUT_POWER_LSB_MW           10U
#define INA226_OUTPUT_CALIBRATION_VALUE    0x0A00U
/* Continuous bus + shunt, 332us each, one average: 664us typical,
 * 730us maximum per datasheet. Host polls for new data every 2ms. */
#define INA226_CONFIGURATION_VALUE        0x4097U

typedef enum
{
  INA226_ERROR_NONE = 0,
  INA226_ERROR_INVALID_ARGUMENT,
  INA226_ERROR_NO_ACK,
  INA226_ERROR_READ_MANUFACTURER_ID,
  INA226_ERROR_BAD_MANUFACTURER_ID,
  INA226_ERROR_READ_DIE_ID,
  INA226_ERROR_BAD_DIE_ID,
  INA226_ERROR_WRITE_CONFIGURATION,
  INA226_ERROR_WRITE_CALIBRATION,
  INA226_ERROR_READ_CONFIGURATION,
  INA226_ERROR_READ_CALIBRATION,
  INA226_ERROR_VERIFY_CONFIGURATION,
  INA226_ERROR_VERIFY_CALIBRATION,
  INA226_ERROR_READ_BUS_VOLTAGE,
  INA226_ERROR_READ_SHUNT_VOLTAGE,
  INA226_ERROR_READ_CURRENT,
  INA226_ERROR_READ_POWER,
  INA226_ERROR_READ_READY
} INA226_Error_t;

typedef struct
{
  I2C_HandleTypeDef *i2c;
  uint8_t address_7bit;
  uint16_t manufacturer_id;
  uint16_t die_id;
  uint16_t calibration_value;
  uint16_t current_lsb_ua;
  uint16_t power_lsb_mw;
  uint16_t configuration_readback;
  uint16_t calibration_readback;
  uint32_t i2c_error_code;
  uint32_t total_error_count;
  uint8_t consecutive_error_count;
  INA226_Error_t last_error;
  bool ready;
} INA226_Device_t;

typedef struct
{
  uint16_t bus_voltage_mv;
  int32_t current_ua;
  uint32_t power_mw;
} INA226_Measurement_t;

/**
 * @brief  清空一个INA226设备对象，仅在系统首次初始化时调用。
 * @param  device INA226设备对象。
 * @retval 无。
 * @note   自动重连时不要调用，否则会丢失累计错误次数。
 */
void INA226_ResetDevice(INA226_Device_t *device);

/**
 * @brief  初始化一个INA226并写入该设备独立的测量参数。
 * @param  device INA226设备对象。
 * @param  i2c 设备所在I2C总线句柄。
 * @param  address_7bit INA226的7位I2C地址。
 * @param  calibration_value 校准寄存器值。
 * @param  current_lsb_ua 电流寄存器每位对应的微安数。
 * @param  power_lsb_mw 功率寄存器每位对应的毫瓦数。
 * @retval 初始化和寄存器回读全部成功时返回true。
 */
bool INA226_Init(INA226_Device_t *device,
                 I2C_HandleTypeDef *i2c,
                 uint8_t address_7bit,
                 uint16_t calibration_value,
                 uint16_t current_lsb_ua,
                 uint16_t power_lsb_mw);

/**
 * @brief  读取一组完整的母线电压、分流电压、电流和功率数据。
 * @param  device INA226设备对象。
 * @param  measurement 用于接收转换后测量值的结构体。
 * @retval 转换完成且电压、电流读取成功时返回true；未完成不计通信错误。
 * @note   任一寄存器失败时不会修改measurement，避免提交不完整数据。
 */
/* Foreground hook immediately after decoding fresh current, before reading
 * voltage. A voltage-read failure cannot suppress overcurrent shutdown. */
void INA226_OnCurrentReady(const INA226_Device_t *device,int32_t current_ua);

bool INA226_ReadMeasurement(INA226_Device_t *device,
                            INA226_Measurement_t *measurement);

/**
 * @brief  判断INA226当前是否已经完成初始化并可读取。
 * @param  device INA226设备对象。
 * @retval 设备可用时返回true。
 */
bool INA226_IsReady(const INA226_Device_t *device);

/**
 * @brief  将INA226错误阶段转换为便于串口查看的英文短字符串。
 * @param  error 错误阶段枚举。
 * @retval 始终有效的常量字符串。
 */
const char *INA226_GetErrorName(INA226_Error_t error);

#ifdef __cplusplus
}
#endif

#endif /* __INA226_H__ */
