/**
  ******************************************************************************
  * @file    ui_page.c
  * @brief   迷你数控电源多页面UI及触摸手势实现。
  ******************************************************************************
  */

#include "ui_page.h"
#include "buzzer.h"
#include "cst836u.h"
#include "firmware_version.h"
#include "lcd.h"
#include "power_config.h"
#include "ui_config.h"
#include "ui_layout.h"

#include <stddef.h>
#include <stdio.h>

#define UI_COLOR_BACKGROUND               (g_ui_theme.background)
#define UI_COLOR_TOP_BAR                  (g_ui_theme.top_bar)
#define UI_COLOR_PANEL                    (g_ui_theme.panel)
#define UI_COLOR_PANEL_VOLT_SELECTED      (g_ui_theme.panel_voltage_selected)
#define UI_COLOR_PANEL_CURR_SELECTED      (g_ui_theme.panel_current_selected)
#define UI_COLOR_PANEL_BORDER             (g_ui_theme.panel_border)
#define UI_COLOR_DARK_PILL                (g_ui_theme.dark_pill)
#define UI_COLOR_BUTTON                   (g_ui_theme.button)
#define UI_COLOR_BUTTON_STEP              (g_ui_theme.button_step)
#define UI_COLOR_BUTTON_OUT_OFF           (g_ui_theme.button_out_off)
#define UI_COLOR_BUTTON_OUT_ON            (g_ui_theme.button_out_on)
#define UI_COLOR_VOLTAGE                  (g_ui_theme.voltage)
#define UI_COLOR_CURRENT                  (g_ui_theme.current)
#define UI_COLOR_GREEN                    (g_ui_theme.green)
#define UI_COLOR_RED                      (g_ui_theme.red)
#define UI_COLOR_PURPLE                   (g_ui_theme.purple)
#define UI_COLOR_LABEL                    (g_ui_theme.label)
#define UI_COLOR_VOLTAGE_LABEL            (g_ui_theme.voltage)
#define UI_COLOR_CURRENT_LABEL            (g_ui_theme.current)
#define UI_COLOR_TEXT                     (g_ui_theme.text)
#define UI_COLOR_WHITE                    (g_ui_theme.white)
#define UI_COLOR_BLACK                    (g_ui_theme.black)

#define UI_TOUCH_STARTUP_GUARD_MS          350U

#define UI_PANEL_BORDER_WIDTH              (g_ui_theme.panel_border_width)
#define UI_GAUGE_HEIGHT                    (g_ui_theme.gauge_height)
#define UI_CARD_RADIUS                     (g_ui_theme.card_radius)
#define UI_INNER_RADIUS                    (g_ui_theme.inner_radius)
#define UI_BUTTON_RADIUS                   (g_ui_theme.button_radius)

#define UI_CARD_DYNAMIC_WIDTH              (g_ui_layout.voltage_dynamic.width)
#define UI_CARD_DYNAMIC_HEIGHT             (g_ui_layout.voltage_dynamic.height)
#define UI_STATS_VALUES_WIDTH              (g_ui_layout.stats_values.width)
#define UI_STATS_VALUES_HEIGHT             (g_ui_layout.stats_values.height)
#define UI_TOP_BAR_WIDTH                   (g_ui_layout.top_bar.width)
#define UI_TOP_BAR_HEIGHT                  (g_ui_layout.top_bar.height)
#define UI_BUTTON_BUFFER_WIDTH             (g_ui_layout.buttons[0].width)
#define UI_BUTTON_BUFFER_HEIGHT            (g_ui_layout.buttons[0].height)
#define UI_BUTTON_WIDTH                    (g_ui_layout.buttons[0].width)
#define UI_BUTTON_TOP                      (g_ui_layout.buttons[0].y)
#define UI_SETTINGS_ITEM_COUNT               6U
#define UI_SETTINGS_VISIBLE_COUNT            4U
#define UI_SETTINGS_ROW_STEP_PX             46U
#define UI_SETTINGS_FIRST_Y                 34U

typedef enum
{
  UI_EDIT_VOLTAGE = 0,
  UI_EDIT_CURRENT
} UI_EditTarget_t;

typedef struct
{
  uint16_t input_voltage_mv;
  uint16_t output_voltage_mv;
  uint16_t output_current_ma;
  uint16_t voltage_setpoint_mv;
  uint16_t current_limit_ma;
  int16_t temperature_decic;
  uint16_t ripple_vpp_mv;
  bool ripple_valid;
  uint32_t output_power_mw;
  uint32_t capacity_uah;
  uint32_t energy_uwh;
  uint8_t step_index;
  uint8_t brightness_percent;
  uint16_t screen_sleep_seconds;
  uint8_t settings_first_item;
  UI_EditTarget_t edit_target;
  UI_Page_t current_page;
  UI_CalibrationStatus_t calibration_status;
  bool output_requested;
  bool power_fault;
  bool fault_clear_request_pending;
  bool calibration_request_pending;
  bool buzzer_enabled;
  bool display_rotated_180;
  bool constant_current;
  bool demo_enabled;
  bool touch_available;
} UI_State_t;

typedef struct
{
  uint16_t start_x;
  uint16_t start_y;
  uint16_t last_x;
  uint16_t last_y;
  uint32_t start_time_ms;
  bool active;
  bool swipe_detected;
} UI_GestureState_t;

static const uint16_t s_voltage_steps_mv[3] = {10U, 100U, 1000U};
static const uint16_t s_current_steps_ma[3] = {1U, 10U, 100U};
static UI_State_t s_ui_state;
static UI_GestureState_t s_ui_gesture;
/* 所有页面共用一块区域合成缓存，避免为每个卡片重复分配RAM。 */
static uint16_t s_ui_region_buffer[UI_RENDER_CACHE_PIXELS];
static uint32_t s_ui_last_engine_time = 0U;
static uint32_t s_ui_last_display_time = 0U;
static uint32_t s_ui_last_activity_ms;
static bool s_ui_sleeping, s_ui_wake_touch_pending;
static const uint16_t s_sleep_seconds[] = {0U,30U,60U,120U,300U,600U};
static uint32_t s_ui_capacity_remainder = 0U;
static uint32_t s_ui_energy_remainder = 0U;
static bool s_ui_last_drawn_cc_mode = false;
static bool s_ui_last_drawn_output = false;
static bool s_ui_last_drawn_fault = false;
static bool s_ui_last_drawn_button_output = false;
static bool s_ui_last_drawn_button_fault = false;
static bool s_ui_touch_debounce_started = false;
static uint32_t s_ui_last_gesture_start_time = 0U;
static uint32_t s_ui_touch_guard_start_time = 0U;
static uint16_t s_ui_last_drawn_input_voltage_mv = 0U;
static int16_t s_ui_last_drawn_temperature_decic = 0;
static uint16_t s_ui_last_drawn_output_voltage_mv = UINT16_MAX;
static uint16_t s_ui_last_drawn_voltage_setpoint_mv = UINT16_MAX;
static uint16_t s_ui_last_drawn_output_current_ma = UINT16_MAX;
static uint16_t s_ui_last_drawn_current_limit_ma = UINT16_MAX;
static uint16_t s_ui_last_drawn_ripple=UINT16_MAX;
static bool s_ui_last_drawn_ripple_valid;
static uint32_t s_ui_last_drawn_output_power_mw = UINT32_MAX;

static void UI_DrawPanel(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height,
                         uint16_t fill_color,
                         uint16_t border_color);
static void UI_BufferDrawCenteredText(uint16_t canvas_width,
                                      uint16_t canvas_height,
                                      uint16_t x,
                                      uint16_t y,
                                      uint16_t width,
                                      uint16_t height,
                                      const char *text,
                                      uint16_t foreground,
                                      LCD_FontSize_t font_size);
static bool UI_PrepareRegionBuffer(uint16_t width,
                                   uint16_t height,
                                   uint16_t background);
static void UI_BufferDrawPanel(uint16_t canvas_width,
                               uint16_t canvas_height,
                               uint16_t x,
                               uint16_t y,
                               uint16_t width,
                               uint16_t height,
                               uint16_t fill_color,
                               uint16_t border_color);
static bool UI_OutputIsOn(void);
static uint16_t UI_OutputStatusColor(void);
static const char *UI_OutputStatusText(void);
static void UI_ComposeModeIndicator(uint16_t canvas_width,
                                    uint16_t canvas_height,
                                    uint16_t x,
                                    uint16_t y);
static void UI_ComposeOutputIndicator(uint16_t canvas_width,
                                      uint16_t canvas_height,
                                      uint16_t x,
                                      uint16_t y);
static uint16_t UI_GetCardColor(UI_EditTarget_t card);
static void UI_DrawModeIndicator(void);
static void UI_DrawOutputIndicator(void);
static void UI_DrawTopBar(void);
static void UI_DrawVoltageCard(void);
static void UI_DrawCurrentCard(void);
static void UI_ComposeVoltageDynamicArea(uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         uint16_t origin_x,
                                         uint16_t origin_y);
static void UI_ComposeCurrentDynamicArea(uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         uint16_t origin_x,
                                         uint16_t origin_y);
static void UI_DrawVoltageDynamicArea(void);
static void UI_DrawCurrentDynamicArea(void);
static void UI_DrawStatsPanel(void);
static void UI_ComposeStatsValues(uint16_t canvas_width,
                                  uint16_t canvas_height,
                                  uint16_t origin_x,
                                  uint16_t origin_y);
static void UI_DrawStatsValues(void);
static void UI_DrawButton(uint8_t button_index);
static void UI_DrawAllButtons(void);
static void UI_DrawNumericPage(void);
static void UI_DrawSettingsPage(void);
static void UI_DrawSettingsBrightness(void);
static void UI_DrawSettingsBuzzer(void);
static void UI_DrawSettingsOrientation(void);
static void UI_DrawSettingsCalibration(void);
static void UI_DrawSettingsVersion(void);
static void UI_DrawSettingsSleep(void);
static bool UI_SettingsCardRect(uint8_t index, UI_Rect_t *rect);
static bool UI_SettingsContains(uint8_t index, uint16_t x, uint16_t y);
static void UI_ScrollSettings(int32_t vertical_distance);
static void UI_DrawSecondaryHeader(const char *title,
                                   const char *hint,
                                   uint16_t title_color);
static void UI_DrawPageIndicator(uint8_t selected_index);
static void UI_ClearPageGaps(UI_Page_t page);
static void UI_DrawCurrentPage(void);
static void UI_SwitchPage(UI_Page_t page);
static bool UI_HandleTouch(uint32_t system_time_ms);
static void UI_HandleTap(uint16_t x, uint16_t y);
static void UI_HandleNumericTap(uint16_t x, uint16_t y);
static void UI_HandleSettingsTap(uint16_t x, uint16_t y);
static void UI_HandleSwipe(int32_t horizontal_distance);
static uint16_t UI_AbsoluteDifference(uint16_t first, uint16_t second);
static void UI_RunDemonstrationModel(uint32_t system_time_ms);
static void UI_RefreshLiveAreas(void);

/** @brief 在局部绘制缓存中绘制一条裁剪后的直线。 */
static void UI_BufferDrawLine(uint16_t canvas_width,
                              uint16_t canvas_height,
                              int32_t x0,
                              int32_t y0,
                              int32_t x1,
                              int32_t y1,
                              uint16_t color)
{
  int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
  int32_t sx = (x0 < x1) ? 1 : -1;
  int32_t dy = -((y1 >= y0) ? (y1 - y0) : (y0 - y1));
  int32_t sy = (y0 < y1) ? 1 : -1;
  int32_t error = dx + dy;
  uint32_t remaining = (uint32_t)canvas_width + canvas_height + 2U;

  while (remaining > 0U)
  {
    int32_t doubled_error;

    if ((x0 >= 0) && (y0 >= 0) &&
        (x0 < canvas_width) && (y0 < canvas_height))
    {
      s_ui_region_buffer[(uint32_t)y0 * canvas_width + (uint32_t)x0] = color;
    }
    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }
    /* 两个方向必须使用同一轮的误差快照，否则部分斜率会越过终点死循环。 */
    doubled_error = 2 * error;
    if (doubled_error >= dy)
    {
      error += dy;
      x0 += sx;
    }
    if (doubled_error <= dx)
    {
      error += dx;
      y0 += sy;
    }
    remaining--;
  }
}

