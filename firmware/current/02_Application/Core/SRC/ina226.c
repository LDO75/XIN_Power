/**
  ******************************************************************************
  * @file    ina226.c
  * @brief   INA226电压、电流与功率监测芯片驱动实现。
  ******************************************************************************
  */

#include "ina226.h"

#include <stddef.h>
#include <string.h>

#define INA226_REG_MASK_ENABLE            0x06U
#define INA226_CONVERSION_READY           0x0008U

#define INA226_REG_CONFIGURATION          0x00U
#define INA226_REG_SHUNT_VOLTAGE          0x01U
#define INA226_REG_BUS_VOLTAGE            0x02U
#define INA226_REG_POWER                  0x03U
#define INA226_REG_CURRENT                0x04U
#define INA226_REG_CALIBRATION            0x05U
#define INA226_REG_MANUFACTURER_ID        0xFEU
#define INA226_REG_DIE_ID                 0xFFU

#define INA226_MANUFACTURER_ID_VALUE      0x5449U
#define INA226_DIE_ID_MASK                0xFFFEU
#define INA226_DIE_ID_VALUE               0x2260U
#define INA226_I2C_TIMEOUT_MS                  5U
#define INA226_READY_TRIALS                    2U

/**
 * @brief  记录一次INA226通信失败。
 * @param  device INA226设备对象。
 * @retval 无。
 */
static void INA226_RecordError(INA226_Device_t *device, INA226_Error_t error)
{
  if (device == NULL)
  {
    return;
  }

  device->total_error_count++;
  if (device->consecutive_error_count < UINT8_MAX)
  {
    device->consecutive_error_count++;
  }
  device->last_error = error;
  device->i2c_error_code = (device->i2c != NULL) ? device->i2c->ErrorCode : 0U;
}

/**
 * @brief  读取INA226的一个16位寄存器。
 * @param  device INA226设备对象。
 * @param  register_address 寄存器地址。
 * @param  value 用于接收寄存器值的指针。
 * @retval 读取成功时返回true。
 */
