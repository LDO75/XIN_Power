/**
  ******************************************************************************
  * @file    temperature_sensor.c
  * @brief   NCP18XH103F03RB温度采样、查表换算与诊断实现。
  ******************************************************************************
  */

#include "temperature_sensor.h"

#include "adc.h"

#include <stddef.h>
#include <string.h>

#define TEMPERATURE_SENSOR_PERIOD_MS                 100U
#define TEMPERATURE_SENSOR_SAMPLE_COUNT               16U
#define TEMPERATURE_SENSOR_ADC_TIMEOUT_MS               2U
#define TEMPERATURE_SENSOR_ADC_FULL_SCALE            4095U
#define TEMPERATURE_SENSOR_SHORT_THRESHOLD              20U
#define TEMPERATURE_SENSOR_OPEN_THRESHOLD             4050U

typedef struct
{
  uint16_t adc_code;
  int16_t temperature_decic;
} TemperatureSensor_TablePoint_t;

/*
 * NCP18XH103F03RB：R25=10kΩ，B25/50=3380K。
 * 分压结构为3.3V--10kΩ--PA0--NTC--GND，ADC参考同为3.3V，
 * 因而查表结果不依赖3.3V电源的绝对误差。
 */
static const TemperatureSensor_TablePoint_t s_temperature_table[] =
{
  {3613U, -200}, {3492U, -150}, {3353U, -100}, {3196U,  -50},
  {3024U,    0}, {2839U,   50}, {2644U,  100}, {2445U,  150},
  {2245U,  200}, {2048U,  250}, {1857U,  300}, {1675U,  350},
  {1505U,  400}, {1347U,  450}, {1203U,  500}, {1072U,  550},
  { 954U,  600}, { 849U,  650}, { 755U,  700}, { 672U,  750},
  { 598U,  800}, { 533U,  850}, { 476U,  900}, { 425U,  950},
  { 380U, 1000}, { 341U, 1050}, { 306U, 1100}, { 276U, 1150},
  { 249U, 1200}, { 224U, 1250}
};

static TemperatureSensor_Snapshot_t s_snapshot;
static uint32_t s_last_sample_time_ms;
static uint32_t s_filtered_adc_x8;
static bool s_filter_initialized;

/** @brief 连续读取多次ADC并返回平均值。 */
static bool TemperatureSensor_ReadAverage(uint16_t *average_adc)
{
  uint32_t sum = 0U;
  uint32_t index;

  if (average_adc == NULL)
  {
    return false;
  }

  for (index = 0U; index < TEMPERATURE_SENSOR_SAMPLE_COUNT; index++)
  {
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
      (void)HAL_ADC_Stop(&hadc1);
      return false;
    }
    if (HAL_ADC_PollForConversion(&hadc1,
                                  TEMPERATURE_SENSOR_ADC_TIMEOUT_MS) != HAL_OK)
    {
      (void)HAL_ADC_Stop(&hadc1);
      return false;
    }
    sum += HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);
  }

  *average_adc = (uint16_t)((sum +
    (TEMPERATURE_SENSOR_SAMPLE_COUNT / 2U)) /
    TEMPERATURE_SENSOR_SAMPLE_COUNT);
  return true;
}

bool TemperatureSensor_AdcToDecic(uint16_t adc_code,
                                  int16_t *temperature_decic)
{
  uint32_t index;

  if ((temperature_decic == NULL) ||
      (adc_code <= TEMPERATURE_SENSOR_SHORT_THRESHOLD) ||
      (adc_code >= TEMPERATURE_SENSOR_OPEN_THRESHOLD))
  {
    return false;
  }

  if (adc_code >= s_temperature_table[0].adc_code)
  {
    *temperature_decic = s_temperature_table[0].temperature_decic;
    return true;
  }

  for (index = 0U;
       index < ((sizeof(s_temperature_table) /
                 sizeof(s_temperature_table[0])) - 1U);
       index++)
  {
    const TemperatureSensor_TablePoint_t *cold = &s_temperature_table[index];
    const TemperatureSensor_TablePoint_t *hot = &s_temperature_table[index + 1U];

    if ((adc_code <= cold->adc_code) && (adc_code >= hot->adc_code))
    {
      uint32_t adc_span = (uint32_t)cold->adc_code - hot->adc_code;
      uint32_t adc_offset = (uint32_t)cold->adc_code - adc_code;
      int32_t temperature_span =
        (int32_t)hot->temperature_decic - cold->temperature_decic;

      *temperature_decic = (int16_t)(cold->temperature_decic +
        (int32_t)((adc_offset * (uint32_t)temperature_span +
                   (adc_span / 2U)) / adc_span));
      return true;
    }
  }

  *temperature_decic =
    s_temperature_table[(sizeof(s_temperature_table) /
                         sizeof(s_temperature_table[0])) - 1U].temperature_decic;
  return true;
}