/**
 * @brief  填充一个矩形UI面板并绘制边框。
 * @param  x 面板左上角X坐标。
 * @param  y 面板左上角Y坐标。
 * @param  width 面板宽度。
 * @param  height 面板高度。
 * @param  fill_color 面板的RGB565填充颜色。
 * @param  border_color 面板的RGB565边框颜色。
 * @retval 无。
 */
static void UI_DrawPanel(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height,
                         uint16_t fill_color,
                         uint16_t border_color)
{
  LCD_FillRoundRect(x, y, width, height, UI_CARD_RADIUS, border_color);
  LCD_FillRoundRect((uint16_t)(x + UI_PANEL_BORDER_WIDTH),
                    (uint16_t)(y + UI_PANEL_BORDER_WIDTH),
                    (uint16_t)(width - (UI_PANEL_BORDER_WIDTH * 2U)),
                    (uint16_t)(height - (UI_PANEL_BORDER_WIDTH * 2U)),
                    (uint8_t)(UI_CARD_RADIUS - UI_PANEL_BORDER_WIDTH),
                    fill_color);
}

/**
 * @brief  校验共用区域缓存容量并填充背景色。
 * @param  width 本次画布宽度。
 * @param  height 本次画布高度。
 * @param  background 画布背景色。
 * @retval 容量足够时返回true；布局超出缓存容量时返回false。
 * @note   后续扩大卡片时可避免静默越界写入RAM。
 */
static bool UI_PrepareRegionBuffer(uint16_t width,
                                   uint16_t height,
                                   uint16_t background)
{
  if ((width == 0U) ||
      (height == 0U) ||
      (((uint32_t)width * (uint32_t)height) > UI_RENDER_CACHE_PIXELS))
  {
    return false;
  }

  LCD_BufferFill(s_ui_region_buffer, width, height, background);
  return true;
}

/**
 * @brief  在局部RAM画布的指定矩形中居中绘制ASCII字符串。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  text 字符串指针。
 * @param  foreground 文字的RGB565颜色。
 * @param  font_size Oxanium字库字号。
 * @retval 无。
 */
static void UI_BufferDrawCenteredText(uint16_t canvas_width,
                                      uint16_t canvas_height,
                                      uint16_t x,
                                      uint16_t y,
                                      uint16_t width,
                                      uint16_t height,
                                      const char *text,
                                      uint16_t foreground,
                                      LCD_FontSize_t font_size)
{
  uint16_t text_width;
  uint16_t text_height;
  uint16_t text_x;
  uint16_t text_y;

  text_width = LCD_GetFontTextWidth(text, font_size);
  text_height = LCD_GetFontLineHeight(font_size);
  text_x = (text_width < width) ? (uint16_t)(x + ((width - text_width) / 2U)) : x;
  text_y = (text_height < height) ? (uint16_t)(y + ((height - text_height) / 2U)) : y;
  LCD_BufferDrawTextFont(s_ui_region_buffer,
                         canvas_width,
                         canvas_height,
                         text_x,
                         text_y,
                         text,
                         foreground,
                         font_size);
}

/**
 * @brief  在RAM画布中绘制带边框的圆角面板。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 面板在画布中的X坐标。
 * @param  y 面板在画布中的Y坐标。
 * @param  width 面板宽度。
 * @param  height 面板高度。
 * @param  fill_color 面板填充颜色。
 * @param  border_color 面板边框颜色。
 * @retval 无。
 */
static void UI_BufferDrawPanel(uint16_t canvas_width,
                               uint16_t canvas_height,
                               uint16_t x,
                               uint16_t y,
                               uint16_t width,
                               uint16_t height,
                               uint16_t fill_color,
                               uint16_t border_color)
{
  LCD_BufferFillRoundRect(s_ui_region_buffer,
                          canvas_width,
                          canvas_height,
                          x,
                          y,
                          width,
                          height,
                          UI_CARD_RADIUS,
                          border_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer,
                          canvas_width,
                          canvas_height,
                          (uint16_t)(x + UI_PANEL_BORDER_WIDTH),
                          (uint16_t)(y + UI_PANEL_BORDER_WIDTH),
                          (uint16_t)(width - (UI_PANEL_BORDER_WIDTH * 2U)),
                          (uint16_t)(height - (UI_PANEL_BORDER_WIDTH * 2U)),
                          (uint8_t)(UI_CARD_RADIUS - UI_PANEL_BORDER_WIDTH),
                          fill_color);
}

/** @brief 所有输出状态组件共用同一状态与颜色映射。 */
static bool UI_OutputIsOn(void)
{
  return s_ui_state.output_requested && !s_ui_state.power_fault;
}

static uint16_t UI_OutputStatusColor(void)
{
  return UI_OutputIsOn() ? UI_COLOR_BUTTON_OUT_ON : UI_COLOR_BUTTON_OUT_OFF;
}

static const char *UI_OutputStatusText(void)
{
  return s_ui_state.power_fault ? "FLT" :
         (UI_OutputIsOn() ? "ON" : "OFF");
}

/**
 * @brief  在任意RAM画布中合成CV/CC状态块。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 状态块左上角X坐标。
 * @param  y 状态块左上角Y坐标。
 * @retval 无。
 */
static void UI_ComposeModeIndicator(uint16_t canvas_width,
                                    uint16_t canvas_height,
                                    uint16_t x,
                                    uint16_t y)
{
  uint16_t mode_color;
  uint16_t mode_background;

  mode_color = UI_COLOR_GREEN;
  mode_background = g_ui_theme.cv_background;

  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          x, y,
                          g_ui_layout.mode_badge.width,
                          g_ui_layout.mode_badge.height,
                          UI_INNER_RADIUS,
                          mode_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(x + 2U), (uint16_t)(y + 2U),
                          (uint16_t)(g_ui_layout.mode_badge.width - 4U),
                          (uint16_t)(g_ui_layout.mode_badge.height - 4U),
                          2U,
                          mode_background);
  LCD_BufferFillCircle(s_ui_region_buffer, canvas_width, canvas_height,
                       (uint16_t)(x + 8U),
                       (uint16_t)(y + (g_ui_layout.mode_badge.height / 2U)),
                       2U,
                       mode_color);
  UI_BufferDrawCenteredText(canvas_width,
                            canvas_height,
                            (uint16_t)(x + 14U),
                            (uint16_t)(y + 2U),
                            (uint16_t)(g_ui_layout.mode_badge.width - 16U),
                            (uint16_t)(g_ui_layout.mode_badge.height - 2U),
                            (s_ui_state.output_requested && s_ui_state.constant_current) ? "CC" : "CV",
                            mode_color,
                            LCD_FONT_OXANIUM_SMALL);
}

/**
 * @brief  在任意RAM画布中合成ON/OFF状态块。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 状态块左上角X坐标。
 * @param  y 状态块左上角Y坐标。
 * @retval 无。
 */
static void UI_ComposeOutputIndicator(uint16_t canvas_width,
                                      uint16_t canvas_height,
                                      uint16_t x,
                                      uint16_t y)
{
  uint16_t output_color;
  uint16_t output_background;

  output_color = UI_OutputStatusColor();
  output_background = UI_OutputIsOn() ?
                      g_ui_theme.output_on_background :
                      g_ui_theme.output_off_background;

  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          x, y,
                          g_ui_layout.output_badge.width,
                          g_ui_layout.output_badge.height,
                          UI_INNER_RADIUS,
                          output_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(x + 2U), (uint16_t)(y + 2U),
                          (uint16_t)(g_ui_layout.output_badge.width - 4U),
                          (uint16_t)(g_ui_layout.output_badge.height - 4U),
                          2U,
                          output_background);
  UI_BufferDrawCenteredText(canvas_width,
                            canvas_height,
                            x,
                            (uint16_t)(y + 2U),
                            g_ui_layout.output_badge.width,
                            (uint16_t)(g_ui_layout.output_badge.height - 1U),
                            UI_OutputStatusText(),
                            output_color,
                            LCD_FONT_OXANIUM_SMALL);
}

/**
 * @brief  获取电压卡片或电流卡片的背景颜色。
 * @param  card 卡片类型。
 * @retval 卡片的RGB565颜色。
 */
static uint16_t UI_GetCardColor(UI_EditTarget_t card)
{
  if (s_ui_state.edit_target != card)
  {
    return UI_COLOR_PANEL;
  }
  return (card == UI_EDIT_VOLTAGE) ?
         UI_COLOR_PANEL_VOLT_SELECTED : UI_COLOR_PANEL_CURR_SELECTED;
}

/**
 * @brief  只绘制顶部状态栏左侧的CV/CC模式指示区域。
 * @retval 无。
 */
static void UI_DrawModeIndicator(void)
{
  const UI_Rect_t *badge = &g_ui_layout.mode_badge;

  /* 完整绘制和局部刷新共用同一个组件合成函数。 */
  if (!UI_PrepareRegionBuffer(badge->width, badge->height, UI_COLOR_TOP_BAR))
  {
    return;
  }
  UI_ComposeModeIndicator(badge->width, badge->height, 0U, 0U);
  LCD_DrawBitmap(badge->x, badge->y, badge->width, badge->height, s_ui_region_buffer);

  s_ui_last_drawn_cc_mode = s_ui_state.constant_current;
}

/**
 * @brief  只绘制顶部状态栏右侧的输出开关状态区域。
 * @retval 无。
 */
static void UI_DrawOutputIndicator(void)
{
  const UI_Rect_t *badge = &g_ui_layout.output_badge;

  if (!UI_PrepareRegionBuffer(badge->width, badge->height, UI_COLOR_TOP_BAR))
  {
    return;
  }
  UI_ComposeOutputIndicator(badge->width, badge->height, 0U, 0U);
  LCD_DrawBitmap(badge->x, badge->y, badge->width, badge->height, s_ui_region_buffer);

  s_ui_last_drawn_output = s_ui_state.output_requested;
  s_ui_last_drawn_fault = s_ui_state.power_fault;
}

/**
 * @brief  绘制页面顶部状态栏。
 * @retval 无。
 */
