/**
  ******************************************************************************
  * @file    lcd.c
  * @brief   ST7789液晶屏驱动及轻量级RGB565绘图功能实现。
  ******************************************************************************
  */

#include "lcd.h"
#include "gpio.h"
#include "spi.h"
#include "tim.h"

#include <stddef.h>
#include <string.h>

#define ST7789_CMD_SWRESET               0x01U
#define ST7789_CMD_SLPOUT                0x11U
#define ST7789_CMD_INVON                 0x21U
#define ST7789_CMD_DISPON                0x29U
#define ST7789_CMD_CASET                 0x2AU
#define ST7789_CMD_RASET                 0x2BU
#define ST7789_CMD_RAMWR                 0x2CU
#define ST7789_CMD_MADCTL                0x36U
#define ST7789_CMD_COLMOD                0x3AU

#define LCD_MADCTL_LANDSCAPE_0           0x60U
#define LCD_MADCTL_LANDSCAPE_180         0xA0U
#define LCD_SPI_TIMEOUT_MS                10U
#define LCD_TRANSFER_BUFFER_SIZE         1024U
#define LCD_FONT_RENDER_MAX_WIDTH        32U
#define LCD_FONT_RENDER_MAX_HEIGHT       40U

#define LCD_CS_LOW()                     HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET)
#define LCD_CS_HIGH()                    HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET)
#define LCD_DC_COMMAND()                 HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_RESET)
#define LCD_DC_DATA()                    HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_SET)
#define LCD_RST_LOW()                    HAL_GPIO_WritePin(LCD_RST_Port, LCD_RST_Pin, GPIO_PIN_RESET)
#define LCD_RST_HIGH()                   HAL_GPIO_WritePin(LCD_RST_Port, LCD_RST_Pin, GPIO_PIN_SET)

__weak void LCD_BackgroundService(void) {}

static bool s_lcd_communication_ok = true;
static LCD_Rotation_t s_lcd_rotation = LCD_ROTATION_LANDSCAPE_0;
static uint8_t s_lcd_transfer_buffer[LCD_TRANSFER_BUFFER_SIZE];
static uint16_t s_lcd_font_render_buffer[LCD_FONT_RENDER_MAX_WIDTH * LCD_FONT_RENDER_MAX_HEIGHT];

typedef struct
{
  uint8_t width;
  uint8_t height;
  int8_t x_offset;
  int8_t y_offset;
  uint8_t advance;
  uint16_t bitmap_offset;
  uint16_t bitmap_size;
} LCD_FontGlyph_t;

typedef struct
{
  const char *characters;
  const LCD_FontGlyph_t *glyphs;
  const uint8_t *bitmap;
  uint8_t glyph_count;
  uint8_t line_height;
  uint8_t tracking;
} LCD_Font_t;

#include "lcd_font_oxanium.inc"

typedef struct { uint16_t code; uint8_t width; uint16_t offset; } LCD_HeitiGlyph_t;
#include "lcd_font_heiti.inc"
static uint16_t LCD_NextUtf8(const char **text)
{
  const uint8_t *p=(const uint8_t *)*text; uint16_t code;
  if (*p<128U) { code=*p; (*text)++; }
  else if ((*p&0xE0U)==0xC0U && p[1] && (p[1]&0xC0U)==0x80U)
  { code=(uint16_t)((p[0]&31U)<<6 | (p[1]&63U)); *text+=2; }
  else if ((*p&0xF0U)==0xE0U && p[1] && p[2] && (p[1]&0xC0U)==0x80U && (p[2]&0xC0U)==0x80U)
  { code=(uint16_t)((p[0]&15U)<<12 | (p[1]&63U)<<6 | (p[2]&63U)); *text+=3; }
  else { code='?'; (*text)++; }
  return code;
}
static const LCD_HeitiGlyph_t *LCD_HeitiGlyph(uint16_t code,LCD_FontSize_t size)
{
  const LCD_HeitiGlyph_t *glyphs=size==LCD_FONT_HEITI_MEDIUM ? s_heiti_18:s_heiti_14;
  unsigned low=0U,high=sizeof(s_heiti_14)/sizeof(s_heiti_14[0]);
  while(low<high) { unsigned mid=(low+high)/2U;
    if(glyphs[mid].code<code){low=mid+1U;}else{high=mid;} }
  if(low<sizeof(s_heiti_14)/sizeof(s_heiti_14[0]) && glyphs[low].code==code){return &glyphs[low];}
  return LCD_HeitiGlyph('?',size);
}
static void LCD_BufferHeiti(uint16_t *pixels,uint16_t width,uint16_t height,
  uint16_t x,uint16_t y,const char *text,uint16_t color,LCD_FontSize_t size)
{
  unsigned line=size==LCD_FONT_HEITI_MEDIUM ? 18U:14U;
  const uint8_t *bits=size==LCD_FONT_HEITI_MEDIUM ? s_heiti_bits_18:s_heiti_bits_14;
  while(*text && x<width)
  {
    const LCD_HeitiGlyph_t *g=LCD_HeitiGlyph(LCD_NextUtf8(&text),size);
    unsigned stride=(g->width+7U)/8U;
    for(unsigned row=0;row<line && y+row<height;row++)
    {
      LCD_BackgroundService();
      for(unsigned col=0;col<g->width && x+col<width;col++)
      { if(bits[g->offset+row*stride+col/8U] & (0x80U>>(col%8U)))
        {pixels[(y+row)*width+x+col]=color;} }
    }
    x=(uint16_t)(x+g->width+1U);
  }
}

static const uint8_t s_font_digits[10][5] =
{
  {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},
  {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},
  {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},
  {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},
  {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},
  {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},
  {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},
  {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},
  {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},
  {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}
};

static const uint8_t s_font_letters[26][5] =
{
  {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU},
  {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U},
  {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U},
  {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU},
  {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U},
  {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U},
  {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU},
  {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU},
  {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U},
  {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U},
  {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U},
  {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U},
  {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU},
  {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU},
  {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU},
  {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U},
  {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU},
  {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U},
  {0x46U, 0x49U, 0x49U, 0x49U, 0x31U},
  {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U},
  {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU},
  {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU},
  {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU},
  {0x63U, 0x14U, 0x08U, 0x14U, 0x63U},
  {0x07U, 0x08U, 0x70U, 0x08U, 0x07U},
  {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}
};

