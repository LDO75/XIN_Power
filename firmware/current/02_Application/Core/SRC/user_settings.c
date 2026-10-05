/**
  ******************************************************************************
  * @file    user_settings.c
  * @brief   用户电压/电流设定的W25Q256双备份掉电保存实现。
  ******************************************************************************
  */

#include "user_settings.h"

#include "power_config.h"
#include "ui_config.h"
#include "w25q256.h"

#include <stddef.h>
#include <string.h>

#define USER_SETTINGS_MAGIC                 0x58534554UL /* XSET */
#define USER_SETTINGS_SCHEMA_VERSION             3U
#define USER_SETTINGS_SLOT_0_ADDRESS        0x01FFC000UL
#define USER_SETTINGS_SLOT_1_ADDRESS        0x01FFD000UL
#define USER_SETTINGS_SAVE_DELAY_MS              1000U
#define USER_SETTINGS_RETRY_DELAY_MS              2000U

typedef struct
{
  uint32_t magic;
  uint16_t schema_version;
  uint16_t size;
  uint32_t sequence;
  uint16_t voltage_setpoint_mv;
  uint16_t current_limit_ma;
  uint8_t fan_start_temperature_c;
  uint8_t reserved[3];
  uint32_t crc32;
} UserSettings_Record_t;

static UserSettings_Data_t s_data;
static uint16_t s_pending_voltage_mv;
static uint16_t s_pending_current_ma;
static uint16_t s_pending_sleep_seconds;
static uint32_t s_sequence;
static uint32_t s_last_change_time_ms;
static uint32_t s_last_write_attempt_ms;
static int8_t s_active_slot;
static bool s_dirty;
typedef enum { SAVE_IDLE, SAVE_ERASING, SAVE_PROGRAMMING, SAVE_VERIFY,
               SAVE_DRAIN } UserSettings_SaveState_t;
static UserSettings_SaveState_t s_save_state;
static UserSettings_Record_t s_write_record;
static uint32_t s_write_address;
static uint32_t s_write_started_ms;
static int8_t s_write_slot;

static uint32_t UserSettings_Crc32(const void *data, size_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= bytes[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
    }
  }
  return ~crc;
}

static bool UserSettings_RecordValid(const UserSettings_Record_t *record)
{
  UserSettings_Record_t copy;
  uint32_t expected_crc;

  if ((record == NULL) ||
      (record->magic != USER_SETTINGS_MAGIC) ||
      ((record->schema_version != 1U) && (record->schema_version != 2U) &&
       (record->schema_version != USER_SETTINGS_SCHEMA_VERSION)) ||
      (record->size != sizeof(UserSettings_Record_t)) ||
      /* Retain the full supported 2-32V setting range across releases. */
      (record->voltage_setpoint_mv < 2000U) ||
      (record->voltage_setpoint_mv > POWER_CFG_VOLTAGE_MAX_MV) ||
      (record->current_limit_ma < POWER_CFG_CURRENT_MIN_MA) ||
      (record->current_limit_ma > POWER_CFG_CURRENT_MAX_MA))
  {
    return false;
  }
  if ((record->schema_version >= 2U) &&
      ((record->fan_start_temperature_c < POWER_CFG_FAN_START_MIN_C) ||
       (record->fan_start_temperature_c > POWER_CFG_FAN_START_MAX_C) ||
       ((record->fan_start_temperature_c % POWER_CFG_FAN_START_STEP_C) != 0U)))
  {
    return false;
  }
  if (record->schema_version==3U &&
      !UI_SleepSecondsValid((uint16_t)(record->reserved[0] | (record->reserved[1]<<8U))))
  { return false; }
  copy = *record;
  expected_crc = copy.crc32;
  copy.crc32 = 0U;
  return UserSettings_Crc32(&copy, sizeof(copy)) == expected_crc;
}

static bool UserSettings_SequenceNewer(uint32_t first, uint32_t second)
{
  return (int32_t)(first - second) > 0;
}

