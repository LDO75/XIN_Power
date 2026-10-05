/**
  ******************************************************************************
  * @file    ui_page.h
  * @brief   迷你数控电源多页面UI及触摸手势接口。
  ******************************************************************************
  */

#ifndef __UI_PAGE_H__
#define __UI_PAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  UI_PAGE_NUMERIC = 0,
  UI_PAGE_WAVEFORM,
  UI_PAGE_SETTINGS
} UI_Page_t;

typedef enum
{
  UI_CALIBRATION_IDLE = 0,
  UI_CALIBRATION_RUNNING,
  UI_CALIBRATION_SAVED,
  UI_CALIBRATION_FAILED,
  UI_CALIBRATION_OUTPUT_MUST_BE_OFF
} UI_CalibrationStatus_t;

typedef struct
{
  uint16_t voltage_setpoint_mv;
  uint16_t current_limit_ma;
  uint8_t brightness_percent;
  bool output_requested;
  bool buzzer_enabled;
  bool display_rotated_180;
} UI_ControlRequest_t;

/**
 * @brief  初始化UI状态并绘制数字显示首页。
 * @param  touch_available CST836U初始化成功时传入true。
 * @retval 无。
 */
void UI_Init(bool touch_available);

/**
 * @brief  运行触摸处理和UI局部刷新。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void UI_Process(uint32_t system_time_ms);

/**
 * @brief  更新触摸控制器在线状态，并按需刷新顶栏提示。
 * @param  touch_available 触摸控制器当前可用时传入true。
 * @retval 无。
 */
void UI_SetTouchAvailable(bool touch_available);

/**
 * @brief  写入真实测量数据，并关闭内置演示数据模型。
 * @param  input_voltage_mv 输入电压，单位为毫伏。
 * @param  output_voltage_mv 输出电压，单位为毫伏。
 * @param  output_current_ma 输出电流，单位为毫安。
 * @param  output_power_mw 输出功率，单位为毫瓦。
 * @param  temperature_decic 温度，单位为0.1摄氏度。
 * @param  constant_current 功率级处于恒流模式时传入true。
 * @retval 无。
 */
void UI_SetMeasurements(uint16_t input_voltage_mv,
                        uint16_t output_voltage_mv,
                        uint16_t output_current_ma,
                        uint32_t output_power_mw,
                        int16_t temperature_decic,
                        bool constant_current);

/**
 * @brief  写入累计输出电量和能量。
 * @param  capacity_uah 累计电量，单位为微安时。
 * @param  energy_uwh 累计能量，单位为微瓦时。
 * @retval 无。
 */
void UI_SetAccumulatedValues(uint32_t capacity_uah, uint32_t energy_uwh);

/**
 * @brief  读取触摸UI选择的设定值和输出请求状态。
 * @param  request 用于接收控制请求的结构体。
 * @retval 无。
 */
void UI_GetControlRequest(UI_ControlRequest_t *request);

/**
 * @brief  由上位机修改输出电压设定值并同步刷新屏幕。
 * @param  voltage_mv 目标输出电压，单位毫伏。
 * @retval 数值处于允许范围并已接受时返回true。
 */
bool UI_SetVoltageSetpoint(uint16_t voltage_mv);

/**
 * @brief  由上位机修改输出电流限制并同步刷新屏幕。
 * @param  current_ma 目标电流限制，单位毫安。
 * @retval 数值处于允许范围并已接受时返回true。
 */
bool UI_SetCurrentLimit(uint16_t current_ma);

/**
 * @brief  由上位机或保护逻辑修改输出开关请求并同步刷新屏幕。
 * @param  enabled 请求开启时传入true，请求关闭时传入false。
 * @retval 无。
 */
void UI_SetOutputRequested(bool enabled);

/**
 * @brief  读取当前正在显示的页面。
 * @retval 当前页面枚举值。
 */
UI_Page_t UI_GetCurrentPage(void);

/**
 * @brief  发生功率级故障时强制清除UI输出请求并刷新开关状态。
 * @retval 无。
 */
void UI_ForceOutputOff(void);

/**
 * @brief  更新功率级故障显示；故障时输出键显示FLT。
 * @param  active 当前存在已锁存故障时传入true。
 * @retval 无。
 */
void UI_SetPowerFault(bool active);

/**
 * @brief  读取并清除本地“确认故障”请求。
 * @retval 故障状态下用户点击过输出键时返回true。
 */
bool UI_TakeFaultClearRequest(void);

/**
 * @brief  读取并清除本地“输出电流零点校准”请求。
 * @retval 用户新发起一次请求时返回true。
 */
bool UI_TakeCurrentZeroCalibrationRequest(void);

/**
 * @brief  更新本地校准卡片状态。
 * @param  status 当前校准状态。
 * @retval 无。
 */
void UI_SetCalibrationStatus(UI_CalibrationStatus_t status);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_H__ */