static const uint8_t s_font_space[5] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t s_font_minus[5] = {0x08U, 0x08U, 0x08U, 0x08U, 0x08U};
static const uint8_t s_font_plus[5] = {0x08U, 0x08U, 0x3EU, 0x08U, 0x08U};
static const uint8_t s_font_dot[5] = {0x00U, 0x60U, 0x60U, 0x00U, 0x00U};
static const uint8_t s_font_colon[5] = {0x00U, 0x36U, 0x36U, 0x00U, 0x00U};
static const uint8_t s_font_left_parenthesis[5] = {0x00U, 0x1CU, 0x22U, 0x41U, 0x00U};
static const uint8_t s_font_right_parenthesis[5] = {0x00U, 0x41U, 0x22U, 0x1CU, 0x00U};
static const uint8_t s_font_slash[5] = {0x20U, 0x10U, 0x08U, 0x04U, 0x02U};
static const uint8_t s_font_degree[5] = {0x06U, 0x09U, 0x09U, 0x06U, 0x00U};
static const uint8_t s_font_question[5] = {0x02U, 0x01U, 0x51U, 0x09U, 0x06U};

static HAL_StatusTypeDef LCD_Transmit(uint8_t *data, uint16_t size);
static void LCD_WriteCommand(uint8_t command, const uint8_t *parameters, uint16_t parameter_count);
static bool LCD_BeginMemoryWrite(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static void LCD_EndMemoryWrite(void);
static const uint8_t *LCD_GetGlyph(char character);
static bool LCD_GlyphPixelIsSet(const uint8_t *glyph,
                                uint16_t glyph_x,
                                uint16_t glyph_y,
                                bool bold);
static const LCD_Font_t *LCD_GetFont(LCD_FontSize_t font_size);
static const LCD_FontGlyph_t *LCD_FindFontGlyph(const LCD_Font_t *font,
                                                 char character);
static uint8_t LCD_GetFontAlpha(const LCD_Font_t *font,
                                const LCD_FontGlyph_t *glyph,
                                uint16_t pixel_index);
static uint16_t LCD_BlendRgb565(uint16_t background,
                                uint16_t foreground,
                                uint8_t alpha);
static void LCD_DrawCharStyled(uint16_t x,
                               uint16_t y,
                               char character,
                               uint16_t foreground,
                               uint16_t background,
                               uint8_t scale,
                               bool bold);
static void LCD_FillHorizontalSpan(int32_t x0, int32_t x1, int32_t y, uint16_t color);
static void LCD_BufferFillHorizontalSpan(uint16_t *pixels,
                                         uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         int32_t x0,
                                         int32_t x1,
                                         int32_t y,
                                         uint16_t color);

/**
 * @brief  使用SPI1阻塞发送命令或像素数据。
 * @param  data 待发送字节缓冲区。
 * @param  size 待发送字节数。
 * @retval HAL发送状态。
 * @note   首版硬件点屏阶段故意不使用DMA，先排除DMA请求映射和中断配置的影响。
 */
static HAL_StatusTypeDef LCD_Transmit(uint8_t *data, uint16_t size)
{
  /* A failed bus must not cost another timeout for every glyph/pixel region. */
  if (!s_lcd_communication_ok) { return HAL_ERROR; }
  if ((data == NULL) || (size == 0U))
  {
    return HAL_OK;
  }

  while (size != 0U)
  {
    uint16_t chunk = size > 128U ? 128U : size;
    LCD_BackgroundService();
    HAL_StatusTypeDef result = HAL_SPI_Transmit(&hspi1, data, chunk, LCD_SPI_TIMEOUT_MS);
    if (result != HAL_OK) { return result; }
    data += chunk;
    size -= chunk;
  }
  LCD_BackgroundService();
  return HAL_OK;
}

/**
 * @brief  向ST7789写入一条命令及其参数。
 * @param  command ST7789命令字节。
 * @param  parameters 可选的参数缓冲区。
 * @param  parameter_count 参数字节数。
 * @retval 无。
 */
static void LCD_WriteCommand(uint8_t command, const uint8_t *parameters, uint16_t parameter_count)
{
  HAL_StatusTypeDef status;

  LCD_CS_LOW();
  LCD_DC_COMMAND();
  status = LCD_Transmit(&command, 1U);

  if ((status == HAL_OK) && (parameters != NULL) && (parameter_count > 0U))
  {
    LCD_DC_DATA();
    status = LCD_Transmit((uint8_t *)parameters, parameter_count);
  }

  LCD_CS_HIGH();
  if (status != HAL_OK)
  {
    s_lcd_communication_ok = false;
  }
}

/**
 * @brief  配置ST7789绘图窗口并进入显存写入模式。
 * @param  x 窗口左上角X坐标。
 * @param  y 窗口左上角Y坐标。
 * @param  width 窗口宽度。
 * @param  height 窗口高度。
 * @retval 窗口有效时返回true。
 */
static bool LCD_BeginMemoryWrite(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
  uint16_t x_end;
  uint16_t y_end;
  uint8_t address_data[4];
  uint8_t command;

  if ((width == 0U) || (height == 0U) || (x >= LCD_WIDTH) || (y >= LCD_HEIGHT))
  {
    return false;
  }

  if (width > (LCD_WIDTH - x))
  {
    width = LCD_WIDTH - x;
  }
  if (height > (LCD_HEIGHT - y))
  {
    height = LCD_HEIGHT - y;
  }

  x_end = (uint16_t)(x + width - 1U);
  y_end = (uint16_t)(y + height - 1U);

  address_data[0] = (uint8_t)(x >> 8U);
  address_data[1] = (uint8_t)x;
  address_data[2] = (uint8_t)(x_end >> 8U);
  address_data[3] = (uint8_t)x_end;
  LCD_WriteCommand(ST7789_CMD_CASET, address_data, sizeof(address_data));

  address_data[0] = (uint8_t)(y >> 8U);
  address_data[1] = (uint8_t)y;
  address_data[2] = (uint8_t)(y_end >> 8U);
  address_data[3] = (uint8_t)y_end;
  LCD_WriteCommand(ST7789_CMD_RASET, address_data, sizeof(address_data));

  command = ST7789_CMD_RAMWR;
  LCD_CS_LOW();
  LCD_DC_COMMAND();
  if (LCD_Transmit(&command, 1U) != HAL_OK)
  {
    LCD_CS_HIGH();
    s_lcd_communication_ok = false;
    return false;
  }
  LCD_DC_DATA();
  return true;
}

/**
 * @brief  结束一次ST7789显存写入事务。
 * @retval 无。
 */
static void LCD_EndMemoryWrite(void)
{
  LCD_CS_HIGH();
}

/**
 * @brief  获取一个受支持ASCII字符的5×7点阵。
 * @param  character 待查询字符。
 * @retval 指向5列点阵数据的指针。
 */
static const uint8_t *LCD_GetGlyph(char character)
{
  if ((character >= '0') && (character <= '9'))
  {
    return s_font_digits[(uint8_t)character - (uint8_t)'0'];
  }

  if ((character >= 'a') && (character <= 'z'))
  {
    character = (char)(character - ('a' - 'A'));
  }
  if ((character >= 'A') && (character <= 'Z'))
  {
    return s_font_letters[(uint8_t)character - (uint8_t)'A'];
  }

  switch (character)
  {
    case ' ': return s_font_space;
    case '-': return s_font_minus;
    case '+': return s_font_plus;
    case '.': return s_font_dot;
    case ':': return s_font_colon;
    case '(': return s_font_left_parenthesis;
    case ')': return s_font_right_parenthesis;
    case '/': return s_font_slash;
    case '^': return s_font_degree;
    default:  return s_font_question;
  }
}

/**
 * @brief  判断字模中的指定像素是否需要显示，并可横向加粗一列。
 * @param  glyph 指向5列字模数据的指针。
 * @param  glyph_x 字模X坐标。
 * @param  glyph_y 字模Y坐标。
 * @param  bold 为true时启用横向加粗。
 * @retval 该像素需要显示时返回true。
 */
static bool LCD_GlyphPixelIsSet(const uint8_t *glyph,
                                uint16_t glyph_x,
                                uint16_t glyph_y,
                                bool bold)
{
  bool pixel_set = false;

  if ((glyph == NULL) || (glyph_y >= 7U))
  {
    return false;
  }

  if ((glyph_x < 5U) &&
      ((glyph[glyph_x] & (uint8_t)(1U << glyph_y)) != 0U))
  {
    pixel_set = true;
  }

  if (bold && (glyph_x > 0U) && (glyph_x <= 5U) &&
      ((glyph[glyph_x - 1U] & (uint8_t)(1U << glyph_y)) != 0U))
  {
    pixel_set = true;
  }

  return pixel_set;
}

/**
 * @brief  根据字号选择Oxanium字库描述结构。
 * @param  font_size 字库字号。
 * @retval 对应字库描述结构的指针。
 */
static const LCD_Font_t *LCD_GetFont(LCD_FontSize_t font_size)
{
  if (font_size == LCD_FONT_OXANIUM_MEDIUM)
  {
    return &s_oxanium_medium_font;
  }
  if (font_size == LCD_FONT_OXANIUM_LARGE)
  {
    return &s_oxanium_large_font;
  }
  if (font_size == LCD_FONT_OXANIUM_VALUE)
  {
    return &s_oxanium_value_font;
  }
  return &s_oxanium_small_font;
}

/**
 * @brief  在指定Oxanium字库中查找字符的字形描述。
 * @param  font 字库描述结构。
 * @param  character 待查找ASCII字符。
 * @retval 字形描述指针；字符不存在时返回问号字形。
 */
static const LCD_FontGlyph_t *LCD_FindFontGlyph(const LCD_Font_t *font,
                                                 char character)
{
  uint8_t index;
  const LCD_FontGlyph_t *fallback = NULL;

  if (font == NULL)
  {
    return NULL;
  }

  for (index = 0U; index < font->glyph_count; index++)
  {
    if (font->characters[index] == '?')
    {
      fallback = &font->glyphs[index];
    }
    if (font->characters[index] == character)
    {
      return &font->glyphs[index];
    }
  }
  return fallback;
}

/**
 * @brief  读取4bpp字形中指定像素的透明度。
 * @param  font 字库描述结构。
 * @param  glyph 字形描述结构。
 * @param  pixel_index 字形紧凑位图中的像素索引。
 * @retval 0～15范围的透明度。
 */
static uint8_t LCD_GetFontAlpha(const LCD_Font_t *font,
                                const LCD_FontGlyph_t *glyph,
                                uint16_t pixel_index)
{
  uint8_t packed_alpha;

  if ((font == NULL) || (glyph == NULL) ||
      ((pixel_index / 2U) >= glyph->bitmap_size))
  {
    return 0U;
  }

  packed_alpha = font->bitmap[glyph->bitmap_offset + (pixel_index / 2U)];
  return ((pixel_index & 1U) == 0U) ?
         (uint8_t)(packed_alpha >> 4U) : (uint8_t)(packed_alpha & 0x0FU);
}

/**
 * @brief  按4位透明度混合两个RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  foreground 前景RGB565颜色。
 * @param  alpha 0～15范围的前景透明度。
 * @retval 混合后的RGB565颜色。
 */
static uint16_t LCD_BlendRgb565(uint16_t background,
                                uint16_t foreground,
                                uint8_t alpha)
{
  uint16_t background_red;
  uint16_t background_green;
  uint16_t background_blue;
  uint16_t foreground_red;
  uint16_t foreground_green;
  uint16_t foreground_blue;
  uint16_t red;
  uint16_t green;
  uint16_t blue;
  uint16_t inverse_alpha;

  if (alpha == 0U)
  {
    return background;
  }
  if (alpha >= 15U)
  {
    return foreground;
  }

  inverse_alpha = (uint16_t)(15U - alpha);
  background_red = (uint16_t)((background >> 11U) & 0x1FU);
  background_green = (uint16_t)((background >> 5U) & 0x3FU);
  background_blue = (uint16_t)(background & 0x1FU);
  foreground_red = (uint16_t)((foreground >> 11U) & 0x1FU);
  foreground_green = (uint16_t)((foreground >> 5U) & 0x3FU);
  foreground_blue = (uint16_t)(foreground & 0x1FU);

  red = (uint16_t)(((background_red * inverse_alpha) +
                    (foreground_red * alpha) + 7U) / 15U);
  green = (uint16_t)(((background_green * inverse_alpha) +
                      (foreground_green * alpha) + 7U) / 15U);
  blue = (uint16_t)(((background_blue * inverse_alpha) +
                     (foreground_blue * alpha) + 7U) / 15U);
  return (uint16_t)((red << 11U) | (green << 5U) | blue);
}

/**
 * @brief  绘制一条经过屏幕边界裁剪的水平线段。
 * @param  x0 起始X坐标。
 * @param  x1 结束X坐标。
 * @param  y Y坐标。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
static void LCD_FillHorizontalSpan(int32_t x0, int32_t x1, int32_t y, uint16_t color)
{
  if ((y < 0) || (y >= (int32_t)LCD_HEIGHT) || (x1 < 0) || (x0 >= (int32_t)LCD_WIDTH))
  {
    return;
  }

  if (x0 < 0)
  {
    x0 = 0;
  }
  if (x1 >= (int32_t)LCD_WIDTH)
  {
    x1 = (int32_t)LCD_WIDTH - 1;
  }
  if (x1 >= x0)
  {
    LCD_FillRect((uint16_t)x0, (uint16_t)y, (uint16_t)(x1 - x0 + 1), 1U, color);
  }
}

/**
 * @brief  在RAM画布中绘制一条经过边界裁剪的水平线段。
 * @param  pixels 画布像素缓冲区。
 * @param  canvas_width 画布宽度。
 * @param  canvas_height 画布高度。
 * @param  x0 起始X坐标。
 * @param  x1 结束X坐标。
 * @param  y Y坐标。
 * @param  color RGB565填充颜色。
 * @retval 无。
 */
static void LCD_BufferFillHorizontalSpan(uint16_t *pixels,
                                         uint16_t canvas_width,
                                         uint16_t canvas_height,
                                         int32_t x0,
                                         int32_t x1,
                                         int32_t y,
                                         uint16_t color)
{
  if ((pixels == NULL) || (y < 0) || (y >= (int32_t)canvas_height) ||
      (x1 < 0) || (x0 >= (int32_t)canvas_width))
  {
    return;
  }

  if (x0 < 0)
  {
    x0 = 0;
  }
  if (x1 >= (int32_t)canvas_width)
  {
    x1 = (int32_t)canvas_width - 1;
  }
  if (x1 >= x0)
  {
    LCD_BufferFillRect(pixels,
                       canvas_width,
                       canvas_height,
                       (uint16_t)x0,
                       (uint16_t)y,
                       (uint16_t)(x1 - x0 + 1),
                       1U,
                       color);
  }
}

/**
 * @brief  将ST7789初始化为320×240横屏模式。
 * @retval 初始化命令均发送成功时返回true。
 */
bool LCD_Init(void)
{
  static const uint8_t porch_setting[] = {0x05U, 0x05U, 0x00U, 0x33U, 0x33U};
  static const uint8_t power_control[] = {0xA4U, 0xA1U};
  static const uint8_t equalize_time[] = {0x09U, 0x09U, 0x08U};
  static const uint8_t gamma_positive[] =
  {
    0xD0U, 0x05U, 0x09U, 0x09U, 0x08U, 0x14U, 0x28U,
    0x33U, 0x3FU, 0x07U, 0x13U, 0x14U, 0x28U, 0x30U
  };
  static const uint8_t gamma_negative[] =
  {
    0xD0U, 0x05U, 0x09U, 0x09U, 0x08U, 0x03U, 0x24U,
    0x32U, 0x32U, 0x3BU, 0x14U, 0x13U, 0x28U, 0x2FU
  };
  uint8_t value;

  s_lcd_communication_ok = true;
  LCD_CS_HIGH();
  LCD_DC_DATA();
  LCD_SetBacklight(0U);

  LCD_RST_LOW();
  HAL_Delay(10U);
  LCD_RST_HIGH();
  HAL_Delay(120U);

  LCD_WriteCommand(ST7789_CMD_SWRESET, NULL, 0U);
  HAL_Delay(120U);
  LCD_WriteCommand(ST7789_CMD_SLPOUT, NULL, 0U);
  HAL_Delay(120U);

  value = 0x05U;
  LCD_WriteCommand(ST7789_CMD_COLMOD, &value, 1U);
  value = 0x1AU;
  LCD_WriteCommand(0xC5U, &value, 1U);
  value = LCD_MADCTL_LANDSCAPE_0;
  LCD_WriteCommand(ST7789_CMD_MADCTL, &value, 1U);
  s_lcd_rotation = LCD_ROTATION_LANDSCAPE_0;

  LCD_WriteCommand(0xB2U, porch_setting, sizeof(porch_setting));
  value = 0x05U;
  LCD_WriteCommand(0xB7U, &value, 1U);
  value = 0x3FU;
  LCD_WriteCommand(0xBBU, &value, 1U);
  value = 0x2CU;
  LCD_WriteCommand(0xC0U, &value, 1U);
  value = 0x01U;
  LCD_WriteCommand(0xC2U, &value, 1U);
  value = 0x0FU;
  LCD_WriteCommand(0xC3U, &value, 1U);
  value = 0x20U;
  LCD_WriteCommand(0xC4U, &value, 1U);
  value = 0x01U;
  LCD_WriteCommand(0xC6U, &value, 1U);
  LCD_WriteCommand(0xD0U, power_control, sizeof(power_control));
  value = 0x03U;
  LCD_WriteCommand(0xE8U, &value, 1U);
  LCD_WriteCommand(0xE9U, equalize_time, sizeof(equalize_time));
  LCD_WriteCommand(0xE0U, gamma_positive, sizeof(gamma_positive));
  LCD_WriteCommand(0xE1U, gamma_negative, sizeof(gamma_negative));

  /* 此批屏在INVOFF下将RGB565颜色整屏反相：橙红变青、黄绿变紫。 */
  LCD_WriteCommand(ST7789_CMD_INVON, NULL, 0U);
  HAL_Delay(120U);
  LCD_WriteCommand(ST7789_CMD_DISPON, NULL, 0U);
  HAL_Delay(120U);

  LCD_Clear(LCD_COLOR_BLACK);
  LCD_SetBacklight(80U);
  return s_lcd_communication_ok;
}

/**
 * @brief  切换320×240横屏的显示方向。
 * @param  rotation 正向横屏或旋转180度横屏。
 * @retval MADCTL命令发送成功时返回true。
 */
bool LCD_SetRotation(LCD_Rotation_t rotation)
{
  uint8_t value;
  bool previous_communication_state;
  bool command_succeeded;

  if ((rotation != LCD_ROTATION_LANDSCAPE_0) &&
      (rotation != LCD_ROTATION_LANDSCAPE_180))
  {
    return false;
  }
  if (rotation == s_lcd_rotation)
  {
    return true;
  }

  value = (rotation == LCD_ROTATION_LANDSCAPE_180) ?
          LCD_MADCTL_LANDSCAPE_180 : LCD_MADCTL_LANDSCAPE_0;
  previous_communication_state = s_lcd_communication_ok;
  s_lcd_communication_ok = true;
  LCD_WriteCommand(ST7789_CMD_MADCTL, &value, 1U);
  command_succeeded = s_lcd_communication_ok;
  if (command_succeeded)
  {
    s_lcd_rotation = rotation;
  }
  s_lcd_communication_ok = previous_communication_state && command_succeeded;
  return command_succeeded;
}

/**
 * @brief  查询当前液晶显示方向。
 * @retval 当前显示方向枚举。
 */
LCD_Rotation_t LCD_GetRotation(void)
{
  return s_lcd_rotation;
}

/**
 * @brief  设置液晶屏背光亮度。
 * @param  percent 亮度百分比，范围0～100。
 * @retval 无。
 */
void LCD_SetBacklight(uint8_t percent)
{
  uint32_t pulse;

  if (percent > 100U)
  {
    percent = 100U;
  }

  (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  pulse = ((__HAL_TIM_GET_AUTORELOAD(&htim1) + 1U) * percent) / 100U;
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pulse);
}

/**
 * @brief  使用一种RGB565颜色填充整个屏幕。
 * @param  color RGB565颜色。
 * @retval 无。
 */
void LCD_Clear(uint16_t color)
{
  LCD_FillRect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

/**
 * @brief  依次显示红、绿、蓝三种纯色，用于确认LCD基础通信是否正常。
 * @param  hold_time_ms 每种颜色保持的时间，单位为毫秒。
 * @retval 无。
 * @note   本函数只属于上电点屏测试，确认硬件正常后可在main.c中取消调用。
 */
void LCD_RunColorTest(uint32_t hold_time_ms)
{
  LCD_Clear(LCD_COLOR_RED);
  HAL_Delay(hold_time_ms);
  LCD_Clear(LCD_COLOR_GREEN);
  HAL_Delay(hold_time_ms);
  LCD_Clear(LCD_COLOR_BLUE);
  HAL_Delay(hold_time_ms);
}

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
                  uint16_t color)
{
  uint32_t remaining_pixels;
  uint16_t chunk_pixels;
  uint16_t index;
  uint16_t maximum_chunk_pixels;
  uint8_t color_high;
  uint8_t color_low;

  if ((width == 0U) || (height == 0U) || (x >= LCD_WIDTH) || (y >= LCD_HEIGHT))
  {
    return;
  }
  if (width > (LCD_WIDTH - x))
  {
    width = LCD_WIDTH - x;
  }
  if (height > (LCD_HEIGHT - y))
  {
    height = LCD_HEIGHT - y;
  }

  color_high = (uint8_t)(color >> 8U);
  color_low = (uint8_t)color;
  maximum_chunk_pixels = (uint16_t)(sizeof(s_lcd_transfer_buffer) / 2U);
  for (index = 0U; index < maximum_chunk_pixels; index++)
  {
    s_lcd_transfer_buffer[index * 2U] = color_high;
    s_lcd_transfer_buffer[index * 2U + 1U] = color_low;
  }

  if (!LCD_BeginMemoryWrite(x, y, width, height))
  {
    return;
  }

  remaining_pixels = (uint32_t)width * height;
  while (remaining_pixels > 0U)
  {
    chunk_pixels = (remaining_pixels > maximum_chunk_pixels) ?
                   maximum_chunk_pixels : (uint16_t)remaining_pixels;
    if (LCD_Transmit(s_lcd_transfer_buffer, (uint16_t)(chunk_pixels * 2U)) != HAL_OK)
    {
      s_lcd_communication_ok = false;
      break;
    }
    remaining_pixels -= chunk_pixels;
  }

  LCD_EndMemoryWrite();
}

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
                       uint16_t color)
{
  uint8_t maximum_radius;

  if ((width == 0U) || (height == 0U))
  {
    return;
  }

  maximum_radius = (uint8_t)(((width < height) ? width : height) / 2U);
  if (radius > maximum_radius)
  {
    radius = maximum_radius;
  }
  if (radius == 0U)
  {
    LCD_FillRect(x, y, width, height, color);
    return;
  }

  LCD_FillRect((uint16_t)(x + radius),
               y,
               (uint16_t)(width - (uint16_t)(radius * 2U)),
               height,
               color);
  LCD_FillRect(x,
               (uint16_t)(y + radius),
               width,
               (uint16_t)(height - (uint16_t)(radius * 2U)),
               color);
  LCD_FillCircle((uint16_t)(x + radius),
                 (uint16_t)(y + radius),
                 radius,
                 color);
  LCD_FillCircle((uint16_t)(x + width - radius - 1U),
                 (uint16_t)(y + radius),
                 radius,
                 color);
  LCD_FillCircle((uint16_t)(x + radius),
                 (uint16_t)(y + height - radius - 1U),
                 radius,
                 color);
  LCD_FillCircle((uint16_t)(x + width - radius - 1U),
                 (uint16_t)(y + height - radius - 1U),
                 radius,
                 color);
}

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
                  uint16_t color)
{
  LCD_DrawRectThick(x, y, width, height, color, 1U);
}

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
                       uint8_t thickness)
{
  if ((width == 0U) || (height == 0U))
  {
    return;
  }

  if (thickness == 0U)
  {
    thickness = 1U;
  }
  if (thickness > (width / 2U))
  {
    thickness = (uint8_t)((width + 1U) / 2U);
  }
  if (thickness > (height / 2U))
  {
    thickness = (uint8_t)((height + 1U) / 2U);
  }

  LCD_FillRect(x, y, width, thickness, color);
  LCD_FillRect(x,
               (uint16_t)(y + height - thickness),
               width,
               thickness,
               color);

  if (height > (uint16_t)(thickness * 2U))
  {
    LCD_FillRect(x,
                 (uint16_t)(y + thickness),
                 thickness,
                 (uint16_t)(height - (uint16_t)(thickness * 2U)),
                 color);
    LCD_FillRect((uint16_t)(x + width - thickness),
                 (uint16_t)(y + thickness),
                 thickness,
                 (uint16_t)(height - (uint16_t)(thickness * 2U)),
                 color);
  }
}

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
                    uint16_t color)
{
  int32_t x = radius;
  int32_t y = 0;
  int32_t error = 1 - (int32_t)radius;
  int32_t cx = center_x;
  int32_t cy = center_y;

  while (x >= y)
  {
    LCD_BackgroundService();
    LCD_FillHorizontalSpan(cx - x, cx + x, cy + y, color);
    LCD_FillHorizontalSpan(cx - x, cx + x, cy - y, color);
    LCD_FillHorizontalSpan(cx - y, cx + y, cy + x, color);
    LCD_FillHorizontalSpan(cx - y, cx + y, cy - x, color);

    y++;
    if (error < 0)
    {
      error += (2 * y) + 1;
    }
    else
    {
      x--;
      error += (2 * (y - x)) + 1;
    }
  }
}

