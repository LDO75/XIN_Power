/**
  ******************************************************************************
  * @file    w25q256.c
  * @brief   W25Q256 SPI NOR Flash最小驱动实现。
  * @note    使用SPI2和PB12片选，采用W25Q256的4字节地址指令。
  ******************************************************************************
  */

#include "w25q256.h"
#include "ota_layout.h"

#include "gpio.h"
#include "spi.h"

#include <stddef.h>
#include <stdio.h>

#define W25Q256_CMD_WRITE_ENABLE         0x06U
#define W25Q256_CMD_READ_STATUS_1        0x05U
#define W25Q256_CMD_READ_JEDEC_ID        0x9FU
#define W25Q256_CMD_READ_4BYTE            0x13U
#define W25Q256_CMD_PAGE_PROGRAM_4BYTE    0x12U
#define W25Q256_CMD_SECTOR_ERASE_4BYTE    0x21U

#define W25Q256_STATUS_BUSY_MASK          0x01U
#define W25Q256_PAGE_SIZE                  256U
#define W25Q256_SECTOR_SIZE               4096U
#define W25Q256_SPI_TIMEOUT_MS              10U
#define W25Q256_PROGRAM_TIMEOUT_MS         500U
#define W25Q256_ERASE_TIMEOUT_MS          5000U

#define W25Q256_CS_LOW()  HAL_GPIO_WritePin(FLA_CS_Port, FLA_CS_Pin, GPIO_PIN_RESET)
#define W25Q256_CS_HIGH() HAL_GPIO_WritePin(FLA_CS_Port, FLA_CS_Pin, GPIO_PIN_SET)

static bool s_w25q256_ready;
static uint8_t s_jedec[3];
static uint8_t s_last_sr1;
static bool s_sr1_valid;
static HAL_StatusTypeDef s_last_io;
static char s_erase_detail[240];
const char *W25Q256_GetEraseDiagnostic(void) { return s_erase_detail; }
static bool W25Q256_EraseFailed(const char *stage, uint32_t address, uint32_t started)
{
  (void)snprintf(s_erase_detail, sizeof(s_erase_detail),
    "@FLASH,op=CAL_ERASE,stage=%s,addr=%08lX,elapsed=%lu,io=%u,hal=%08lX,spi_state=%u,sr1_valid=%u,sr1=%02X,jedec=%02X%02X%02X\r\n",
    stage, (unsigned long)address, (unsigned long)(HAL_GetTick()-started),
    (unsigned)s_last_io, (unsigned long)hspi2.ErrorCode, (unsigned)hspi2.State,
    s_sr1_valid ? 1U : 0U, (unsigned)s_last_sr1,
    (unsigned)s_jedec[0], (unsigned)s_jedec[1], (unsigned)s_jedec[2]);
  return false;
}

/**
 * @brief  通过SPI发送数据。
 */
static bool W25Q256_Transmit(const uint8_t *data, uint16_t length)
{
  s_last_io = HAL_SPI_Transmit(&hspi2,
                          (uint8_t *)data,
                          length,
                          W25Q256_SPI_TIMEOUT_MS);
  return s_last_io == HAL_OK;
}

/**
 * @brief  通过SPI接收数据。
 */
static bool W25Q256_Receive(uint8_t *data, uint16_t length)
{
  s_last_io = HAL_SPI_Receive(&hspi2,
                         data,
                         length,
                         W25Q256_SPI_TIMEOUT_MS);
  return s_last_io == HAL_OK;
}

/**
 * @brief  读取状态寄存器1。
 */
static bool W25Q256_ReadStatus(uint8_t *status)
{
  uint8_t command = W25Q256_CMD_READ_STATUS_1;
  bool result;

  if (status == NULL)
  {
    return false;
  }

  W25Q256_CS_LOW();
  result = W25Q256_Transmit(&command, 1U) && W25Q256_Receive(status, 1U);
  W25Q256_CS_HIGH();
  return result;
}

/**
 * @brief  等待Flash结束内部编程或擦除操作。
 */
