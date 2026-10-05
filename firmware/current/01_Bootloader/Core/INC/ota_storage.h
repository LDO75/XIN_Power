#ifndef XIN_POWER_OTA_STORAGE_H
#define XIN_POWER_OTA_STORAGE_H

#include "ota_layout.h"
#include <stdbool.h>
#include <stdint.h>

uint32_t OtaStorage_Crc32Update(uint32_t crc, const void *data,
                                uint32_t length);
bool OtaStorage_ReadMetadata(OtaMetadata_t *metadata);
bool OtaStorage_WriteMetadata(OtaMetadata_t *metadata);
bool OtaStorage_ValidateImage(uint32_t slot_base, uint32_t length,
                              uint32_t expected_crc);
bool OtaStorage_ValidateApplication(void);
bool OtaStorage_BootloaderPresent(void);
bool OtaStorage_CrcExternal(uint32_t address, uint32_t length,
                            uint32_t *result);

#endif
