/* Short host checks using the REAL calibration and output-control sources.
 * Fake measurements do not verify physical overshoot or load regulation. */
#include "power_control.h"
#include "calibration.h"
#include "w25q256.h"
#include "i2c.h"
#include "usart.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#c); exit(1); } } while(0)
#define BASE 0x01FFE000UL
I2C_HandleTypeDef hi2c1, hi2c2;
UART_HandleTypeDef husart1, husart2;
static uint8_t flash[8192];
static uint32_t tick;
static uint16_t dac;
static GPIO_PinState ce;
static unsigned writes;
static uint32_t sample_period=20;
uint32_t PowerMonitor_GetSamplePeriod(void) { return sample_period; }
static bool fail_dac, fail_commit;
static unsigned fail_stage;
bool W25Q256_Init(void) { return true; }
bool W25Q256_IsReady(void) { return true; }
bool W25Q256_Read(uint32_t a, void *p, uint32_t n)
{ CHECK(a>=BASE && a+n<=BASE+sizeof(flash)); if ((fail_stage==3 && (a-BASE)%4096==256) || (fail_stage==6 && (a-BASE)%4096==0)) return false; memcpy(p,flash+a-BASE,n); if ((fail_stage==4 && (a-BASE)%4096==256) || (fail_stage==7 && (a-BASE)%4096==0)) ((uint8_t *)p)[0]^=1; return true; }
bool W25Q256_EraseSector(uint32_t a)
{ CHECK(a>=BASE && a<BASE+sizeof(flash)); if (fail_stage==1) return false; memset(flash+((a-BASE)&~4095UL),255,4096); return true; }
bool W25Q256_Write(uint32_t a, const void *p, uint32_t n)
{
  CHECK(a>=BASE && a+n<=BASE+sizeof(flash));
  if ((fail_commit || fail_stage==5) && ((a-BASE)%4096==0)) { return false; }
  if (fail_stage==2 && ((a-BASE)%4096==256)) return false;
  const uint8_t *q=p; for(uint32_t i=0;i<n;i++) { flash[a-BASE+i]&=q[i]; } return true;
}
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState value)
{ (void)p; (void)pin; ce=value; }
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *i,uint16_t a,uint32_t n,uint32_t t)
{ (void)i; (void)a; (void)n; (void)t; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i,uint16_t a,uint8_t *b,uint16_t n,uint32_t t)
{ (void)i; (void)a; (void)t; CHECK(n==3); if(fail_dac) return HAL_ERROR;
  dac=((uint16_t)b[1]<<4)|(b[2]>>4); writes++; return HAL_OK; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin)
{ (void)p; (void)pin; return ce; }
void DebugConsole_Write(const char *p) { if(strstr(p,"[FAULT]")) CHECK(ce==GPIO_PIN_SET); }
static Calibration_Data_t Data(void) { Calibration_Data_t d; Calibration_Get(&d); return d; }
static PowerControl_Status_t Status(void) { PowerControl_Status_t s; PowerControl_GetStatus(&s); return s; }
static void Init(PowerControl_Request_t *r,PowerMonitor_Snapshot_t *m,uint16_t target,uint32_t now)
{
  memset(r,0,sizeof(*r)); memset(m,0,sizeof(*m)); tick=now; sample_period=20; ce=GPIO_PIN_SET; dac=0; writes=0; fail_dac=false;
  r->voltage_setpoint_mv=target; r->current_limit_ma=1500; r->temperature_valid=true; r->temperature_decic=300;
  m->input_online=m->output_online=m->data_valid=true; m->input_voltage_raw_mv=27882;
  m->sample_sequence=1; m->last_valid_sample_time_ms=tick;
  CHECK(PowerControl_Init()); PowerControl_Process(tick,r,m);
}
static void Fresh(PowerControl_Request_t *r,PowerMonitor_Snapshot_t *m,uint32_t now,uint16_t actual)
{ tick=now; m->last_valid_sample_time_ms=tick; m->sample_sequence++;
  m->output_voltage_raw_mv=m->output_voltage_uncalibrated_mv=actual; PowerControl_Process(tick,r,m); }
static void Run(PowerControl_Request_t *r,PowerMonitor_Snapshot_t *m,uint16_t actual,unsigned length)
{ uint32_t origin=tick; for(unsigned dt=20;dt<=length;dt+=20) Fresh(r,m,origin+dt,actual); }
static uint32_t Crc(const uint8_t *bytes,unsigned n)
{
  uint32_t crc=UINT32_MAX;
  for(unsigned i=0;i<n;i++) { crc^=bytes[i]; for(unsigned j=0;j<8;j++) crc=(crc>>1)^((crc&1)?0xEDB88320UL:0); }
  return ~crc;
}