static void UI_DrawTopBar(void)
{
  char text[16];
  const UI_Rect_t *rect;

  if (!UI_PrepareRegionBuffer(UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT, UI_COLOR_TOP_BAR))
  {
    return;
  }
  LCD_BufferFillRect(s_ui_region_buffer,
                     UI_TOP_BAR_WIDTH,
                     UI_TOP_BAR_HEIGHT,
                     0U,
                     22U,
                     UI_TOP_BAR_WIDTH,
                     2U,
                     UI_COLOR_PANEL_BORDER);

  UI_ComposeModeIndicator(UI_TOP_BAR_WIDTH,
                          UI_TOP_BAR_HEIGHT,
                          g_ui_layout.mode_badge.x,
                          g_ui_layout.mode_badge.y);

  rect = &g_ui_layout.input_voltage_label;
  LCD_BufferDrawTextFont(s_ui_region_buffer, UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT,
                         rect->x, rect->y, "VIN:", UI_COLOR_LABEL, LCD_FONT_OXANIUM_SMALL);
  (void)snprintf(text,
                 sizeof(text),
                 "%lu.%03lu",
                 (unsigned long)(s_ui_state.input_voltage_mv / 1000U),
                 (unsigned long)(s_ui_state.input_voltage_mv % 1000U));
  rect = &g_ui_layout.input_voltage_value;
  LCD_BufferDrawTextFont(s_ui_region_buffer, UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT,
                         rect->x, rect->y, text, UI_COLOR_VOLTAGE, LCD_FONT_OXANIUM_SMALL);

  rect = &g_ui_layout.top_separator;
  LCD_BufferFillRect(s_ui_region_buffer,
                     UI_TOP_BAR_WIDTH,
                     UI_TOP_BAR_HEIGHT,
                     rect->x,
                     rect->y,
                     rect->width,
                     rect->height,
                     UI_COLOR_PANEL_BORDER);

  rect = &g_ui_layout.temperature_label;
  LCD_BufferDrawTextFont(s_ui_region_buffer, UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT,
                         rect->x, rect->y, "TEMP:", UI_COLOR_LABEL, LCD_FONT_OXANIUM_SMALL);
  (void)snprintf(text,
                 sizeof(text),
                 "%ldC",
                 (long)(s_ui_state.temperature_decic / 10));
  rect = &g_ui_layout.temperature_value;
  LCD_BufferDrawTextFont(s_ui_region_buffer, UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT,
                         rect->x, rect->y, text, UI_COLOR_TEXT, LCD_FONT_OXANIUM_SMALL);

  if (!s_ui_state.touch_available)
  {
    rect = &g_ui_layout.touch_warning;
    LCD_BufferDrawTextFont(s_ui_region_buffer, UI_TOP_BAR_WIDTH, UI_TOP_BAR_HEIGHT,
                           rect->x, rect->y, "TP!", UI_COLOR_RED, LCD_FONT_OXANIUM_SMALL);
  }

  UI_ComposeOutputIndicator(UI_TOP_BAR_WIDTH,
                            UI_TOP_BAR_HEIGHT,
                            g_ui_layout.output_badge.x,
                            g_ui_layout.output_badge.y);
  if(s_ui_last_drawn_input_voltage_mv==UINT16_MAX ||
     s_ui_last_drawn_cc_mode!=s_ui_state.constant_current ||
     s_ui_last_drawn_output!=s_ui_state.output_requested ||
     s_ui_last_drawn_fault!=s_ui_state.power_fault)
  {LCD_DrawBitmap(0U,0U,UI_TOP_BAR_WIDTH,UI_TOP_BAR_HEIGHT,s_ui_region_buffer);}
  else
  {
    const UI_Rect_t *v=&g_ui_layout.input_voltage_value,*t=&g_ui_layout.temperature_value;
    if(s_ui_last_drawn_input_voltage_mv!=s_ui_state.input_voltage_mv)
    {LCD_DrawBitmapStrided(v->x,v->y,v->width,v->height,
      s_ui_region_buffer+v->y*UI_TOP_BAR_WIDTH+v->x,UI_TOP_BAR_WIDTH);}
    if(s_ui_last_drawn_temperature_decic/10!=s_ui_state.temperature_decic/10)
    {LCD_DrawBitmapStrided(t->x,t->y,t->width,t->height,
      s_ui_region_buffer+t->y*UI_TOP_BAR_WIDTH+t->x,UI_TOP_BAR_WIDTH);}
  }

  s_ui_last_drawn_cc_mode = s_ui_state.constant_current;
  s_ui_last_drawn_output = s_ui_state.output_requested;
  s_ui_last_drawn_fault = s_ui_state.power_fault;
  s_ui_last_drawn_input_voltage_mv = s_ui_state.input_voltage_mv;
  s_ui_last_drawn_temperature_decic = s_ui_state.temperature_decic;
}

/**
 * @brief  绘制完整的电压卡片。
 * @retval 无。
 */
static void UI_DrawVoltageCard(void)
{
  const UI_Rect_t *card = &g_ui_layout.voltage_card;
  uint16_t background = UI_GetCardColor(UI_EDIT_VOLTAGE);
  uint16_t border = (s_ui_state.edit_target == UI_EDIT_VOLTAGE) ?
                    UI_COLOR_VOLTAGE : UI_COLOR_PANEL_BORDER;

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     background, border);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         8U, 3U, "VOLTAGE",
                         UI_COLOR_VOLTAGE_LABEL,
                         LCD_FONT_OXANIUM_MEDIUM);

  if (s_ui_state.edit_target == UI_EDIT_VOLTAGE)
  {
    LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                            112U, 3U, 36U, 15U,
                            UI_INNER_RADIUS, UI_COLOR_VOLTAGE);
    UI_BufferDrawCenteredText(card->width, card->height,
                              112U, 5U, 36U, 13U,
                              "EDIT", UI_COLOR_BLACK,
                              LCD_FONT_OXANIUM_SMALL);
  }

  UI_ComposeVoltageDynamicArea(card->width, card->height, 6U, 19U);
  LCD_DrawBitmap(card->x, card->y, card->width, card->height, s_ui_region_buffer);
  s_ui_last_drawn_output_voltage_mv = s_ui_state.output_voltage_mv;
  s_ui_last_drawn_voltage_setpoint_mv = s_ui_state.voltage_setpoint_mv;
}

/**
 * @brief  绘制完整的电流卡片。
 * @retval 无。
 */
static void UI_DrawCurrentCard(void)
{
  const UI_Rect_t *card = &g_ui_layout.current_card;
  uint16_t background = UI_GetCardColor(UI_EDIT_CURRENT);
  uint16_t border = (s_ui_state.edit_target == UI_EDIT_CURRENT) ?
                    UI_COLOR_CURRENT : UI_COLOR_PANEL_BORDER;

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     background, border);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         8U, 3U, "CURRENT",
                         UI_COLOR_CURRENT_LABEL,
                         LCD_FONT_OXANIUM_MEDIUM);

  if (s_ui_state.edit_target == UI_EDIT_CURRENT)
  {
    LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                            112U, 3U, 36U, 15U,
                            UI_INNER_RADIUS, UI_COLOR_CURRENT);
    UI_BufferDrawCenteredText(card->width, card->height,
                              112U, 5U, 36U, 13U,
                              "EDIT", UI_COLOR_BLACK,
                              LCD_FONT_OXANIUM_SMALL);
  }

  UI_ComposeCurrentDynamicArea(card->width, card->height, 6U, 19U);
  LCD_DrawBitmap(card->x, card->y, card->width, card->height, s_ui_region_buffer);
  s_ui_last_drawn_output_current_ma = s_ui_state.output_current_ma;
  s_ui_last_drawn_current_limit_ma = s_ui_state.current_limit_ma;
}

/**
 * @brief  在指定RAM画布中合成电压动态区域。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  origin_x 动态区域在画布中的X偏移。
 * @param  origin_y 动态区域在画布中的Y偏移。
 * @retval 无。
 */
static void UI_ComposeVoltageDynamicArea(uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         uint16_t origin_x,
                                         uint16_t origin_y)
{
  char text[16];
  uint16_t gauge_width;
  uint16_t text_x;

  (void)snprintf(text,
                 sizeof(text),
                 "%02lu.%03lu",
                 (unsigned long)(s_ui_state.output_voltage_mv / 1000U),
                 (unsigned long)(s_ui_state.output_voltage_mv % 1000U));
  text_x = (uint16_t)((UI_CARD_DYNAMIC_WIDTH -
                       LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_VALUE) -
                       LCD_GetFontTextWidth("V", LCD_FONT_OXANIUM_MEDIUM) - 4U) / 2U);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 3U),
                         text, UI_COLOR_VOLTAGE, LCD_FONT_OXANIUM_VALUE);
  text_x = (uint16_t)(text_x +
                      LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_VALUE) + 4U);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 12U),
                         "V", UI_COLOR_VOLTAGE, LCD_FONT_OXANIUM_MEDIUM);

  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(origin_x + 2U),
                          (uint16_t)(origin_y + 37U),
                          137U, 19U, UI_INNER_RADIUS, UI_COLOR_PANEL_BORDER);
  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(origin_x + 4U),
                          (uint16_t)(origin_y + 39U),
                          133U, 15U, 2U, UI_COLOR_DARK_PILL);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + 8U),
                         (uint16_t)(origin_y + 40U),
                         "SET", UI_COLOR_VOLTAGE_LABEL, LCD_FONT_OXANIUM_SMALL);
  (void)snprintf(text,
                 sizeof(text),
                 "%02lu.%03lu V",
                 (unsigned long)(s_ui_state.voltage_setpoint_mv / 1000U),
                 (unsigned long)(s_ui_state.voltage_setpoint_mv % 1000U));
  text_x = (uint16_t)(134U - LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_SMALL));
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 40U),
                         text, UI_COLOR_WHITE, LCD_FONT_OXANIUM_SMALL);

  LCD_BufferFillRect(s_ui_region_buffer, canvas_width, canvas_height,
                     (uint16_t)(origin_x + 2U),
                     (uint16_t)(origin_y + 60U),
                     137U, UI_GAUGE_HEIGHT, UI_COLOR_PANEL_BORDER);
  gauge_width = (uint16_t)(((uint32_t)s_ui_state.output_voltage_mv * 137U) /
                           POWER_CFG_VOLTAGE_MAX_MV);
  if (gauge_width > 137U)
  {
    gauge_width = 137U;
  }
  if (gauge_width > 0U)
  {
    LCD_BufferFillRect(s_ui_region_buffer, canvas_width, canvas_height,
                       (uint16_t)(origin_x + 2U),
                       (uint16_t)(origin_y + 60U),
                       gauge_width, UI_GAUGE_HEIGHT, UI_COLOR_VOLTAGE);
  }
}

/**
 * @brief  在RAM中合成并一次提交电压卡片的动态区域。
 * @retval 无。
 */
static void UI_DrawVoltageDynamicArea(void)
{
  const UI_Rect_t *dynamic = &g_ui_layout.voltage_dynamic;

  if (!UI_PrepareRegionBuffer(dynamic->width,
                              dynamic->height,
                              UI_GetCardColor(UI_EDIT_VOLTAGE)))
  {
    return;
  }
  UI_ComposeVoltageDynamicArea(dynamic->width, dynamic->height, 0U, 0U);
  if(s_ui_last_drawn_output_voltage_mv==UINT16_MAX || s_ui_last_drawn_voltage_setpoint_mv!=s_ui_state.voltage_setpoint_mv)
  {LCD_DrawBitmap(dynamic->x,dynamic->y,dynamic->width,dynamic->height,s_ui_region_buffer);}
  else
  {
    /* The SET/OCP pill is unchanged during normal 50Hz measurements. */
    LCD_DrawBitmap(dynamic->x,dynamic->y,dynamic->width,35U,s_ui_region_buffer);
    if((uint32_t)s_ui_last_drawn_output_voltage_mv*137U/POWER_CFG_VOLTAGE_MAX_MV !=
       (uint32_t)s_ui_state.output_voltage_mv*137U/POWER_CFG_VOLTAGE_MAX_MV)
    {LCD_DrawBitmap(dynamic->x,dynamic->y+60U,dynamic->width,4U,
      s_ui_region_buffer+60U*dynamic->width);}
  }
  s_ui_last_drawn_output_voltage_mv = s_ui_state.output_voltage_mv;
  s_ui_last_drawn_voltage_setpoint_mv = s_ui_state.voltage_setpoint_mv;
}

/**
 * @brief  在指定RAM画布中合成电流动态区域。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  origin_x 动态区域在画布中的X偏移。
 * @param  origin_y 动态区域在画布中的Y偏移。
 * @retval 无。
 */
static void UI_ComposeCurrentDynamicArea(uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         uint16_t origin_x,
                                         uint16_t origin_y)
{
  char text[16];
  uint16_t gauge_width;
  uint16_t text_x;

  (void)snprintf(text,
                 sizeof(text),
                 "%lu.%03lu",
                 (unsigned long)(s_ui_state.output_current_ma / 1000U),
                 (unsigned long)(s_ui_state.output_current_ma % 1000U));
  text_x = (uint16_t)((UI_CARD_DYNAMIC_WIDTH -
                       LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_VALUE) -
                       LCD_GetFontTextWidth("A", LCD_FONT_OXANIUM_MEDIUM) - 4U) / 2U);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 3U),
                         text, UI_COLOR_CURRENT, LCD_FONT_OXANIUM_VALUE);
  text_x = (uint16_t)(text_x +
                      LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_VALUE) + 4U);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 12U),
                         "A", UI_COLOR_CURRENT, LCD_FONT_OXANIUM_MEDIUM);

  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(origin_x + 2U),
                          (uint16_t)(origin_y + 37U),
                          137U, 19U, UI_INNER_RADIUS, UI_COLOR_PANEL_BORDER);
  LCD_BufferFillRoundRect(s_ui_region_buffer, canvas_width, canvas_height,
                          (uint16_t)(origin_x + 4U),
                          (uint16_t)(origin_y + 39U),
                          133U, 15U, 2U, UI_COLOR_DARK_PILL);
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + 8U),
                         (uint16_t)(origin_y + 40U),
                         "OCP", UI_COLOR_CURRENT_LABEL, LCD_FONT_OXANIUM_SMALL);
  (void)snprintf(text,
                 sizeof(text),
                 "%lu.%03lu A",
                 (unsigned long)(s_ui_state.current_limit_ma / 1000U),
                 (unsigned long)(s_ui_state.current_limit_ma % 1000U));
  text_x = (uint16_t)(134U - LCD_GetFontTextWidth(text, LCD_FONT_OXANIUM_SMALL));
  LCD_BufferDrawTextFont(s_ui_region_buffer, canvas_width, canvas_height,
                         (uint16_t)(origin_x + text_x),
                         (uint16_t)(origin_y + 40U),
                         text, UI_COLOR_WHITE, LCD_FONT_OXANIUM_SMALL);

  LCD_BufferFillRect(s_ui_region_buffer, canvas_width, canvas_height,
                     (uint16_t)(origin_x + 2U),
                     (uint16_t)(origin_y + 60U),
                     137U, UI_GAUGE_HEIGHT, UI_COLOR_PANEL_BORDER);
  gauge_width = (uint16_t)(((uint32_t)s_ui_state.output_current_ma * 137U) /
                           POWER_CFG_CURRENT_MAX_MA);
  if (gauge_width > 137U)
  {
    gauge_width = 137U;
  }
  if (gauge_width > 0U)
  {
    LCD_BufferFillRect(s_ui_region_buffer, canvas_width, canvas_height,
                       (uint16_t)(origin_x + 2U),
                       (uint16_t)(origin_y + 60U),
                       gauge_width, UI_GAUGE_HEIGHT, UI_COLOR_CURRENT);
  }
}

