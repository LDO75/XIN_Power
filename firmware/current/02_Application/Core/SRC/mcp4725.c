/**
  ******************************************************************************
  * @file    mcp4725.c
  * @brief   MCP4725十二位I2C数模转换器驱动实现。
  ******************************************************************************
  */

#include "mcp4725.h"

#include <stddef.h>
#include <string.h>

#define MCP4725_I2C_TIMEOUT_MS              5U
#define MCP4725_READY_TRIALS                 2U
#define MCP4725_WRITE_DAC_COMMAND          0x40U

/**
 * @brief  记录一次MCP4725通信错误。
 * @param  device MCP4725设备对象。
 * @retval 无。
 */
static void MCP4725_RecordError(MCP4725_Device_t *device)
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
}

bool MCP4725_Init(MCP4725_Device_t *device,
                  I2C_HandleTypeDef *i2c,
                  uint8_t address_7bit)
{
  if ((device == NULL) || (i2c == NULL) || (address_7bit > 0x7FU))
  {
    return false;
  }

  (void)memset(device, 0, sizeof(*device));
  device->i2c = i2c;
  device->address_7bit = address_7bit;
  if (HAL_I2C_IsDeviceReady(i2c,
                            (uint16_t)(address_7bit << 1U),
                            MCP4725_READY_TRIALS,
                            MCP4725_I2C_TIMEOUT_MS) != HAL_OK)
  {
    MCP4725_RecordError(device);
    return false;
  }

  device->ready = true;
  return true;
}

bool MCP4725_WriteVolatile(MCP4725_Device_t *device, uint16_t code)
{
  uint8_t data[3];

  if ((device == NULL) || (device->i2c == NULL) || !device->ready)
  {
    return false;
  }
  if (code > MCP4725_MAX_CODE)
  {
    code = MCP4725_MAX_CODE;
  }

  data[0] = MCP4725_WRITE_DAC_COMMAND;
  data[1] = (uint8_t)(code >> 4U);
  data[2] = (uint8_t)(code << 4U);
  if (HAL_I2C_Master_Transmit(device->i2c,
                              (uint16_t)(device->address_7bit << 1U),
                              data,
                              sizeof(data),
                              MCP4725_I2C_TIMEOUT_MS) != HAL_OK)
  {
    MCP4725_RecordError(device);
    if (device->consecutive_error_count >= 3U)
    {
      device->ready = false;
    }
    return false;
  }

  device->last_code = code;
  device->consecutive_error_count = 0U;
  return true;
}
