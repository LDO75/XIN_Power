/**
  ******************************************************************************
  * @file    lcd.h
  * @brief   ST7789液晶屏驱动及轻量级RGB565绘图接口。
  ******************************************************************************
  */

#ifndef __LCD_H__
#define __LCD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH                         320U
#define LCD_HEIGHT                        240U

#define LCD_RGB565(red, green, blue)      \
  ((uint16_t)((((uint16_t)(red) & 0xF8U) << 8U) | \
              (((uint16_t)(green) & 0xFCU) << 3U) | \
              (((uint16_t)(blue)) >> 3U)))

#define LCD_COLOR_BLACK                   0x0000U
#define LCD_COLOR_WHITE                   0xFFFFU
#define LCD_COLOR_RED                     0xF800U
#define LCD_COLOR_GREEN                   0x07E0U
#define LCD_COLOR_BLUE                    0x001FU

typedef enum
{
  LCD_FONT_OXANIUM_SMALL = 0,
  LCD_FONT_OXANIUM_MEDIUM,
  LCD_FONT_OXANIUM_LARGE,
  LCD_FONT_OXANIUM_VALUE,
  LCD_FONT_HEITI_SMALL,
  LCD_FONT_HEITI_MEDIUM
} LCD_FontSize_t;

typedef enum
{
  LCD_ROTATION_LANDSCAPE_0 = 0,
  LCD_ROTATION_LANDSCAPE_180
} LCD_Rotation_t;

/**
 * @brief  将ST7789初始化为320×240横屏模式。
 * @retval 初始化命令均发送成功时返回true。
 */
bool LCD_Init(void);

/**
 * @brief  切换320×240横屏的显示方向。
 * @param  rotation 正向横屏或旋转180度横屏。
 * @retval MADCTL命令发送成功时返回true。
 */
bool LCD_SetRotation(LCD_Rotation_t rotation);

/**
 * @brief  查询当前液晶显示方向。
 * @retval 当前显示方向枚举。
 */
LCD_Rotation_t LCD_GetRotation(void);

/**
 * @brief  设置液晶屏背光亮度。
 * @param  percent 亮度百分比，范围0～100。
 * @retval 无。
 */
void LCD_SetBacklight(uint8_t percent);

/**
 * @brief  使用一种RGB565颜色填充整个屏幕。
 * @param  color RGB565颜色。
 * @retval 无。
 */
void LCD_Clear(uint16_t color);

/**
 * @brief  依次显示红、绿、蓝三种纯色，用于确认LCD基础通信是否正常。
 * @param  hold_time_ms 每种颜色保持的时间，单位为毫秒。
 * @retval 无。
 */
void LCD_RunColorTest(uint32_t hold_time_ms);

/**
 * @brief  填充一个矩形显示区域。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_FillRect(uint16_t x,
                  uint16_t y,
                  uint16_t width,
                  uint16_t height,
                  uint16_t color);

/**
 * @brief  绘制一个实心圆角矩形。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  radius 圆角半径，单位为像素。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_FillRoundRect(uint16_t x,
                       uint16_t y,
                       uint16_t width,
                       uint16_t height,
                       uint8_t radius,
                       uint16_t color);

/**
 * @brief  绘制一个矩形边框。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  color RGB565线条颜色。
 * @retval 无。
 */
void LCD_DrawRect(uint16_t x,
                  uint16_t y,
                  uint16_t width,
                  uint16_t height,
                  uint16_t color);

/**
 * @brief  绘制指定线宽的矩形边框。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  color RGB565线条颜色。
 * @param  thickness 边框线宽，单位为像素。
 * @retval 无。
 */
void LCD_DrawRectThick(uint16_t x,
                       uint16_t y,
                       uint16_t width,
                       uint16_t height,
                       uint16_t color,
                       uint8_t thickness);

/**
 * @brief  绘制一个实心圆。
 * @param  center_x 圆心X坐标。
 * @param  center_y 圆心Y坐标。
 * @param  radius 圆半径，单位为像素。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_FillCircle(uint16_t center_x,
                    uint16_t center_y,
                    uint16_t radius,
                    uint16_t color);

/**
 * @brief  绘制一幅RGB565位图。
 * @param  x 位图左上角X坐标。
 * @param  y 位图左上角Y坐标。
 * @param  width 位图宽度。
 * @param  height 位图高度。
 * @param  pixels 指向CPU字节序RGB565像素数据的指针。
 * @retval 无。
 */
/* Draw a crop without copying or sending unchanged surrounding pixels. */
void LCD_DrawBitmapStrided(uint16_t x,uint16_t y,uint16_t width,uint16_t height,
  const uint16_t *pixels,uint16_t stride);

void LCD_DrawBitmap(uint16_t x,
                    uint16_t y,
                    uint16_t width,
                    uint16_t height,
                    const uint16_t *pixels);

/**
 * @brief  以整数倍缩放绘制一个5×7 ASCII字符。
 * @param  x 字符左上角X坐标。
 * @param  y 字符左上角Y坐标。
 * @param  character ASCII字符。
 * @param  foreground 前景RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @retval 无。
 */
void LCD_DrawChar(uint16_t x,
                  uint16_t y,
                  char character,
                  uint16_t foreground,
                  uint16_t background,
                  uint8_t scale);

/**
 * @brief  绘制以空字符结尾的ASCII字符串。
 * @param  x 字符串左上角X坐标。
 * @param  y 字符串左上角Y坐标。
 * @param  text 字符串指针。
 * @param  foreground 前景RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @retval 无。
 */
