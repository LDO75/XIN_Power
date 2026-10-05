#include "user_settings.h"
#include "w25q256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t slots[2][4096];
static unsigned busy_polls, writes;
static bool corrupt;
typedef struct {
  uint32_t magic;
  uint16_t schema, size;
  uint32_t sequence;
  uint16_t voltage, current;
  uint8_t fan, reserved[3];
  uint32_t crc;
} LegacySettings;
static uint32_t test_crc(const void *data, size_t length)
{
  const uint8_t *bytes = data;
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; bit++)
    { crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0); }
  }
  return ~crc;
}
static uint8_t *address_ptr(uint32_t address)
{
  assert(address >= 0x01FFC000U && address < 0x01FFE000U);
  return &slots[(address - 0x01FFC000U) / 4096U][address % 4096U];
}
bool W25Q256_IsReady(void) { return true; }
bool W25Q256_IsBusy(bool *busy)
{
  *busy = busy_polls > 0;
  if (busy_polls) { busy_polls--; }
  return true;
}
bool W25Q256_StartSectorErase(uint32_t address)
{
  assert(!busy_polls);
  memset(address_ptr(address), 0xFF, 4096);
  busy_polls = 3;
  return true;
}
bool W25Q256_StartPageProgram(uint32_t address, const void *data, uint16_t length)
{
  assert(!busy_polls);
  memcpy(address_ptr(address), data, length);
  if (corrupt) { address_ptr(address)[0] ^= 1U; }
  busy_polls = 2;
  writes++;
  return true;
}
bool W25Q256_Read(uint32_t address, void *data, uint32_t length)
{
  assert(!busy_polls);
  memcpy(data, address_ptr(address), length);
  return true;
}
/* Sync erase/write deliberately NOT provided: a regression will fail to link. */
int main(void)
{
  UserSettings_Data_t data;
  memset(slots, 0xFF, sizeof(slots));
  UserSettings_Init();
  assert(!UserSettings_Process(12000, 3000, 100, true));
  assert(!UserSettings_Process(12000, 3000, 1200, true));
  assert(UserSettings_IsSaving() && writes == 0U);
  /* OUT ON during erase: allow control immediately, defer programming. */
  for (unsigned t = 1210; t <= 1300; t += 10)
  { assert(!UserSettings_Process(12000, 3000, t, false)); }
  assert(writes == 0U && UserSettings_IsSaving());
  bool saved = false;
  for (unsigned t = 1310; t < 1450; t += 10)
  { saved |= UserSettings_Process(12000, 3000, t, true); }
  assert(saved && !UserSettings_IsSaving() && writes == 1U);
  UserSettings_Init();
  UserSettings_Get(&data);
  assert(data.loaded_from_flash && data.voltage_setpoint_mv == 12000U);
  /* Changing values while writing must remain dirty after committing snapshot. */
  assert(!UserSettings_Process(9000, 2000, 3000, true));
  assert(!UserSettings_Process(9000, 2000, 4100, true));
  saved = false;
  for (unsigned t = 4110; t < 4250; t += 10)
  { saved |= UserSettings_Process(8000, 2500, t, true); }
  assert(saved && writes == 2U);
  saved = false;
  for (unsigned t = 6200; t < 6400; t += 10)
  { saved |= UserSettings_Process(8000, 2500, t, true); }
  assert(saved && writes == 3U);
  UserSettings_Init();
  UserSettings_Get(&data);
  assert(data.voltage_setpoint_mv == 8000U && data.current_limit_ma == 2500U);
  /* Bad new record never replaces the last verified record. */
  corrupt = true;
  UserSettings_Process(5000, 100, 7000, true);
  for (unsigned t = 8100; t < 8300; t += 10)
  { assert(!UserSettings_Process(5000, 100, t, true)); }
  UserSettings_Init();
  UserSettings_Get(&data);
  assert(data.voltage_setpoint_mv == 8000U);
  /* Old 2.5V is clamped to 3.2V; current retained, new bounds persisted. */
  memset(slots, 0xFF, sizeof(slots));
  busy_polls = writes = 0; corrupt = false;
  LegacySettings old = {0};
  old.magic = 0x58534554U; old.schema = 2; old.size = sizeof(old);
  old.sequence = 20; old.voltage = 2500; old.current = 2300; old.fan = 40;
  old.crc = test_crc(&old, sizeof(old)); memcpy(slots[0], &old, sizeof(old));
  UserSettings_Init(); UserSettings_Get(&data);
  assert(data.loaded_from_flash && data.voltage_setpoint_mv == 3200 && data.current_limit_ma == 2300);
  assert(!UserSettings_Process(3199, 2300, 2000, true));
  assert(!UserSettings_IsSaving() && writes == 0);
  assert(!UserSettings_Process(3200, 2300, 2100, true));
  saved = false;
  for (unsigned t = 3110; t < 3300; t += 10) { saved |= UserSettings_Process(3200, 2300, t, true); }
  assert(saved && writes == 1);
  UserSettings_Init(); UserSettings_Get(&data);
  assert(data.voltage_setpoint_mv == 3200 && data.current_limit_ma == 2300);
  assert(data.screen_sleep_seconds==60); /* Legacy reserved bytes aren't a timer. */
  assert(!UserSettings_SetScreenSleepSeconds(17,4000));
  assert(UserSettings_SetScreenSleepSeconds(120,4000));
  for(unsigned t=5100;t<5200;t+=10)assert(!UserSettings_Process(3200,2300,t,false));
  assert(!UserSettings_IsSaving()); /* Screen settings never stop output for flash. */
  saved=false;
  for(unsigned t=5300;t<5480;t+=10)saved|=UserSettings_Process(3200,2300,t,true);
  assert(saved);UserSettings_Init();UserSettings_Get(&data);assert(data.screen_sleep_seconds==120);
  assert(UserSettings_SetScreenSleepSeconds(300,6000));
  UserSettings_Process(3200,2300,7100,true);
  assert(UserSettings_SetScreenSleepSeconds(0,7110));
  saved=false;for(unsigned t=7110;t<7290;t+=10)saved|=UserSettings_Process(3200,2300,t,true);
  assert(saved);UserSettings_Get(&data);assert(data.screen_sleep_seconds==300);
  saved=false;for(unsigned t=9300;t<9480;t+=10)saved|=UserSettings_Process(3200,2300,t,true);
  assert(saved);UserSettings_Init();UserSettings_Get(&data);assert(data.screen_sleep_seconds==0);
  puts("PASS: sleep timer schema3 persistence, legacy default, OFF-only async save, changed-while-saving and disabled timer");
  puts("3.2-32V settings + background saves + legacy retention: PASS");
  return 0;
}
