/**
  ******************************************************************************
  * @file    calibration.c
  * @brief   独立测量校准与16点DAC输出曲线、W25Q256双备份持久化。
  ******************************************************************************
  */

#include "calibration.h"

#include "power_config.h"
#include "w25q256.h"

#include <stddef.h>
#include <string.h>

#define CALIBRATION_MAGIC                 0x5843414CUL /* XCAL */
#define CALIBRATION_SCHEMA_VERSION             2U
#define CALIBRATION_SCHEMA_VERSION_LEGACY      1U
#define CALIBRATION_VALID_VOLTAGE_MASK       0x01U
#define CALIBRATION_VALID_CURRENT_MASK       0x02U
#define CALIBRATION_SLOT_0_ADDRESS        0x01FFE000UL
#define CALIBRATION_SLOT_1_ADDRESS        0x01FFF000UL
#define CALIBRATION_GAIN_MIN_PPM           700000L
#define CALIBRATION_GAIN_MAX_PPM          1300000L
#define CALIBRATION_VOLTAGE_OFFSET_MAX_MV    1500L
#define CALIBRATION_CURRENT_OFFSET_MAX_UA  500000L
#define CALIBRATION_DAC_CODE_MAX              4095U
#define CALIBRATION_CURVE_MAGIC          0x58435552UL /* XCUR */
/* Extension stays in the SAME two reserved sectors, after the legacy record.
 * Write the extension first and the legacy record last as the commit marker.
 * 5.9 firmware can still read the unchanged schema-2 measurement record. */
#define CALIBRATION_CURVE_OFFSET               256U

typedef struct
{
  uint32_t magic;
  uint16_t schema_version;
  uint16_t size;
  uint32_t sequence;
  int32_t voltage_gain_ppm;
  int32_t voltage_offset_mv;
  int32_t current_gain_ppm;
  int32_t current_offset_ua;
  uint16_t dac_voltage_1_mv;
  uint16_t dac_code_1;
  uint16_t dac_voltage_2_mv;
  uint16_t dac_code_2;
  uint8_t measurement_valid;
  uint8_t dac_valid;
  uint16_t reserved;
  uint32_t crc32;
} Calibration_Record_t;

typedef struct
{
  uint32_t magic;
  uint16_t schema;
  uint16_t size;
  uint32_t sequence;
  uint32_t point_mask;
  uint16_t reserved;
  Calibration_Point_t points[CALIBRATION_POINT_COUNT];
  uint32_t crc32;
} Calibration_CurveRecord_t;

typedef struct
{
  uint32_t magic;
  uint16_t schema, size;
  uint32_t sequence;
  uint16_t point_mask, reserved;
  Calibration_Point_t points[16];
  uint32_t crc32;
} Calibration_CurveRecordV3_t;

typedef struct
{
  uint32_t magic;
  uint16_t schema, size;
  uint32_t sequence, point_mask;
  uint16_t reserved;
  Calibration_Point_t points[17];
  uint32_t crc32;
} Calibration_CurveRecordV4_t;
static const uint16_t s_old_targets[17] =
  {3000,3300,5000,7200,8400,9000,12000,18000,20000,24000,
   28000,32000,3600,3900,4000,4500,2000};

static const uint16_t s_point_targets[CALIBRATION_POINT_COUNT] =
  {3200U, 3300U, 3500U, 3800U, 4000U, 4200U, 5000U, 7200U,
   8400U, 9000U, 12000U, 18000U, 20000U, 24000U, 28000U, 32000U};
/* Schema 5 IDs are ordered by nominal voltage. Older schemas migrate by target. */
static const uint8_t s_point_order[CALIBRATION_POINT_COUNT] =
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static Calibration_Data_t s_data;
static uint32_t s_sequence;
static int8_t s_active_slot = -1;
static const char *s_save_error = "NONE";

const char *Calibration_GetSaveError(void) { return s_save_error; }

static uint32_t Calibration_Crc32(const void *data, size_t length)
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

