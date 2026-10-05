/**
  ******************************************************************************
  * @file    power_monitor.c
  * @brief   输入、输出双INA226采样、故障恢复和诊断输出实现。
  ******************************************************************************
  */

#include "power_monitor.h"

#include "calibration.h"
#include "debug_console.h"
#include "power_config.h"

#include "i2c.h"
#include "ina226.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define POWER_MONITOR_RETRY_PERIOD_MS      1000U
#define POWER_MONITOR_DEBUG_PERIOD_MS      1000U
#define POWER_MONITOR_OFFLINE_ERROR_COUNT     3U
#define POWER_MONITOR_SCAN_TIMEOUT_MS          2U
#define POWER_MONITOR_ACCUMULATOR_DIVISOR  3600U
#define POWER_MONITOR_DISPLAY_PERIOD_MS     20U
#define POWER_MONITOR_FILTER_SHIFT             2U

__weak void PowerMonitor_OnCurrentSample(bool output, int32_t calibrated_ua,
  int32_t uncalibrated_ua, uint16_t voltage_mv, uint32_t now)
{ (void)output; (void)calibrated_ua; (void)uncalibrated_ua; (void)voltage_mv; (void)now; }

/* Exact rolling 1s extrema from unfiltered valid voltage samples. Five hundred
 * 16-bit values use 1KB; scan once per 20ms, not on every sample. */
static uint16_t s_ripple_samples[500];
static uint32_t s_ripple_times[500];
static uint16_t s_ripple_write, s_ripple_count;
static uint32_t s_ripple_scan_ms;
static PowerMonitor_Snapshot_t s_snapshot;
/* Qualify stability only after an actual output/DAC transition. Once admitted,
 * all fluctuations are measured; a large Vpp does not pause itself. */
#define RIPPLE_MIN_HOLD_MS 200U
#define RIPPLE_STABLE_MS 400U
#define RIPPLE_STABLE_SPAN_MV 20U
#define RIPPLE_WINDOW_MS 1000U
static bool s_ripple_context_known, s_ripple_enabled, s_ripple_settling;
static bool s_ripple_stable_started, s_ripple_window_started;
static uint16_t s_ripple_dac, s_ripple_stable_low, s_ripple_stable_high;
static uint32_t s_ripple_transition_ms, s_ripple_stable_ms, s_ripple_window_ms;

void PowerMonitor_SetRippleContext(bool enabled, uint16_t dac, uint32_t now)
{
  if (!s_ripple_context_known || enabled != s_ripple_enabled || dac != s_ripple_dac)
  {
    s_ripple_context_known = true;
    s_ripple_enabled = enabled;
    s_ripple_dac = dac;
    s_ripple_transition_ms = now;
    s_ripple_settling = true;
    s_ripple_stable_started = s_ripple_window_started = false;
    s_ripple_write = s_ripple_count = 0U;
    /* Preserve the last published Vpp while adjusting, including OFF decay. */
  }
}

static void PowerMonitor_AddRippleSample(uint16_t mv, uint32_t now)
{
  if (s_snapshot.sample_sequence != 0U &&
      now - s_snapshot.last_valid_sample_time_ms > 20U)
  {
    s_snapshot.ripple_valid = false;
    s_snapshot.ripple_vpp_mv = 0U;
    s_ripple_write = s_ripple_count = 0U;
    s_ripple_settling = true;
    s_ripple_stable_started = s_ripple_window_started = false;
    s_ripple_transition_ms = now;
  }
  if (s_ripple_settling)
  {
    if (now - s_ripple_transition_ms < RIPPLE_MIN_HOLD_MS) { return; }
    if (!s_ripple_stable_started)
    {
      s_ripple_stable_started = true;
      s_ripple_stable_ms = now;
      s_ripple_stable_low = s_ripple_stable_high = mv;
    }
    if (mv < s_ripple_stable_low) { s_ripple_stable_low = mv; }
    if (mv > s_ripple_stable_high) { s_ripple_stable_high = mv; }
    if (s_ripple_stable_high - s_ripple_stable_low > RIPPLE_STABLE_SPAN_MV)
    {
      s_ripple_stable_ms = now;
      s_ripple_stable_low = s_ripple_stable_high = mv;
      return;
    }
    if (now - s_ripple_stable_ms < RIPPLE_STABLE_MS) { return; }
    s_ripple_settling = false;
  }
  if (!s_ripple_window_started)
  {
    s_ripple_window_started = true;
    s_ripple_window_ms = now;
  }
  s_ripple_samples[s_ripple_write] = mv;
  s_ripple_times[s_ripple_write] = now;
  s_ripple_write = (uint16_t)((s_ripple_write + 1U) % 500U);
  if (s_ripple_count < 500U) { s_ripple_count++; }
}

