#ifndef XIN_POWER_OTA_UPDATER_H
#define XIN_POWER_OTA_UPDATER_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_TRANSFER_CHUNK_MAX 192U

typedef struct
{
  uint32_t length;
  uint32_t received;
  uint32_t crc32;
  uint32_t version_code;
  bool active;
  bool verified;
} OtaUpdater_Status_t;

bool OtaUpdater_Begin(uint32_t length, uint32_t crc32,
                      uint32_t version_code);
bool OtaUpdater_Write(uint32_t offset, const uint8_t *data, uint32_t length);
bool OtaUpdater_Finalize(void);
bool OtaUpdater_Apply(void);
bool OtaUpdater_Abort(void);
void OtaUpdater_Process(uint32_t now_ms, bool system_healthy);
void OtaUpdater_StartTrial(uint32_t now_ms);
bool OtaUpdater_IsBusy(void);
void OtaUpdater_GetStatus(OtaUpdater_Status_t *status);

#endif