uint16_t Calibration_PointTarget(uint8_t index)
{
  return index < CALIBRATION_POINT_COUNT ? s_point_targets[index] : 0U;
}

static bool Calibration_PointsValid(uint32_t mask,
                                    const Calibration_Point_t *points)
{
  if ((mask & ~((1UL << CALIBRATION_POINT_COUNT) - 1UL)) != 0U) { return false; }
  int previous = -1;
  uint8_t order;
  for (order = 0U; order < CALIBRATION_POINT_COUNT; order++)
  {
    uint8_t i = s_point_order[order];
    if ((mask & (1U << i)) == 0U) { continue; }
    if ((points[i].actual_mv == 0U) ||
        (points[i].dac_code > CALIBRATION_DAC_CODE_MAX)) { return false; }
    /* Voltage orders the interpolation intervals. Measured DAC codes may
     * increase, decrease or repeat: do not reject a real captured pair. */
    if ((previous >= 0) &&
        (points[i].actual_mv <= points[previous].actual_mv)) { return false; }
    previous = i;
  }
  return true;
}

static bool Calibration_CurveValid(const Calibration_CurveRecord_t *record)
{
  Calibration_CurveRecord_t copy = *record;
  uint32_t crc = copy.crc32;
  copy.crc32 = 0U;
  return record->magic == CALIBRATION_CURVE_MAGIC && record->schema == 5U &&
    record->reserved == POWER_CFG_DAC_HARDWARE_TAG &&
    record->size == sizeof(*record) &&
    Calibration_Crc32(&copy, sizeof(copy)) == crc &&
    Calibration_PointsValid(record->point_mask, record->points);
}

/* New explicit 4.99k profile: historical 5110 tags were reused on 7.5k
 * boards, so neither 5110 nor 7620 identifies a reusable DAC curve.
 * Keep ADC coefficients, ignore incompatible DAC mapping; do not erase flash. */
static bool Calibration_TagSupported(uint16_t tag)
{ return tag == POWER_CFG_DAC_HARDWARE_TAG; }

static bool Calibration_ReadCurve(uint32_t address, uint32_t sequence,
                                  Calibration_CurveRecord_t *curve)
{
  Calibration_CurveRecordV4_t old;
  if (!W25Q256_Read(address, &old, sizeof(old))) { return false; }
  if (old.magic == UINT32_MAX) { curve->magic = UINT32_MAX; return true; }
  if (old.schema == 5U)
  {
    (void)memcpy(curve, &old, sizeof(*curve));
    return Calibration_CurveValid(curve) && curve->sequence == sequence;
  }
  uint32_t mask, crc, computed;
  uint16_t tag;
  Calibration_Point_t points[17];
  (void)memset(points, 0, sizeof(points));
  if (old.schema == 3U)
  {
    Calibration_CurveRecordV3_t v3;
    (void)memcpy(&v3, &old, sizeof(v3));
    crc=v3.crc32; v3.crc32=0U;
    if (v3.magic != CALIBRATION_CURVE_MAGIC || v3.size != sizeof(v3) ||
        v3.sequence != sequence) { return false; }
    mask=v3.point_mask; tag=v3.reserved;
    computed=Calibration_Crc32(&v3,sizeof(v3));
    (void)memcpy(points,v3.points,sizeof(v3.points));
  }
  else if (old.schema == 4U)
  {
    crc=old.crc32; old.crc32=0U;
    if (old.magic != CALIBRATION_CURVE_MAGIC || old.size != sizeof(old) ||
        old.sequence != sequence || (old.point_mask & ~0x1FFFFUL)) { return false; }
    mask=old.point_mask; tag=old.reserved;
    computed=Calibration_Crc32(&old,sizeof(old));
    (void)memcpy(points,old.points,sizeof(points));
  }
  else { return false; }
  if (computed != crc || !Calibration_TagSupported(tag)) { return false; }
  (void)memset(curve,0,sizeof(*curve));
  curve->magic=CALIBRATION_CURVE_MAGIC; curve->schema=5U;
  curve->size=sizeof(*curve); curve->sequence=sequence; curve->reserved=tag;
  for (unsigned i=0U;i<17U;i++)
  {
    if (!(mask & (1UL<<i))) { continue; }
    if (points[i].actual_mv == 0U || points[i].dac_code > 4095U) { return false; }
    for (unsigned j=0U;j<CALIBRATION_POINT_COUNT;j++)
    {
      if (s_old_targets[i] == s_point_targets[j])
      { curve->points[j]=points[i]; curve->point_mask |= 1UL<<j; break; }
    }
  }
  curve->crc32=Calibration_Crc32(curve,sizeof(*curve));
  return Calibration_CurveValid(curve);
}

