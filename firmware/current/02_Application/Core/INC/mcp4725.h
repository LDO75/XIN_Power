/**
  ******************************************************************************
  * @file    mcp4725.h
  * @brief   MCP4725十二位I2C数模转换器驱动接口。
  ******************************************************************************
  */

#ifndef __MCP4725_H__
#define __MCP4725_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

#define MCP4725_ADDRESS_7BIT              0x60U
#define MCP4725_MAX_CODE                  4095U

typedef struct
{
  I2C_HandleTypeDef *i2c;
  uint8_t address_7bit;
  uint16_t last_code;
  uint32_t total_error_count;
  uint8_t consecutive_error_count;
  bool ready;
} MCP4725_Device_t;

/**
 * @brief  初始化MCP4725设备对象并检查器件应答。
 * @param  device MCP4725设备对象。
 * @param  i2c MCP4725所在的I2C总线。
 * @param  address_7bit 七位I2C地址。
 * @retval 器件应答正常时返回true。
 */
bool MCP4725_Init(MCP4725_Device_t *device,
                  I2C_HandleTypeDef *i2c,
                  uint8_t address_7bit);

/**
 * @brief  仅更新易失DAC寄存器，不写入内部EEPROM。
 * @param  device MCP4725设备对象。
 * @param  code 十二位DAC码值，超出4095时自动钳位。
 * @retval 写入成功时返回true。
 */
bool MCP4725_WriteVolatile(MCP4725_Device_t *device, uint16_t code);

#ifdef __cplusplus
}
#endif

#endif /* __MCP4725_H__ */
