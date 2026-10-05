#include "lcd.h"
#include "spi.h"
#include "gpio.h"
#include "tim.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim1;
void HAL_Delay(uint32_t x){(void)x;}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *i,uint32_t c){(void)i;(void)c;return HAL_OK;}
static uint16_t pixels[300*80];
static unsigned calls,offset,max_chunk;
static bool data_mode,pixel_mode;
void LCD_BackgroundService(void){calls++;}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState v)
{if(p==LCD_DC_Port && pin==LCD_DC_Pin)data_mode=v==GPIO_PIN_SET;}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *i,uint8_t *p,uint16_t n,uint32_t t)
{
 (void)i;(void)t;CHECK(n<=128);if(n>max_chunk)max_chunk=n;
 if(!data_mode){pixel_mode=p[0]==0x2c;return HAL_OK;}
 if(pixel_mode) {
  CHECK(n%2==0);
  for(unsigned k=0;k<n;k+=2){CHECK(offset<300*80);CHECK(((p[k]<<8)|p[k+1])==pixels[offset]);offset++;}
 }
 return HAL_OK;
}
int main(void)
{
 for(unsigned n=0;n<300*80;n++)pixels[n]=(uint16_t)(n*19);
 LCD_DrawBitmap(0,0,300,80,pixels);
 CHECK(offset==300*80 && max_chunk==128 && calls>400);
 unsigned before=calls;LCD_BufferFill(pixels,300,80,0x1234);
 CHECK(calls-before>=93);for(unsigned n=0;n<300*80;n++)CHECK(pixels[n]==0x1234);
 puts("PASS: LCD yields across SPI chunks and canvas fill; <=128-byte transfers preserve all RGB565 pixels");return 0;
}