static void Calibration_SetDefaultsInternal(void)
{
  (void)memset(&s_data, 0, sizeof(s_data));
  s_data.voltage_gain_ppm = 1000000L;
  s_data.current_gain_ppm = 1000000L;
}

static bool Calibration_RecordValid(const Calibration_Record_t *record)
{
  Calibration_Record_t copy;
  uint32_t expected_crc;

  if ((record == NULL) ||
      (record->magic != CALIBRATION_MAGIC) ||
      ((record->schema_version != CALIBRATION_SCHEMA_VERSION) &&
       (record->schema_version != CALIBRATION_SCHEMA_VERSION_LEGACY)) ||
      (record->size != sizeof(Calibration_Record_t)))
  {
    return false;
  }
  copy = *record;
  expected_crc = copy.crc32;
  copy.crc32 = 0U;
  return Calibration_Crc32(&copy, sizeof(copy)) == expected_crc;
}

/**
 * @brief  判断序号a是否比序号b更新，兼容32位序号回绕。
 */
static bool Calibration_SequenceNewer(uint32_t a, uint32_t b)
{
  return (int32_t)(a - b) > 0;
}

/**
 * @brief  将有效记录装载到运行参数。
 */
static void Calibration_LoadRecord(const Calibration_Record_t *record,
                                   int8_t slot,
                                   const Calibration_CurveRecord_t *curve)
{
  s_sequence = record->sequence;
  s_active_slot = slot;
  s_data.voltage_gain_ppm = record->voltage_gain_ppm;
  s_data.voltage_offset_mv = record->voltage_offset_mv;
  s_data.current_gain_ppm = record->current_gain_ppm;
  s_data.current_offset_ua = record->current_offset_ua;
  s_data.dac_voltage_1_mv = record->dac_voltage_1_mv;
  s_data.dac_code_1 = record->dac_code_1;
  s_data.dac_voltage_2_mv = record->dac_voltage_2_mv;
  s_data.dac_code_2 = record->dac_code_2;
  if (record->schema_version == CALIBRATION_SCHEMA_VERSION_LEGACY)
  {
    /*
     * V5.8.1的电流LSB为0.2mA/bit，不能沿用到8A量程。
     * 旧记录只迁移电压和DAC校准，电流恢复默认增益并等待重新校准。
     */
    s_data.voltage_valid = record->measurement_valid != 0U;
    s_data.current_gain_ppm = 1000000L;
    s_data.current_offset_ua = 0L;
    s_data.current_valid = false;
  }
  else
  {
    s_data.voltage_valid =
      (record->measurement_valid & CALIBRATION_VALID_VOLTAGE_MASK) != 0U;
    s_data.current_valid =
      (record->measurement_valid & CALIBRATION_VALID_CURRENT_MASK) != 0U;
  }
  s_data.measurement_valid = s_data.voltage_valid && s_data.current_valid;
  s_data.dac_valid = record->dac_valid != 0U &&
    Calibration_TagSupported(record->reserved) &&
    record->dac_voltage_1_mv >= POWER_CFG_VOLTAGE_MIN_MV &&
    record->dac_voltage_2_mv >= POWER_CFG_VOLTAGE_MIN_MV;
  s_data.loaded_from_flash = true;
  if (curve != NULL && curve->magic == CALIBRATION_CURVE_MAGIC)
  {
    s_data.point_mask = curve->point_mask;
    (void)memcpy(s_data.points, curve->points, sizeof(s_data.points));
  }
}

