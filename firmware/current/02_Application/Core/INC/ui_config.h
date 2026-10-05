/**
  ******************************************************************************
  * @file    ui_config.h
  * @brief   数控电源UI刷新、触摸和设置行为参数。
  ******************************************************************************
  */

#ifndef __UI_CONFIG_H__
#define __UI_CONFIG_H__

#include <stdbool.h>
#include <stdint.h>
#define UI_SCREEN_SLEEP_DEFAULT_SECONDS 60U
static inline bool UI_SleepSecondsValid(uint16_t seconds)
{ return seconds==0U || seconds==30U || seconds==60U || seconds==120U || seconds==300U || seconds==600U; }

/* UI调度与动态数据显示周期。 */
#define UI_ENGINE_PERIOD_MS               20U
#define UI_DISPLAY_PERIOD_MS              20U

/* 点击、滑动识别与重复触摸抑制参数。 */
#define UI_TOUCH_DEBOUNCE_MS              60U
#define UI_SWIPE_MIN_DISTANCE_PX          36U
#define UI_SWIPE_MAX_VERTICAL_PX          28U
#define UI_TAP_MAX_DISTANCE_PX            12U
#define UI_SWIPE_MAX_DURATION_MS         700U

/* 设置页面亮度范围与单次调整步进。 */
#define UI_BRIGHTNESS_MIN_PERCENT         10U
#define UI_BRIGHTNESS_MAX_PERCENT        100U
#define UI_BRIGHTNESS_STEP_PERCENT        10U

/* 每次有效触摸的蜂鸣器提示时长。 */
#define UI_BUZZER_TOUCH_DURATION_MS       28U

#endif /* __UI_CONFIG_H__ */
