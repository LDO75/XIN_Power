#include "main.h"
#include "gpio.h"
#include "ota_storage.h"
#include "spi.h"
#include "w25q256.h"

#include <string.h>

/* Only SPI2 is used here; never initialize the display or power control. */
SPI_HandleTypeDef hspi2;
volatile const char g_xin_power_boot_signature[] = "XIN_POWER_BL_V1";

#if (FLASH_BLOCK_SIZE != OTA_BOOT_SIZE) || \
    (OTA_APP_SIZE % FLASH_BLOCK_SIZE != 0U)
#error "Internal flash block layout does not match the BL/app partition"
#endif

static union
{
  uint32_t words[OTA_INTERNAL_PAGE_SIZE / 4U];
  uint8_t bytes[OTA_INTERNAL_PAGE_SIZE];
} s_page;
extern void Boot_JumpAsm(uint32_t stack_pointer, uint32_t reset_address);

static void Boot_SafePins(void)
{
  GPIO_InitTypeDef pin = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  /* Drive the active-low power enable high before switching to output mode. */
  HAL_GPIO_WritePin(P_EN_Port, P_EN_Pin, GPIO_PIN_SET);
  pin.Pin = P_EN_Pin;
  pin.Mode = GPIO_MODE_OUTPUT_PP;
  pin.Pull = GPIO_NOPULL;
  pin.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(P_EN_Port, &pin);

  HAL_GPIO_WritePin(FLA_CS_Port, FLA_CS_Pin, GPIO_PIN_SET);
  pin.Pin = FLA_CS_Pin;
  HAL_GPIO_Init(FLA_CS_Port, &pin);
}

static bool Boot_InitSpi(void)
{
  GPIO_InitTypeDef pins = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_SPI2_CLK_ENABLE();
  pins.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  pins.Mode = GPIO_MODE_AF_PP;
  pins.Pull = GPIO_NOPULL;
  pins.Speed = GPIO_SPEED_FREQ_HIGH;
  pins.Alternate = GPIO_AF3_SPI2;
  HAL_GPIO_Init(GPIOB, &pins);

  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  return HAL_SPI_Init(&hspi2) == HAL_OK;
}

static bool Boot_InternalCrc(uint32_t length, uint32_t expected)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t offset = 0U;
  uint32_t chunk;

  while (offset < length)
  {
    chunk = length - offset;
    if (chunk > OTA_INTERNAL_PAGE_SIZE)
    {
      chunk = OTA_INTERNAL_PAGE_SIZE;
    }
    crc = OtaStorage_Crc32Update(crc,
      (const void *)(OTA_APP_BASE + offset), chunk);
    offset += chunk;
  }
  return (crc ^ 0xFFFFFFFFUL) == expected;
}

static bool Boot_BackupApplication(uint32_t *backup_crc)
{
  uint32_t offset;
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t verify_crc;

  if ((backup_crc == NULL) || !OtaStorage_ValidateApplication())
  {
    return false;
  }
  for (offset = 0U; offset < OTA_APP_SIZE;
       offset += OTA_W25_SECTOR_SIZE)
  {
    if (!W25Q256_EraseSector(OTA_ROLLBACK_BASE + offset))
    {
      return false;
    }
  }
  for (offset = 0U; offset < OTA_APP_SIZE;
       offset += OTA_INTERNAL_PAGE_SIZE)
  {
    const void *page = (const void *)(OTA_APP_BASE + offset);
    crc = OtaStorage_Crc32Update(crc, page, OTA_INTERNAL_PAGE_SIZE);
    if (!W25Q256_Write(OTA_ROLLBACK_BASE + offset, page,
                       OTA_INTERNAL_PAGE_SIZE))
    {
      return false;
    }
  }
  *backup_crc = crc ^ 0xFFFFFFFFUL;
  return OtaStorage_CrcExternal(OTA_ROLLBACK_BASE, OTA_APP_SIZE,
                                &verify_crc) &&
         (verify_crc == *backup_crc) &&
         OtaStorage_ValidateImage(OTA_ROLLBACK_BASE, OTA_APP_SIZE,
                                  *backup_crc);
}