/**
 * @brief  在RAM中合成并一次提交电流卡片的动态区域。
 * @retval 无。
 */
static void UI_DrawCurrentDynamicArea(void)
{
  const UI_Rect_t *dynamic = &g_ui_layout.current_dynamic;

  if (!UI_PrepareRegionBuffer(dynamic->width,
                              dynamic->height,
                              UI_GetCardColor(UI_EDIT_CURRENT)))
  {
    return;
  }
  UI_ComposeCurrentDynamicArea(dynamic->width, dynamic->height, 0U, 0U);
  if(s_ui_last_drawn_output_current_ma==UINT16_MAX || s_ui_last_drawn_current_limit_ma!=s_ui_state.current_limit_ma)
  {LCD_DrawBitmap(dynamic->x,dynamic->y,dynamic->width,dynamic->height,s_ui_region_buffer);}
  else
  {
    /* The SET/OCP pill is unchanged during normal 50Hz measurements. */
    LCD_DrawBitmap(dynamic->x,dynamic->y,dynamic->width,35U,s_ui_region_buffer);
    if((uint32_t)s_ui_last_drawn_output_current_ma*137U/POWER_CFG_CURRENT_MAX_MA !=
       (uint32_t)s_ui_state.output_current_ma*137U/POWER_CFG_CURRENT_MAX_MA)
    {LCD_DrawBitmap(dynamic->x,dynamic->y+60U,dynamic->width,4U,
      s_ui_region_buffer+60U*dynamic->width);}
  }
  s_ui_last_drawn_output_current_ma = s_ui_state.output_current_ma;
  s_ui_last_drawn_current_limit_ma = s_ui_state.current_limit_ma;
}

/**
 * @brief  绘制功率、电量和能量统计面板。
 * @retval 无。
 */
static bool UI_ComposeStatsPanel(void)
{
  const UI_Rect_t *panel=&g_ui_layout.stats_panel;
  if (!UI_PrepareRegionBuffer(panel->width,panel->height,UI_COLOR_BACKGROUND)) { return false; }
  UI_BufferDrawPanel(panel->width,panel->height,0,0,panel->width,panel->height,
    UI_COLOR_PANEL,UI_COLOR_PANEL_BORDER);
  LCD_BufferFillRect(s_ui_region_buffer,panel->width,panel->height,154,6,1,26,UI_COLOR_PANEL_BORDER);
  UI_BufferDrawCenteredText(panel->width,panel->height,2,2,150,14,
    "波动 Vpp",UI_COLOR_LABEL,LCD_FONT_HEITI_SMALL);
  UI_BufferDrawCenteredText(panel->width,panel->height,156,2,150,14,
    "POWER (W)",UI_COLOR_LABEL,LCD_FONT_OXANIUM_SMALL);
  UI_ComposeStatsValues(panel->width,panel->height,2,18);
  return true;
}

static void UI_DrawStatsPanel(void)
{
  const UI_Rect_t *panel=&g_ui_layout.stats_panel;
  if (!UI_ComposeStatsPanel()) { return; }
  LCD_DrawBitmap(panel->x,panel->y,panel->width,panel->height,s_ui_region_buffer);
  s_ui_last_drawn_output_power_mw=s_ui_state.output_power_mw;
  s_ui_last_drawn_ripple=s_ui_state.ripple_vpp_mv;
  s_ui_last_drawn_ripple_valid=s_ui_state.ripple_valid;
}

/**
 * @brief  在指定RAM画布中合成统计数值。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  origin_x 统计数值区的X偏移。
 * @param  origin_y 统计数值区的Y偏移。
 * @retval 无。
 */
static void UI_ComposeStatsValues(uint16_t canvas_width,uint16_t canvas_height,
                                  uint16_t origin_x,uint16_t origin_y)
{
  char text[24];
  if (s_ui_state.ripple_valid)
  { (void)snprintf(text,sizeof(text),"%u mV",s_ui_state.ripple_vpp_mv); }
  else { (void)snprintf(text,sizeof(text),"-- mV"); }
  UI_BufferDrawCenteredText(canvas_width,canvas_height,origin_x,origin_y,150,17,
    text,UI_COLOR_VOLTAGE,LCD_FONT_HEITI_MEDIUM);
  (void)snprintf(text,sizeof(text),"%lu.%02lu W",
    (unsigned long)(s_ui_state.output_power_mw/1000U),
    (unsigned long)((s_ui_state.output_power_mw%1000U)/10U));
  UI_BufferDrawCenteredText(canvas_width,canvas_height,origin_x+154U,origin_y,150,17,
    text,UI_COLOR_GREEN,LCD_FONT_OXANIUM_MEDIUM);
}

/**
 * @brief  仅重绘统计面板中的实时数据。
 * @retval 无。
 */
static void UI_DrawStatsValues(void)
{
  const UI_Rect_t *values = &g_ui_layout.stats_values;
  const UI_Rect_t *panel = &g_ui_layout.stats_panel;
  if (!UI_ComposeStatsPanel()) { return; }
  /* Copy from the same full composition, preserving rounded border pixels. */
  const uint16_t *pixels = s_ui_region_buffer +
    (values->y-panel->y)*panel->width + (values->x-panel->x);

  if(s_ui_last_drawn_ripple!=s_ui_state.ripple_vpp_mv ||
     s_ui_last_drawn_ripple_valid!=s_ui_state.ripple_valid)
  {LCD_DrawBitmapStrided(values->x,values->y,150U,values->height,pixels,panel->width);}
  if(s_ui_last_drawn_output_power_mw/10U!=s_ui_state.output_power_mw/10U)
  {LCD_DrawBitmapStrided(values->x+154U,values->y,150U,values->height,
    pixels+154U,panel->width);}
  s_ui_last_drawn_output_power_mw = s_ui_state.output_power_mw;
  s_ui_last_drawn_ripple=s_ui_state.ripple_vpp_mv;
  s_ui_last_drawn_ripple_valid=s_ui_state.ripple_valid;
}

/**
 * @brief  绘制底部四个触摸按键中的一个。
 * @param  button_index 按键索引，范围0～3。
 * @retval 无。
 */
static void UI_DrawButton(uint8_t button_index)
{
  char text[16];
  uint16_t fill_color = UI_COLOR_BUTTON;
  uint16_t border_color = UI_COLOR_PANEL_BORDER;
  uint16_t function_color;
  uint16_t left;

  if (button_index >= 4U)
  {
    return;
  }

  left = g_ui_layout.buttons[button_index].x;
  function_color = (s_ui_state.edit_target == UI_EDIT_VOLTAGE) ?
                   UI_COLOR_VOLTAGE : UI_COLOR_CURRENT;
  if (button_index == 2U)
  {
    fill_color = (s_ui_state.edit_target == UI_EDIT_VOLTAGE) ?
                 UI_COLOR_PANEL_VOLT_SELECTED : UI_COLOR_BUTTON_STEP;
    border_color = function_color;
  }
  else if (button_index == 3U)
  {
    fill_color = UI_OutputStatusColor();
    border_color = fill_color;
  }

  if (!UI_PrepareRegionBuffer(UI_BUTTON_BUFFER_WIDTH,
                              UI_BUTTON_BUFFER_HEIGHT,
                              UI_COLOR_BACKGROUND))
  {
    return;
  }
  LCD_BufferFillRoundRect(s_ui_region_buffer,
                          UI_BUTTON_BUFFER_WIDTH,
                          UI_BUTTON_BUFFER_HEIGHT,
                          0U, 0U,
                          UI_BUTTON_BUFFER_WIDTH,
                          UI_BUTTON_BUFFER_HEIGHT,
                          UI_BUTTON_RADIUS,
                          border_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer,
                          UI_BUTTON_BUFFER_WIDTH,
                          UI_BUTTON_BUFFER_HEIGHT,
                          2U, 2U,
                          (uint16_t)(UI_BUTTON_BUFFER_WIDTH - 4U),
                          (uint16_t)(UI_BUTTON_BUFFER_HEIGHT - 4U),
                          (uint8_t)(UI_BUTTON_RADIUS - 2U),
                          fill_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer,
                          UI_BUTTON_BUFFER_WIDTH,
                          UI_BUTTON_BUFFER_HEIGHT,
                          12U, 4U, 49U, 2U, 1U, border_color);

  if (button_index == 0U)
  {
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 5U, UI_BUTTON_WIDTH, 36U,
                              "-", UI_COLOR_WHITE, LCD_FONT_OXANIUM_LARGE);
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 43U, UI_BUTTON_WIDTH, 17U,
                              (s_ui_state.edit_target == UI_EDIT_VOLTAGE) ? "DEC V" : "DEC I",
                              UI_COLOR_LABEL, LCD_FONT_OXANIUM_SMALL);
  }
  else if (button_index == 1U)
  {
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 5U, UI_BUTTON_WIDTH, 36U,
                              "+", UI_COLOR_WHITE, LCD_FONT_OXANIUM_LARGE);
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 43U, UI_BUTTON_WIDTH, 17U,
                              (s_ui_state.edit_target == UI_EDIT_VOLTAGE) ? "INC V" : "INC I",
                              UI_COLOR_LABEL, LCD_FONT_OXANIUM_SMALL);
  }
  else if (button_index == 2U)
  {
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 7U, UI_BUTTON_WIDTH, 26U,
                              "STEP", function_color, LCD_FONT_OXANIUM_MEDIUM);
    if (s_ui_state.edit_target == UI_EDIT_VOLTAGE)
    {
      uint16_t step_mv = s_voltage_steps_mv[s_ui_state.step_index];
      (void)snprintf(text,
                     sizeof(text),
                     "%lu.%02luV",
                     (unsigned long)(step_mv / 1000U),
                     (unsigned long)((step_mv % 1000U) / 10U));
    }
    else
    {
      (void)snprintf(text,
                     sizeof(text),
                     "%luMA",
                     (unsigned long)s_current_steps_ma[s_ui_state.step_index]);
    }
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 41U, UI_BUTTON_WIDTH, 18U,
                              text, function_color, LCD_FONT_OXANIUM_SMALL);
  }
  else
  {
    uint16_t text_color = UI_OutputIsOn() ? UI_COLOR_BLACK : UI_COLOR_WHITE;
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 7U, UI_BUTTON_WIDTH, 26U,
                              "OUT", text_color, LCD_FONT_OXANIUM_MEDIUM);
    UI_BufferDrawCenteredText(UI_BUTTON_BUFFER_WIDTH, UI_BUTTON_BUFFER_HEIGHT,
                              0U, 41U, UI_BUTTON_WIDTH, 18U,
                              UI_OutputStatusText(),
                              text_color, LCD_FONT_OXANIUM_SMALL);
  }

  LCD_DrawBitmap(left,
                 UI_BUTTON_TOP,
                 UI_BUTTON_BUFFER_WIDTH,
                 UI_BUTTON_BUFFER_HEIGHT,
                 s_ui_region_buffer);
  if (button_index == 3U)
  {
    s_ui_last_drawn_button_output = s_ui_state.output_requested;
    s_ui_last_drawn_button_fault = s_ui_state.power_fault;
  }
}

