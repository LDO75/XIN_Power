/**
  ******************************************************************************
  * @file    w25q256.h
  * @brief   W25Q256 SPI NOR Flash最小驱动接口。
  ******************************************************************************
  */

#ifndef __W25Q256_H__
#define __W25Q256_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化并探测W25Q256。
 * @retval true  器件响应正常。
 * @retval false 器件未响应或型号异常。
 */
bool W25Q256_Init(void);

/**
 * @brief  查询Flash是否已经通过初始化探测。
 * @retval true/false
 */
bool W25Q256_IsReady(void);
/* Background operations: caller polls BUSY, without spinning/waiting. */
bool W25Q256_IsBusy(bool *busy);
bool W25Q256_StartSectorErase(uint32_t address);
bool W25Q256_StartPageProgram(uint32_t address, const void *data, uint16_t length);

/**
 * @brief  从W25Q256读取数据。
 * @param  address 32位Flash地址。
 * @param  data    接收缓冲区。
 * @param  length  读取长度。
 * @retval true/false
 */
bool W25Q256_Read(uint32_t address, void *data, uint32_t length);

/**
 * @brief  擦除包含指定地址的4KB扇区。
 * @param  address 扇区内任意地址。
 * @retval true/false
 */
bool W25Q256_EraseSector(uint32_t address);
/** @brief Cached erase failure details; does not access the bus. */
const char *W25Q256_GetEraseDiagnostic(void);

/**
 * @brief  向已经擦除的区域写入数据，自动处理256字节分页。
 * @param  address 32位Flash地址。
 * @param  data    待写入数据。
 * @param  length  写入长度。
 * @retval true/false
 */
bool W25Q256_Write(uint32_t address, const void *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __W25Q256_H__ */
