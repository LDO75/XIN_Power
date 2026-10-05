#include "ota_storage.h"
#include "w25q256.h"

#include <stddef.h>
#include <string.h>

uint32_t OtaStorage_Crc32Update(uint32_t crc, const void *data,
                                uint32_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= bytes[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return crc;
}

static bool OtaStorage_ValidRecord(const OtaMetadata_t *record)
{
  uint32_t crc;

  if ((record->magic != OTA_METADATA_MAGIC) ||
      (record->schema != OTA_METADATA_SCHEMA) ||
      (record->hardware_id != OTA_HARDWARE_ID) ||
      (record->state < OTA_STATE_STAGED) ||
      (record->state > OTA_STATE_VERIFIED) ||
      (record->image_length < 8U) ||
      (record->image_length > OTA_APP_SIZE))
  {
    return false;
  }
  crc = OtaStorage_Crc32Update(0xFFFFFFFFUL, record,
                               (uint32_t)offsetof(OtaMetadata_t, record_crc32));
  return (crc ^ 0xFFFFFFFFUL) == record->record_crc32;
}

bool OtaStorage_ReadMetadata(OtaMetadata_t *metadata)
{
  OtaMetadata_t a;
  OtaMetadata_t b;
  bool a_ok;
  bool b_ok;

  if ((metadata == NULL) || !W25Q256_IsReady())
  {
    return false;
  }
  a_ok = W25Q256_Read(OTA_META_A_BASE, &a, sizeof(a)) &&
         OtaStorage_ValidRecord(&a);
  b_ok = W25Q256_Read(OTA_META_B_BASE, &b, sizeof(b)) &&
         OtaStorage_ValidRecord(&b);
  if (!a_ok && !b_ok)
  {
    return false;
  }
  *metadata = (a_ok && (!b_ok || ((int32_t)(a.generation - b.generation) > 0))) ?
              a : b;
  return true;
}

bool OtaStorage_WriteMetadata(OtaMetadata_t *metadata)
{
  OtaMetadata_t previous;
  OtaMetadata_t verify;
  uint32_t target;

  if ((metadata == NULL) || !W25Q256_IsReady())
  {
    return false;
  }
  if (OtaStorage_ReadMetadata(&previous))
  {
    metadata->generation = previous.generation + 1U;
  }
  else
  {
    metadata->generation = 1U;
  }
  metadata->magic = OTA_METADATA_MAGIC;
  metadata->schema = OTA_METADATA_SCHEMA;
  metadata->hardware_id = OTA_HARDWARE_ID;
  metadata->record_crc32 =
    OtaStorage_Crc32Update(0xFFFFFFFFUL, metadata,
                          (uint32_t)offsetof(OtaMetadata_t, record_crc32)) ^
    0xFFFFFFFFUL;
  target = ((metadata->generation & 1U) != 0U) ?
           OTA_META_A_BASE : OTA_META_B_BASE;
  if (!W25Q256_EraseSector(target) ||
      !W25Q256_Write(target, metadata, sizeof(*metadata)) ||
      !W25Q256_Read(target, &verify, sizeof(verify)))
  {
    return false;
  }
  return (memcmp(metadata, &verify, sizeof(verify)) == 0) &&
         OtaStorage_ValidRecord(&verify);
}

bool OtaStorage_CrcExternal(uint32_t address, uint32_t length,
                            uint32_t *result)
{
  uint8_t buffer[OTA_INTERNAL_PAGE_SIZE];
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t chunk;

  if ((result == NULL) || (length == 0U) ||
      (address >= OTA_W25_CAPACITY) ||
      (length > OTA_W25_CAPACITY - address))
  {
    return false;
  }
  while (length > 0U)
  {
    chunk = (length > sizeof(buffer)) ? sizeof(buffer) : length;
    if (!W25Q256_Read(address, buffer, chunk))
    {
      return false;
    }
    crc = OtaStorage_Crc32Update(crc, buffer, chunk);
    address += chunk;
    length -= chunk;
  }
  *result = crc ^ 0xFFFFFFFFUL;
  return true;
}

bool OtaStorage_ValidateImage(uint32_t slot_base, uint32_t length,
                              uint32_t expected_crc)
{
  uint32_t vectors[2];
  uint32_t crc;

  if ((length < 8U) || (length > OTA_APP_SIZE) ||
      ((slot_base != OTA_STAGE_BASE) &&
       (slot_base != OTA_ROLLBACK_BASE)) ||
      !W25Q256_Read(slot_base, vectors, sizeof(vectors)))
  {
    return false;
  }
  if ((vectors[0] < 0x20000000UL) || (vectors[0] > 0x20010000UL) ||
      ((vectors[0] & 7U) != 0U) ||
      ((vectors[1] & 1U) == 0U) ||
      ((vectors[1] & ~1UL) < OTA_APP_BASE) ||
      ((vectors[1] & ~1UL) >= OTA_APP_BASE + length))
  {
    return false;
  }
  return OtaStorage_CrcExternal(slot_base, length, &crc) &&
         (crc == expected_crc);
}

bool OtaStorage_ValidateApplication(void)
{
  const uint32_t *vectors = (const uint32_t *)OTA_APP_BASE;

  return (vectors[0] >= 0x20000000UL) &&
         (vectors[0] <= 0x20010000UL) &&
         ((vectors[0] & 7U) == 0U) &&
         ((vectors[1] & 1U) != 0U) &&
         ((vectors[1] & ~1UL) >= OTA_APP_BASE) &&
         ((vectors[1] & ~1UL) < OTA_APP_END);
}

bool OtaStorage_BootloaderPresent(void)
{
  static const char signature[] = "XIN_POWER_BL_V1";
  uint32_t address;

  for (address = OTA_BOOT_BASE;
       address <= OTA_APP_BASE - sizeof(signature); address++)
  {
    if (memcmp((const void *)address, signature,
               sizeof(signature) - 1U) == 0)
    {
      return true;
    }
  }
  return false;
}
