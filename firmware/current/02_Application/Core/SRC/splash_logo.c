#include "splash_logo.h"
#include "lcd.h"

/* Only one scanline is buffered, keeping the logo out of the 64 KiB RAM. */
static uint16_t s_scanline[LCD_WIDTH];

void SplashLogo_Draw(void)
{
  uint32_t offset = 0U;
  uint16_t x = 0U;
  uint16_t y = 0U;
  uint16_t color;
  uint8_t count;

  LCD_Clear(LCD_COLOR_BLACK);

  while ((offset + 2U < g_splash_logo_rle_size) && (y < LCD_HEIGHT))
  {
    count = g_splash_logo_rle[offset++];
    color = (uint16_t)g_splash_logo_rle[offset] |
            (uint16_t)((uint16_t)g_splash_logo_rle[offset + 1U] << 8U);
    offset += 2U;
    if ((count == 0U) || ((uint32_t)x + count > LCD_WIDTH))
    {
      break;
    }
    while (count-- > 0U)
    {
      s_scanline[x++] = color;
    }
    if (x == LCD_WIDTH)
    {
      LCD_DrawBitmap(0U, y, LCD_WIDTH, 1U, s_scanline);
      x = 0U;
      y++;
    }
  }
  if (y != LCD_HEIGHT)
  {
    LCD_Clear(0x0883U); /* Same dark blue as the 5.9.2 splash asset. */
  }
}