/**
 * @brief  绘制底部全部四个触摸按键。
 * @retval 无。
 */
static void UI_DrawAllButtons(void)
{
  uint8_t index;

  for (index = 0U; index < 4U; index++)
  {
    UI_DrawButton(index);
  }
}

/**
 * @brief  绘制页面底部的三页位置指示点。
 * @param  selected_index 当前页索引：0为波形页，1为数字页，2为设置页。
 * @retval 无。
 */
static void UI_DrawPageIndicator(uint8_t selected_index)
{
  uint8_t index;
  uint16_t color;
  uint16_t local_x;
  const UI_Rect_t *indicator = &g_ui_layout.page_indicator;

  if (!UI_PrepareRegionBuffer(indicator->width,
                              indicator->height,
                              UI_COLOR_BACKGROUND))
  {
    return;
  }
  for (index = 0U; index < UI_LAYOUT_PAGE_COUNT; index++)
  {
    local_x = (uint16_t)(g_ui_layout.page_dot_x[index] - indicator->x);
    if (index == selected_index)
    {
      color = (selected_index == 0U) ? UI_COLOR_VOLTAGE :
              ((selected_index == 2U) ? UI_COLOR_PURPLE : UI_COLOR_LABEL);
      LCD_BufferFillCircle(s_ui_region_buffer,
                           indicator->width,
                           indicator->height,
                           local_x,
                           (uint16_t)(g_ui_layout.page_dot_y - indicator->y),
                           2U,
                           color);
    }
    else
    {
      LCD_BufferFillCircle(s_ui_region_buffer,
                           indicator->width,
                           indicator->height,
                           local_x,
                           (uint16_t)(g_ui_layout.page_dot_y - indicator->y),
                           1U,
                           UI_COLOR_PANEL_BORDER);
    }
  }
  LCD_DrawBitmap(indicator->x,
                 indicator->y,
                 indicator->width,
                 indicator->height,
                 s_ui_region_buffer);
}

/**
 * @brief  绘制完整的320×240数字显示页面。
 * @retval 无。
 */
static void UI_DrawNumericPage(void)
{
  UI_ClearPageGaps(UI_PAGE_NUMERIC);
  /* A complete page needs the complete header, even when VIN/TEMP are unchanged. */
  s_ui_last_drawn_input_voltage_mv = UINT16_MAX;
  UI_DrawTopBar();
  UI_DrawVoltageCard();
  UI_DrawCurrentCard();
  UI_DrawStatsPanel();
  UI_DrawAllButtons();
  UI_DrawPageIndicator(1U);
}

/**
 * @brief  绘制设置页面的屏幕亮度卡片。
 * @retval 无。
 */
static bool UI_SettingsCardRect(uint8_t index, UI_Rect_t *rect)
{
  if ((rect == NULL) || (index < s_ui_state.settings_first_item) ||
      (index >= s_ui_state.settings_first_item + UI_SETTINGS_VISIBLE_COUNT))
  {
    return false;
  }
  *rect = g_ui_layout.brightness_card;
  rect->y = (uint16_t)(UI_SETTINGS_FIRST_Y +
            (index - s_ui_state.settings_first_item) * UI_SETTINGS_ROW_STEP_PX);
  return true;
}

static bool UI_SettingsContains(uint8_t index, uint16_t x, uint16_t y)
{
  UI_Rect_t rect;
  return UI_SettingsCardRect(index, &rect) && UI_LayoutContains(&rect, x, y);
}

static void UI_DrawSettingsBrightness(void)
{
  char text[12];
  uint16_t gauge_width;
  UI_Rect_t card_storage;
  const UI_Rect_t *card = &card_storage;

  if (!UI_SettingsCardRect(0U, &card_storage))
  {
    return;
  }

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     UI_COLOR_PANEL, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 10U, "屏幕亮度",
                         UI_COLOR_LABEL, LCD_FONT_HEITI_MEDIUM);

  LCD_BufferFillRect(s_ui_region_buffer, card->width, card->height,
                     12U, 36U, 168U, 3U, UI_COLOR_PANEL_BORDER);
  gauge_width = (uint16_t)(((uint32_t)s_ui_state.brightness_percent * 168U) / 100U);
  LCD_BufferFillRect(s_ui_region_buffer, card->width, card->height,
                     12U, 36U, gauge_width, 3U, UI_COLOR_VOLTAGE);

  (void)snprintf(text, sizeof(text), "%u%%", s_ui_state.brightness_percent);
  UI_BufferDrawCenteredText(card->width, card->height,
                            148U, 12U, 47U, 20U,
                            text, UI_COLOR_VOLTAGE,
                            LCD_FONT_HEITI_MEDIUM);

  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          208U, 5U, 34U, 33U,
                          UI_INNER_RADIUS, UI_COLOR_PANEL_BORDER);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          210U, 7U, 30U, 29U,
                          2U, UI_COLOR_BUTTON);
  UI_BufferDrawCenteredText(card->width, card->height,
                            210U, 7U, 30U, 29U,
                            "-", UI_COLOR_WHITE,
                            LCD_FONT_HEITI_MEDIUM);

  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          250U, 5U, 34U, 33U,
                          UI_INNER_RADIUS, UI_COLOR_VOLTAGE);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          252U, 7U, 30U, 29U,
                          2U, UI_COLOR_PANEL_VOLT_SELECTED);
  UI_BufferDrawCenteredText(card->width, card->height,
                            252U, 7U, 30U, 29U,
                            "+", UI_COLOR_VOLTAGE,
                            LCD_FONT_HEITI_MEDIUM);
  LCD_DrawBitmap(card->x, card->y,
                 card->width, card->height,
                 s_ui_region_buffer);
}

/**
 * @brief  绘制设置页面的蜂鸣器开关卡片。
 * @retval 无。
 */
static void UI_DrawSettingsBuzzer(void)
{
  uint16_t switch_color;
  uint16_t switch_background;
  UI_Rect_t card_storage;
  const UI_Rect_t *card = &card_storage;

  if (!UI_SettingsCardRect(1U, &card_storage))
  {
    return;
  }

  switch_color = s_ui_state.buzzer_enabled ? UI_COLOR_GREEN : UI_COLOR_RED;
  switch_background = s_ui_state.buzzer_enabled ?
                      g_ui_theme.output_on_background :
                      g_ui_theme.output_off_background;

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     UI_COLOR_PANEL, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 5U, "蜂鸣器",
                         UI_COLOR_LABEL, LCD_FONT_HEITI_MEDIUM);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 24U, "触摸声音",
                         UI_COLOR_VOLTAGE_LABEL, LCD_FONT_HEITI_SMALL);

  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          232U, 8U, 48U, 27U,
                          UI_BUTTON_RADIUS, switch_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          234U, 10U, 44U, 23U,
                          4U, switch_background);
  UI_BufferDrawCenteredText(card->width, card->height,
                            234U, 10U, 44U, 23U,
                            s_ui_state.buzzer_enabled ? "开启" : "关闭",
                            switch_color,
                            LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(card->x, card->y,
                 card->width, card->height,
                 s_ui_region_buffer);
}

/**
 * @brief  绘制设置页面的屏幕方向卡片。
 * @retval 无。
 */
static void UI_DrawSettingsOrientation(void)
{
  UI_Rect_t card_storage;
  const UI_Rect_t *card = &card_storage;

  if (!UI_SettingsCardRect(2U, &card_storage))
  {
    return;
  }

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     UI_COLOR_PANEL, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 5U, "显示方向",
                         UI_COLOR_CURRENT, LCD_FONT_HEITI_MEDIUM);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 24U, "屏幕与触摸同步",
                         UI_COLOR_LABEL, LCD_FONT_HEITI_SMALL);

  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          214U, 8U, 66U, 27U,
                          UI_BUTTON_RADIUS, UI_COLOR_CURRENT);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          216U, 10U, 62U, 23U,
                          4U, UI_COLOR_PANEL_CURR_SELECTED);
  UI_BufferDrawCenteredText(card->width, card->height,
                            216U, 10U, 62U, 23U,
                            s_ui_state.display_rotated_180 ? "倒置" : "正向",
                            UI_COLOR_CURRENT,
                            LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(card->x, card->y,
                 card->width, card->height,
                 s_ui_region_buffer);
}

/** @brief 绘制本地输出电流零点校准卡片。 */
static void UI_DrawSettingsCalibration(void)
{
  const char *status_text = "开始";
  uint16_t status_color = UI_COLOR_VOLTAGE;
  UI_Rect_t card_storage;
  const UI_Rect_t *card = &card_storage;

  if (!UI_SettingsCardRect(3U, &card_storage))
  {
    return;
  }

  if (s_ui_state.calibration_status == UI_CALIBRATION_RUNNING)
  {
    status_text = "采集中";
    status_color = UI_COLOR_CURRENT;
  }
  else if (s_ui_state.calibration_status == UI_CALIBRATION_SAVED)
  {
    status_text = "已保存";
    status_color = UI_COLOR_GREEN;
  }
  else if (s_ui_state.calibration_status == UI_CALIBRATION_FAILED)
  {
    status_text = "失败";
    status_color = UI_COLOR_RED;
  }
  else if (s_ui_state.calibration_status ==
           UI_CALIBRATION_OUTPUT_MUST_BE_OFF)
  {
    status_text = "先关闭";
    status_color = UI_COLOR_RED;
  }

  if (!UI_PrepareRegionBuffer(card->width, card->height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card->width, card->height,
                     0U, 0U, card->width, card->height,
                     UI_COLOR_PANEL, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 5U, "电流零点校准",
                         UI_COLOR_PURPLE, LCD_FONT_HEITI_SMALL);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card->width, card->height,
                         12U, 24U, "关闭输出并断开负载",
                         UI_COLOR_LABEL, LCD_FONT_HEITI_SMALL);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          206U, 7U, 78U, 29U,
                          UI_BUTTON_RADIUS, status_color);
  LCD_BufferFillRoundRect(s_ui_region_buffer, card->width, card->height,
                          208U, 9U, 74U, 25U,
                          4U, UI_COLOR_DARK_PILL);
  UI_BufferDrawCenteredText(card->width, card->height,
                            208U, 9U, 74U, 25U,
                            status_text, status_color,
                            LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(card->x, card->y,
                 card->width, card->height,
                 s_ui_region_buffer);
}