void Calibration_Init(void)
{
  Calibration_Record_t slot_0;
  Calibration_Record_t slot_1;
  Calibration_CurveRecord_t curve_0;
  Calibration_CurveRecord_t curve_1;
  bool slot_0_valid;
  bool slot_1_valid;

  (void)memset(&curve_0, 0xFF, sizeof(curve_0));
  (void)memset(&curve_1, 0xFF, sizeof(curve_1));
  Calibration_SetDefaultsInternal();
  s_sequence = 0U;
  s_active_slot = -1;
  if (!W25Q256_Init())
  {
    return;
  }

  slot_0_valid = W25Q256_Read(CALIBRATION_SLOT_0_ADDRESS,
                              &slot_0,
                              sizeof(slot_0)) &&
                 Calibration_RecordValid(&slot_0);
  slot_1_valid = W25Q256_Read(CALIBRATION_SLOT_1_ADDRESS,
                              &slot_1,
                              sizeof(slot_1)) &&
                 Calibration_RecordValid(&slot_1);
  /* A damaged extension invalidates the whole newer slot, allowing fallback
   * to the previous complete calibration. Erased extensions are legacy data. */
  slot_0_valid = slot_0_valid &&
    (!Calibration_TagSupported(slot_0.reserved) ||
     Calibration_ReadCurve(CALIBRATION_SLOT_0_ADDRESS + CALIBRATION_CURVE_OFFSET,
                           slot_0.sequence, &curve_0));
  slot_1_valid = slot_1_valid &&
    (!Calibration_TagSupported(slot_1.reserved) ||
     Calibration_ReadCurve(CALIBRATION_SLOT_1_ADDRESS + CALIBRATION_CURVE_OFFSET,
                           slot_1.sequence, &curve_1));

  if (slot_0_valid && slot_1_valid)
  {
    if (Calibration_SequenceNewer(slot_1.sequence, slot_0.sequence))
    {
      Calibration_LoadRecord(&slot_1, 1, &curve_1);
    }
    else
    {
      Calibration_LoadRecord(&slot_0, 0, &curve_0);
    }
  }
  else if (slot_0_valid)
  {
    Calibration_LoadRecord(&slot_0, 0, &curve_0);
  }
  else if (slot_1_valid)
  {
    Calibration_LoadRecord(&slot_1, 1, &curve_1);
  }
}

void Calibration_Get(Calibration_Data_t *data)
{
  if (data != NULL)
  {
    *data = s_data;
  }
}

void Calibration_ResetDefaults(void)
{
  Calibration_SetDefaultsInternal();
}

bool Calibration_SetVoltage(int32_t gain_ppm, int32_t offset_mv)
{
  if ((gain_ppm < CALIBRATION_GAIN_MIN_PPM) ||
      (gain_ppm > CALIBRATION_GAIN_MAX_PPM) ||
      (offset_mv < -CALIBRATION_VOLTAGE_OFFSET_MAX_MV) ||
      (offset_mv > CALIBRATION_VOLTAGE_OFFSET_MAX_MV))
  {
    return false;
  }
  s_data.voltage_gain_ppm = gain_ppm;
  s_data.voltage_offset_mv = offset_mv;
  s_data.voltage_valid = true;
  s_data.measurement_valid = s_data.current_valid;
  s_data.loaded_from_flash = false;
  return true;
}

bool Calibration_SetCurrent(int32_t gain_ppm, int32_t offset_ua)
{
  if ((gain_ppm < CALIBRATION_GAIN_MIN_PPM) ||
      (gain_ppm > CALIBRATION_GAIN_MAX_PPM) ||
      (offset_ua < -CALIBRATION_CURRENT_OFFSET_MAX_UA) ||
      (offset_ua > CALIBRATION_CURRENT_OFFSET_MAX_UA))
  {
    return false;
  }
  s_data.current_gain_ppm = gain_ppm;
  s_data.current_offset_ua = offset_ua;
  s_data.current_valid = true;
  s_data.measurement_valid = s_data.voltage_valid;
  s_data.loaded_from_flash = false;
  return true;
}