static INA226_Device_t s_input_device;
static INA226_Device_t s_output_device;
static INA226_Measurement_t s_input_measurement;
static INA226_Measurement_t s_output_measurement;
static uint64_t s_capacity_remainder;
static uint64_t s_energy_remainder;
static uint32_t s_last_sample_time_ms;
static uint32_t s_last_display_time_ms;
static uint32_t s_rate_start_ms, s_rate_start_sequence, s_max_sample_gap_ms;
static uint32_t s_attempt_count, s_late_count;
static uint32_t s_sample_period_ms = POWER_CFG_SAMPLE_PERIOD_MS;

bool PowerMonitor_SetSamplePeriod(uint32_t period_ms)
{
  if (period_ms < 1U || period_ms > 2000U) { return false; }
  s_sample_period_ms = period_ms;
  return true;
}
uint32_t PowerMonitor_GetSamplePeriod(void) { return s_sample_period_ms; }
static uint32_t s_last_accumulate_time_ms;
static uint32_t s_last_retry_time_ms;
static uint32_t s_last_debug_time_ms;
static bool s_snapshot_updated;
static bool s_filter_initialized;

/**
 * @brief  对无符号测量值执行1/4权重的一阶低通滤波。
 * @param  filtered 上一次滤波结果。
 * @param  sample 本次原始测量值。
 * @retval 新的滤波结果。
 */
static uint32_t PowerMonitor_FilterUnsigned(uint32_t filtered, uint32_t sample)
{
  int64_t difference = (int64_t)sample - (int64_t)filtered;

  if ((difference > -(int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)) &&
      (difference < (int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)))
  {
    return sample;
  }
  return (uint32_t)((int64_t)filtered +
                    (difference / (int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)));
}

/**
 * @brief  对有符号测量值执行1/4权重的一阶低通滤波。
 * @param  filtered 上一次滤波结果。
 * @param  sample 本次原始测量值。
 * @retval 新的滤波结果。
 */
static int32_t PowerMonitor_FilterSigned(int32_t filtered, int32_t sample)
{
  int64_t difference = (int64_t)sample - (int64_t)filtered;

  if ((difference > -(int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)) &&
      (difference < (int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)))
  {
    return sample;
  }
  return (int32_t)((int64_t)filtered +
                   (difference / (int64_t)(1UL << POWER_MONITOR_FILTER_SHIFT)));
}

/**
 * @brief  通过USART1发送一段已经格式化好的诊断文字。
 * @param  text 以零结尾的字符串。
 * @retval 无。
 */
static void PowerMonitor_UartWrite(const char *text)
{
  size_t length;

  if (text == NULL)
  {
    return;
  }
  length = strlen(text);
  if (length > UINT16_MAX)
  {
    length = UINT16_MAX;
  }
  DebugConsole_Write(text);
}

/**
 * @brief  上电时探测I2C1上的三个预期器件地址。
 * @retval 本次扫描发现的器件数量。
 * @note   只探测0x15、0x40和0x45，避免遍历空地址拖慢启动或干扰总线。
 */