static void UI_DrawSettingsSleep(void)
{
  UI_Rect_t card;
  char text[20];
  if (!UI_SettingsCardRect(4U,&card) ||
      !UI_PrepareRegionBuffer(card.width,card.height,UI_COLOR_BACKGROUND)) { return; }
  UI_BufferDrawPanel(card.width,card.height,0U,0U,card.width,card.height,
                    UI_COLOR_PANEL,UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer,card.width,card.height,
    12U,5U,"屏幕休眠",UI_COLOR_LABEL,LCD_FONT_HEITI_SMALL);
  LCD_BufferDrawTextFont(s_ui_region_buffer,card.width,card.height,
    12U,24U,"触摸唤醒 输出保持",UI_COLOR_VOLTAGE_LABEL,LCD_FONT_HEITI_SMALL);
  if (s_ui_state.screen_sleep_seconds==0U) { (void)snprintf(text,sizeof text,"关闭"); }
  else if (s_ui_state.screen_sleep_seconds<60U) { (void)snprintf(text,sizeof text,"%u秒",s_ui_state.screen_sleep_seconds); }
  else { (void)snprintf(text,sizeof text,"%u分钟",s_ui_state.screen_sleep_seconds/60U); }
  LCD_BufferFillRoundRect(s_ui_region_buffer,card.width,card.height,
    205U,7U,80U,29U,UI_BUTTON_RADIUS,UI_COLOR_GREEN);
  UI_BufferDrawCenteredText(card.width,card.height,205U,7U,80U,29U,
    text,UI_COLOR_BLACK,LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(card.x,card.y,card.width,card.height,s_ui_region_buffer);
}

/** @brief Show the same firmware identity returned by the INFO command. */
static void UI_DrawSettingsVersion(void)
{
  UI_Rect_t card;

  if (!UI_SettingsCardRect(5U, &card) ||
      !UI_PrepareRegionBuffer(card.width, card.height, UI_COLOR_BACKGROUND))
  {
    return;
  }
  UI_BufferDrawPanel(card.width, card.height,
                     0U, 0U, card.width, card.height,
                     UI_COLOR_PANEL, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card.width, card.height,
                         12U, 5U, "固件版本",
                         UI_COLOR_LABEL, LCD_FONT_HEITI_SMALL);
  LCD_BufferDrawTextFont(s_ui_region_buffer, card.width, card.height,
                         12U, 24U, "电源硬件 V2",
                         UI_COLOR_VOLTAGE_LABEL, LCD_FONT_HEITI_SMALL);
  UI_BufferDrawCenteredText(card.width, card.height,
                            205U, 8U, 80U, 27U,
                            "V" XIN_POWER_FIRMWARE_VERSION,
                            UI_COLOR_GREEN, LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(card.x, card.y, card.width, card.height,
                 s_ui_region_buffer);
}

/**
 * @brief  利用共用缓存整块绘制二级页面顶栏。
 * @param  title 页面标题。
 * @param  hint 右侧滑动提示。
 * @param  title_color 页面标题颜色。
 * @retval 无。
 */
static void UI_DrawSecondaryHeader(const char *title,
                                   const char *hint,
                                   uint16_t title_color)
{
  const UI_Rect_t *header = &g_ui_layout.secondary_header;

  if (!UI_PrepareRegionBuffer(header->width, header->height, UI_COLOR_TOP_BAR))
  {
    return;
  }
  LCD_BufferFillRect(s_ui_region_buffer, header->width, header->height,
                     0U, (uint16_t)(header->height - 1U),
                     header->width, 1U, UI_COLOR_PANEL_BORDER);
  LCD_BufferDrawTextFont(s_ui_region_buffer, header->width, header->height,
                         14U, 5U, title, title_color,
                         LCD_FONT_HEITI_MEDIUM);
  LCD_BufferDrawTextFont(s_ui_region_buffer, header->width, header->height,
                         210U, 7U, hint, UI_COLOR_LABEL,
                         LCD_FONT_HEITI_SMALL);
  LCD_DrawBitmap(header->x, header->y,
                 header->width, header->height,
                 s_ui_region_buffer);
}

/**
 * @brief  绘制设置页面。
 * @retval 无。
 */
static void UI_DrawSettingsPage(void)
{
  uint16_t thumb_height;
  uint16_t thumb_y = 34U;
  uint8_t scroll_range = UI_SETTINGS_ITEM_COUNT - UI_SETTINGS_VISIBLE_COUNT;

  UI_ClearPageGaps(UI_PAGE_SETTINGS);
  UI_DrawSecondaryHeader("电源设置", "左滑返回", UI_COLOR_PURPLE);
  UI_DrawSettingsBrightness();
  UI_DrawSettingsBuzzer();
  UI_DrawSettingsOrientation();
  UI_DrawSettingsCalibration();
  UI_DrawSettingsSleep();
  UI_DrawSettingsVersion();
  if (scroll_range > 0U)
  {
    LCD_FillRect(312U, 34U, 3U, 181U, UI_COLOR_PANEL_BORDER);
    thumb_height = (uint16_t)((181U * UI_SETTINGS_VISIBLE_COUNT) /
                              UI_SETTINGS_ITEM_COUNT);
    thumb_y = (uint16_t)(thumb_y +
      ((181U - thumb_height) * s_ui_state.settings_first_item) /
      scroll_range);
    LCD_FillRect(312U, thumb_y, 3U, thumb_height, UI_COLOR_PURPLE);
  }
  UI_DrawPageIndicator(0U);
}

/** @brief 将最新测量值按100ms周期加入本地波形历史。 */


/** @brief 绘制一个带网格的波形区域。 */


/** @brief 刷新电压、电流两条本地趋势波形。 */


/** @brief 绘制本地实时波形页面。 */


/**
 * @brief  在页面切换前只清理新页面不会覆盖的缝隙。
 * @param  page 即将绘制的目标页面。
 * @retval 无。
 * @note   完整切页由UI_DrawCurrentPage先清黑；此处填充目标页面的背景缝隙。
 */
static void UI_ClearPageGaps(UI_Page_t page)
{
  const UI_Rect_t *regions=page==UI_PAGE_SETTINGS ? g_ui_settings_clear_regions : g_ui_numeric_clear_regions;
  uint8_t count=page==UI_PAGE_SETTINGS ? g_ui_settings_clear_region_count : g_ui_numeric_clear_region_count;
  for(unsigned i=0;i<count;i++)
  { LCD_FillRect(regions[i].x,regions[i].y,regions[i].width,regions[i].height,UI_COLOR_BACKGROUND); }
}

/**
 * @brief  根据当前页面状态绘制完整页面。
 * @retval 无。
 */
static void UI_DrawCurrentPage(void)
{
  if (s_ui_sleeping) { return; }
  /* Clear all old pixels, including rounded corners, header and splash.
     LCD transfers continue yielding to INA/current protection. */
  LCD_Clear(LCD_COLOR_BLACK);
  if (s_ui_state.current_page == UI_PAGE_SETTINGS)
  {
    UI_DrawSettingsPage();
  }
  else
  {
    UI_DrawNumericPage();
  }
}

/**
 * @brief  切换到指定页面并执行一次完整重绘。
 * @param  page 目标页面。
 * @retval 无。
 */
static void UI_SwitchPage(UI_Page_t page)
{
  if (s_ui_state.current_page == page)
  {
    return;
  }

  s_ui_state.current_page = page;
  UI_DrawCurrentPage();
}

/**
 * @brief  将一次点击应用到数字显示页面状态。
 * @param  x 显示区域X坐标。
 * @param  y 显示区域Y坐标。
 * @retval 无。
 */
static void UI_HandleNumericTap(uint16_t x, uint16_t y)
{
  uint16_t step;

  if (UI_LayoutContains(&g_ui_layout.voltage_card, x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.edit_target != UI_EDIT_VOLTAGE)
    {
      s_ui_state.edit_target = UI_EDIT_VOLTAGE;
      UI_DrawVoltageCard();
      UI_DrawCurrentCard();
      UI_DrawButton(0U);
      UI_DrawButton(1U);
      UI_DrawButton(2U);
    }
    return;
  }

  if (UI_LayoutContains(&g_ui_layout.current_card, x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.edit_target != UI_EDIT_CURRENT)
    {
      s_ui_state.edit_target = UI_EDIT_CURRENT;
      UI_DrawVoltageCard();
      UI_DrawCurrentCard();
      UI_DrawButton(0U);
      UI_DrawButton(1U);
      UI_DrawButton(2U);
    }
    return;
  }

  if (UI_LayoutContains(&g_ui_layout.buttons[0], x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.edit_target == UI_EDIT_VOLTAGE)
    {
      step = s_voltage_steps_mv[s_ui_state.step_index];
      s_ui_state.voltage_setpoint_mv =
        (s_ui_state.voltage_setpoint_mv >
         (uint16_t)(POWER_CFG_VOLTAGE_MIN_MV + step)) ?
        (uint16_t)(s_ui_state.voltage_setpoint_mv - step) :
        POWER_CFG_VOLTAGE_MIN_MV;
      UI_DrawVoltageDynamicArea();
    }
    else
    {
      step = s_current_steps_ma[s_ui_state.step_index];
      s_ui_state.current_limit_ma =
        (s_ui_state.current_limit_ma >
         (uint16_t)(POWER_CFG_CURRENT_MIN_MA + step)) ?
        (uint16_t)(s_ui_state.current_limit_ma - step) :
        POWER_CFG_CURRENT_MIN_MA;
      UI_DrawCurrentDynamicArea();
    }
    return;
  }

  if (UI_LayoutContains(&g_ui_layout.buttons[1], x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.edit_target == UI_EDIT_VOLTAGE)
    {
      step = s_voltage_steps_mv[s_ui_state.step_index];
      if (s_ui_state.voltage_setpoint_mv <=
          (uint16_t)(POWER_CFG_VOLTAGE_MAX_MV - step))
      {
        s_ui_state.voltage_setpoint_mv =
          (uint16_t)(s_ui_state.voltage_setpoint_mv + step);
      }
      else
      {
        s_ui_state.voltage_setpoint_mv = POWER_CFG_VOLTAGE_MAX_MV;
      }
      UI_DrawVoltageDynamicArea();
    }
    else
    {
      step = s_current_steps_ma[s_ui_state.step_index];
      if (s_ui_state.current_limit_ma <=
          (uint16_t)(POWER_CFG_CURRENT_MAX_MA - step))
      {
        s_ui_state.current_limit_ma =
          (uint16_t)(s_ui_state.current_limit_ma + step);
      }
      else
      {
        s_ui_state.current_limit_ma = POWER_CFG_CURRENT_MAX_MA;
      }
      UI_DrawCurrentDynamicArea();
    }
    return;
  }

  if (UI_LayoutContains(&g_ui_layout.buttons[2], x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    s_ui_state.step_index = (uint8_t)((s_ui_state.step_index + 1U) % 3U);
    UI_DrawButton(2U);
    return;
  }

  if (UI_LayoutContains(&g_ui_layout.buttons[3], x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.power_fault)
    {
      s_ui_state.fault_clear_request_pending = true;
      s_ui_state.output_requested = false;
    }
    else
    {
      s_ui_state.output_requested = !s_ui_state.output_requested;
    }
    if (!s_ui_state.output_requested)
    {
      s_ui_state.constant_current = false;
    }
    s_ui_last_display_time = HAL_GetTick();
    /* Defer LCD SPI work until the control loop has seen the new request. */
  }
}

/**
 * @brief  计算两个无符号坐标之间的绝对差值。
 * @param  first 第一个坐标。
 * @param  second 第二个坐标。
 * @retval 两个坐标之间的绝对距离。
 */
static uint16_t UI_AbsoluteDifference(uint16_t first, uint16_t second)
{
  return (first >= second) ? (uint16_t)(first - second) :
                             (uint16_t)(second - first);
}

/**
 * @brief  处理设置页面中的一次点击。
 * @param  x 点击X坐标。
 * @param  y 点击Y坐标。
 * @retval 无。
 */
static void UI_HandleSettingsTap(uint16_t x, uint16_t y)
{
  if (UI_SettingsContains(0U, x, y) && (x >= 220U) && (x < 254U))
  {
    if (s_ui_state.brightness_percent > UI_BRIGHTNESS_MIN_PERCENT)
    {
      Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
      s_ui_state.brightness_percent =
        (uint8_t)(s_ui_state.brightness_percent - UI_BRIGHTNESS_STEP_PERCENT);
      LCD_SetBacklight(s_ui_state.brightness_percent);
      UI_DrawSettingsBrightness();
    }
    return;
  }

  if (UI_SettingsContains(0U, x, y) && (x >= 262U) && (x < 296U))
  {
    if (s_ui_state.brightness_percent < UI_BRIGHTNESS_MAX_PERCENT)
    {
      Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
      s_ui_state.brightness_percent =
        (uint8_t)(s_ui_state.brightness_percent + UI_BRIGHTNESS_STEP_PERCENT);
      LCD_SetBacklight(s_ui_state.brightness_percent);
      UI_DrawSettingsBrightness();
    }
    return;
  }

  if (UI_SettingsContains(1U, x, y))
  {
    s_ui_state.buzzer_enabled = !s_ui_state.buzzer_enabled;
    Buzzer_SetEnabled(s_ui_state.buzzer_enabled);
    if (s_ui_state.buzzer_enabled)
    {
      Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    }
    UI_DrawSettingsBuzzer();
    return;
  }

  if (UI_SettingsContains(2U, x, y))
  {
    LCD_Rotation_t target_rotation;

    target_rotation = s_ui_state.display_rotated_180 ?
                      LCD_ROTATION_LANDSCAPE_0 :
                      LCD_ROTATION_LANDSCAPE_180;
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (LCD_SetRotation(target_rotation))
    {
      s_ui_state.display_rotated_180 = !s_ui_state.display_rotated_180;
      CST836U_SetTransform(s_ui_state.display_rotated_180 ?
                           CST836U_TRANSFORM_ROTATE_90_CCW :
                           CST836U_TRANSFORM_ROTATE_90_CW);
      /* 方向变化会重新映射整块显存，因此立即完整重绘当前页面。 */
      UI_DrawCurrentPage();
    }
    return;
  }

  if (UI_SettingsContains(4U,x,y))
  {
    for (unsigned i=0;i<sizeof(s_sleep_seconds)/sizeof(s_sleep_seconds[0]);i++)
    {
      if (s_sleep_seconds[i]==s_ui_state.screen_sleep_seconds)
      { (void)UI_SetScreenSleepSeconds(s_sleep_seconds[(i+1U)%6U]); break; }
    }
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    UI_DrawSettingsSleep();
    return;
  }

  if (UI_SettingsContains(3U, x, y))
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    if (s_ui_state.output_requested)
    {
      s_ui_state.calibration_status = UI_CALIBRATION_OUTPUT_MUST_BE_OFF;
    }
    else if (s_ui_state.calibration_status != UI_CALIBRATION_RUNNING)
    {
      s_ui_state.calibration_request_pending = true;
      s_ui_state.calibration_status = UI_CALIBRATION_RUNNING;
    }
    UI_DrawSettingsCalibration();
  }
}

/**
 * @brief  根据当前页面处理一次短距离点击。
 * @param  x 点击起始X坐标。
 * @param  y 点击起始Y坐标。
 * @retval 无。
 */
static void UI_HandleTap(uint16_t x, uint16_t y)
{
  if (!s_ui_sleeping && s_ui_state.current_page == UI_PAGE_NUMERIC)
  {
    UI_HandleNumericTap(x, y);
  }
  else if (s_ui_state.current_page == UI_PAGE_SETTINGS)
  {
    UI_HandleSettingsTap(x, y);
  }
}

/**
 * @brief  根据水平滑动方向执行页面切换。
 * @param  horizontal_distance 水平位移，负值为左滑，正值为右滑。
 * @retval 无。
 */
static void UI_HandleSwipe(int32_t horizontal_distance)
{
  if (horizontal_distance>0 && s_ui_state.current_page==UI_PAGE_NUMERIC)
  { Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS); UI_SwitchPage(UI_PAGE_SETTINGS); }
  else if (horizontal_distance<0 && s_ui_state.current_page==UI_PAGE_SETTINGS)
  { Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS); UI_SwitchPage(UI_PAGE_NUMERIC); }
}