static bool W25Q256_WaitReady(uint32_t timeout_ms)
{
  uint32_t start_time_ms = HAL_GetTick();
  uint8_t status;

  do
  {
    if (!W25Q256_ReadStatus(&status))
    {
      return false;
    }
    s_last_sr1 = status;
    s_sr1_valid = true;
    if ((status & W25Q256_STATUS_BUSY_MASK) == 0U)
    {
      return true;
    }
  } while ((HAL_GetTick() - start_time_ms) < timeout_ms);

  return false;
}

/**
 * @brief  发送写使能指令。
 */
static bool W25Q256_WriteEnable(void)
{
  uint8_t command = W25Q256_CMD_WRITE_ENABLE;
  bool result;

  W25Q256_CS_LOW();
  result = W25Q256_Transmit(&command, 1U);
  W25Q256_CS_HIGH();
  return result;
}

/**
 * @brief  生成包含4字节地址的5字节指令头。
 */
static void W25Q256_BuildAddressCommand(uint8_t command,
                                        uint32_t address,
                                        uint8_t header[5])
{
  header[0] = command;
  header[1] = (uint8_t)(address >> 24U);
  header[2] = (uint8_t)(address >> 16U);
  header[3] = (uint8_t)(address >> 8U);
  header[4] = (uint8_t)address;
}

bool W25Q256_Init(void)
{
  uint8_t command = W25Q256_CMD_READ_JEDEC_ID;
  uint8_t jedec_id[3] = {0U};
  bool result;

  s_w25q256_ready = false;
  W25Q256_CS_HIGH();
  HAL_Delay(1U);

  W25Q256_CS_LOW();
  result = W25Q256_Transmit(&command, 1U) &&
           W25Q256_Receive(jedec_id, sizeof(jedec_id));
  W25Q256_CS_HIGH();
  if (!result)
  {
    return false;
  }

  /* 容量码0x19对应256Mbit；同时排除总线悬空时的全0/全FF响应。 */
  if ((jedec_id[0] == 0x00U) || (jedec_id[0] == 0xFFU) ||
      (jedec_id[2] != 0x19U))
  {
    return false;
  }

  for (unsigned i=0; i<3; i++) { s_jedec[i] = jedec_id[i]; }
  s_w25q256_ready = W25Q256_WaitReady(W25Q256_SPI_TIMEOUT_MS);
  return s_w25q256_ready;
}

bool W25Q256_IsReady(void)
{
  return s_w25q256_ready;
}

bool W25Q256_IsBusy(bool *busy)
{
  uint8_t status;
  if (!s_w25q256_ready || (busy == NULL) || !W25Q256_ReadStatus(&status))
  { return false; }
  *busy = (status & W25Q256_STATUS_BUSY_MASK) != 0U;
  return true;
}

bool W25Q256_StartSectorErase(uint32_t address)
{
  uint8_t header[5];
  bool busy;
  bool result;
  if ((address >= OTA_W25_CAPACITY) || !W25Q256_IsBusy(&busy) || busy ||
      !W25Q256_WriteEnable()) { return false; }
  address &= ~(W25Q256_SECTOR_SIZE - 1U);
  W25Q256_BuildAddressCommand(W25Q256_CMD_SECTOR_ERASE_4BYTE, address, header);
  W25Q256_CS_LOW();
  result = W25Q256_Transmit(header, sizeof(header));
  W25Q256_CS_HIGH();
  return result; /* NOR erases in the background; NEVER wait here. */
}

bool W25Q256_StartPageProgram(uint32_t address, const void *data, uint16_t length)
{
  uint8_t header[5];
  bool busy;
  bool result;
  if ((data == NULL) || (length == 0U) ||
      (length > W25Q256_PAGE_SIZE - (address % W25Q256_PAGE_SIZE)) ||
      (address >= OTA_W25_CAPACITY) || (length > OTA_W25_CAPACITY - address) ||
      !W25Q256_IsBusy(&busy) || busy || !W25Q256_WriteEnable())
  { return false; }
  W25Q256_BuildAddressCommand(W25Q256_CMD_PAGE_PROGRAM_4BYTE, address, header);
  W25Q256_CS_LOW();
  result = W25Q256_Transmit(header, sizeof(header)) &&
           W25Q256_Transmit((const uint8_t *)data, length);
  W25Q256_CS_HIGH();
  return result;
}