static uint8_t PowerMonitor_ScanI2C1(void)
{
  char text[40];
  static const uint8_t expected_addresses[] = {0x15U, 0x40U, 0x45U};
  uint8_t address;
  uint8_t index;
  uint8_t found_count = 0U;

  PowerMonitor_UartWrite("[I2C1-SCAN] START\r\n");
  for (index = 0U;
       index < (uint8_t)(sizeof(expected_addresses) /
                         sizeof(expected_addresses[0]));
       index++)
  {
    address = expected_addresses[index];
    if (HAL_I2C_IsDeviceReady(&hi2c1,
                              (uint16_t)(address << 1U),
                              1U,
                              POWER_MONITOR_SCAN_TIMEOUT_MS) == HAL_OK)
    {
      (void)snprintf(text, sizeof(text),
                     "[I2C1-SCAN] FOUND 0x%02X\r\n",
                     (unsigned int)address);
      PowerMonitor_UartWrite(text);
      found_count++;
    }
  }
  (void)snprintf(text, sizeof(text),
                 "[I2C1-SCAN] END count=%u\r\n",
                 (unsigned int)found_count);
  PowerMonitor_UartWrite(text);
  return found_count;
}

/**
 * @brief  输出一个INA226的初始化阶段、ID和寄存器回读信息。
 * @param  name 设备名称，例如IN或OUT。
 * @param  device INA226设备对象。
 * @retval 无。
 */
static void PowerMonitor_PrintDeviceDetail(const char *name,
                                           const INA226_Device_t *device)
{
  char text[192];

  if ((name == NULL) || (device == NULL))
  {
    return;
  }
  (void)snprintf(text,
                 sizeof(text),
                 "[INA226-%s] addr=0x%02X state=%s fail=%s "
                 "mfg=0x%04X die=0x%04X cfg=0x%04X cal=0x%04X "
                 "hal=0x%08lX err=%lu\r\n",
                 name,
                 (unsigned int)device->address_7bit,
                 device->ready ? "OK" : "OFF",
                 INA226_GetErrorName(device->last_error),
                 (unsigned int)device->manufacturer_id,
                 (unsigned int)device->die_id,
                 (unsigned int)device->configuration_readback,
                 (unsigned int)device->calibration_readback,
                 (unsigned long)device->i2c_error_code,
                 (unsigned long)device->total_error_count);
  PowerMonitor_UartWrite(text);
}

/**
 * @brief  将有符号微安值转换为UI使用的非负毫安值。
 * @param  current_ua 有符号电流，单位为微安。
 * @retval 四舍五入后的非负毫安值。
 */
static uint16_t PowerMonitor_CurrentToMilliamp(int32_t current_ua)
{
  uint32_t current_ma;

  if (current_ua <= 0)
  {
    return 0U;
  }

  current_ma = ((uint32_t)current_ua + 500U) / 1000U;
  return (current_ma > UINT16_MAX) ? UINT16_MAX : (uint16_t)current_ma;
}

/**
 * @brief  根据连续通信错误次数更新一个INA226的在线状态。
 * @param  device INA226设备对象。
 * @param  online 对应在线状态指针。
 * @retval 无。
 */
static void PowerMonitor_UpdateOnlineState(INA226_Device_t *device,
                                           bool *online)
{
  if ((device == NULL) || (online == NULL))
  {
    return;
  }

  if (device->consecutive_error_count >= POWER_MONITOR_OFFLINE_ERROR_COUNT)
  {
    device->ready = false;
    *online = false;
  }
}

/**
 * @brief  对离线INA226执行低频自动重连。
 * @retval 无。
 */
static void PowerMonitor_RetryOfflineDevices(void)
{
  if (!s_snapshot.input_online)
  {
    s_snapshot.input_online = INA226_Init(&s_input_device,
                                         &hi2c1,
                                         INA226_INPUT_ADDRESS_7BIT,
                                         INA226_INPUT_CALIBRATION_VALUE,
                                         INA226_INPUT_CURRENT_LSB_UA,
                                         INA226_INPUT_POWER_LSB_MW);
  }

  if (!s_snapshot.output_online)
  {
    s_snapshot.output_online = INA226_Init(&s_output_device,
                                          &hi2c1,
                                          INA226_OUTPUT_ADDRESS_7BIT,
                                          INA226_OUTPUT_CALIBRATION_VALUE,
                                          INA226_OUTPUT_CURRENT_LSB_UA,
                                          INA226_OUTPUT_POWER_LSB_MW);
  }

  /* 重连只代表器件重新应答，必须等下一组双路完整采样后数据才有效。 */
  if (!s_snapshot.input_online || !s_snapshot.output_online)
  {
    s_snapshot.data_valid = false;
  }
}