bool Calibration_SetCurrentZero(int32_t raw_zero_ua)
{
  int64_t offset_ua = -(((int64_t)raw_zero_ua *
                         s_data.current_gain_ppm) / 1000000LL);

  if ((offset_ua < -CALIBRATION_CURRENT_OFFSET_MAX_UA) ||
      (offset_ua > CALIBRATION_CURRENT_OFFSET_MAX_UA))
  {
    return false;
  }
  return Calibration_SetCurrent(s_data.current_gain_ppm, (int32_t)offset_ua);
}

bool Calibration_SaveCurrentZero(int32_t raw_zero_ua)
{
  Calibration_Data_t previous_data = s_data;
  uint32_t previous_sequence = s_sequence;
  int8_t previous_slot = s_active_slot;

  if (Calibration_SetCurrentZero(raw_zero_ua) && Calibration_Save())
  {
    return true;
  }

  /* Flash失败时不要出现“显示失败但运行参数已经变化”的半提交状态。 */
  s_data = previous_data;
  s_sequence = previous_sequence;
  s_active_slot = previous_slot;
  return false;
}

bool Calibration_SetDac(uint16_t voltage_1_mv,
                        uint16_t code_1,
                        uint16_t voltage_2_mv,
                        uint16_t code_2)
{
  if ((voltage_1_mv < POWER_CFG_VOLTAGE_MIN_MV) ||
      (voltage_2_mv > POWER_CFG_VOLTAGE_MAX_MV) ||
      (voltage_1_mv >= voltage_2_mv) ||
      (code_1 > CALIBRATION_DAC_CODE_MAX) ||
      (code_2 > CALIBRATION_DAC_CODE_MAX) ||
      (code_1 <= code_2))
  {
    return false;
  }
  s_data.dac_voltage_1_mv = voltage_1_mv;
  s_data.dac_code_1 = code_1;
  s_data.dac_voltage_2_mv = voltage_2_mv;
  s_data.dac_code_2 = code_2;
  s_data.dac_valid = true;
  s_data.loaded_from_flash = false;
  return true;
}

