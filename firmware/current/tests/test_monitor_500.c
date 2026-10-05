#include "ina226.h"
#include "power_monitor.h"
#include "i2c.h"
#include "calibration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %u: %s\n",__LINE__,#x);exit(1);} }while(0)
I2C_HandleTypeDef hi2c1,hi2c2;
static uint32_t tick,reads;
static uint16_t cfg[2],cal[2],bus[2]={9600,2400};
static int16_t current[2]={500,250};
static bool ready[2]={true,true};
static int fail_reg=-1,fail_device=-1;
static unsigned hook_count;static bool hook_output[20000];
uint32_t HAL_GetTick(void){return tick;}
uint16_t Calibration_ApplyVoltage(uint16_t x){return x;}
int32_t Calibration_ApplyCurrent(int32_t x){return x;}
void DebugConsole_Write(const char *s){(void)s;}
void PowerMonitor_OnCurrentSample(bool output,int32_t c,int32_t u,uint16_t v,uint32_t now)
{(void)c;(void)u;(void)v;CHECK(now==tick);CHECK(hook_count<20000);hook_output[hook_count++]=output;}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *i,uint16_t a,uint32_t n,uint32_t t)
{(void)i;(void)a;(void)n;(void)t;return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i,uint16_t a,uint16_t reg,uint16_t rsize,uint8_t *p,uint16_t n,uint32_t timeout)
{(void)i;(void)rsize;(void)timeout;CHECK(n==2);int d=a==(0x40<<1)?0:1;uint16_t value=(p[0]<<8)|p[1];if(reg==0)cfg[d]=value;else if(reg==5)cal[d]=value;else CHECK(0);return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i,uint16_t a,uint16_t reg,uint16_t rsize,uint8_t *p,uint16_t n,uint32_t timeout)
{
 (void)i;(void)rsize;(void)timeout;CHECK(n==2);int d=a==(0x40<<1)?0:1;reads++;
 if((int)reg==fail_reg && d==fail_device)return HAL_ERROR;
 uint16_t value=0;
 switch(reg){case 0:value=cfg[d];break;case 5:value=cal[d];break;case 0xfe:value=0x5449;break;case 0xff:value=0x2260;break;
 case 6:value=ready[d]?8:0;break;case 2:value=bus[d];break;case 4:value=(uint16_t)current[d];break;
 default:CHECK(0); /* No redundant shunt/power read on the fast path. */}
 p[0]=value>>8;p[1]=value;return HAL_OK;
}
static PowerMonitor_Snapshot_t Snapshot(void){PowerMonitor_Snapshot_t s;PowerMonitor_GetSnapshot(&s);return s;}

int main(void)
{
 CHECK(INA226_CONFIGURATION_VALUE==0x4097);CHECK(PowerMonitor_GetSamplePeriod()==2);
 CHECK(PowerMonitor_Init());Snapshot();reads=0;hook_count=0;
 for(tick=1;tick<=1000;tick++) {
  bus[1]=2400+(tick%100<50?0:40);PowerMonitor_Process(tick);PowerMonitor_Snapshot_t s=Snapshot();
  CHECK(s.sample_sequence==tick/2);uint32_t before=reads;PowerMonitor_Process(tick);CHECK(reads==before);
 }
 PowerMonitor_Snapshot_t s=Snapshot();CHECK(reads==3000 && s.sample_hz==500 && s.sample_maxgap_ms==2);
 CHECK(!s.ripple_valid && hook_count==1000); /* Unsettled at startup: no Vpp yet. */
 for(unsigned i=0;i<1000;i++)CHECK(hook_output[i]==(i%2==0)); /* output current always first */
 ready[1]=false;tick=1002;PowerMonitor_Process(tick);s=Snapshot();CHECK(s.sample_sequence==500 && hook_count==1001);
 CHECK(!hook_output[1000]); /* input current still checked with unavailable output */
 ready[1]=true;fail_reg=2;fail_device=1;tick=1004;PowerMonitor_Process(tick);s=Snapshot();
 CHECK(s.sample_sequence==500 && hook_count==1003 && hook_output[1001]); /* voltage failure cannot mask fresh current */
 fail_reg=4;tick=1006;PowerMonitor_Process(tick);s=Snapshot();CHECK(s.sample_sequence==500 && hook_count==1004);
 fail_reg=-1;tick=1008;PowerMonitor_Process(tick);s=Snapshot();CHECK(s.sample_sequence==501);
 ready[0]=ready[1]=false;tick=1040;PowerMonitor_Process(tick);s=Snapshot();CHECK(!s.ripple_valid);
 ready[0]=ready[1]=true;tick=2020;bus[1]=2400;PowerMonitor_Process(tick);s=Snapshot();CHECK(!s.ripple_valid);
 tick=2040;PowerMonitor_Process(tick);s=Snapshot();CHECK(!s.ripple_valid);
 /* A real transition freezes the old statistic, discards the ramp and starts
    a complete fresh 1s window only after stability qualification. */
 tick=3000;hook_count=0;CHECK(PowerMonitor_Init());Snapshot();
 PowerMonitor_SetRippleContext(true,3000,tick);
 bus[1]=2640; /* 3.3V */
 for(tick=3002;tick<=4620;tick+=2)PowerMonitor_Process(tick);
 s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==0);
 /* Once settled, real 50mV variation must be visible, not self-suppressed. */
 for(;tick<=5700;tick+=2){bus[1]=2640+(tick%100<50?0:40);PowerMonitor_Process(tick);}
 s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==50);
 PowerMonitor_SetRippleContext(true,2900,tick);
 for(uint32_t end=tick+600;tick<end;tick+=2){bus[1]+=2;PowerMonitor_Process(tick);s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==50);}
 bus[1]=4000; /* Stable at 5V, ramp must never enter the new Vpp. */
 uint32_t plateau=tick;
 for(;tick<plateau+1400;tick+=2){PowerMonitor_Process(tick);s=Snapshot();CHECK(s.ripple_vpp_mv==50);}
 for(;tick<plateau+1500;tick+=2)PowerMonitor_Process(tick);
 s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==0);
 /* Repeating the same applied state does not freeze valid measurements. */
 for(uint32_t end=tick+1100;tick<end;tick+=2){PowerMonitor_SetRippleContext(true,2900,tick);bus[1]=4000+(tick%100<50?0:40);PowerMonitor_Process(tick);}
 s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==50);
 /* OFF decay is excluded, then stable OFF residual is still measured. */
 PowerMonitor_SetRippleContext(false,0,tick);bus[1]=200;
 for(uint32_t end=tick+1620;tick<end;tick+=2)PowerMonitor_Process(tick);
 s=Snapshot();CHECK(s.ripple_valid && s.ripple_vpp_mv==0);
 tick+=100;PowerMonitor_Process(tick);s=Snapshot();CHECK(!s.ripple_valid);
 tick=0xfffffffcU;hook_count=0;CHECK(PowerMonitor_Init());Snapshot();
 PowerMonitor_SetRippleContext(true,3000,tick);bus[1]=2640;
 for(unsigned n=1;n<=1620;n++){tick++;PowerMonitor_Process(tick);s=Snapshot();CHECK(s.sample_sequence==n/2);}
 CHECK(s.ripple_valid && s.ripple_vpp_mv==0);
 puts("PASS: 500Hz/current-first hooks; Vpp ramp freeze, fresh window, real fluctuation, OFF, sample-loss recovery and tick wrap");return 0;
}