/**
 * @brief  根据真实输出电流和功率累计电量与能量。
 * @param  elapsed_ms 距离上次累计的毫秒数。
 * @retval 无。
 */
static void PowerMonitor_Accumulate(uint32_t elapsed_ms)
{
  uint16_t output_current_ma;
  uint64_t capacity_numerator;
  uint64_t energy_numerator;

  if (!s_snapshot.output_online || (elapsed_ms == 0U))
  {
    return;
  }

  output_current_ma = PowerMonitor_CurrentToMilliamp(s_snapshot.output_current_ua);
  capacity_numerator = s_capacity_remainder +
                       ((uint64_t)output_current_ma * elapsed_ms);
  energy_numerator = s_energy_remainder +
                     ((uint64_t)s_snapshot.output_power_mw * elapsed_ms);

  s_snapshot.capacity_uah +=
    (uint32_t)(capacity_numerator / POWER_MONITOR_ACCUMULATOR_DIVISOR);
  s_snapshot.energy_uwh +=
    (uint32_t)(energy_numerator / POWER_MONITOR_ACCUMULATOR_DIVISOR);
  s_capacity_remainder = capacity_numerator % POWER_MONITOR_ACCUMULATOR_DIVISOR;
  s_energy_remainder = energy_numerator % POWER_MONITOR_ACCUMULATOR_DIVISOR;
}

/**
 * @brief  通过USART1输出双路INA226诊断数据。
 * @retval 无。
 */


/**
 * @brief  初始化输入端和输出端两个INA226。
 * @retval 两个器件都初始化成功时返回true。
 */
bool PowerMonitor_Init(void)
{
  uint32_t now;

  INA226_ResetDevice(&s_input_device);
  INA226_ResetDevice(&s_output_device);
  (void)PowerMonitor_ScanI2C1();

  s_snapshot.input_voltage_mv = 0U;
  s_snapshot.input_current_ua = 0;
  s_snapshot.input_power_mw = 0U;
  s_snapshot.output_voltage_mv = 0U;
  s_snapshot.output_current_ua = 0;
  s_snapshot.output_power_mw = 0U;
  s_snapshot.input_voltage_raw_mv = 0U;
  s_snapshot.input_current_raw_ua = 0;
  s_snapshot.input_power_raw_mw = 0U;
  s_snapshot.output_voltage_raw_mv = 0U;
  s_snapshot.output_current_raw_ua = 0;
  s_snapshot.output_power_raw_mw = 0U;
  s_snapshot.output_voltage_uncalibrated_mv = 0U;
  s_snapshot.output_current_uncalibrated_ua = 0;
  s_snapshot.sample_sequence = 0U;
  s_snapshot.last_valid_sample_time_ms = 0U;
  s_snapshot.capacity_uah = 0U;
  s_snapshot.energy_uwh = 0U;
  s_snapshot.input_error_count = 0U;
  s_snapshot.output_error_count = 0U;
  s_snapshot.input_online = INA226_Init(&s_input_device,
                                       &hi2c1,
                                       INA226_INPUT_ADDRESS_7BIT,
                                       INA226_INPUT_CALIBRATION_VALUE,
                                       INA226_INPUT_CURRENT_LSB_UA,
                                       INA226_INPUT_POWER_LSB_MW);
  s_snapshot.output_online = INA226_Init(&s_output_device,
                                        &hi2c1,
                                        INA226_OUTPUT_ADDRESS_7BIT,
                                        INA226_OUTPUT_CALIBRATION_VALUE,
                                        INA226_OUTPUT_CURRENT_LSB_UA,
                                        INA226_OUTPUT_POWER_LSB_MW);
  s_snapshot.data_valid = false;
  s_snapshot.input_error_count = s_input_device.total_error_count;
  s_snapshot.output_error_count = s_output_device.total_error_count;
  PowerMonitor_PrintDeviceDetail("IN", &s_input_device);
  PowerMonitor_PrintDeviceDetail("OUT", &s_output_device);

  now = HAL_GetTick();
  s_last_sample_time_ms = now;
  s_last_display_time_ms = now;
  s_rate_start_ms = now;
  s_rate_start_sequence = s_max_sample_gap_ms = 0U;
  s_attempt_count = s_late_count = 0U;
  s_last_accumulate_time_ms = now;
  s_last_retry_time_ms = now;
  s_last_debug_time_ms = now;
  s_capacity_remainder = 0U;
  s_energy_remainder = 0U;
  s_snapshot_updated = true;
  s_filter_initialized = false;
  s_ripple_write=s_ripple_count=0U; s_ripple_scan_ms=now;
  s_snapshot.ripple_vpp_mv=s_snapshot.sample_hz=s_snapshot.sample_maxgap_ms=0U;
  s_snapshot.ripple_valid=false;
  s_ripple_context_known = false;
  s_ripple_settling = true;
  s_ripple_stable_started = s_ripple_window_started = false;
  s_ripple_transition_ms = now;
  return (s_snapshot.input_online && s_snapshot.output_online);
}