bool TemperatureSensor_Process(uint32_t system_time_ms)
{
  uint16_t raw_adc;
  uint16_t filtered_adc;
  int16_t temperature_decic;

  if ((system_time_ms - s_last_sample_time_ms) <
      TEMPERATURE_SENSOR_PERIOD_MS)
  {
    return false;
  }
  s_last_sample_time_ms = system_time_ms;

  if (!TemperatureSensor_ReadAverage(&raw_adc))
  {
    s_snapshot.valid = false;
    s_snapshot.warning = false;
    s_snapshot.fault = TEMPERATURE_SENSOR_FAULT_ADC;
    s_snapshot.last_update_time_ms = system_time_ms;
    s_snapshot.sample_sequence++;
    return true;
  }

  if (!s_filter_initialized)
  {
    s_filtered_adc_x8 = (uint32_t)raw_adc * 8U;
    s_filter_initialized = true;
  }
  else
  {
    /* 一阶低通：新样本权重1/8，抑制开关电源噪声造成的温度跳动。 */
    s_filtered_adc_x8 = s_filtered_adc_x8 -
      ((s_filtered_adc_x8 + 4U) / 8U) + raw_adc;
  }
  filtered_adc = (uint16_t)((s_filtered_adc_x8 + 4U) / 8U);

  s_snapshot.raw_adc = filtered_adc;
  s_snapshot.last_update_time_ms = system_time_ms;
  s_snapshot.sample_sequence++;

  if (filtered_adc <= TEMPERATURE_SENSOR_SHORT_THRESHOLD)
  {
    s_snapshot.valid = false;
    s_snapshot.warning = false;
    s_snapshot.fault = TEMPERATURE_SENSOR_FAULT_SHORT;
  }
  else if (filtered_adc >= TEMPERATURE_SENSOR_OPEN_THRESHOLD)
  {
    s_snapshot.valid = false;
    s_snapshot.warning = false;
    s_snapshot.fault = TEMPERATURE_SENSOR_FAULT_OPEN;
  }
  else if (TemperatureSensor_AdcToDecic(filtered_adc, &temperature_decic))
  {
    s_snapshot.temperature_decic = temperature_decic;
    s_snapshot.valid = true;
    s_snapshot.warning = false;
    s_snapshot.fault = TEMPERATURE_SENSOR_FAULT_NONE;
  }
  else
  {
    s_snapshot.valid = false;
    s_snapshot.warning = false;
    s_snapshot.fault = TEMPERATURE_SENSOR_FAULT_ADC;
  }
  return true;
}

bool TemperatureSensor_Init(uint32_t system_time_ms)
{
  (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_filter_initialized = false;
  s_last_sample_time_ms = system_time_ms - TEMPERATURE_SENSOR_PERIOD_MS;
  (void)TemperatureSensor_Process(system_time_ms);
  return s_snapshot.valid;
}

bool TemperatureSensor_GetSnapshot(TemperatureSensor_Snapshot_t *snapshot)
{
  if (snapshot == NULL)
  {
    return false;
  }
  *snapshot = s_snapshot;
  return true;
}

const char *TemperatureSensor_GetFaultName(TemperatureSensor_Fault_t fault)
{
  static const char *const names[] = {"NONE", "ADC", "SHORT", "OPEN"};

  if ((unsigned int)fault >= (sizeof(names) / sizeof(names[0])))
  {
    return "UNKNOWN";
  }
  return names[(unsigned int)fault];
}