static bool INA226_ReadRegister(INA226_Device_t *device,
                                uint8_t register_address,
                                uint16_t *value)
{
  uint8_t data[2];

  if ((device == NULL) || (device->i2c == NULL) || (value == NULL))
  {
    return false;
  }

  if (HAL_I2C_Mem_Read(device->i2c,
                       (uint16_t)(device->address_7bit << 1U),
                       register_address,
                       I2C_MEMADD_SIZE_8BIT,
                       data,
                       sizeof(data),
                       INA226_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }

  *value = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
  return true;
}

/**
 * @brief  写入INA226的一个16位寄存器。
 * @param  device INA226设备对象。
 * @param  register_address 寄存器地址。
 * @param  value 需要写入的16位数值。
 * @retval 写入成功时返回true。
 */
static bool INA226_WriteRegister(INA226_Device_t *device,
                                 uint8_t register_address,
                                 uint16_t value)
{
  uint8_t data[2];

  if ((device == NULL) || (device->i2c == NULL))
  {
    return false;
  }

  data[0] = (uint8_t)(value >> 8U);
  data[1] = (uint8_t)value;
  if (HAL_I2C_Mem_Write(device->i2c,
                        (uint16_t)(device->address_7bit << 1U),
                        register_address,
                        I2C_MEMADD_SIZE_8BIT,
                        data,
                        sizeof(data),
                        INA226_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }

  return true;
}

/**
 * @brief  清空一个INA226设备对象，仅在系统首次初始化时调用。
 * @param  device INA226设备对象。
 * @retval 无。
 */
void INA226_ResetDevice(INA226_Device_t *device)
{
  if (device != NULL)
  {
    (void)memset(device, 0, sizeof(*device));
  }
}

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
                 uint16_t power_lsb_mw)
{
  if ((device == NULL) || (i2c == NULL) || (address_7bit > 0x7FU) ||
      (calibration_value == 0U) || (current_lsb_ua == 0U) ||
      (power_lsb_mw == 0U))
  {
    if (device != NULL)
    {
      device->last_error = INA226_ERROR_INVALID_ARGUMENT;
      device->i2c_error_code = 0U;
      device->total_error_count++;
    }
    return false;
  }

  device->i2c = i2c;
  device->address_7bit = address_7bit;
  device->calibration_value = calibration_value;
  device->current_lsb_ua = current_lsb_ua;
  device->power_lsb_mw = power_lsb_mw;
  device->manufacturer_id = 0U;
  device->die_id = 0U;
  device->configuration_readback = 0U;
  device->calibration_readback = 0U;
  device->i2c_error_code = 0U;
  device->last_error = INA226_ERROR_NONE;
  device->ready = false;

  if (HAL_I2C_IsDeviceReady(i2c,
                            (uint16_t)(address_7bit << 1U),
                            INA226_READY_TRIALS,
                            INA226_I2C_TIMEOUT_MS) != HAL_OK)
  {
    INA226_RecordError(device, INA226_ERROR_NO_ACK);
    return false;
  }

  if (!INA226_ReadRegister(device, INA226_REG_MANUFACTURER_ID,
                           &device->manufacturer_id))
  {
    INA226_RecordError(device, INA226_ERROR_READ_MANUFACTURER_ID);
    return false;
  }
  if (device->manufacturer_id != INA226_MANUFACTURER_ID_VALUE)
  {
    INA226_RecordError(device, INA226_ERROR_BAD_MANUFACTURER_ID);
    return false;
  }
  if (!INA226_ReadRegister(device, INA226_REG_DIE_ID, &device->die_id))
  {
    INA226_RecordError(device, INA226_ERROR_READ_DIE_ID);
    return false;
  }
  if ((device->die_id & INA226_DIE_ID_MASK) != INA226_DIE_ID_VALUE)
  {
    INA226_RecordError(device, INA226_ERROR_BAD_DIE_ID);
    return false;
  }

  if (!INA226_WriteRegister(device, INA226_REG_CONFIGURATION,
                            INA226_CONFIGURATION_VALUE))
  {
    INA226_RecordError(device, INA226_ERROR_WRITE_CONFIGURATION);
    return false;
  }
  if (!INA226_WriteRegister(device, INA226_REG_CALIBRATION,
                             device->calibration_value))
  {
    INA226_RecordError(device, INA226_ERROR_WRITE_CALIBRATION);
    return false;
  }
  if (!INA226_ReadRegister(device, INA226_REG_CONFIGURATION,
                           &device->configuration_readback))
  {
    INA226_RecordError(device, INA226_ERROR_READ_CONFIGURATION);
    return false;
  }
  if (!INA226_ReadRegister(device, INA226_REG_CALIBRATION,
                           &device->calibration_readback))
  {
    INA226_RecordError(device, INA226_ERROR_READ_CALIBRATION);
    return false;
  }
  if (device->configuration_readback != INA226_CONFIGURATION_VALUE)
  {
    INA226_RecordError(device, INA226_ERROR_VERIFY_CONFIGURATION);
    return false;
  }
  if (device->calibration_readback != device->calibration_value)
  {
    INA226_RecordError(device, INA226_ERROR_VERIFY_CALIBRATION);
    return false;
  }

  device->consecutive_error_count = 0U;
  device->last_error = INA226_ERROR_NONE;
  device->ready = true;
  return true;
}

/**
 * @brief  读取一组完整的母线电压、分流电压、电流和功率数据。
 * @param  device INA226设备对象。
 * @param  measurement 用于接收转换后测量值的结构体。
 * @retval 确认新转换完成且电压、电流读取成功时返回true。
 */
__weak void INA226_OnCurrentReady(const INA226_Device_t *device,int32_t current_ua)
{(void)device;(void)current_ua;}

bool INA226_ReadMeasurement(INA226_Device_t *device,
                            INA226_Measurement_t *measurement)
{
  INA226_Measurement_t temporary;
  uint16_t bus_raw;
  uint16_t current_raw;
  uint16_t flags;

  if ((device == NULL) || (measurement == NULL) || !device->ready)
  { return false; }
  if (!INA226_ReadRegister(device, INA226_REG_MASK_ENABLE, &flags))
  {
    INA226_RecordError(device, INA226_ERROR_READ_READY);
    return false;
  }
  /* Reading CVRF clears it. A successful poll with no completed conversion
   * is not an I2C failure and must NOT manufacture a new sample sequence. */
  if ((flags & INA226_CONVERSION_READY) == 0U)
  {
    device->consecutive_error_count = 0U;
    device->i2c_error_code = 0U;
    device->last_error = INA226_ERROR_NONE;
    return false;
  }
  if (!INA226_ReadRegister(device, INA226_REG_CURRENT, &current_raw))
  {
    INA226_RecordError(device, INA226_ERROR_READ_CURRENT);
    return false;
  }
  INA226_OnCurrentReady(device,(int32_t)(int16_t)current_raw*device->current_lsb_ua);
  if (!INA226_ReadRegister(device, INA226_REG_BUS_VOLTAGE, &bus_raw))
  {
    INA226_RecordError(device, INA226_ERROR_READ_BUS_VOLTAGE);
    return false;
  }
  temporary.bus_voltage_mv = (uint16_t)(((uint32_t)bus_raw * 5U) / 4U);
  temporary.current_ua = (int32_t)(int16_t)current_raw * device->current_lsb_ua;
  /* Avoid reading redundant shunt/power registers on the shared 400kHz bus.
   * Voltage/current are sequential conversions, not simultaneous samples. */
  temporary.power_mw = temporary.current_ua > 0 ?
    (uint32_t)(((uint64_t)temporary.bus_voltage_mv *
                (uint32_t)temporary.current_ua) / 1000000ULL) : 0U;

  *measurement = temporary;
  device->consecutive_error_count = 0U;
  device->i2c_error_code = 0U;
  device->last_error = INA226_ERROR_NONE;
  return true;
}

/**
 * @brief  判断INA226当前是否已经完成初始化并可读取。
 * @param  device INA226设备对象。
 * @retval 设备可用时返回true。
 */
bool INA226_IsReady(const INA226_Device_t *device)
{
  return ((device != NULL) && device->ready);
}

/**
 * @brief  将INA226错误阶段转换为便于串口查看的英文短字符串。
 * @param  error 错误阶段枚举。
 * @retval 始终有效的常量字符串。
 */
const char *INA226_GetErrorName(INA226_Error_t error)
{
  static const char *const names[] =
  {
    "NONE", "ARG", "NO_ACK", "READ_MFG", "BAD_MFG",
    "READ_DIE", "BAD_DIE", "WRITE_CFG", "WRITE_CAL",
    "READ_CFG", "READ_CAL", "VERIFY_CFG", "VERIFY_CAL",
    "READ_BUS", "READ_SHUNT", "READ_CURRENT", "READ_POWER", "READ_READY"
  };

  if ((unsigned int)error >= (sizeof(names) / sizeof(names[0])))
  {
    return "UNKNOWN";
  }
  return names[(unsigned int)error];
}