bool W25Q256_Read(uint32_t address, void *data, uint32_t length)
{
  uint8_t header[5];
  uint8_t *destination = (uint8_t *)data;
  uint16_t chunk;
  bool result = true;

  if (!s_w25q256_ready || (data == NULL) || (length == 0U) ||
      (address >= OTA_W25_CAPACITY) ||
      (length > OTA_W25_CAPACITY - address))
  {
    return false;
  }

  W25Q256_BuildAddressCommand(W25Q256_CMD_READ_4BYTE, address, header);
  W25Q256_CS_LOW();
  result = W25Q256_Transmit(header, sizeof(header));
  while (result && (length > 0U))
  {
    chunk = (length > UINT16_MAX) ? UINT16_MAX : (uint16_t)length;
    result = W25Q256_Receive(destination, chunk);
    destination += chunk;
    length -= chunk;
  }
  W25Q256_CS_HIGH();
  return result;
}

bool W25Q256_EraseSector(uint32_t address)
{
  uint8_t header[5];
  uint32_t started = HAL_GetTick();
  s_erase_detail[0] = '\0';
  s_sr1_valid = false;
  s_last_io = HAL_OK;
  if (!s_w25q256_ready || address >= OTA_W25_CAPACITY)
  { return W25Q256_EraseFailed("PRECHECK", address, started); }
  if (!W25Q256_WaitReady(W25Q256_SPI_TIMEOUT_MS))
  { return W25Q256_EraseFailed("PRE_WAIT", address, started); }
  if (!W25Q256_WriteEnable())
  { return W25Q256_EraseFailed("WREN_TX", address, started); }
  address &= ~(W25Q256_SECTOR_SIZE - 1U);
  W25Q256_BuildAddressCommand(W25Q256_CMD_SECTOR_ERASE_4BYTE, address, header);
  W25Q256_CS_LOW();
  bool result = W25Q256_Transmit(header, sizeof(header));
  W25Q256_CS_HIGH();
  if (!result) { return W25Q256_EraseFailed("ERASE_TX", address, started); }
  if (!W25Q256_WaitReady(W25Q256_ERASE_TIMEOUT_MS))
  { return W25Q256_EraseFailed("ERASE_WAIT", address, started); }
  return true;
}

bool W25Q256_Write(uint32_t address, const void *data, uint32_t length)
{
  const uint8_t *source = (const uint8_t *)data;
  uint8_t header[5];
  uint32_t page_remaining;
  uint16_t chunk;
  bool result;

  if (!s_w25q256_ready || (data == NULL) || (length == 0U) ||
      (address >= OTA_W25_CAPACITY) ||
      (length > OTA_W25_CAPACITY - address))
  {
    return false;
  }

  while (length > 0U)
  {
    page_remaining = W25Q256_PAGE_SIZE - (address % W25Q256_PAGE_SIZE);
    chunk = (length < page_remaining) ? (uint16_t)length :
                                        (uint16_t)page_remaining;
    if (!W25Q256_WaitReady(W25Q256_SPI_TIMEOUT_MS) ||
        !W25Q256_WriteEnable())
    {
      return false;
    }

    W25Q256_BuildAddressCommand(W25Q256_CMD_PAGE_PROGRAM_4BYTE,
                                address,
                                header);
    W25Q256_CS_LOW();
    result = W25Q256_Transmit(header, sizeof(header)) &&
             W25Q256_Transmit(source, chunk);
    W25Q256_CS_HIGH();
    if (!result || !W25Q256_WaitReady(W25Q256_PROGRAM_TIMEOUT_MS))
    {
      return false;
    }

    address += chunk;
    source += chunk;
    length -= chunk;
  }
  return true;
}
