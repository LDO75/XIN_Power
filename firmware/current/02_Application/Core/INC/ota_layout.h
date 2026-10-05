#ifndef XIN_POWER_OTA_LAYOUT_H
#define XIN_POWER_OTA_LAYOUT_H

#include <stdint.h>

/* PY32F403xD: 384 KiB internal flash; first 32 KiB reserved for the BL. */
#define OTA_BOOT_BASE             0x08000000UL
#define OTA_BOOT_SIZE             0x00008000UL
#define OTA_APP_BASE              0x08008000UL
#define OTA_APP_SIZE              0x00058000UL
#define OTA_APP_END               (OTA_APP_BASE + OTA_APP_SIZE)

/* W25Q256: two 512 KiB slots and two independent 4 KiB journals.
 * 0x01FFC000..0x01FFFFFF remains reserved for settings and calibration. */
#define OTA_W25_CAPACITY          0x02000000UL
#define OTA_STAGE_BASE            0x00000000UL
#define OTA_ROLLBACK_BASE         0x00080000UL
#define OTA_SLOT_SIZE             0x00080000UL
#define OTA_META_A_BASE           0x00100000UL
#define OTA_META_B_BASE           0x00101000UL
#define OTA_W25_SECTOR_SIZE       4096UL
#define OTA_INTERNAL_PAGE_SIZE    256UL
#define OTA_SETTINGS_START        0x01FFC000UL

#if (OTA_BOOT_BASE + OTA_BOOT_SIZE != OTA_APP_BASE) || \
    (OTA_APP_END != 0x08060000UL) || \
    (OTA_STAGE_BASE + OTA_SLOT_SIZE > OTA_ROLLBACK_BASE) || \
    (OTA_ROLLBACK_BASE + OTA_SLOT_SIZE > OTA_META_A_BASE) || \
    (OTA_META_B_BASE + OTA_W25_SECTOR_SIZE > OTA_SETTINGS_START)
#error "OTA memory layout overlaps the bootloader, application or settings"
#endif

#define OTA_HARDWARE_ID           0xF4030002UL
#define OTA_METADATA_MAGIC        0x58494E55UL /* XINU */
#define OTA_METADATA_SCHEMA       1UL

typedef enum
{
  OTA_STATE_STAGED = 1,
  OTA_STATE_COPYING = 2,
  OTA_STATE_TRIAL = 3,
  OTA_STATE_CONFIRMED = 4,
  OTA_STATE_ROLLBACK = 5,
  OTA_STATE_CANCELLED = 6,
  OTA_STATE_VERIFIED = 7
} OtaState_t;

typedef struct
{
  uint32_t magic;
  uint32_t schema;
  uint32_t generation;
  uint32_t state;
  uint32_t hardware_id;
  uint32_t version_code;
  uint32_t image_length;
  uint32_t image_crc32;
  uint32_t backup_crc32;
  uint32_t record_crc32;
} OtaMetadata_t;

#endif
