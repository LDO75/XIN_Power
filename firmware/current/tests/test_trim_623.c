/* Real production PI, stubbed plant. This does not replace hardware load tests. */
#define main legacy_main
#include "test_620.c"
#undef main
#include "power_config.h"

static void Enable(PowerControl_Request_t *r,PowerMonitor_Snapshot_t *m,uint16_t target,uint32_t now)
{ Init(r,m,target,now);sample_period=2;r->output_requested=true;Fresh(r,m,now+2,target); }
static void Gates(void)
{
  PowerControl_Request_t r;PowerMonitor_Snapshot_t m;
  for(unsigned target=3200;target<=32000;target+=100)
  {
    Enable(&r,&m,target,0);uint16_t base=dac;
    for(unsigned n=0;n<200;n++)Fresh(&r,&m,tick+2,target-60);
    CHECK(Status().pid_enabled && dac<base);
    CHECK(PowerControl_GetRippleDacCode()==base);
    unsigned hold=dac,before=writes,updates=Status().pid_updates;
    for(unsigned n=0;n<1000;n++)PowerControl_Process(tick,&r,&m);
    CHECK(dac==hold && writes==before && Status().pid_updates==updates);
    for(unsigned n=0;n<500;n++)Fresh(&r,&m,tick+2,target-101);
    CHECK(!Status().pid_enabled && dac==hold && writes==before);
    Fresh(&r,&m,tick+2,target-100);CHECK(Status().pid_enabled);
    Fresh(&r,&m,tick+2,target+101);CHECK(!Status().pid_enabled);
    Fresh(&r,&m,tick+2,target+100);CHECK(Status().pid_enabled);
    Fresh(&r,&m,tick+2,target);CHECK(dac==hold);
    r.output_requested=false;Fresh(&r,&m,tick+2,target);
    CHECK(dac==0 && !Status().pid_enabled);
  }
  puts("PASS: PI full-span +/-100mV inclusive gate, outside hold, duplicate sample rejection, deadband, OFF and stable Vpp context");
}
static void Transitions(void)
{
  PowerControl_Request_t r;PowerMonitor_Snapshot_t m;Enable(&r,&m,3300,0);
  for(unsigned n=0;n<200;n++)Fresh(&r,&m,tick+2,3240);
  r.voltage_setpoint_mv=5000;fail_dac=true;unsigned old=dac;
  Fresh(&r,&m,tick+2,5000);CHECK(dac==old);
  fail_dac=false;Fresh(&r,&m,tick+2,5000);CHECK(dac==PowerControl_VoltageToDacCode(5000));
  CHECK(PowerControl_GetRippleDacCode()==dac);
  for(unsigned n=0;n<200;n++)Fresh(&r,&m,tick+2,4940);
  CHECK(PowerControl_BeginDacCalibration(5000));Fresh(&r,&m,tick+2,4940);
  unsigned fixed=dac,before=writes;
  for(unsigned n=0;n<1000;n++)Fresh(&r,&m,tick+2,4940);
  CHECK(dac==fixed && writes==before && !Status().pid_enabled);
  CHECK(PowerControl_StepDacCalibration(1,&m));CHECK(PowerControl_GetRippleDacCode()==dac);
  PowerControl_EndDacCalibration();CHECK(!Status().output_enabled);
  Enable(&r,&m,3300,0xfffffff0U);
  for(unsigned n=0;n<200;n++)Fresh(&r,&m,tick+2,3240);
  CHECK(Status().pid_enabled && dac<PowerControl_VoltageToDacCode(3300));
  old=dac;Fresh(&r,&m,tick+30,3240);CHECK(dac==old);
  m.data_valid=false;PowerControl_ServiceVoltageTrim(tick,&r,&m);CHECK(!Status().pid_enabled);
  unsigned updates=Status().pid_updates; m.data_valid=true;fail_dac=true;
  for(unsigned n=0;n<200;n++)Fresh(&r,&m,tick+2,3240);
  CHECK(dac==old);fail_dac=false;Fresh(&r,&m,tick+2,3240);
  CHECK(old-dac<=1 && Status().pid_updates>=updates);
  for(unsigned n=0;n<600;n++) { unsigned previous=dac;Fresh(&r,&m,tick+2,3240);CHECK(abs((int)dac-(int)previous)<=1); }
  CHECK(dac<old); /* driver re-probe recovers without accumulated PI jump */
  PowerControl_ServiceCurrentSample(tick+1,true,1500001,1500001,3240,&r,&m);
  ExpectFault(POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT);
  before=writes;PowerControl_ServiceVoltageTrim(tick+2,&r,&m);CHECK(writes==before);
  puts("PASS: PI setpoint/write-failure rebase, manual calibration fixed DAC, sample gap/tick wrap, failed trim no windup and current trip precedence");
}
static void Model(unsigned target,double tau_ms,unsigned delay_ms,int load,double gain)
{
  PowerControl_Request_t r;PowerMonitor_Snapshot_t m;Enable(&r,&m,target,0);
  unsigned base=dac;double voltage=target;
  double commands[256]={0};unsigned slot=0,delay=delay_ms/2;
  double slope=100000.0/5110.0*3000.0/4096.0*gain;
  double worst_after=0,last_error=0;uint32_t last_write=tick;
  for(unsigned n=1;n<=4000;n++)
  {
    unsigned ms=n*2,old=dac;
    double disturbance=ms>=500 && ms<4500 ? load : 0;
    double command=((int)base-(int)dac)*slope;
    double old_command=delay ? commands[(slot+256-delay)%256] : command;
    commands[slot]=command;slot=(slot+1)%256;
    double intended=target+old_command-disturbance;
    voltage+=(intended-voltage)*(1.0-exp(-2.0/tau_ms));
    int noise=n%3==0 ? 2 : n%3==1 ? -2 : 0;
    unsigned before=writes;Fresh(&r,&m,tick+2,(uint16_t)lround(voltage+noise));
    CHECK(Status().fault==0 && Status().output_enabled);
    CHECK(abs((int)dac-(int)old)<=1);
    if(writes!=before){CHECK(tick-last_write>=10);last_write=tick;}
    if(ms>=4000 && ms<4500) CHECK(fabs(voltage-target)<12);
    if(ms>=4500) { double deviation=(voltage-target)*(load>0 ? -1 : 1); if(deviation>worst_after)worst_after=deviation; }
    last_error=voltage-target;

  }
  printf("MODEL: target=%u tau=%.0fms delay=%ums gain=%.1f load=%dmV final=%.2fmV reverse_peak=%.2fmV writes=%u\n",
    target,tau_ms,delay_ms,gain,load,last_error,worst_after,writes);
  CHECK(fabs(last_error)<12 && worst_after<25);
}
static void Saturation(void)
{
  memset(flash,255,sizeof(flash));Calibration_Init();CHECK(Calibration_SavePoint(15,32000,2));
  PowerControl_Request_t r;PowerMonitor_Snapshot_t m;Enable(&r,&m,32000,0);
  for(unsigned n=0;n<5000;n++)Fresh(&r,&m,tick+2,31940);
  CHECK(dac==0);unsigned before=writes;
  for(unsigned n=0;n<500;n++)Fresh(&r,&m,tick+2,31940);
  CHECK(dac==0 && writes==before);
  for(unsigned n=0;n<100;n++)Fresh(&r,&m,tick+2,32060);
  CHECK(dac>0 && Status().fault==0);
  Calibration_ResetDefaults();
  puts("PASS: PI DAC saturation does not wind up; reversed error exits saturation");
}
int main(void)
{
  Calibration_ResetDefaults();Gates();Transitions();Saturation();
  for(unsigned target=3200;target<=32000;target+=14400)
    for(unsigned ti=0;ti<4;ti++)
      for(unsigned delay=0;delay<=20;delay+=10)
        for(int load=-80;load<=80;load+=160)
          for(unsigned g=1;g<=3;g++) { const unsigned taus[]={5,20,80,200};Model(target,taus[ti],delay,load,g*0.5); }
  puts("PASS: assumption-based first-order plant sweep, +/-80mV loading/unloading, measurement noise, 0..20ms delay and 0.5..1.5 actuator gain");
  return 0;
}
