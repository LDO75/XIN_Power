/**
  ******************************************************************************
  * @file    ui_layout.c
  * @brief   320x240数控电源UI布局和主题参数。
  ******************************************************************************
  */

#include "ui_layout.h"
#include "lcd.h"

#include <stddef.h>

/*
 * 页面所有主区域的绝对坐标集中在此处。
 * 后续调整一整行或一整列时，只需修改这个结构。
 */
const UI_Layout_t g_ui_layout =
{
  .top_bar = {0U, 0U, 320U, 24U},
  .mode_badge = {4U, 4U, 60U, 16U},
  .input_voltage_label = {72U, 5U, 34U, 14U},
  .input_voltage_value = {106U, 5U, 54U, 14U},
  .top_separator = {160U, 6U, 1U, 12U},
  .temperature_label = {170U, 5U, 50U, 14U},
  .temperature_value = {220U, 5U, 32U, 14U},
  .touch_warning = {252U, 5U, 28U, 14U},
  .output_badge = {280U, 4U, 36U, 16U},
  .voltage_card = {6U, 26U, 153U, 88U},
  .current_card = {162U, 26U, 153U, 88U},
  .voltage_dynamic = {12U, 45U, 141U, 64U},
  .current_dynamic = {168U, 45U, 141U, 64U},
  .stats_panel = {6U, 118U, 308U, 38U},
  .stats_values = {8U, 136U, 304U, 17U},
  .buttons =
  {
    {6U, 162U, 73U, 70U},
    {84U, 162U, 73U, 70U},
    {162U, 162U, 73U, 70U},
    {240U, 162U, 73U, 70U}
  },
  .page_indicator = {145U, 233U, 31U, 7U},
  .secondary_header = {0U, 0U, 320U, 28U},
  .brightness_card = {12U, 34U, 296U, 43U},
  .brightness_minus = {220U, 39U, 34U, 33U},
  .brightness_plus = {262U, 39U, 34U, 33U},
  .buzzer_card = {12U, 80U, 296U, 43U},
  .buzzer_switch = {244U, 88U, 48U, 27U},
  .orientation_card = {12U, 126U, 296U, 43U},
  .orientation_switch = {226U, 134U, 66U, 27U},
  .calibration_card = {12U, 172U, 296U, 43U},
  /* 显示顺序为：设置页、数字页。 */
  .page_dot_x = {155U, 165U},
  .page_dot_y = 236U
};

/*
 * 颜色、线宽和圆角集中在此处。
 * 页面绘制文件不再分散保存RGB数值。
 */
const UI_Theme_t g_ui_theme =
{
  .background = LCD_RGB565(4U, 8U, 14U),
  .top_bar = LCD_RGB565(9U, 16U, 27U),
  .panel = LCD_RGB565(11U, 20U, 31U),
  .panel_voltage_selected = LCD_RGB565(5U, 25U, 34U),
  .panel_current_selected = LCD_RGB565(31U, 25U, 8U),
  .panel_border = LCD_RGB565(53U, 78U, 108U),
  .dark_pill = LCD_RGB565(6U, 9U, 14U),
  .button = LCD_RGB565(24U, 32U, 46U),
  .button_step = LCD_RGB565(36U, 32U, 24U),
  .button_out_off = LCD_RGB565(220U, 0U, 0U),
  .button_out_on = LCD_RGB565(0U, 160U, 72U),
  .voltage = LCD_RGB565(0U, 232U, 184U),
  .current = LCD_RGB565(255U, 194U, 64U),
  .green = LCD_RGB565(0U, 160U, 80U),
  .red = LCD_RGB565(220U, 0U, 0U),
  .purple = LCD_RGB565(210U, 168U, 255U),
  .label = LCD_RGB565(218U, 234U, 248U),
  .text = LCD_RGB565(235U, 243U, 250U),
  .white = LCD_RGB565(255U, 255U, 255U),
  .black = LCD_RGB565(0U, 0U, 0U),
  .cv_background = LCD_RGB565(8U, 38U, 31U),
  .cc_background = LCD_RGB565(31U, 25U, 8U),
  .output_on_background = LCD_RGB565(0U, 42U, 21U),
  .output_off_background = LCD_RGB565(54U, 0U, 0U),
  .panel_border_width = 2U,
  .gauge_height = 4U,
  .card_radius = 5U,
  .inner_radius = 4U,
  .button_radius = 6U
};

/*
 * 页面组件无法覆盖到的窄缝集中在这里。
 * 页面切换只擦除这些区域，避免整屏清空产生黑屏闪烁。
 */
const UI_Rect_t g_ui_numeric_clear_regions[] =
{
  {0U, 24U, 320U, 2U},
  {0U, 114U, 320U, 4U},
  {0U, 156U, 320U, 6U},
  {0U, 232U, 320U, 8U},
  {0U, 26U, 6U, 206U},
  {159U, 26U, 3U, 88U},
  {315U, 26U, 5U, 88U},
  {314U, 118U, 6U, 38U},
  {79U, 162U, 5U, 70U},
  {157U, 162U, 5U, 70U},
  {235U, 162U, 5U, 70U},
  {313U, 162U, 7U, 70U}
};

const uint8_t g_ui_numeric_clear_region_count =
  (uint8_t)(sizeof(g_ui_numeric_clear_regions) / sizeof(g_ui_numeric_clear_regions[0]));

const UI_Rect_t g_ui_settings_clear_regions[] =
{
  {0U, 28U, 320U, 6U},
  {0U, 215U, 320U, 25U},
  {0U, 34U, 12U, 181U},
  {308U, 34U, 12U, 181U},
  {0U, 77U, 320U, 3U},
  {0U, 123U, 320U, 3U},
  {0U, 169U, 320U, 3U}
};

const uint8_t g_ui_settings_clear_region_count =
  (uint8_t)(sizeof(g_ui_settings_clear_regions) / sizeof(g_ui_settings_clear_regions[0]));

/**
 * @brief  判断屏幕坐标是否位于指定布局矩形内。
 * @param  rect 布局矩形指针。
 * @param  x 屏幕X坐标。
 * @param  y 屏幕Y坐标。
 * @retval 位于矩形内时返回true。
 */
bool UI_LayoutContains(const UI_Rect_t *rect, uint16_t x, uint16_t y)
{
  if (rect == NULL)
  {
    return false;
  }

  return ((x >= rect->x) &&
          (y >= rect->y) &&
          (x < (uint16_t)(rect->x + rect->width)) &&
          (y < (uint16_t)(rect->y + rect->height)));
}