bool Calibration_Save(void)
{
  Calibration_Record_t record;
  Calibration_Record_t verify;
  Calibration_CurveRecord_t curve;
  Calibration_CurveRecord_t curve_verify;
  uint32_t target_address;
  int8_t target_slot;

  s_save_error = "NONE";
  (void)memset(&record, 0, sizeof(record));
  record.magic = CALIBRATION_MAGIC;
  record.schema_version = CALIBRATION_SCHEMA_VERSION;
  record.size = sizeof(record);
  record.sequence = s_sequence + 1U;
  record.voltage_gain_ppm = s_data.voltage_gain_ppm;
  record.voltage_offset_mv = s_data.voltage_offset_mv;
  record.current_gain_ppm = s_data.current_gain_ppm;
  record.current_offset_ua = s_data.current_offset_ua;
  record.dac_voltage_1_mv = s_data.dac_voltage_1_mv;
  record.dac_code_1 = s_data.dac_code_1;
  record.dac_voltage_2_mv = s_data.dac_voltage_2_mv;
  record.dac_code_2 = s_data.dac_code_2;
  record.measurement_valid =
    (uint8_t)((s_data.voltage_valid ? CALIBRATION_VALID_VOLTAGE_MASK : 0U) |
              (s_data.current_valid ? CALIBRATION_VALID_CURRENT_MASK : 0U));
  record.dac_valid = s_data.dac_valid ? 1U : 0U;
  record.reserved = POWER_CFG_DAC_HARDWARE_TAG;
  record.crc32 = 0U;
  record.crc32 = Calibration_Crc32(&record, sizeof(record));
  (void)memset(&curve, 0, sizeof(curve));
  curve.magic = CALIBRATION_CURVE_MAGIC;
  curve.schema = 5U;
  curve.reserved = POWER_CFG_DAC_HARDWARE_TAG;
  curve.size = sizeof(curve);
  curve.sequence = record.sequence;
  curve.point_mask = s_data.point_mask;
  (void)memcpy(curve.points, s_data.points, sizeof(curve.points));
  if (!Calibration_PointsValid(curve.point_mask, curve.points))
  { s_save_error = "POINT_ORDER_OR_VALUE"; return false; }
  curve.crc32 = Calibration_Crc32(&curve, sizeof(curve));

  if (!W25Q256_IsReady() && !W25Q256_Init())
  {
    s_save_error = "FLASH_INIT_FAILED";
    return false;
  }

  target_slot = (s_active_slot == 0) ? 1 : 0;
  target_address = (target_slot == 0) ? CALIBRATION_SLOT_0_ADDRESS :
                                        CALIBRATION_SLOT_1_ADDRESS;
  /* Report the exact failed persistence stage; keep the older slot intact. */
  if (!W25Q256_EraseSector(target_address))
  { s_save_error = "FLASH_ERASE_FAILED"; return false; }
  if (!W25Q256_Write(target_address + CALIBRATION_CURVE_OFFSET,
                     &curve, sizeof(curve)))
  { s_save_error = "FLASH_CURVE_WRITE_FAILED"; return false; }
  if (!W25Q256_Read(target_address + CALIBRATION_CURVE_OFFSET,
                    &curve_verify, sizeof(curve_verify)))
  { s_save_error = "FLASH_CURVE_READ_FAILED"; return false; }
  if (memcmp(&curve, &curve_verify, sizeof(curve)) != 0)
  { s_save_error = "FLASH_CURVE_READBACK_MISMATCH"; return false; }
  if (!Calibration_CurveValid(&curve_verify))
  { s_save_error = "FLASH_CURVE_CRC_OR_FORMAT"; return false; }
  if (!W25Q256_Write(target_address, &record, sizeof(record)))
  { s_save_error = "FLASH_RECORD_WRITE_FAILED"; return false; }
  if (!W25Q256_Read(target_address, &verify, sizeof(verify)))
  { s_save_error = "FLASH_RECORD_READ_FAILED"; return false; }
  if (memcmp(&record, &verify, sizeof(record)) != 0)
  { s_save_error = "FLASH_RECORD_READBACK_MISMATCH"; return false; }
  if (!Calibration_RecordValid(&verify))
  { s_save_error = "FLASH_RECORD_CRC_OR_FORMAT"; return false; }

  s_active_slot = target_slot;
  s_sequence = record.sequence;
  s_data.loaded_from_flash = true;
  return true;
}

bool Calibration_SavePoint(uint8_t index, uint16_t actual_mv, uint16_t code)
{
  Calibration_Data_t previous = s_data;
  if (index >= CALIBRATION_POINT_COUNT)
  { s_save_error = "POINT_INDEX"; return false; }
  s_data.points[index].actual_mv = actual_mv;
  s_data.points[index].dac_code = code;
  s_data.point_mask |= (1UL << index);
  if (!Calibration_PointsValid(s_data.point_mask, s_data.points))
  { s_save_error = "POINT_ORDER_OR_VALUE"; s_data = previous; return false; }
  if (!Calibration_Save())
  { s_data = previous; return false; }
  return true;
}

uint16_t Calibration_ApplyVoltage(uint16_t raw_mv)
{
  int64_t result;

  if (!s_data.voltage_valid)
  {
    return raw_mv;
  }
  result = ((int64_t)raw_mv * s_data.voltage_gain_ppm + 500000LL) /
           1000000LL + s_data.voltage_offset_mv;
  if (result < 0)
  {
    return 0U;
  }
  return (result > UINT16_MAX) ? UINT16_MAX : (uint16_t)result;
}