/**
 * @brief  周期执行双INA226采样、自动重连、电量累计和串口诊断。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void INA226_OnCurrentReady(const INA226_Device_t *device,int32_t current_ua)
{
  bool output=device==&s_output_device;
  int32_t calibrated=output?Calibration_ApplyCurrent(current_ua):current_ua;
  /* Fresh current; voltage is the last complete sample at this early hook. */
  PowerMonitor_OnCurrentSample(output,calibrated,current_ua,
    output?s_snapshot.output_voltage_raw_mv:s_snapshot.input_voltage_raw_mv,HAL_GetTick());
}

static void PowerMonitor_UpdateRipple(uint32_t now)
{
  if(now-s_ripple_scan_ms<20U){return;}s_ripple_scan_ms=now;
  if (!s_snapshot.data_valid || now-s_snapshot.last_valid_sample_time_ms>20U)
  {
    /* Never retain a valid statistic across missing/stale samples. */
    s_snapshot.ripple_valid = false;
    s_snapshot.ripple_vpp_mv = 0U;
    s_ripple_write = s_ripple_count = 0U;
    s_ripple_settling = true;
    s_ripple_stable_started = s_ripple_window_started = false;
    s_ripple_transition_ms = now;
    return;
  }
  if (s_ripple_settling || !s_ripple_window_started ||
      now-s_ripple_window_ms<RIPPLE_WINDOW_MS) { return; }
  uint16_t low=UINT16_MAX,high=0U,count=0U;
  for(unsigned i=0;i<s_ripple_count;i++)
  {
    if(now-s_ripple_times[i]>=1000U){continue;}
    uint16_t value=s_ripple_samples[i];
    if(value<low){low=value;}if(value>high){high=value;}count++;
  }
  s_snapshot.ripple_valid=count>=2U && s_snapshot.data_valid &&
    now-s_snapshot.last_valid_sample_time_ms<=20U;
  s_snapshot.ripple_vpp_mv=s_snapshot.ripple_valid?(uint16_t)(high-low):0U;
}