static bool Boot_ProgramFromSlot(uint32_t slot, uint32_t length,
                                 uint32_t expected_crc)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t error = 0U;
  uint32_t offset;
  uint32_t count;
  uint32_t index;
  bool blank;
  bool okay = true;

  if (!OtaStorage_ValidateImage(slot, length, expected_crc) ||
      (HAL_FLASH_Unlock() != HAL_OK))
  {
    return false;
  }
  erase.TypeErase = FLASH_TYPEERASE_BLOCKERASE;
  erase.BlockAddress = OTA_APP_BASE;
  erase.NbBlocks = OTA_APP_SIZE / FLASH_BLOCK_SIZE;
  if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK)
  {
    okay = false;
  }
  for (offset = 0U; okay && (offset < length);
       offset += OTA_INTERNAL_PAGE_SIZE)
  {
    count = length - offset;
    if (count > OTA_INTERNAL_PAGE_SIZE)
    {
      count = OTA_INTERNAL_PAGE_SIZE;
    }
    (void)memset(s_page.bytes, 0xFF, sizeof(s_page.bytes));
    if (!W25Q256_Read(slot + offset, s_page.bytes, count))
    {
      okay = false;
      break;
    }
    blank = true;
    for (index = 0U; index < OTA_INTERNAL_PAGE_SIZE; index++)
    {
      if (s_page.bytes[index] != 0xFFU)
      {
        blank = false;
        break;
      }
    }
    if (!blank &&
        (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE,
                           OTA_APP_BASE + offset,
                           s_page.words) != HAL_OK))
    {
      okay = false;
      break;
    }
    if (memcmp((const void *)(OTA_APP_BASE + offset),
               s_page.bytes, OTA_INTERNAL_PAGE_SIZE) != 0)
    {
      okay = false;
      break;
    }
  }
  (void)HAL_FLASH_Lock();
  return okay && OtaStorage_ValidateApplication() &&
         Boot_InternalCrc(length, expected_crc);
}

static bool Boot_Rollback(OtaMetadata_t *metadata)
{
  if (!Boot_ProgramFromSlot(OTA_ROLLBACK_BASE, OTA_APP_SIZE,
                            metadata->backup_crc32))
  {
    return false;
  }
  metadata->state = OTA_STATE_ROLLBACK;
  return OtaStorage_WriteMetadata(metadata);
}

static void Boot_JumpToApplication(void)
{
  const uint32_t *vectors = (const uint32_t *)OTA_APP_BASE;
  uint32_t reset_address = vectors[1];
  uint32_t stack_pointer = vectors[0];
  uint32_t index;

  __disable_irq();
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  for (index = 0U; index < 8U; index++)
  {
    NVIC->ICER[index] = 0xFFFFFFFFUL;
    NVIC->ICPR[index] = 0xFFFFFFFFUL;
  }
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  SCB->VTOR = OTA_APP_BASE;
  Boot_JumpAsm(stack_pointer, reset_address);
  for (;;)
  {
  }
}

int main(void)
{
  OtaMetadata_t metadata;
  bool has_metadata;
  bool app_valid;

  Boot_SafePins();
  HAL_Init();
  if (g_xin_power_boot_signature[0] != 'X')
  {
    for (;;)
    {
    }
  }
  if (!Boot_InitSpi() || !W25Q256_Init())
  {
    /* Do not run a possibly half-written image without the recovery store. */
    for (;;)
    {
    }
  }
  has_metadata = OtaStorage_ReadMetadata(&metadata);
  app_valid = OtaStorage_ValidateApplication();
  if (!has_metadata)
  {
    if (app_valid)
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_STAGED)
  {
    if (app_valid &&
        OtaStorage_ValidateImage(OTA_STAGE_BASE,
                                 metadata.image_length,
                                 metadata.image_crc32) &&
        Boot_BackupApplication(&metadata.backup_crc32))
    {
      metadata.state = OTA_STATE_COPYING;
      if (OtaStorage_WriteMetadata(&metadata))
      {
        if (Boot_ProgramFromSlot(OTA_STAGE_BASE,
                                 metadata.image_length,
                                 metadata.image_crc32))
        {
          metadata.state = OTA_STATE_TRIAL;
          if (OtaStorage_WriteMetadata(&metadata))
          {
            Boot_JumpToApplication();
          }
        }
        if (Boot_Rollback(&metadata))
        {
          Boot_JumpToApplication();
        }
      }
    }
    else if (app_valid)
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_COPYING)
  {
    if (Boot_ProgramFromSlot(OTA_STAGE_BASE,
                             metadata.image_length,
                             metadata.image_crc32))
    {
      metadata.state = OTA_STATE_TRIAL;
      if (OtaStorage_WriteMetadata(&metadata))
      {
        Boot_JumpToApplication();
      }
    }
    if (Boot_Rollback(&metadata))
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_TRIAL)
  {
    /* Reset before the application confirms startup: restore known-good. */
    if (Boot_Rollback(&metadata))
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_CONFIRMED)
  {
    if (app_valid &&
        Boot_InternalCrc(metadata.image_length, metadata.image_crc32))
    {
      Boot_JumpToApplication();
    }
    if (Boot_Rollback(&metadata))
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_ROLLBACK)
  {
    if (app_valid && Boot_InternalCrc(OTA_APP_SIZE,
                                      metadata.backup_crc32))
    {
      Boot_JumpToApplication();
    }
    if (Boot_Rollback(&metadata))
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_CANCELLED)
  {
    if (app_valid)
    {
      Boot_JumpToApplication();
    }
  }
  else if (metadata.state == OTA_STATE_VERIFIED)
  {
    /* Power loss before FW APPLY must keep the currently running app. */
    if (app_valid)
    {
      Boot_JumpToApplication();
    }
  }
  /* Recovery requires SWD; output remains disabled. */
  for (;;)
  {
  }
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void Error_Handler(void)
{
  for (;;)
  {
  }
}
