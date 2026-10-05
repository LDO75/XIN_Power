#include "ota_updater.h"

#include "main.h"
#include "ota_storage.h"
#include "w25q256.h"

#include <string.h>

static OtaUpdater_Status_t s_status;
static bool s_reset_pending;
static uint32_t s_reset_time_ms;
static bool s_trial_pending;
static uint32_t s_trial_start_ms;
static uint32_t s_last_trial_write_ms;

bool OtaUpdater_Begin(uint32_t length, uint32_t crc32,
                      uint32_t version_code)
{
  OtaMetadata_t metadata;
  uint32_t address;
  uint32_t erase_end;

  if (!W25Q256_IsReady() || s_status.active || s_trial_pending ||
      (length < 8U) || (length > OTA_APP_SIZE) ||
      (version_code == 0U))
  {
    return false;
  }
  if (OtaStorage_ReadMetadata(&metadata) &&
      ((metadata.state == OTA_STATE_COPYING) ||
       (metadata.state == OTA_STATE_TRIAL)))
  {
    return false;
  }
  erase_end = (length + OTA_W25_SECTOR_SIZE - 1U) &
              ~(OTA_W25_SECTOR_SIZE - 1U);
  for (address = OTA_STAGE_BASE; address < erase_end;
       address += OTA_W25_SECTOR_SIZE)
  {
    if (!W25Q256_EraseSector(address))
    {
      return false;
    }
  }
  (void)memset(&s_status, 0, sizeof(s_status));
  s_status.length = length;
  s_status.crc32 = crc32;
  s_status.version_code = version_code;
  s_status.active = true;
  return true;
}

bool OtaUpdater_Write(uint32_t offset, const uint8_t *data, uint32_t length)
{
  uint8_t verify[OTA_TRANSFER_CHUNK_MAX];

  if (!s_status.active || s_status.verified || (data == NULL) ||
      (length == 0U) || (length > OTA_TRANSFER_CHUNK_MAX) ||
      (offset > s_status.received) ||
      (length > s_status.length - offset))
  {
    return false;
  }
  /* A retransmitted frame is successful only if the stored bytes match. */
  if (offset < s_status.received)
  {
    return (length <= s_status.received - offset) &&
           W25Q256_Read(OTA_STAGE_BASE + offset, verify, length) &&
           (memcmp(data, verify, length) == 0);
  }
  if (!W25Q256_Write(OTA_STAGE_BASE + offset, data, length) ||
      !W25Q256_Read(OTA_STAGE_BASE + offset, verify, length) ||
      (memcmp(data, verify, length) != 0))
  {
    return false;
  }
  s_status.received += length;
  return true;
}

bool OtaUpdater_Finalize(void)
{
  OtaMetadata_t metadata;

  if (!s_status.active || s_status.verified ||
      (s_status.received != s_status.length) ||
      !OtaStorage_ValidateImage(OTA_STAGE_BASE, s_status.length,
                                s_status.crc32))
  {
    return false;
  }
  (void)memset(&metadata, 0, sizeof(metadata));
  /* A verified image must not install until FW APPLY is acknowledged. */
  metadata.state = OTA_STATE_VERIFIED;
  metadata.version_code = s_status.version_code;
  metadata.image_length = s_status.length;
  metadata.image_crc32 = s_status.crc32;
  if (!OtaStorage_WriteMetadata(&metadata))
  {
    return false;
  }
  s_status.verified = true;
  return true;
}

bool OtaUpdater_Apply(void)
{
  OtaMetadata_t metadata;

  if (!s_status.active || !s_status.verified || s_reset_pending)
  {
    return false;
  }
  if (!OtaStorage_ReadMetadata(&metadata) ||
      (metadata.state != OTA_STATE_VERIFIED) ||
      (metadata.image_length != s_status.length) ||
      (metadata.image_crc32 != s_status.crc32) ||
      (metadata.version_code != s_status.version_code))
  {
    return false;
  }
  metadata.state = OTA_STATE_STAGED;
  if (!OtaStorage_WriteMetadata(&metadata))
  {
    return false;
  }
  s_reset_pending = true;
  s_reset_time_ms = HAL_GetTick();
  return true;
}

bool OtaUpdater_Abort(void)
{
  OtaMetadata_t metadata;

  if (OtaStorage_ReadMetadata(&metadata) &&
      ((metadata.state == OTA_STATE_VERIFIED) ||
       (metadata.state == OTA_STATE_STAGED)))
  {
    /* The current internal app is unchanged; the staged image is cancelled. */
    metadata.state = OTA_STATE_CANCELLED;
    if (!OtaStorage_WriteMetadata(&metadata))
    {
      return false;
    }
  }
  else if (s_status.verified)
  {
    return false;
  }
  (void)memset(&s_status, 0, sizeof(s_status));
  s_reset_pending = false;
  return true;
}

void OtaUpdater_Process(uint32_t now_ms, bool system_healthy)
{
  if (s_trial_pending && system_healthy &&
      ((now_ms - s_trial_start_ms) >= 2000U) &&
      ((now_ms - s_last_trial_write_ms) >= 1000U))
  {
    OtaMetadata_t metadata;
    s_last_trial_write_ms = now_ms;
    if (OtaStorage_ReadMetadata(&metadata) &&
        (metadata.state == OTA_STATE_TRIAL))
    {
      metadata.state = OTA_STATE_CONFIRMED;
      if (OtaStorage_WriteMetadata(&metadata))
      {
        s_trial_pending = false;
      }
    }
  }
  /* Allow the interrupt-driven UART ACK to leave before resetting. */
  if (s_reset_pending && ((now_ms - s_reset_time_ms) >= 500U))
  {
    NVIC_SystemReset();
  }
}

void OtaUpdater_StartTrial(uint32_t now_ms)
{
  OtaMetadata_t metadata;

  s_trial_pending = false;
  if (OtaStorage_ReadMetadata(&metadata) &&
      (metadata.state == OTA_STATE_TRIAL))
  {
    s_trial_pending = true;
    s_trial_start_ms = now_ms;
    s_last_trial_write_ms = now_ms;
  }
}

bool OtaUpdater_IsBusy(void)
{
  return s_status.active || s_trial_pending;
}

void OtaUpdater_GetStatus(OtaUpdater_Status_t *status)
{
  if (status != NULL)
  {
    *status = s_status;
  }
}