static void UserSettings_LoadRecord(const UserSettings_Record_t *record,
                                    int8_t slot)
{
  s_data.voltage_setpoint_mv = record->voltage_setpoint_mv;
  if (s_data.voltage_setpoint_mv < POWER_CFG_VOLTAGE_MIN_MV)
  { s_data.voltage_setpoint_mv = POWER_CFG_VOLTAGE_MIN_MV; }
  s_data.current_limit_ma = record->current_limit_ma;
  s_data.screen_sleep_seconds = record->schema_version>=3U ?
    (uint16_t)(record->reserved[0] | (record->reserved[1]<<8U)) : UI_SCREEN_SLEEP_DEFAULT_SECONDS;
  s_pending_sleep_seconds = s_data.screen_sleep_seconds;
  s_data.loaded_from_flash = true;
  s_pending_voltage_mv = s_data.voltage_setpoint_mv;
  s_pending_current_ma = record->current_limit_ma;
  s_sequence = record->sequence;
  s_active_slot = slot;
  s_dirty = s_data.voltage_setpoint_mv != record->voltage_setpoint_mv;
}

void UserSettings_Init(void)
{
  UserSettings_Record_t slot_0;
  UserSettings_Record_t slot_1;
  bool slot_0_valid;
  bool slot_1_valid;

  (void)memset(&s_data, 0, sizeof(s_data));
  s_data.voltage_setpoint_mv = 5000U;
  s_data.current_limit_ma = POWER_CFG_CURRENT_MIN_MA;
  s_data.screen_sleep_seconds = s_pending_sleep_seconds = UI_SCREEN_SLEEP_DEFAULT_SECONDS;
  s_pending_voltage_mv = s_data.voltage_setpoint_mv;
  s_pending_current_ma = s_data.current_limit_ma;
  s_sequence = 0U;
  s_active_slot = -1;
  s_dirty = false;
  s_save_state = SAVE_IDLE;
  s_last_change_time_ms = 0U;
  s_last_write_attempt_ms = 0U;

  if (!W25Q256_IsReady())
  {
    return;
  }
  slot_0_valid = W25Q256_Read(USER_SETTINGS_SLOT_0_ADDRESS,
                              &slot_0,
                              sizeof(slot_0)) &&
                 UserSettings_RecordValid(&slot_0);
  slot_1_valid = W25Q256_Read(USER_SETTINGS_SLOT_1_ADDRESS,
                              &slot_1,
                              sizeof(slot_1)) &&
                 UserSettings_RecordValid(&slot_1);
  if (slot_0_valid && slot_1_valid)
  {
    if (UserSettings_SequenceNewer(slot_1.sequence, slot_0.sequence))
    {
      UserSettings_LoadRecord(&slot_1, 1);
    }
    else
    {
      UserSettings_LoadRecord(&slot_0, 0);
    }
  }
  else if (slot_0_valid)
  {
    UserSettings_LoadRecord(&slot_0, 0);
  }
  else if (slot_1_valid)
  {
    UserSettings_LoadRecord(&slot_1, 1);
  }
}

void UserSettings_Get(UserSettings_Data_t *data)
{
  if (data != NULL)
  {
    *data = s_data;
  }
}

static bool UserSettings_StartSave(uint32_t now)
{
  UserSettings_Record_t record;
  int8_t target_slot = (s_active_slot == 0) ? 1 : 0;
  uint32_t target_address = (target_slot == 0) ?
                            USER_SETTINGS_SLOT_0_ADDRESS :
                            USER_SETTINGS_SLOT_1_ADDRESS;

  if (!W25Q256_IsReady())
  {
    return false;
  }
  (void)memset(&record, 0, sizeof(record));
  record.magic = USER_SETTINGS_MAGIC;
  record.schema_version = USER_SETTINGS_SCHEMA_VERSION;
  record.size = sizeof(record);
  record.sequence = s_sequence + 1U;
  record.voltage_setpoint_mv = s_pending_voltage_mv;
  record.current_limit_ma = s_pending_current_ma;
  /* 保留旧记录字段布局以兼容5.8.5，风扇策略现为固定40°C。 */
  record.fan_start_temperature_c = POWER_CFG_FAN_START_DEFAULT_C;
  record.reserved[0] = (uint8_t)s_pending_sleep_seconds;
  record.reserved[1] = (uint8_t)(s_pending_sleep_seconds>>8U);
  record.crc32 = UserSettings_Crc32(&record, sizeof(record));

  if (!W25Q256_StartSectorErase(target_address))
  {
    return false;
  }
  s_write_record = record;
  s_write_slot = target_slot;
  s_write_address = target_address;
  s_write_started_ms = now;
  s_save_state = SAVE_ERASING;
  return true;
}

bool UserSettings_IsSaving(void)
{
  return s_save_state != SAVE_IDLE;
}