/**
 * @brief  绘制一幅RGB565位图。
 * @param  x 位图左上角X坐标。
 * @param  y 位图左上角Y坐标。
 * @param  width 位图宽度。
 * @param  height 位图高度。
 * @param  pixels 指向CPU字节序RGB565像素数据的指针。
 * @retval 无。
 */
void LCD_DrawBitmap(uint16_t x,uint16_t y,uint16_t width,uint16_t height,const uint16_t *pixels)
{LCD_DrawBitmapStrided(x,y,width,height,pixels,width);}

void LCD_DrawBitmapStrided(uint16_t x,
                    uint16_t y,
                    uint16_t width,
                    uint16_t height,
                    const uint16_t *pixels,uint16_t stride)
{
  uint16_t source_width;
  uint16_t draw_width;
  uint16_t draw_height;
  uint16_t row;
  uint16_t column;
  uint16_t chunk_pixels;
  uint16_t index;
  uint16_t pixel;
  uint16_t maximum_chunk_pixels;

  if ((pixels == NULL) || (width == 0U) || (height == 0U) ||
      (x >= LCD_WIDTH) || (y >= LCD_HEIGHT))
  {
    return;
  }

  source_width = stride;
  if(stride<width){return;}
  draw_width = (width > (LCD_WIDTH - x)) ? (LCD_WIDTH - x) : width;
  draw_height = (height > (LCD_HEIGHT - y)) ? (LCD_HEIGHT - y) : height;
  maximum_chunk_pixels = (uint16_t)(sizeof(s_lcd_transfer_buffer) / 2U);

  if (!LCD_BeginMemoryWrite(x, y, draw_width, draw_height))
  {
    return;
  }

  for (row = 0U; row < draw_height; row++)
  {
    LCD_BackgroundService();
    column = 0U;
    while (column < draw_width)
    {
      chunk_pixels = (uint16_t)(draw_width - column);
      if (chunk_pixels > maximum_chunk_pixels)
      {
        chunk_pixels = maximum_chunk_pixels;
      }

      for (index = 0U; index < chunk_pixels; index++)
      {
        pixel = pixels[(uint32_t)row * source_width + column + index];
        s_lcd_transfer_buffer[index * 2U] = (uint8_t)(pixel >> 8U);
        s_lcd_transfer_buffer[index * 2U + 1U] = (uint8_t)pixel;
      }

      if (LCD_Transmit(s_lcd_transfer_buffer, (uint16_t)(chunk_pixels * 2U)) != HAL_OK)
      {
        s_lcd_communication_ok = false;
        LCD_EndMemoryWrite();
        return;
      }
      column = (uint16_t)(column + chunk_pixels);
    }
  }

  LCD_EndMemoryWrite();
}

