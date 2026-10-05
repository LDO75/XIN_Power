/**
  ******************************************************************************
  * @file    ui_layout.h
  * @brief   320x240数控电源UI布局和主题配置。
  ******************************************************************************
  */

#ifndef __UI_LAYOUT_H__
#define __UI_LAYOUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define UI_LAYOUT_BUTTON_COUNT             4U
#define UI_LAYOUT_PAGE_COUNT               3U
/* 以最大局部绘制区域容量为基准；其他形状只需保证像素总数不超限。 */
#define UI_RENDER_CACHE_PIXELS           (296U * 58U)
#define UI_RENDER_CACHE_BYTES            (UI_RENDER_CACHE_PIXELS * sizeof(uint16_t))

typedef struct
{
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} UI_Rect_t;

typedef struct
{
  UI_Rect_t top_bar;
  UI_Rect_t mode_badge;
  UI_Rect_t input_voltage_label;
  UI_Rect_t input_voltage_value;
  UI_Rect_t top_separator;
  UI_Rect_t temperature_label;
  UI_Rect_t temperature_value;
  UI_Rect_t touch_warning;
  UI_Rect_t output_badge;
  UI_Rect_t voltage_card;
  UI_Rect_t current_card;
  UI_Rect_t voltage_dynamic;
  UI_Rect_t current_dynamic;
  UI_Rect_t stats_panel;
  UI_Rect_t stats_values;
  UI_Rect_t buttons[UI_LAYOUT_BUTTON_COUNT];
  UI_Rect_t page_indicator;
  UI_Rect_t secondary_header;
  UI_Rect_t brightness_card;
  UI_Rect_t brightness_minus;
  UI_Rect_t brightness_plus;
  UI_Rect_t buzzer_card;
  UI_Rect_t buzzer_switch;
  UI_Rect_t orientation_card;
  UI_Rect_t orientation_switch;
  UI_Rect_t calibration_card;
  UI_Rect_t waveform_panel;
  UI_Rect_t waveform_voltage_plot;
  UI_Rect_t waveform_current_plot;
  uint16_t page_dot_x[UI_LAYOUT_PAGE_COUNT];
  uint16_t page_dot_y;
} UI_Layout_t;

typedef struct
{
  uint16_t background;
  uint16_t top_bar;
  uint16_t panel;
  uint16_t panel_voltage_selected;
  uint16_t panel_current_selected;
  uint16_t panel_border;
  uint16_t dark_pill;
  uint16_t button;
  uint16_t button_step;
  uint16_t button_out_off;
  uint16_t button_out_on;
  uint16_t voltage;
  uint16_t current;
  uint16_t green;
  uint16_t red;
  uint16_t purple;
  uint16_t label;
  uint16_t text;
  uint16_t white;
  uint16_t black;
  uint16_t waveform_grid;
  uint16_t cv_background;
  uint16_t cc_background;
  uint16_t output_on_background;
  uint16_t output_off_background;
  uint8_t panel_border_width;
  uint8_t gauge_height;
  uint8_t card_radius;
  uint8_t inner_radius;
  uint8_t button_radius;
} UI_Theme_t;

extern const UI_Layout_t g_ui_layout;
extern const UI_Theme_t g_ui_theme;

/* 页面切换时需要清除的裸露缝隙，也统一由布局模块维护。 */
extern const UI_Rect_t g_ui_numeric_clear_regions[];
extern const uint8_t g_ui_numeric_clear_region_count;
extern const UI_Rect_t g_ui_settings_clear_regions[];
extern const uint8_t g_ui_settings_clear_region_count;
extern const UI_Rect_t g_ui_waveform_clear_regions[];
extern const uint8_t g_ui_waveform_clear_region_count;

/**
 * @brief  判断屏幕坐标是否位于指定布局矩形内。
 * @param  rect 布局矩形指针。
 * @param  x 屏幕X坐标。
 * @param  y 屏幕Y坐标。
 * @retval 位于矩形内时返回true。
 */
bool UI_LayoutContains(const UI_Rect_t *rect, uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif

#endif /* __UI_LAYOUT_H__ */