static bool UserSettings_AdvanceSave(uint32_t now, bool allow_write)
{
  bool busy;
  UserSettings_Record_t verify;
  if (!W25Q256_IsBusy(&busy))
  {
    /* Keep ownership until the flash answers idle, even after a bus error. */
    s_save_state = SAVE_DRAIN;
    s_last_write_attempt_ms = now;
    return false;
  }
  if (busy)
  {
    if ((now - s_write_started_ms) > 5000U)
    { s_save_state = SAVE_DRAIN; }
    return false; /* one status read per pass, no busy loop */
  }
  if (s_save_state == SAVE_DRAIN)
  {
    s_save_state = SAVE_IDLE;
    s_last_write_attempt_ms = now;
    return false;
  }
  if (s_save_state == SAVE_ERASING)
  {
    if (!allow_write) { return false; } /* immediate output ON remains possible */
    if (!W25Q256_StartPageProgram(s_write_address, &s_write_record,
                                 (uint16_t)sizeof(s_write_record)))
    { s_save_state = SAVE_DRAIN; return false; }
    s_write_started_ms = now;
    s_save_state = SAVE_PROGRAMMING;
    return false;
  }
  if (s_save_state == SAVE_PROGRAMMING)
  {
    s_save_state = SAVE_VERIFY;
    return false;
  }
  if (s_save_state == SAVE_VERIFY)
  {
    s_save_state = SAVE_IDLE;
    s_last_write_attempt_ms = now;
    if (!W25Q256_Read(s_write_address, &verify, sizeof(verify)) ||
        !UserSettings_RecordValid(&verify) ||
        (memcmp(&verify, &s_write_record, sizeof(verify)) != 0))
    { return false; } /* previous slot stays intact */
    s_sequence = verify.sequence;
    s_active_slot = s_write_slot;
    s_data.voltage_setpoint_mv = verify.voltage_setpoint_mv;
    s_data.current_limit_ma = verify.current_limit_ma;
    s_data.screen_sleep_seconds = (uint16_t)(verify.reserved[0] | (verify.reserved[1]<<8U));
    s_data.loaded_from_flash = true;
    s_dirty = (s_pending_voltage_mv != s_data.voltage_setpoint_mv) ||
              (s_pending_current_ma != s_data.current_limit_ma) ||
              (s_pending_sleep_seconds != s_data.screen_sleep_seconds);
    return true;
  }
  return false;
}

bool UserSettings_Process(uint16_t voltage_mv,
                          uint16_t current_ma,
                          uint32_t system_time_ms,
                          bool allow_write)
{
  if ((voltage_mv < POWER_CFG_VOLTAGE_MIN_MV) ||
      (voltage_mv > POWER_CFG_VOLTAGE_MAX_MV) ||
      (current_ma < POWER_CFG_CURRENT_MIN_MA) ||
      (current_ma > POWER_CFG_CURRENT_MAX_MA))
  {
    return false;
  }
  if ((voltage_mv != s_pending_voltage_mv) ||
      (current_ma != s_pending_current_ma))
  {
    s_pending_voltage_mv = voltage_mv;
    s_pending_current_ma = current_ma;
    s_last_change_time_ms = system_time_ms;
    s_dirty = (voltage_mv != s_data.voltage_setpoint_mv) ||
              (current_ma != s_data.current_limit_ma) ||
              (s_pending_sleep_seconds != s_data.screen_sleep_seconds);
  }
  if (UserSettings_IsSaving())
  { return UserSettings_AdvanceSave(system_time_ms, allow_write); }
  if (!s_dirty || !allow_write ||
      ((system_time_ms - s_last_change_time_ms) <
       USER_SETTINGS_SAVE_DELAY_MS) ||
      ((s_last_write_attempt_ms != 0U) &&
       ((system_time_ms - s_last_write_attempt_ms) <
        USER_SETTINGS_RETRY_DELAY_MS)))
  {
    return false;
  }
  s_last_write_attempt_ms = system_time_ms;
  (void)UserSettings_StartSave(system_time_ms);
  return false; /* success is reported only after later read-back verification */
}

bool UserSettings_SetScreenSleepSeconds(uint16_t seconds, uint32_t now)
{
  if (!UI_SleepSecondsValid(seconds)) { return false; }
  if (seconds != s_pending_sleep_seconds)
  {
    s_pending_sleep_seconds = seconds;
    s_last_change_time_ms = now;
    s_dirty = seconds != s_data.screen_sleep_seconds ||
      s_pending_voltage_mv != s_data.voltage_setpoint_mv ||
      s_pending_current_ma != s_data.current_limit_ma;
  }
  return true;
}