void LCD_DrawText(uint16_t x,
                  uint16_t y,
                  const char *text,
                  uint16_t foreground,
                  uint16_t background,
                  uint8_t scale);

/**
 * @brief  使用加粗笔画绘制ASCII字符串。
 * @param  x 字符串左上角X坐标。
 * @param  y 字符串左上角Y坐标。
 * @param  text 字符串指针。
 * @param  foreground 前景RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @retval 无。
 */
void LCD_DrawTextBold(uint16_t x,
                      uint16_t y,
                      const char *text,
                      uint16_t foreground,
                      uint16_t background,
                      uint8_t scale);

/**
 * @brief  使用Oxanium抗锯齿字库绘制字符串。
 * @param  x 字符串行框左上角X坐标。
 * @param  y 字符串行框左上角Y坐标。
 * @param  text 字符串指针。
 * @param  foreground 前景RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  font_size 字库字号。
 * @retval 无。
 */
void LCD_DrawTextFont(uint16_t x,
                      uint16_t y,
                      const char *text,
                      uint16_t foreground,
                      uint16_t background,
                      LCD_FontSize_t font_size);

/**
 * @brief  使用指定颜色填充一块RAM中的RGB565画布。
 * @param  pixels 画布像素缓冲区。
 * @param  width 画布宽度。
 * @param  height 画布高度。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_BufferFill(uint16_t *pixels,
                    uint16_t width,
                    uint16_t height,
                    uint16_t color);

/**
 * @brief  在RAM画布中填充一个矩形区域。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_BufferFillRect(uint16_t *pixels,
                        uint16_t canvas_width,
                        uint16_t canvas_height,
                        uint16_t x,
                        uint16_t y,
                        uint16_t width,
                        uint16_t height,
                        uint16_t color);

/**
 * @brief  在RAM画布中绘制一个实心圆角矩形。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  radius 圆角半径，单位为像素。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_BufferFillRoundRect(uint16_t *pixels,
                             uint16_t canvas_width,
                             uint16_t canvas_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t width,
                             uint16_t height,
                             uint8_t radius,
                             uint16_t color);

/**
 * @brief  在RAM画布中绘制指定线宽的矩形边框。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 矩形左上角X坐标。
 * @param  y 矩形左上角Y坐标。
 * @param  width 矩形宽度。
 * @param  height 矩形高度。
 * @param  color RGB565线条颜色。
 * @param  thickness 边框线宽，单位为像素。
 * @retval 无。
 */
void LCD_BufferDrawRectThick(uint16_t *pixels,
                             uint16_t canvas_width,
                             uint16_t canvas_height,
                             uint16_t x,
                             uint16_t y,
                             uint16_t width,
                             uint16_t height,
                             uint16_t color,
                             uint8_t thickness);

/**
 * @brief  在RAM画布中绘制一个实心圆。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  center_x 圆心X坐标。
 * @param  center_y 圆心Y坐标。
 * @param  radius 圆半径，单位为像素。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
void LCD_BufferFillCircle(uint16_t *pixels,
                          uint16_t canvas_width,
                          uint16_t canvas_height,
                          uint16_t center_x,
                          uint16_t center_y,
                          uint16_t radius,
                          uint16_t color);

/**
 * @brief  在RAM画布中绘制可选加粗的ASCII字符串。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 字符串左上角X坐标。
 * @param  y 字符串左上角Y坐标。
 * @param  text 字符串指针。
 * @param  foreground 前景RGB565颜色。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @param  bold 为true时加粗字符笔画。
 * @retval 无。
 */
void LCD_BufferDrawText(uint16_t *pixels,
                        uint16_t canvas_width,
                        uint16_t canvas_height,
                        uint16_t x,
                        uint16_t y,
                        const char *text,
                        uint16_t foreground,
                        uint8_t scale,
                        bool bold);

/**
 * @brief  在RAM画布中使用Oxanium抗锯齿字库绘制字符串。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x 字符串行框左上角X坐标。
 * @param  y 字符串行框左上角Y坐标。
 * @param  text 字符串指针。
 * @param  foreground 前景RGB565颜色。
 * @param  font_size 字库字号。
 * @retval 无。
 */
void LCD_BufferDrawTextFont(uint16_t *pixels,
                            uint16_t canvas_width,
                            uint16_t canvas_height,
                            uint16_t x,
                            uint16_t y,
                            const char *text,
                            uint16_t foreground,
                            LCD_FontSize_t font_size);

/**
 * @brief  计算LCD_DrawText()绘制字符串时占用的像素宽度。
 * @param  text 字符串指针。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @retval 字符串宽度，单位为像素。
 */
uint16_t LCD_GetTextWidth(const char *text, uint8_t scale);

/**
 * @brief  计算Oxanium字库绘制字符串时占用的像素宽度。
 * @param  text 字符串指针。
 * @param  font_size 字库字号。
 * @retval 字符串宽度，单位为像素。
 */
uint16_t LCD_GetFontTextWidth(const char *text, LCD_FontSize_t font_size);

/**
 * @brief  读取Oxanium字库的行高。
 * @param  font_size 字库字号。
 * @retval 字库行高，单位为像素。
 */
uint8_t LCD_GetFontLineHeight(LCD_FontSize_t font_size);

/* Foreground yield hook; application overrides the weak no-op. */
void LCD_BackgroundService(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H__ */