static void UI_ScrollSettings(int32_t vertical_distance)
{
  uint8_t previous = s_ui_state.settings_first_item;
  uint8_t scroll_range = UI_SETTINGS_ITEM_COUNT - UI_SETTINGS_VISIBLE_COUNT;

  if ((vertical_distance < 0) &&
      (s_ui_state.settings_first_item < scroll_range))
  {
    s_ui_state.settings_first_item++;
  }
  else if ((vertical_distance > 0) &&
           (s_ui_state.settings_first_item > 0U))
  {
    s_ui_state.settings_first_item--;
  }
  if (s_ui_state.settings_first_item != previous)
  {
    Buzzer_Beep(UI_BUZZER_TOUCH_DURATION_MS);
    UI_DrawSettingsPage();
  }
}

/**
 * @brief  轮询CST836U事件并区分点击与全屏水平滑动。
 * @param  system_time_ms 当前系统时间，单位为毫秒。
 * @retval 本轮读取并处理了触摸事件时返回true。
 */
static bool UI_HandleTouch(uint32_t system_time_ms)
{
  CST836U_Point_t point;
  uint16_t horizontal_distance;
  uint16_t vertical_distance;
  uint32_t gesture_duration_ms;
  int32_t signed_horizontal_distance;
  int32_t signed_vertical_distance;

  if (!s_ui_state.touch_available)
  {
    return false;
  }

  /* 丢弃触摸芯片复位和上电过程中残留的事件，避免误触发输出开关。 */
  if ((system_time_ms - s_ui_touch_guard_start_time) <
      UI_TOUCH_STARTUP_GUARD_MS)
  {
    if (CST836U_GetEvent(&point))
    {
      s_ui_gesture.active = false;
      s_ui_gesture.swipe_detected = false;
      return true;
    }
    return false;
  }

  if (!CST836U_GetEvent(&point))
  {
    return false;
  }

  s_ui_last_activity_ms = system_time_ms;
  if (s_ui_sleeping)
  {
    s_ui_sleeping = false;
    s_ui_wake_touch_pending = point.event != CST836U_EVENT_UP;
    s_ui_gesture.active = s_ui_gesture.swipe_detected = false;
    s_ui_touch_debounce_started = false;
    /* Repaint while dark. The whole waking gesture is consumed, so tapping
       the hidden OUT button cannot change the output request. */
    UI_DrawCurrentPage();
    LCD_SetBacklight(s_ui_state.brightness_percent);
    return true;
  }
  if (s_ui_wake_touch_pending)
  {
    if (point.event == CST836U_EVENT_UP) { s_ui_wake_touch_pending = false; }
    else if (point.event == CST836U_EVENT_DOWN) { s_ui_wake_touch_pending = false; }
    if (point.event != CST836U_EVENT_DOWN) { return true; }
  }

  if ((point.event == CST836U_EVENT_DOWN) ||
      ((point.event == CST836U_EVENT_CONTACT) && !s_ui_gesture.active))
  {
    if (s_ui_touch_debounce_started &&
        ((system_time_ms - s_ui_last_gesture_start_time) < UI_TOUCH_DEBOUNCE_MS))
    {
      return true;
    }

    s_ui_gesture.start_x = point.x;
    s_ui_gesture.start_y = point.y;
    s_ui_gesture.last_x = point.x;
    s_ui_gesture.last_y = point.y;
    s_ui_gesture.start_time_ms = system_time_ms;
    s_ui_gesture.active = true;
    s_ui_gesture.swipe_detected = false;
    s_ui_touch_debounce_started = true;
    s_ui_last_gesture_start_time = system_time_ms;
    return true;
  }

  if (!s_ui_gesture.active)
  {
    return true;
  }

  s_ui_gesture.last_x = point.x;
  s_ui_gesture.last_y = point.y;
  horizontal_distance = UI_AbsoluteDifference(s_ui_gesture.start_x,
                                               s_ui_gesture.last_x);
  vertical_distance = UI_AbsoluteDifference(s_ui_gesture.start_y,
                                             s_ui_gesture.last_y);
  gesture_duration_ms = system_time_ms - s_ui_gesture.start_time_ms;
  signed_horizontal_distance = (int32_t)s_ui_gesture.last_x -
                               (int32_t)s_ui_gesture.start_x;
  signed_vertical_distance = (int32_t)s_ui_gesture.last_y -
                             (int32_t)s_ui_gesture.start_y;

  if ((horizontal_distance >= UI_SWIPE_MIN_DISTANCE_PX) &&
      (vertical_distance <= UI_SWIPE_MAX_VERTICAL_PX) &&
      (gesture_duration_ms <= UI_SWIPE_MAX_DURATION_MS))
  {
    s_ui_gesture.swipe_detected = true;
  }

  if (point.event != CST836U_EVENT_UP)
  {
    return true;
  }

  if ((s_ui_state.current_page == UI_PAGE_SETTINGS) &&
      (vertical_distance >= UI_SWIPE_MIN_DISTANCE_PX) &&
      (horizontal_distance <= UI_SWIPE_MAX_VERTICAL_PX) &&
      (gesture_duration_ms <= UI_SWIPE_MAX_DURATION_MS))
  {
    UI_ScrollSettings(signed_vertical_distance);
  }
  else if (s_ui_gesture.swipe_detected)
  {
    UI_HandleSwipe(signed_horizontal_distance);
  }
  else if ((horizontal_distance <= UI_TAP_MAX_DISTANCE_PX) &&
           (vertical_distance <= UI_TAP_MAX_DISTANCE_PX))
  {
    UI_HandleTap(s_ui_gesture.start_x, s_ui_gesture.start_y);
  }

  s_ui_gesture.active = false;
  s_ui_gesture.swipe_detected = false;
  return true;
}

/**
 * @brief  更新内置的阻性负载演示数据模型。
 * @param  system_time_ms 当前系统时间，单位为毫秒。
 * @retval 无。
 */
static void UI_RunDemonstrationModel(uint32_t system_time_ms)
{
  uint32_t elapsed_ms;
  uint32_t ideal_current_ma;
  uint32_t target_voltage_mv;
  uint32_t target_current_ma;
  uint32_t capacity_numerator;
  uint32_t energy_numerator;
  int32_t voltage_error;

  elapsed_ms = system_time_ms - s_ui_last_engine_time;
  if (elapsed_ms < UI_ENGINE_PERIOD_MS)
  {
    return;
  }
  if (elapsed_ms > 200U)
  {
    elapsed_ms = UI_ENGINE_PERIOD_MS;
  }
  s_ui_last_engine_time = system_time_ms;

  if (s_ui_state.output_requested)
  {
    /* 使用固定5.000欧姆的阻性负载生成演示数据。 */
    ideal_current_ma = ((uint32_t)s_ui_state.voltage_setpoint_mv * 1000U) / 5000U;
    if (ideal_current_ma > s_ui_state.current_limit_ma)
    {
      s_ui_state.constant_current = true;
      target_current_ma = s_ui_state.current_limit_ma;
      target_voltage_mv = ((uint32_t)s_ui_state.current_limit_ma * 5000U) / 1000U;
    }
    else
    {
      s_ui_state.constant_current = false;
      target_current_ma = ideal_current_ma;
      target_voltage_mv = s_ui_state.voltage_setpoint_mv;
    }

    voltage_error = (int32_t)target_voltage_mv - s_ui_state.output_voltage_mv;
    s_ui_state.output_voltage_mv =
      (uint16_t)((int32_t)s_ui_state.output_voltage_mv + ((voltage_error * 35) / 100));
    s_ui_state.output_current_ma = (uint16_t)target_current_ma;
  }
  else
  {
    s_ui_state.constant_current = false;
    s_ui_state.output_voltage_mv =
      (uint16_t)(((uint32_t)s_ui_state.output_voltage_mv * 60U) / 100U);
    if (s_ui_state.output_voltage_mv < 50U)
    {
      s_ui_state.output_voltage_mv = 0U;
    }
    s_ui_state.output_current_ma = 0U;
  }

  s_ui_state.output_power_mw =
    ((uint32_t)s_ui_state.output_voltage_mv * s_ui_state.output_current_ma) / 1000U;

  if (s_ui_state.output_requested)
  {
    capacity_numerator =
      ((uint32_t)s_ui_state.output_current_ma * elapsed_ms) + s_ui_capacity_remainder;
    s_ui_state.capacity_uah += capacity_numerator / 3600U;
    s_ui_capacity_remainder = capacity_numerator % 3600U;

    energy_numerator = (s_ui_state.output_power_mw * elapsed_ms) + s_ui_energy_remainder;
    s_ui_state.energy_uwh += energy_numerator / 3600U;
    s_ui_energy_remainder = energy_numerator % 3600U;
  }
}

/**
 * @brief  只刷新实时数据小区域，不重绘整个页面。
 * @retval 无。
 */
static void UI_RefreshLiveAreas(void)
{
  bool output_changed;
  bool output_button_changed;
  bool mode_changed;

  if ((s_ui_last_drawn_output_voltage_mv != s_ui_state.output_voltage_mv) ||
      (s_ui_last_drawn_voltage_setpoint_mv != s_ui_state.voltage_setpoint_mv))
  {
    UI_DrawVoltageDynamicArea();
  }

  if ((s_ui_last_drawn_output_current_ma != s_ui_state.output_current_ma) ||
      (s_ui_last_drawn_current_limit_ma != s_ui_state.current_limit_ma))
  {
    UI_DrawCurrentDynamicArea();
  }

  if ((s_ui_last_drawn_output_power_mw != s_ui_state.output_power_mw) ||
      (s_ui_last_drawn_ripple != s_ui_state.ripple_vpp_mv) ||
      (s_ui_last_drawn_ripple_valid != s_ui_state.ripple_valid))
  {
    UI_DrawStatsValues();
  }

  output_changed = (s_ui_last_drawn_output != s_ui_state.output_requested) ||
                   (s_ui_last_drawn_fault != s_ui_state.power_fault);
  output_button_changed =
      (s_ui_last_drawn_button_output != s_ui_state.output_requested) ||
      (s_ui_last_drawn_button_fault != s_ui_state.power_fault);
  mode_changed = (s_ui_last_drawn_cc_mode != s_ui_state.constant_current);

  if ((s_ui_last_drawn_input_voltage_mv != s_ui_state.input_voltage_mv) ||
      (s_ui_last_drawn_temperature_decic != s_ui_state.temperature_decic))
  {
    UI_DrawTopBar();
  }
  else
  {
    if (mode_changed || output_changed)
    {
      UI_DrawModeIndicator();
    }
    if (output_changed)
    {
      UI_DrawOutputIndicator();
    }
  }
  /* The top badge and the bottom OUT key have independent draw history.
     VIN/TEMP redraws must not make the key appear up to date. */
  if (output_button_changed)
  {
    UI_DrawButton(3U);
  }
}