static void Curves(void)
{
  const uint16_t targets[]={3200,3300,3500,3800,4000,4200,5000,7200,8400,9000,12000,18000,20000,24000,28000,32000};
  const uint16_t codes[]={2645,2638,2624,2603,2589,2575,2519,2366,2282,2240,2031,1612,1472,1193,914,635};
  memset(flash,255,sizeof(flash));Calibration_Init();
  CHECK(Calibration_SetVoltage(999800,3));CHECK(Calibration_SetCurrent(1002915,407));
  CHECK(Calibration_SavePoint(0,3198,codes[0]));
  uint16_t code;CHECK(Calibration_MapDac(3198,&code)&&code==codes[0]);
  for(unsigned i=0;i<16;i++) {CHECK(Calibration_PointTarget(i)==targets[i]);CHECK(Calibration_SavePoint(i,targets[i]-2,codes[i]));}
  Calibration_Init();CHECK(Data().point_mask==65535 && Data().loaded_from_flash);
  uint16_t previous=4095;
  for(unsigned mv=3200;mv<=32000;mv++) {CHECK(Calibration_MapDac(mv,&code));CHECK(code<=previous);previous=code;}
  Calibration_Data_t before=Data();fail_commit=true;CHECK(!Calibration_SavePoint(2,3499,2623));fail_commit=false;
  CHECK(strcmp(Calibration_GetSaveError(),"FLASH_RECORD_WRITE_FAILED")==0);
  Calibration_Data_t after=Data();CHECK(!memcmp(after.points,before.points,sizeof(before.points)));
  Calibration_Init();CHECK(Data().points[2].actual_mv==3498);
  CHECK(Calibration_SavePoint(2,3499,2623));Calibration_Init();CHECK(Data().points[2].actual_mv==3499);
  CHECK(Data().voltage_gain_ppm==999800 && Data().current_offset_ua==407);
  puts("PASS: 16 ascending points, immediate single-point mapping, full-span interpolation, failed commit/readback");
}
static void Migration(void)
{
  const uint16_t old_targets[]={3000,3300,5000,7200,8400,9000,12000,18000,20000,24000,28000,32000,3600,3900,4000,4500,2000};
  typedef struct {uint32_t magic;uint16_t schema,size;uint32_t sequence,mask;uint16_t tag;Calibration_Point_t points[17];uint32_t crc;} Old4;
  typedef struct {uint32_t magic;uint16_t schema,size;uint32_t sequence;uint16_t mask,tag;Calibration_Point_t points[16];uint32_t crc;} Old3;
  for(unsigned schema=3;schema<=4;schema++) {
    memset(flash,255,sizeof(flash));Calibration_Init();CHECK(Calibration_SetVoltage(999800,3));CHECK(Calibration_SetCurrent(1002915,407));CHECK(Calibration_Save());
    uint8_t *rec=flash;/* locate record by magic rather than assumed slot */
    if(*(uint32_t*)rec!=0x5843414cU) {rec=flash+4096;}
    uint32_t seq;memcpy(&seq,rec+8,4);
    if(schema==4) {
      Old4 c={0};c.magic=0x58435552;c.schema=4;c.size=sizeof(c);c.sequence=seq;c.mask=131071;c.tag=0x4991;
      for(unsigned i=0;i<17;i++){c.points[i].actual_mv=old_targets[i]-2;c.points[i].dac_code=(uint16_t)(4000-old_targets[i]/10);}
      c.crc=Crc((uint8_t*)&c,sizeof(c));memcpy(rec+256,&c,sizeof(c));
    } else {
      Old3 c={0};c.magic=0x58435552;c.schema=3;c.size=sizeof(c);c.sequence=seq;c.mask=65535;c.tag=0x4991;
      for(unsigned i=0;i<16;i++){c.points[i].actual_mv=old_targets[i]-2;c.points[i].dac_code=(uint16_t)(4000-old_targets[i]/10);}
      c.crc=Crc((uint8_t*)&c,sizeof(c));memcpy(rec+256,&c,sizeof(c));
    }
    uint8_t copy[8192];memcpy(copy,flash,sizeof(copy));Calibration_Init();CHECK(!memcmp(copy,flash,sizeof(copy)));
    CHECK(Data().voltage_gain_ppm==999800 && Data().current_offset_ua==407);
    uint32_t expected=0;
    for(unsigned i=0;i<16;i++) {
      unsigned target=Calibration_PointTarget(i);bool found=false;
      for(unsigned j=0;j<(schema==4?17:16);j++) if(old_targets[j]==target) {
        found=true;expected|=1U<<i;CHECK(Data().points[i].actual_mv==target-2);CHECK(Data().points[i].dac_code==4000-target/10);
      }
      if(!found)CHECK(!(Data().point_mask&(1U<<i)));
    }
    CHECK(Data().point_mask==expected);CHECK(Calibration_SavePoint(0,3198,3680));Calibration_Init();CHECK(Data().point_mask==(expected|1));
  }
  puts("PASS: schema 3/4 migrate by nominal voltage, no boot-time rewrite, new schema 5 save, ADC coefficients retained");
}
static void Formula(void)
{
  Calibration_ResetDefaults();unsigned previous=4095;
  for(unsigned target=3200;target<=32000;target++) {
    unsigned code=PowerControl_VoltageToDacCode(target);
    double voltage=1.22*(1+100000.0/7620+100000.0/5110)-100000.0/5110*(3.0*code/4096);
    CHECK(code<=previous && fabs(voltage*1000-target)<7.18);previous=code;
  }
  CHECK(PowerControl_VoltageToDacCode(0)==PowerControl_VoltageToDacCode(3200));
  CHECK(PowerControl_VoltageToDacCode(65535)==PowerControl_VoltageToDacCode(32000));
  puts("PASS: independent KCL 3.2..32V sweep, resistor profile and 12-bit quantization");
}
static void ExpectFault(PowerControl_Fault_t f) {CHECK(Status().fault==f && ce==GPIO_PIN_SET && dac==0 && !Status().output_enabled);}
static void Protections(void)
{
  PowerControl_Request_t r;PowerMonitor_Snapshot_t m;
  Init(&r,&m,3300,0);r.output_requested=true;r.current_limit_ma=100;Fresh(&r,&m,2,3300);
  /* Empty load and low voltage have no independent short fault. */
  for(unsigned i=0;i<500;i++)Fresh(&r,&m,tick+2,(uint16_t)(i%500));CHECK(Status().fault==0);
  PowerControl_ServiceCurrentSample(tick+1,true,100000,100000,200,&r,&m);CHECK(Status().fault==0);
  m.data_valid=false;m.input_online=false;
  PowerControl_ServiceCurrentSample(tick+2,true,99999,100001,200,&r,&m);ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT);
  CHECK(PowerControl_ClearFault());r.output_requested=false;Fresh(&r,&m,tick+1,0);CHECK(ce==GPIO_PIN_SET);
  Init(&r,&m,3300,0);r.output_requested=true;Fresh(&r,&m,2,3300);m.output_online=false;
  PowerControl_ServiceCurrentSample(3,false,4800001,4800001,28000,&r,&m);ExpectFault(POWER_CONTROL_FAULT_INPUT_OVERCURRENT);
  Init(&r,&m,3300,0);r.output_requested=true;Fresh(&r,&m,2,3300);
  m.input_voltage_raw_mv=3999;Fresh(&r,&m,4,3300);Fresh(&r,&m,203,3300);CHECK(Status().fault==0);
  Fresh(&r,&m,204,3300);ExpectFault(POWER_CONTROL_FAULT_INPUT_UNDERVOLTAGE);
  Init(&r,&m,3200,0);r.output_requested=true;Fresh(&r,&m,2,3520);Run(&r,&m,3520,200);CHECK(Status().fault==0);
  Fresh(&r,&m,tick+2,3521);Run(&r,&m,3521,100);ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE);
  Init(&r,&m,32000,0);r.output_requested=true;Fresh(&r,&m,2,32000);
  r.voltage_setpoint_mv=3200;Fresh(&r,&m,4,32000);
  for(unsigned dt=2;dt<=1800;dt+=2) {
    unsigned v=(unsigned)(32000*exp(-(double)dt/600));if(v<3200)v=3200;
    Fresh(&r,&m,4+dt,(uint16_t)v);CHECK(Status().fault==0);
  }
  Run(&r,&m,3800,140);ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE);
  /* Stuck voltage must no longer hold a downward reference forever. */
  Init(&r,&m,4000,0);r.output_requested=true;Fresh(&r,&m,2,4000);r.voltage_setpoint_mv=3200;
  Fresh(&r,&m,4,4000);for(unsigned i=0;i<500 && !Status().fault;i++)Fresh(&r,&m,tick+2,4000);
  ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE);
  Init(&r,&m,3200,0);r.output_requested=true;Fresh(&r,&m,2,3200);r.temperature_decic=800;
  PowerControl_ServiceProtections(3,&r,&m);ExpectFault(POWER_CONTROL_FAULT_OVERTEMPERATURE);
  Init(&r,&m,3200,0);r.output_requested=true;Fresh(&r,&m,2,3200);
  PowerControl_Process(501,&r,&m);CHECK(Status().fault==0);PowerControl_Process(502,&r,&m);ExpectFault(POWER_CONTROL_FAULT_MONITOR_OFFLINE);
  Init(&r,&m,3200,0);CHECK(PowerControl_BeginDacCalibration(3200));r.output_requested=true;Fresh(&r,&m,2,3200);
  CHECK(PowerControl_StepDacCalibration(-5,&m));Run(&r,&m,5000,200);CHECK(Status().fault==0);
  Fresh(&r,&m,tick+2,33001);ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE);
  Init(&r,&m,3300,0);r.output_requested=true;Fresh(&r,&m,2,3300);unsigned before=writes;
  Run(&r,&m,3180,500);CHECK(writes==before);r.output_requested=false;Fresh(&r,&m,tick+1,300);
  before=writes;Run(&r,&m,220,1000);CHECK(writes==before && Status().fault==0 && ce==GPIO_PIN_SET);
  puts("PASS: independent channel first-sample OC, inclusive boundary, no voltage-only SHORT, UV/OV/OTP/loss latch, RC decay and outside-band/OFF DAC hold");
}
int main(void){Curves();Migration();Formula();Protections();return 0;}