/**
 * @brief  使用指定笔画样式绘制一个5×7 ASCII字符。
 * @param  x 字符左上角X坐标。
 * @param  y 字符左上角Y坐标。
 * @param  character ASCII字符。
 * @param  foreground 前景RGB565颜色。
 * @param  background 背景RGB565颜色。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @param  bold 为true时加粗字符笔画。
 * @retval 无。
 */
static void LCD_DrawCharStyled(uint16_t x,
                               uint16_t y,
                               char character,
                               uint16_t foreground,
                               uint16_t background,
                               uint8_t scale,
                               bool bold)
{
  const uint8_t *glyph;
  uint16_t character_width;
  uint16_t character_height;
  uint16_t pixel_x;
  uint16_t pixel_y;
  uint16_t glyph_x;
  uint16_t glyph_y;
  uint16_t color;
  uint32_t buffer_index;

  if (scale == 0U)
  {
    scale = 1U;
  }
  if (scale > 3U)
  {
    scale = 3U;
  }

  character_width = (uint16_t)(6U * scale);
  character_height = (uint16_t)(8U * scale);
  if (((uint32_t)character_width * character_height * 2U) > sizeof(s_lcd_transfer_buffer))
  {
    return;
  }

  glyph = LCD_GetGlyph(character);
  buffer_index = 0U;
  for (pixel_y = 0U; pixel_y < character_height; pixel_y++)
  {
    glyph_y = (uint16_t)(pixel_y / scale);
    for (pixel_x = 0U; pixel_x < character_width; pixel_x++)
    {
      glyph_x = (uint16_t)(pixel_x / scale);
      color = background;
      if (LCD_GlyphPixelIsSet(glyph, glyph_x, glyph_y, bold))
      {
        color = foreground;
      }

      s_lcd_transfer_buffer[buffer_index++] = (uint8_t)(color >> 8U);
      s_lcd_transfer_buffer[buffer_index++] = (uint8_t)color;
    }
  }

  if (LCD_BeginMemoryWrite(x, y, character_width, character_height))
  {
    if (LCD_Transmit(s_lcd_transfer_buffer, (uint16_t)buffer_index) != HAL_OK)
    {
      s_lcd_communication_ok = false;
    }
    LCD_EndMemoryWrite();
  }
}

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
                  uint8_t scale)
{
  LCD_DrawCharStyled(x,
                     y,
                     character,
                     foreground,
                     background,
                     scale,
                     false);
}

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
                  uint8_t scale)
{
  uint16_t cursor_x = x;
  uint16_t character_width;

  if (text == NULL)
  {
    return;
  }
  if (scale == 0U)
  {
    scale = 1U;
  }
  if (scale > 3U)
  {
    scale = 3U;
  }
  character_width = (uint16_t)(6U * scale);

  while (*text != '\0')
  {
    if ((cursor_x + character_width) > LCD_WIDTH)
    {
      break;
    }
    LCD_DrawCharStyled(cursor_x,
                       y,
                       *text,
                       foreground,
                       background,
                       scale,
                       false);
    cursor_x = (uint16_t)(cursor_x + character_width);
    text++;
  }
}

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
                      uint8_t scale)
{
  uint16_t cursor_x = x;
  uint16_t character_width;

  if (text == NULL)
  {
    return;
  }
  if (scale == 0U)
  {
    scale = 1U;
  }
  if (scale > 3U)
  {
    scale = 3U;
  }
  character_width = (uint16_t)(6U * scale);

  while (*text != '\0')
  {
    if ((cursor_x + character_width) > LCD_WIDTH)
    {
      break;
    }
    LCD_DrawCharStyled(cursor_x,
                       y,
                       *text,
                       foreground,
                       background,
                       scale,
                       true);
    cursor_x = (uint16_t)(cursor_x + character_width);
    text++;
  }
}

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
                      LCD_FontSize_t font_size)
{
  if(font_size==LCD_FONT_HEITI_SMALL || font_size==LCD_FONT_HEITI_MEDIUM)
  {
    if(!text){return;} uint8_t line=LCD_GetFontLineHeight(font_size);
    while(*text)
    {
      const char *begin=text; const LCD_HeitiGlyph_t *g=LCD_HeitiGlyph(LCD_NextUtf8(&text),font_size);
      char one[4]={0}; size_t length=(size_t)(text-begin); if(length>3U){length=3U;}
      (void)memcpy(one,begin,length); uint16_t advance=g->width+1U;
      if(x+advance>LCD_WIDTH){break;}
      LCD_BufferFill(s_lcd_font_render_buffer,advance,line,background);
      LCD_BufferHeiti(s_lcd_font_render_buffer,advance,line,0,0,one,foreground,font_size);
      LCD_DrawBitmap(x,y,advance,line,s_lcd_font_render_buffer); x+=advance;
    }
    return;
  }

  const LCD_Font_t *font;
  const LCD_FontGlyph_t *glyph;
  uint16_t cursor_x;
  uint16_t row;
  uint16_t column;
  int16_t glyph_x;
  int16_t glyph_y;
  uint16_t glyph_pixel_index;
  uint8_t alpha;
  uint32_t pixel_index;

  if (text == NULL)
  {
    return;
  }

  font = LCD_GetFont(font_size);
  cursor_x = x;
  while (*text != '\0')
  {
    glyph = LCD_FindFontGlyph(font, *text);
    if ((glyph == NULL) ||
        (glyph->advance > LCD_FONT_RENDER_MAX_WIDTH) ||
        (font->line_height > LCD_FONT_RENDER_MAX_HEIGHT) ||
        ((cursor_x + glyph->advance) > LCD_WIDTH))
    {
      break;
    }

    LCD_BufferFill(s_lcd_font_render_buffer,
                   glyph->advance,
                   font->line_height,
                   background);
    for (row = 0U; row < glyph->height; row++)
    {
      LCD_BackgroundService();
      for (column = 0U; column < glyph->width; column++)
      {
        glyph_x = (int16_t)glyph->x_offset + (int16_t)column;
        glyph_y = (int16_t)glyph->y_offset + (int16_t)row;
        if ((glyph_x < 0) || (glyph_y < 0) ||
            (glyph_x >= glyph->advance) || (glyph_y >= font->line_height))
        {
          continue;
        }
        glyph_pixel_index = (uint16_t)((row * glyph->width) + column);
        alpha = LCD_GetFontAlpha(font, glyph, glyph_pixel_index);
        pixel_index = ((uint32_t)glyph_y * glyph->advance) + (uint16_t)glyph_x;
        s_lcd_font_render_buffer[pixel_index] =
          LCD_BlendRgb565(s_lcd_font_render_buffer[pixel_index], foreground, alpha);
      }
    }

    LCD_DrawBitmap(cursor_x,
                   y,
                   glyph->advance,
                   font->line_height,
                   s_lcd_font_render_buffer);
    cursor_x = (uint16_t)(cursor_x + glyph->advance);
    text++;
  }
}

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
                    uint16_t color)
{
  uint32_t index;
  uint32_t total_pixels;

  if (pixels == NULL)
  {
    return;
  }

  total_pixels = (uint32_t)width * height;
  for (index = 0U; index < total_pixels; index++)
  {
    if ((index & 255U) == 0U) { LCD_BackgroundService(); }
    pixels[index] = color;
  }
}

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
                        uint16_t color)
{
  uint16_t row;
  uint16_t column;
  uint16_t draw_width;
  uint16_t draw_height;
  uint32_t row_offset;

  if ((pixels == NULL) || (width == 0U) || (height == 0U) ||
      (x >= canvas_width) || (y >= canvas_height))
  {
    return;
  }

  draw_width = (width > (canvas_width - x)) ? (canvas_width - x) : width;
  draw_height = (height > (canvas_height - y)) ? (canvas_height - y) : height;

  for (row = 0U; row < draw_height; row++)
  {
    LCD_BackgroundService();
    row_offset = ((uint32_t)y + row) * canvas_width + x;
    for (column = 0U; column < draw_width; column++)
    {
      pixels[row_offset + column] = color;
    }
  }
}

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
                             uint16_t color)
{
  uint8_t maximum_radius;

  if ((pixels == NULL) || (width == 0U) || (height == 0U))
  {
    return;
  }

  maximum_radius = (uint8_t)(((width < height) ? width : height) / 2U);
  if (radius > maximum_radius)
  {
    radius = maximum_radius;
  }
  if (radius == 0U)
  {
    LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                       x, y, width, height, color);
    return;
  }

  LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                     (uint16_t)(x + radius), y,
                     (uint16_t)(width - (uint16_t)(radius * 2U)), height, color);
  LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                     x, (uint16_t)(y + radius),
                     width, (uint16_t)(height - (uint16_t)(radius * 2U)), color);
  LCD_BufferFillCircle(pixels, canvas_width, canvas_height,
                       (uint16_t)(x + radius), (uint16_t)(y + radius), radius, color);
  LCD_BufferFillCircle(pixels, canvas_width, canvas_height,
                       (uint16_t)(x + width - radius - 1U),
                       (uint16_t)(y + radius), radius, color);
  LCD_BufferFillCircle(pixels, canvas_width, canvas_height,
                       (uint16_t)(x + radius),
                       (uint16_t)(y + height - radius - 1U), radius, color);
  LCD_BufferFillCircle(pixels, canvas_width, canvas_height,
                       (uint16_t)(x + width - radius - 1U),
                       (uint16_t)(y + height - radius - 1U), radius, color);
}

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
                             uint8_t thickness)
{
  if ((pixels == NULL) || (width == 0U) || (height == 0U))
  {
    return;
  }

  if (thickness == 0U)
  {
    thickness = 1U;
  }
  if (thickness > (width / 2U))
  {
    thickness = (uint8_t)((width + 1U) / 2U);
  }
  if (thickness > (height / 2U))
  {
    thickness = (uint8_t)((height + 1U) / 2U);
  }

  LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                     x, y, width, thickness, color);
  LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                     x, (uint16_t)(y + height - thickness),
                     width, thickness, color);

  if (height > (uint16_t)(thickness * 2U))
  {
    LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                       x, (uint16_t)(y + thickness),
                       thickness,
                       (uint16_t)(height - (uint16_t)(thickness * 2U)),
                       color);
    LCD_BufferFillRect(pixels, canvas_width, canvas_height,
                       (uint16_t)(x + width - thickness),
                       (uint16_t)(y + thickness),
                       thickness,
                       (uint16_t)(height - (uint16_t)(thickness * 2U)),
                       color);
  }
}

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
                          uint16_t color)
{
  int32_t x = radius;
  int32_t y = 0;
  int32_t error = 1 - (int32_t)radius;
  int32_t cx = center_x;
  int32_t cy = center_y;

  while (x >= y)
  {
    LCD_BackgroundService();
    LCD_BufferFillHorizontalSpan(pixels, canvas_width, canvas_height,
                                 cx - x, cx + x, cy + y, color);
    LCD_BufferFillHorizontalSpan(pixels, canvas_width, canvas_height,
                                 cx - x, cx + x, cy - y, color);
    LCD_BufferFillHorizontalSpan(pixels, canvas_width, canvas_height,
                                 cx - y, cx + y, cy + x, color);
    LCD_BufferFillHorizontalSpan(pixels, canvas_width, canvas_height,
                                 cx - y, cx + y, cy - x, color);

    y++;
    if (error < 0)
    {
      error += (2 * y) + 1;
    }
    else
    {
      x--;
      error += (2 * (y - x)) + 1;
    }
  }
}

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
                        bool bold)
{
  const uint8_t *glyph;
  uint16_t cursor_x = x;
  uint16_t character_width;
  uint16_t character_height;
  uint16_t pixel_x;
  uint16_t pixel_y;
  uint16_t glyph_x;
  uint16_t glyph_y;
  uint32_t pixel_index;

  if ((pixels == NULL) || (text == NULL))
  {
    return;
  }
  if (scale == 0U)
  {
    scale = 1U;
  }
  if (scale > 3U)
  {
    scale = 3U;
  }

  character_width = (uint16_t)(6U * scale);
  character_height = (uint16_t)(8U * scale);
  while ((*text != '\0') && (cursor_x < canvas_width))
  {
    glyph = LCD_GetGlyph(*text);
    for (pixel_y = 0U; pixel_y < character_height; pixel_y++)
    {
      if ((uint32_t)y + pixel_y >= canvas_height)
      {
        break;
      }
      glyph_y = (uint16_t)(pixel_y / scale);
      for (pixel_x = 0U; pixel_x < character_width; pixel_x++)
      {
        if ((uint32_t)cursor_x + pixel_x >= canvas_width)
        {
          break;
        }
        glyph_x = (uint16_t)(pixel_x / scale);
        if (LCD_GlyphPixelIsSet(glyph, glyph_x, glyph_y, bold))
        {
          pixel_index = ((uint32_t)y + pixel_y) * canvas_width + cursor_x + pixel_x;
          pixels[pixel_index] = foreground;
        }
      }
    }

    cursor_x = (uint16_t)(cursor_x + character_width);
    text++;
  }
}

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
                            LCD_FontSize_t font_size)
{
  if(font_size==LCD_FONT_HEITI_SMALL || font_size==LCD_FONT_HEITI_MEDIUM)
  { if(pixels && text){LCD_BufferHeiti(pixels,canvas_width,canvas_height,x,y,text,foreground,font_size);} return; }

  const LCD_Font_t *font;
  const LCD_FontGlyph_t *glyph;
  uint16_t cursor_x;
  uint16_t row;
  uint16_t column;
  uint16_t glyph_pixel_index;
  int32_t destination_x;
  int32_t destination_y;
  uint32_t destination_index;
  uint8_t alpha;

  if ((pixels == NULL) || (text == NULL))
  {
    return;
  }

  font = LCD_GetFont(font_size);
  cursor_x = x;
  while ((*text != '\0') && (cursor_x < canvas_width))
  {
    glyph = LCD_FindFontGlyph(font, *text);
    if (glyph == NULL)
    {
      break;
    }

    for (row = 0U; row < glyph->height; row++)
    {
      LCD_BackgroundService();
      destination_y = (int32_t)y + glyph->y_offset + row;
      if ((destination_y < 0) || (destination_y >= canvas_height))
      {
        continue;
      }
      for (column = 0U; column < glyph->width; column++)
      {
        destination_x = (int32_t)cursor_x + glyph->x_offset + column;
        if ((destination_x < 0) || (destination_x >= canvas_width))
        {
          continue;
        }
        glyph_pixel_index = (uint16_t)((row * glyph->width) + column);
        alpha = LCD_GetFontAlpha(font, glyph, glyph_pixel_index);
        destination_index = ((uint32_t)destination_y * canvas_width) +
                            (uint32_t)destination_x;
        pixels[destination_index] =
          LCD_BlendRgb565(pixels[destination_index], foreground, alpha);
      }
    }

    cursor_x = (uint16_t)(cursor_x + glyph->advance);
    text++;
  }
}