int32_t Calibration_ApplyCurrent(int32_t raw_ua)
{
  int64_t result;

  if (!s_data.current_valid)
  {
    return raw_ua;
  }
  result = ((int64_t)raw_ua * s_data.current_gain_ppm) / 1000000LL +
           s_data.current_offset_ua;
  if (result > INT32_MAX)
  {
    return INT32_MAX;
  }
  if (result < INT32_MIN)
  {
    return INT32_MIN;
  }
  return (int32_t)result;
}

bool Calibration_MapDac(uint16_t voltage_mv, uint16_t *code)
{
  int64_t numerator;
  int64_t result;
  int32_t voltage_span;

  int first = -1, second = -1, previous = -1, last = -1, lower = -1, upper = -1;
  uint8_t order;
  if (code == NULL) { return false; }
  /* Map measured voltage/DAC pairs directly. Outside the measured span use
   * the nearest end segment. Point IDs label the calibration locations only. */
  for (order = 0U; order < CALIBRATION_POINT_COUNT; order++)
  {
    uint8_t i = s_point_order[order];
    if ((s_data.point_mask & (1U << i)) == 0U) { continue; }
    if (voltage_mv == s_data.points[i].actual_mv)
    { *code = s_data.points[i].dac_code; return true; }
    if (first < 0) { first = i; }
    else if (second < 0) { second = i; }
    previous = last;
    last = i;
    if (s_data.points[i].actual_mv <= voltage_mv) { lower = i; }
    else if (upper < 0) { upper = i; }
  }
  if (second >= 0)
  {
    if (lower < 0) { lower = first; upper = second; }
    if (upper < 0) { lower = previous; upper = last; }
    voltage_span = s_data.points[upper].actual_mv - s_data.points[lower].actual_mv;
    numerator = (int64_t)((int32_t)voltage_mv - s_data.points[lower].actual_mv) *
      ((int32_t)s_data.points[upper].dac_code - s_data.points[lower].dac_code);
    /* Nearest DAC code for either slope; ties round away from zero.
     * Fixed targets always produce one fixed code, never time dithering. */
    int64_t quotient = numerator >= 0 ?
      (numerator + voltage_span / 2) / voltage_span :
      -((-numerator + voltage_span / 2) / voltage_span);
    result = s_data.points[lower].dac_code + quotient;
    if (result < 0) { result = 0; }
    if (result > CALIBRATION_DAC_CODE_MAX) { result = CALIBRATION_DAC_CODE_MAX; }
    *code = (uint16_t)result;
    return true;
  }
  if (!s_data.dac_valid)
  {
    return false;
  }
  voltage_span = (int32_t)s_data.dac_voltage_2_mv -
                 (int32_t)s_data.dac_voltage_1_mv;
  numerator = (int64_t)((int32_t)voltage_mv -
                        (int32_t)s_data.dac_voltage_1_mv) *
              ((int32_t)s_data.dac_code_2 - (int32_t)s_data.dac_code_1);
  result = (int64_t)s_data.dac_code_1 + (numerator >= 0 ?
    (numerator + voltage_span / 2) / voltage_span :
    -((-numerator + voltage_span / 2) / voltage_span));
  if (result < 0)
  {
    result = 0;
  }
  else if (result > CALIBRATION_DAC_CODE_MAX)
  {
    result = CALIBRATION_DAC_CODE_MAX;
  }
  *code = (uint16_t)result;
  return true;
}

bool Calibration_GetLowerAnchor(uint16_t voltage_mv, uint16_t *code)
{
  int best = -1;
  uint8_t i;
  if (code == NULL) { return false; }
  for (i = 0U; i < CALIBRATION_POINT_COUNT; i++)
  {
    if ((s_data.point_mask & (1U << i)) != 0U &&
        s_data.points[i].actual_mv <= voltage_mv &&
        (best < 0 || s_data.points[i].actual_mv > s_data.points[best].actual_mv))
    { best = i; }
  }
  if (best < 0) { return false; }
  *code = s_data.points[best].dac_code;
  return true;
}