/**
 * @brief  初始化并绘制数字显示首页。
 * @param  touch_available CST836U初始化成功时传入true。
 * @retval 无。
 */
void UI_Init(bool touch_available)
{
  s_ui_state.input_voltage_mv = 0U;
  s_ui_state.output_voltage_mv = 0U;
  s_ui_state.output_current_ma = 0U;
  s_ui_state.voltage_setpoint_mv = 5000U;
  /* 未保存电流设定时使用原有默认值，作为过流关断阈值。 */
  s_ui_state.current_limit_ma = POWER_CFG_CURRENT_MIN_MA;
  s_ui_state.temperature_decic = 330;
  s_ui_state.output_power_mw = 0U;
  s_ui_state.capacity_uah = 0U;
  s_ui_state.energy_uwh = 0U;
  s_ui_state.step_index = 1U;
  s_ui_state.brightness_percent = 80U;
  s_ui_state.screen_sleep_seconds = UI_SCREEN_SLEEP_DEFAULT_SECONDS;
  s_ui_sleeping = s_ui_wake_touch_pending = false;
  s_ui_last_activity_ms = HAL_GetTick();
  s_ui_state.settings_first_item = 0U;
  s_ui_state.edit_target = UI_EDIT_VOLTAGE;
  s_ui_state.current_page = UI_PAGE_NUMERIC;
  s_ui_state.calibration_status = UI_CALIBRATION_IDLE;
  s_ui_state.output_requested = false;
  s_ui_state.power_fault = false;
  s_ui_state.fault_clear_request_pending = false;
  s_ui_state.calibration_request_pending = false;
  s_ui_state.buzzer_enabled = true;
  s_ui_state.display_rotated_180 = false;
  s_ui_state.constant_current = false;
  /* 实机版本默认只接受外部测量值，不在上电第一帧显示演示数据。 */
  s_ui_state.demo_enabled = false;
  s_ui_state.touch_available = touch_available;

  s_ui_last_engine_time = HAL_GetTick();
  s_ui_last_display_time = s_ui_last_engine_time;
  s_ui_capacity_remainder = 0U;
  s_ui_energy_remainder = 0U;
  s_ui_gesture.start_x = 0U;
  s_ui_gesture.start_y = 0U;
  s_ui_gesture.last_x = 0U;
  s_ui_gesture.last_y = 0U;
  s_ui_gesture.start_time_ms = 0U;
  s_ui_gesture.active = false;
  s_ui_gesture.swipe_detected = false;
  s_ui_touch_debounce_started = false;
  s_ui_last_gesture_start_time = 0U;
  s_ui_touch_guard_start_time = s_ui_last_engine_time;
  s_ui_last_drawn_input_voltage_mv = UINT16_MAX;
  s_ui_last_drawn_temperature_decic = INT16_MIN;
  s_ui_last_drawn_output_voltage_mv = UINT16_MAX;
  s_ui_last_drawn_voltage_setpoint_mv = UINT16_MAX;
  s_ui_last_drawn_output_current_ma = UINT16_MAX;
  s_ui_last_drawn_current_limit_ma = UINT16_MAX;
  s_ui_last_drawn_output_power_mw = UINT32_MAX;
  (void)Buzzer_Init();
  Buzzer_SetEnabled(s_ui_state.buzzer_enabled);
  (void)LCD_SetRotation(LCD_ROTATION_LANDSCAPE_0);
  CST836U_SetTransform(CST836U_TRANSFORM_ROTATE_90_CW);
  LCD_SetBacklight(s_ui_state.brightness_percent);
  UI_DrawCurrentPage();
}

/**
 * @brief  更新触摸控制器在线状态，并按需刷新顶栏提示。
 * @param  touch_available 触摸控制器当前可用时传入true。
 * @retval 无。
 */
void UI_SetTouchAvailable(bool touch_available)
{
  if (s_ui_state.touch_available == touch_available)
  {
    return;
  }

  s_ui_state.touch_available = touch_available;
  s_ui_gesture.active = false;
  s_ui_gesture.swipe_detected = false;
  s_ui_touch_debounce_started = false;
  if (touch_available)
  {
    /* 恢复后的短时间仍丢弃残留中断，避免把恢复动作识别成点击。 */
    s_ui_touch_guard_start_time = HAL_GetTick();
  }

  if (!s_ui_sleeping && s_ui_state.current_page == UI_PAGE_NUMERIC)
  {
    s_ui_last_drawn_input_voltage_mv=UINT16_MAX;
    UI_DrawTopBar();
  }
}

/**
 * @brief  运行触摸处理和UI局部刷新。
 * @param  system_time_ms 当前HAL毫秒计数值。
 * @retval 无。
 */
void UI_ProcessInput(uint32_t system_time_ms)
{
  Buzzer_Process(system_time_ms);
  (void)UI_HandleTouch(system_time_ms);

  if (s_ui_state.demo_enabled)
  {
    UI_RunDemonstrationModel(system_time_ms);
  }

}

/* Render only AFTER input, commands, sampling and the power-control pass. */
void UI_Render(uint32_t system_time_ms)
{
  if (!s_ui_sleeping && s_ui_state.touch_available && !s_ui_gesture.active &&
      s_ui_state.screen_sleep_seconds != 0U &&
      system_time_ms-s_ui_last_activity_ms >= (uint32_t)s_ui_state.screen_sleep_seconds*1000U)
  {
    s_ui_sleeping = true;
    s_ui_wake_touch_pending = false;
    LCD_SetBacklight(0U);
    LCD_Clear(LCD_COLOR_BLACK);
  }
  if (s_ui_sleeping) { return; }
  if (s_ui_state.current_page==UI_PAGE_NUMERIC &&
      system_time_ms-s_ui_last_display_time>=UI_DISPLAY_PERIOD_MS)
  { s_ui_last_display_time=system_time_ms; UI_RefreshLiveAreas(); }
}

void UI_Process(uint32_t system_time_ms)
{
  UI_ProcessInput(system_time_ms);
  UI_Render(HAL_GetTick());
}

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
                        bool constant_current)
{
  s_ui_state.demo_enabled = false;
  s_ui_state.input_voltage_mv = input_voltage_mv;
  s_ui_state.output_voltage_mv = output_voltage_mv;
  s_ui_state.output_current_ma = output_current_ma;
  s_ui_state.temperature_decic = temperature_decic;
  s_ui_state.constant_current = constant_current;
  s_ui_state.output_power_mw = output_power_mw;
}

/**
 * @brief  写入累计输出电量和能量。
 * @param  capacity_uah 累计电量，单位为微安时。
 * @param  energy_uwh 累计能量，单位为微瓦时。
 * @retval 无。
 */
void UI_SetAccumulatedValues(uint32_t capacity_uah, uint32_t energy_uwh)
{
  s_ui_state.capacity_uah = capacity_uah;
  s_ui_state.energy_uwh = energy_uwh;
}

/**
 * @brief  读取触摸UI选择的设定值、输出请求和界面设置。
 * @param  request 用于接收控制请求的结构体。
 * @retval 无。
 */
void UI_GetControlRequest(UI_ControlRequest_t *request)
{
  if (request == NULL)
  {
    return;
  }

  request->voltage_setpoint_mv = s_ui_state.voltage_setpoint_mv;
  request->current_limit_ma = s_ui_state.current_limit_ma;
  request->brightness_percent = s_ui_state.brightness_percent;
  request->screen_sleep_seconds = s_ui_state.screen_sleep_seconds;
  request->output_requested = s_ui_state.output_requested;
  request->buzzer_enabled = s_ui_state.buzzer_enabled;
  request->display_rotated_180 = s_ui_state.display_rotated_180;
}

/**
 * @brief  由上位机修改输出电压设定值并同步刷新屏幕。
 * @param  voltage_mv 目标输出电压，单位毫伏。
 * @retval 数值处于允许范围并已接受时返回true。
 */
bool UI_SetVoltageSetpoint(uint16_t voltage_mv)
{
  if ((voltage_mv < POWER_CFG_VOLTAGE_MIN_MV) ||
      (voltage_mv > POWER_CFG_VOLTAGE_MAX_MV))
  {
    return false;
  }
  if (s_ui_state.voltage_setpoint_mv == voltage_mv)
  {
    return true;
  }

  s_ui_state.voltage_setpoint_mv = voltage_mv;
  /* Draw-history detects this change during UI_Render, after hardware control. */
  return true;
}

/**
 * @brief  由上位机修改输出电流限制并同步刷新屏幕。
 * @param  current_ma 目标电流限制，单位毫安。
 * @retval 数值处于允许范围并已接受时返回true。
 */
bool UI_SetCurrentLimit(uint16_t current_ma)
{
  if ((current_ma < POWER_CFG_CURRENT_MIN_MA) ||
      (current_ma > POWER_CFG_CURRENT_MAX_MA))
  {
    return false;
  }
  if (s_ui_state.current_limit_ma == current_ma)
  {
    return true;
  }

  s_ui_state.current_limit_ma = current_ma;
  return true;
}

/**
 * @brief  由上位机或保护逻辑修改输出开关请求并同步刷新屏幕。
 * @param  enabled 请求开启时传入true，请求关闭时传入false。
 * @retval 无。
 */
void UI_SetOutputRequested(bool enabled)
{
  if (s_ui_state.output_requested == enabled)
  {
    return;
  }

  s_ui_state.output_requested = enabled;
  if (!enabled)
  {
    s_ui_state.constant_current = false;
  }
  s_ui_last_display_time = HAL_GetTick();
  /* Next periodic refresh draws the switch after hardware control runs. */
}

/**
 * @brief  读取当前正在显示的页面。
 * @retval 当前页面枚举值。
 */
UI_Page_t UI_GetCurrentPage(void)
{
  return s_ui_state.current_page;
}


/**
 * @brief  发生功率级故障时强制清除UI输出请求并刷新开关状态。
 * @retval 无。
 */
void UI_ForceOutputOff(void)
{
  UI_SetOutputRequested(false);
}

void UI_SetPowerFault(bool active)
{
  if (s_ui_state.power_fault == active)
  {
    return;
  }

  s_ui_state.power_fault = active;
  if (active)
  {
    s_ui_state.output_requested = false;
  }
  if (!s_ui_sleeping && s_ui_state.current_page == UI_PAGE_NUMERIC)
  {
    UI_DrawModeIndicator();
    UI_DrawOutputIndicator();
    UI_DrawButton(3U);
  }
}

bool UI_TakeFaultClearRequest(void)
{
  bool pending = s_ui_state.fault_clear_request_pending;
  s_ui_state.fault_clear_request_pending = false;
  return pending;
}

bool UI_TakeCurrentZeroCalibrationRequest(void)
{
  bool pending = s_ui_state.calibration_request_pending;
  s_ui_state.calibration_request_pending = false;
  return pending;
}

void UI_SetCalibrationStatus(UI_CalibrationStatus_t status)
{
  if (s_ui_state.calibration_status == status)
  {
    return;
  }
  s_ui_state.calibration_status = status;
  if (!s_ui_sleeping && s_ui_state.current_page == UI_PAGE_SETTINGS)
  {
    UI_DrawSettingsCalibration();
  }
}

void UI_SetRipple(uint16_t millivolts,bool valid)
{ s_ui_state.ripple_vpp_mv=millivolts; s_ui_state.ripple_valid=valid; }

bool UI_SetScreenSleepSeconds(uint16_t seconds)
{
  if (!UI_SleepSecondsValid(seconds)) { return false; }
  s_ui_state.screen_sleep_seconds = seconds;
  s_ui_last_activity_ms = HAL_GetTick();
  return true;
}
