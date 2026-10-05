#include "../02_Application/Core/SRC/ui_page.c"
#include "../02_Application/Core/SRC/splash_logo.c"
#include "../02_Application/Core/SRC/splash_logo_data.c"
#include "spi.h"
#include "gpio.h"
#include "tim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);}}while(0)
SPI_HandleTypeDef hspi1;TIM_HandleTypeDef htim1;static TIM_TypeDef timer;
static uint32_t tick;static bool data_mode;static uint8_t command;
static unsigned x0,x1,y0,y1,cursor,pixel_bytes;static uint16_t screen[240][320];
static uint16_t expected_main[240][320],expected_settings[240][320];
static unsigned full_black_passes;static bool full_black;
static CST836U_Point_t touch;static bool touch_pending;
uint32_t HAL_GetTick(void){return tick;}
void HAL_Delay(uint32_t t){(void)t;}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *t,uint32_t ch){(void)t;(void)ch;return HAL_OK;}
void LCD_BackgroundService(void){}
bool Buzzer_Init(void){return true;}void Buzzer_SetEnabled(bool on){(void)on;}
void Buzzer_Beep(uint16_t t){(void)t;}void Buzzer_Process(uint32_t t){(void)t;}
void CST836U_SetTransform(CST836U_Transform_t t){(void)t;}
bool CST836U_GetEvent(CST836U_Point_t *p){if(!touch_pending)return false;*p=touch;touch_pending=false;return true;}
static void Touch(CST836U_Event_t event,uint16_t x,uint16_t y,uint32_t now)
{touch.event=event;touch.x=x;touch.y=y;touch_pending=true;tick=now;UI_ProcessInput(now);}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState v)
{if(p==LCD_DC_Port && pin==LCD_DC_Pin)data_mode=v==GPIO_PIN_SET;}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi,uint8_t *p,uint16_t n,uint32_t timeout)
{
 (void)spi;(void)timeout;
 if(!data_mode){command=p[0];if(command==0x2c){cursor=0;full_black=x0==0 && x1==319 && y0==0 && y1==239;}return HAL_OK;}
 if(command==0x2a){CHECK(n==4);x0=(p[0]<<8)|p[1];x1=(p[2]<<8)|p[3];}
 else if(command==0x2b){CHECK(n==4);y0=(p[0]<<8)|p[1];y1=(p[2]<<8)|p[3];}
 else if(command==0x2c){CHECK(n%2==0);pixel_bytes+=n;for(unsigned k=0;k<n;k+=2){unsigned x=x0+cursor%(x1-x0+1),y=y0+cursor/(x1-x0+1);CHECK(x<320 && y<240 && y<=y1);screen[y][x]=(p[k]<<8)|p[k+1];if(screen[y][x])full_black=false;cursor++;}if(full_black && cursor==320*240)full_black_passes++;}
 return HAL_OK;
}
static void Save(const char *path)
{
 FILE *out=fopen(path,"wb");CHECK(out);fprintf(out,"P6\n320 240\n255\n");
 for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++){uint16_t p=screen[y][x];uint8_t rgb[]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,out);}fclose(out);
}
static bool SameMain(void)
{
 for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)if(screen[y][x]!=expected_main[y][x]){fprintf(stderr,"Pixel differs at %u,%u: expected %04x got %04x\n",x,y,expected_main[y][x],screen[y][x]);Save("device_return_621.ppm");return false;}
 return true;
}
int main(void)
{
 memset(screen,0xA5,sizeof screen);SplashLogo_Draw();CHECK(full_black_passes==1);
 memcpy(expected_main,screen,sizeof screen);
 memset(screen,0x5A,sizeof screen);SplashLogo_Draw();CHECK(full_black_passes==2);CHECK(SameMain());
 htim1.Instance=&timer;timer.ARR=99;UI_Init(true);UI_SetMeasurements(28000,3300,1000,3300,302,false);UI_SetRipple(5,true);UI_Render(20);Save("device_main_620.ppm");
 unsigned before=pixel_bytes;UI_SetMeasurements(28001,3301,1001,3301,302,false);UI_Render(39);CHECK(pixel_bytes==before);
 UI_Render(40);unsigned bytes=pixel_bytes-before;CHECK(bytes<27000);printf("PASS: 20ms partial update sends %u pixel bytes (no SET/OCP redraw)\n",bytes);
 memcpy(expected_main,screen,sizeof screen);
 UI_HandleSwipe(-100);CHECK(UI_GetCurrentPage()==UI_PAGE_NUMERIC);
 unsigned clears=full_black_passes;
 UI_HandleSwipe(100);CHECK(UI_GetCurrentPage()==UI_PAGE_SETTINGS);CHECK(full_black_passes==clears+1);Save("device_settings_620.ppm");
 memcpy(expected_settings,screen,sizeof screen);
 UI_HandleSwipe(-100);CHECK(UI_GetCurrentPage()==UI_PAGE_NUMERIC);CHECK(SameMain());
 /* Poison every old pixel, including rounded corners and the header. */
 memset(screen,0xA5,sizeof screen);UI_DrawCurrentPage();CHECK(memcmp(expected_main,screen,sizeof screen)==0);
 memset(screen,0xA5,sizeof screen);UI_HandleSwipe(100);CHECK(memcmp(expected_settings,screen,sizeof screen)==0);
 for(unsigned i=0;i<5;i++){UI_HandleSwipe(-100);CHECK(memcmp(expected_main,screen,sizeof screen)==0);UI_HandleSwipe(100);CHECK(memcmp(expected_settings,screen,sizeof screen)==0);}
 UI_ScrollSettings(-100);Save("device_settings_scroll_620.ppm");UI_HandleSwipe(-100);CHECK(UI_GetCurrentPage()==UI_PAGE_NUMERIC);CHECK(memcmp(expected_main,screen,sizeof screen)==0);
 CHECK(LCD_GetFontTextWidth("电源设置",LCD_FONT_HEITI_MEDIUM)>50);
 UI_HandleSwipe(100);UI_ScrollSettings(-100);UI_ScrollSettings(-100);Save("device_sleep_622.ppm");
 UI_Rect_t card;CHECK(UI_SettingsCardRect(4,&card));CHECK(s_ui_state.screen_sleep_seconds==60);
 UI_HandleSettingsTap(card.x+250,card.y+20);CHECK(s_ui_state.screen_sleep_seconds==120);
 UI_HandleSwipe(-100);
 tick=0;CHECK(UI_SetScreenSleepSeconds(30));UI_SetOutputRequested(true);
 UI_ControlRequest_t a={0},b={0};UI_GetControlRequest(&a);
 tick=29999;UI_Render(tick);CHECK(!s_ui_sleeping);
 tick=30000;UI_Render(tick);CHECK(s_ui_sleeping && timer.CCR1==0);
 for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++)CHECK(screen[y][x]==0);
 UI_GetControlRequest(&b);CHECK(memcmp(&a,&b,sizeof a)==0);
 before=pixel_bytes;UI_SetMeasurements(28001,3350,1001,3350,310,false);UI_SetRipple(8,true);UI_Render(30500);CHECK(pixel_bytes==before);
 Touch(CST836U_EVENT_DOWN,270,195,30600);CHECK(!s_ui_sleeping && timer.CCR1==80);CHECK(s_ui_state.output_requested);
 Touch(CST836U_EVENT_CONTACT,270,195,30610);Touch(CST836U_EVENT_UP,270,195,30620);CHECK(s_ui_state.output_requested);
 CHECK(!s_ui_wake_touch_pending);CHECK(s_ui_state.output_voltage_mv==3350);
 Touch(CST836U_EVENT_DOWN,270,195,30800);Touch(CST836U_EVENT_UP,270,195,30810);CHECK(!s_ui_state.output_requested);
 CHECK(UI_SetScreenSleepSeconds(0));tick=1000000;UI_Render(tick);CHECK(!s_ui_sleeping);
 tick=0xfffffff0U;CHECK(UI_SetScreenSleepSeconds(30));tick+=30000;UI_Render(tick);CHECK(s_ui_sleeping);
 before=pixel_bytes;UI_SetPowerFault(true);UI_SetCalibrationStatus(UI_CALIBRATION_SAVED);UI_Render(tick+20);CHECK(pixel_bytes==before && s_ui_state.power_fault);
 Touch(CST836U_EVENT_DOWN,270,195,tick+100);Touch(CST836U_EVENT_UP,270,195,tick+10);CHECK(s_ui_state.power_fault && !s_ui_state.output_requested && !s_ui_state.fault_clear_request_pending);
 puts("PASS: screen sleep black/backlight OFF, unchanged power request, measurements/protection continue, consumed wake tap, disabled timer, settings selection and tick wrap");
 puts("PASS: splash black clear and deterministic full-screen render; pixel-identical main/settings round trips including unchanged VIN/TEMP header, poisoned pixels and scrolling");return 0;
}
