/**
  ******************************************************************************
  * @file    power_config.h
  * @brief   数控电源量程、保护阈值和FB调压参数集中配置。
  ******************************************************************************
  */

#ifndef __POWER_CONFIG_H__
#define __POWER_CONFIG_H__

/* 用户可调范围。 */
#define POWER_CFG_VOLTAGE_MIN_MV              3200U
#define POWER_CFG_VOLTAGE_MAX_MV             32000U
#define POWER_CFG_CURRENT_MIN_MA                100U
#define POWER_CFG_CURRENT_MAX_MA               8000U

/* Short-circuit overcurrent uses these SAME immediate current trips.
 * No independent low-voltage, small-current or startup-delay short detector. */
#define POWER_CFG_INPUT_TRIP_MA                 4800U
#define POWER_CFG_OUTPUT_TRIP_MA                8500U

/* 功率区NTC与风扇。 */
#define POWER_CFG_FAN_START_DEFAULT_C                40U
#define POWER_CFG_FAN_STOP_C                         35U
#define POWER_CFG_OUTPUT_BLEEDER_OHM               1800U
#define POWER_CFG_FAN_START_MIN_C                    30U
#define POWER_CFG_FAN_START_MAX_C                    65U
#define POWER_CFG_FAN_START_STEP_C                    1U

/* SC8701 FB网络和MCP4725参考电压。 */
#define POWER_CFG_FB_REFERENCE_UV            1220000UL
#define POWER_CFG_DAC_REFERENCE_UV           3000000UL
#define POWER_CFG_FB_UPPER_OHM                100000UL
#define POWER_CFG_FB_LOWER_OHM                  7620UL

/* R274=100k, R314+R315=7.62k, R316+R317=4.99k+120=5.11k. */
#define POWER_CFG_FB_INJECTION_OHM              5110UL
#define POWER_CFG_DAC_SCALE_CODES               4096UL
#define POWER_CFG_SAMPLE_PERIOD_MS                2U
#define POWER_CFG_DAC_HARDWARE_TAG             0x4991U

#define POWER_CFG_INPUT_UV_MV                   4000U
#define POWER_CFG_INPUT_UV_DELAY_MS               200U
#define POWER_CFG_OUTPUT_ABSOLUTE_OV_MV         33000U
#define POWER_CFG_OUTPUT_OV_MARGIN_MV             300U
#define POWER_CFG_OUTPUT_OV_PERMILLE              100U
/* C9+C10+C285=300uF, C238+C239+C241=30uF, bypasses ~3uF.
 * 400uF covers nominal capacitance plus 20% tolerance. R292 is confirmed
 * fitted as 1.8k, giving a conservative 720ms RC time constant. */
#define POWER_CFG_OUTPUT_DISCHARGE_CAP_UF         400U
#define POWER_CFG_OUTPUT_DISCHARGE_TAU_MS \
  ((POWER_CFG_OUTPUT_BLEEDER_OHM * POWER_CFG_OUTPUT_DISCHARGE_CAP_UF + 999U) / 1000U)
#define POWER_CFG_OUTPUT_OV_DELAY_MS              100U
#define POWER_CFG_OVER_TEMPERATURE_DECIC          800
#define POWER_CFG_MONITOR_LOSS_MIN_MS             500U
#define POWER_CFG_MONITOR_LOSS_PERIODS              3U

/* Small-error outer voltage loop: PI (D=0), per fresh 2ms sample.
 * +/-100mV is the observation gate, NOT a total compensation limit.
 * Coefficients are trial defaults pending measured load-step tuning. */
#define POWER_CFG_TRIM_BAND_MV                 100L
#define POWER_CFG_TRIM_DEADBAND_MV               8L
#define POWER_CFG_TRIM_KP                       0.20f
#define POWER_CFG_TRIM_KI_PER_SECOND            2.00f
#define POWER_CFG_TRIM_MAX_SAMPLE_GAP_MS         20U
#define POWER_CFG_TRIM_DAC_INTERVAL_MS           10U
#endif /* __POWER_CONFIG_H__ */