void PowerMonitor_Process(uint32_t system_time_ms)
{
  uint32_t elapsed_ms;
  bool input_updated = false;
  bool output_updated = false;

  if ((system_time_ms - s_last_sample_time_ms) >= s_sample_period_ms)
  {
    s_attempt_count++;
    if (system_time_ms - s_last_sample_time_ms > s_sample_period_ms) { s_late_count++; }
    s_last_sample_time_ms = system_time_ms;

    if (s_snapshot.output_online)
    {
      output_updated=INA226_ReadMeasurement(&s_output_device,&s_output_measurement);
      PowerMonitor_UpdateOnlineState(&s_output_device,&s_snapshot.output_online);
    }
    if (s_snapshot.input_online)
    {
      input_updated=INA226_ReadMeasurement(&s_input_device,&s_input_measurement);
      PowerMonitor_UpdateOnlineState(&s_input_device,&s_snapshot.input_online);
    }

    if (input_updated && output_updated)
    {
      uint16_t corrected_output_voltage_mv;
      int32_t corrected_output_current_ua;
      uint32_t corrected_output_power_mw;

      /* 两路全部成功后再原子式提交，避免保护逻辑使用不同时刻的数据。 */
      s_snapshot.input_voltage_raw_mv = s_input_measurement.bus_voltage_mv;
      s_snapshot.input_current_raw_ua = s_input_measurement.current_ua;
      s_snapshot.input_power_raw_mw = s_input_measurement.power_mw;
      s_snapshot.output_voltage_uncalibrated_mv =
        s_output_measurement.bus_voltage_mv;
      s_snapshot.output_current_uncalibrated_ua =
        s_output_measurement.current_ua;
      corrected_output_voltage_mv = Calibration_ApplyVoltage(
        s_snapshot.output_voltage_uncalibrated_mv);
      corrected_output_current_ua = Calibration_ApplyCurrent(
        s_snapshot.output_current_uncalibrated_ua);
      /* 一键校准消除零点偏移；泄放电阻只在端口有电压时产生电流，
         必须按实际端口电压逐帧扣除，不能固化成一个零点偏移。 */
      {
        int32_t bleeder_current_ua = (int32_t)(
          ((uint32_t)corrected_output_voltage_mv * 1000U +
           (POWER_CFG_OUTPUT_BLEEDER_OHM / 2U)) /
          POWER_CFG_OUTPUT_BLEEDER_OHM);
        corrected_output_current_ua =
          (corrected_output_current_ua > bleeder_current_ua) ?
          (corrected_output_current_ua - bleeder_current_ua) : 0;
      }
      corrected_output_power_mw = (corrected_output_current_ua > 0) ?
        (uint32_t)(((uint64_t)corrected_output_voltage_mv *
                    (uint32_t)corrected_output_current_ua) / 1000000ULL) : 0U;
      PowerMonitor_AddRippleSample(corrected_output_voltage_mv, HAL_GetTick());
      s_snapshot.output_voltage_raw_mv = corrected_output_voltage_mv;
      s_snapshot.output_current_raw_ua = corrected_output_current_ua;
      s_snapshot.output_power_raw_mw = corrected_output_power_mw;

      if (!s_filter_initialized)
      {
        s_snapshot.input_voltage_mv = s_snapshot.input_voltage_raw_mv;
        s_snapshot.input_current_ua = s_snapshot.input_current_raw_ua;
        s_snapshot.input_power_mw = s_snapshot.input_power_raw_mw;
        s_snapshot.output_voltage_mv = s_snapshot.output_voltage_raw_mv;
        s_snapshot.output_current_ua = s_snapshot.output_current_raw_ua;
        s_snapshot.output_power_mw = s_snapshot.output_power_raw_mw;
        s_filter_initialized = true;
      }
      else if (system_time_ms - s_last_display_time_ms >= POWER_MONITOR_DISPLAY_PERIOD_MS)
      {
        s_last_display_time_ms = system_time_ms;
        s_snapshot.input_voltage_mv = (uint16_t)PowerMonitor_FilterUnsigned(
          s_snapshot.input_voltage_mv, s_snapshot.input_voltage_raw_mv);
        s_snapshot.input_current_ua = PowerMonitor_FilterSigned(
          s_snapshot.input_current_ua, s_snapshot.input_current_raw_ua);
        s_snapshot.input_power_mw = PowerMonitor_FilterUnsigned(
          s_snapshot.input_power_mw, s_snapshot.input_power_raw_mw);
        /* Large real steps (OUT ON/OFF or SET) bypass display smoothing.
         * Fine noise still uses the existing filter; control uses raw samples. */
        int32_t voltage_step = (int32_t)s_snapshot.output_voltage_raw_mv -
                               s_snapshot.output_voltage_mv;
        s_snapshot.output_voltage_mv =
          ((voltage_step > 100) || (voltage_step < -100)) ?
          s_snapshot.output_voltage_raw_mv :
          (uint16_t)PowerMonitor_FilterUnsigned(s_snapshot.output_voltage_mv,
                                                s_snapshot.output_voltage_raw_mv);
        s_snapshot.output_current_ua = PowerMonitor_FilterSigned(
          s_snapshot.output_current_ua, s_snapshot.output_current_raw_ua);
        s_snapshot.output_power_mw = PowerMonitor_FilterUnsigned(
          s_snapshot.output_power_mw, s_snapshot.output_power_raw_mw);
      }
      uint32_t completed_ms = HAL_GetTick();
      if (s_snapshot.sample_sequence != 0U)
      {
        uint32_t gap = completed_ms - s_snapshot.last_valid_sample_time_ms;
        if (gap > s_max_sample_gap_ms) { s_max_sample_gap_ms = gap; }
      }
      s_snapshot.sample_sequence++;
      s_snapshot.last_valid_sample_time_ms = completed_ms;
      s_snapshot.data_valid = true;
      s_snapshot_updated = true;
    }
    else if (!s_snapshot.input_online || !s_snapshot.output_online)
    {
      /* 离线后禁止继续使用最后一帧旧数据开启输出。 */
      s_snapshot.data_valid = false;
      s_filter_initialized = false;
      s_snapshot_updated = true;
    }

    elapsed_ms = system_time_ms - s_last_accumulate_time_ms;
    s_last_accumulate_time_ms = system_time_ms;
    PowerMonitor_Accumulate(elapsed_ms);

    s_snapshot.input_error_count = s_input_device.total_error_count;
    s_snapshot.output_error_count = s_output_device.total_error_count;
  }

  PowerMonitor_UpdateRipple(HAL_GetTick());

  if ((system_time_ms - s_last_retry_time_ms) >= POWER_MONITOR_RETRY_PERIOD_MS)
  {
    s_last_retry_time_ms = system_time_ms;
    PowerMonitor_RetryOfflineDevices();
    s_snapshot.input_error_count = s_input_device.total_error_count;
    s_snapshot.output_error_count = s_output_device.total_error_count;
    s_snapshot_updated = true;
  }

  if ((system_time_ms - s_last_debug_time_ms) >= POWER_MONITOR_DEBUG_PERIOD_MS)
  {
    s_last_debug_time_ms = system_time_ms;
    uint32_t dt=system_time_ms-s_rate_start_ms;
    s_snapshot.sample_hz=dt ? (uint16_t)((uint64_t)(s_snapshot.sample_sequence-
      s_rate_start_sequence)*1000U/dt) : 0U;
    s_snapshot.sample_maxgap_ms=s_max_sample_gap_ms>65535U ? 65535U : (uint16_t)s_max_sample_gap_ms;
    s_rate_start_ms = system_time_ms;
    s_rate_start_sequence = s_snapshot.sample_sequence;
    s_max_sample_gap_ms = s_attempt_count = s_late_count = 0U;
  }
}

/**
 * @brief  获取最近一次完整的双路监测快照。
 * @param  snapshot 用于接收监测数据的结构体。
 * @retval 自上次读取后存在新测量数据时返回true。
 */
bool PowerMonitor_GetSnapshot(PowerMonitor_Snapshot_t *snapshot)
{
  bool updated;

  if (snapshot == NULL)
  {
    return false;
  }

  *snapshot = s_snapshot;
  updated = s_snapshot_updated;
  s_snapshot_updated = false;
  return updated;
}