/**
 * @brief  计算LCD_DrawText()绘制字符串时占用的像素宽度。
 * @param  text 字符串指针。
 * @param  scale 像素缩放倍数，有效范围1～3。
 * @retval 字符串宽度，单位为像素。
 */
uint16_t LCD_GetTextWidth(const char *text, uint8_t scale)
{
  size_t length;

  if (text == NULL)
  {
    return 0U;
  }
  if (scale == 0U)
  {
    scale = 1U;
  }
  if (scale > 3U)
  {
    scale = 3U;
  }

  length = strlen(text);
  if (length > (UINT16_MAX / (6U * scale)))
  {
    return UINT16_MAX;
  }
  return (uint16_t)(length * 6U * scale);
}

/**
 * @brief  计算Oxanium字库绘制字符串时占用的像素宽度。
 * @param  text 字符串指针。
 * @param  font_size 字库字号。
 * @retval 字符串宽度，单位为像素。
 */
uint16_t LCD_GetFontTextWidth(const char *text, LCD_FontSize_t font_size)
{
  if(font_size==LCD_FONT_HEITI_SMALL || font_size==LCD_FONT_HEITI_MEDIUM)
  { uint16_t width=0U; if(text){while(*text){width+=(uint16_t)(LCD_HeitiGlyph(LCD_NextUtf8(&text),font_size)->width+1U);}} return width?width-1U:0U; }

  const LCD_Font_t *font;
  const LCD_FontGlyph_t *glyph;
  uint16_t width = 0U;

  if (text == NULL)
  {
    return 0U;
  }

  font = LCD_GetFont(font_size);
  while (*text != '\0')
  {
    glyph = LCD_FindFontGlyph(font, *text);
    if (glyph != NULL)
    {
      width = (uint16_t)(width + glyph->advance);
    }
    text++;
  }

  if (width >= font->tracking)
  {
    width = (uint16_t)(width - font->tracking);
  }
  return width;
}

/**
 * @brief  读取Oxanium字库的行高。
 * @param  font_size 字库字号。
 * @retval 字库行高，单位为像素。
 */
uint8_t LCD_GetFontLineHeight(LCD_FontSize_t font_size)
{
  if(font_size==LCD_FONT_HEITI_SMALL){return 14U;}
  if(font_size==LCD_FONT_HEITI_MEDIUM){return 18U;}
  return LCD_GetFont(font_size)->line_height;
}
