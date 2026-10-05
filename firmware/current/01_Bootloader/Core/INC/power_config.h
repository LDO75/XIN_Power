/**
  ******************************************************************************
  * @file    power_config.h
  * @brief   数控电源量程、保护阈值和FB调压参数集中配置。
  ******************************************************************************
  */

#ifndef __POWER_CONFIG_H__
#define __POWER_CONFIG_H__

/* 用户可调范围。 */
#define POWER_CFG_VOLTAGE_MIN_MV              2000U
#define POWER_CFG_VOLTAGE_MAX_MV             32000U
#define POWER_CFG_CURRENT_MIN_MA                100U
#define POWER_CFG_CURRENT_MAX_MA               8000U

/* 初期功率与输入能力约束。 */
#define POWER_CFG_POWER_MAX_MW               100000UL
#define POWER_CFG_INPUT_CONTINUOUS_MA           4500U
#define POWER_CFG_ESTIMATED_EFFICIENCY_PERMILLE  900U

/* 软件紧急关断阈值。 */
#define POWER_CFG_INPUT_TRIP_MA                 4800U
#define POWER_CFG_OUTPUT_TRIP_MA                8500U
#define POWER_CFG_INPUT_TRIP_MV                28500U
#define POWER_CFG_OUTPUT_TRIP_MV               33000U
#define POWER_CFG_POWER_TRIP_MW               110000UL

/* 功率区NTC与风扇。 */
#define POWER_CFG_TEMPERATURE_WARNING_DECIC        600
#define POWER_CFG_TEMPERATURE_TRIP_DECIC           750
#define POWER_CFG_FAN_START_DEFAULT_C                40U
#define POWER_CFG_OUTPUT_BLEEDER_OHM               1800U
#define POWER_CFG_FAN_START_MIN_C                    30U
#define POWER_CFG_FAN_START_MAX_C                    65U
#define POWER_CFG_FAN_START_STEP_C                    1U

/* SC8701 FB网络和MCP4725参考电压。 */
#define POWER_CFG_FB_REFERENCE_UV            1220000UL
#define POWER_CFG_DAC_REFERENCE_UV           3000000UL
#define POWER_CFG_FB_UPPER_OHM                100000UL
#define POWER_CFG_FB_LOWER_OHM                  7620UL

#endif /* __POWER_CONFIG_H__ */
