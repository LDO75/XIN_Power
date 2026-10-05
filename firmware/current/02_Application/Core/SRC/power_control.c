/**
 * @file power_control.c
 * @brief Calibrated DAC feedforward plus gated PI voltage trim; basic protections latch faults.
 */
#include "power_control.h"
#include "calibration.h"
#include "debug_console.h"
#include "gpio.h"
#include "i2c.h"
#include "mcp4725.h"
#include "power_config.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define POWER_CONTROL_DAC_RETRY_MS 1000U
#define POWER_CONTROL_DEBUG_PERIOD_MS 1000U

__weak void DebugConsole_OnPowerFault(uint32_t now,PowerControl_Fault_t fault,uint8_t phase,
  uint16_t dac,const PowerControl_Request_t *request,const PowerMonitor_Snapshot_t *measurement)
{(void)now;(void)fault;(void)phase;(void)dac;(void)request;(void)measurement;}

static MCP4725_Device_t s_dac;
static PowerControl_Status_t s_status;
static uint16_t s_applied_dac_code;
static uint16_t s_setpoint_mv;
static uint16_t s_calibration_code;
static bool s_calibration_active;
static uint32_t s_last_sample_sequence;
static uint32_t s_last_dac_retry_time_ms;
static uint32_t s_last_debug_time_ms;
static char s_fault_context[320];
typedef struct { uint32_t since; bool active; } PowerControl_Timer_t;
static PowerControl_Timer_t s_uv_timer, s_ov_timer;
static bool s_protection_active;
static uint32_t s_last_fresh_time_ms;
static uint16_t s_ov_reference_mv;
static uint32_t s_ov_reference_time_ms;
static uint16_t s_trim_target_mv, s_trim_base_code;
static uint32_t s_trim_sequence, s_trim_time, s_trim_write_time;
static float s_trim_integral, s_trim_mv_per_code;
static bool s_trim_have_sample, s_trim_history;

static void PowerControl_ResetTrim(void)
{
  s_trim_target_mv=0U; s_trim_base_code=0U;
  s_trim_have_sample=s_trim_history=false; s_trim_integral=0.0f;
  s_status.pid_enabled=false; s_status.pid_delta=0;
  s_status.pid_error_mv=0; s_status.pid_feedback_mv=0;
  s_status.pid_interval_ms=0U;
}

/* Estimate local actuator gain from the same curve used for feedforward.
 * Endpoints use a one-sided interval; no table write or learned calibration. */
static void PowerControl_PrimeTrim(uint16_t target, uint16_t base)
{
  PowerControl_ResetTrim();
  s_trim_target_mv=target; s_trim_base_code=base;
  uint16_t lo=target>POWER_CFG_VOLTAGE_MIN_MV+100U ? target-100U : POWER_CFG_VOLTAGE_MIN_MV;
  uint16_t hi=target<POWER_CFG_VOLTAGE_MAX_MV-100U ? target+100U : POWER_CFG_VOLTAGE_MAX_MV;
  int32_t span=(int32_t)PowerControl_VoltageToDacCode(lo)-PowerControl_VoltageToDacCode(hi);
  s_trim_mv_per_code=span>0 ? (float)(hi-lo)/(float)span :
    (float)POWER_CFG_FB_UPPER_OHM/POWER_CFG_FB_INJECTION_OHM*
      ((float)POWER_CFG_DAC_REFERENCE_UV/1000.0f)/POWER_CFG_DAC_SCALE_CODES;
  s_trim_write_time=HAL_GetTick();
  s_status.pid_revision=1U;
}

uint16_t PowerControl_GetRippleDacCode(void)
{
  /* Normal PI motion must remain visible in Vpp. Only feedforward/setpoint,
     manual calibration steps and actual enable transitions reset its window. */
  return s_status.output_enabled && !s_calibration_active ? s_trim_base_code : s_applied_dac_code;
}

static void PowerControl_ResetProtection(void)
{
  s_protection_active = false;
  s_last_sample_sequence = 0U;
  s_uv_timer.active = s_ov_timer.active = false;
}

/* Qualify consecutive fresh observations, with unsigned tick-wrap arithmetic. */
static bool PowerControl_Qualified(PowerControl_Timer_t *timer, bool bad,
                                   uint32_t now, uint32_t delay)
{
  if (!bad) { timer->active = false; return false; }
  if (!timer->active) { timer->active = true; timer->since = now; }
  return now - timer->since >= delay;
}

static void PowerControl_DisableHardware(void)
{
  HAL_GPIO_WritePin(P_EN_Port, P_EN_Pin, GPIO_PIN_SET);
  s_status.output_enabled = false;
}

/* Write errors are command errors, not an extra automatic power fault. */
static bool PowerControl_WriteDac(uint16_t code)
{
  if (!s_dac.ready) { return false; }
  if (!MCP4725_WriteVolatile(&s_dac, code))
  {
    DebugConsole_Write("[DAC] write failed; previous applied code retained\r\n");
    return false;
  }
  s_applied_dac_code = code;
  s_status.dac_code = code;
  s_status.dac_write_count++;
  s_status.dac_write_time_ms = HAL_GetTick();
  return true;
}

static void PowerControl_ParkDac(void)
{
  /* DAC=0 can command high voltage: close CE before OFF parking. */
  PowerControl_DisableHardware();
  PowerControl_ResetTrim();
  if (s_applied_dac_code != 0U) { (void)PowerControl_WriteDac(0U); }
}

static void PowerControl_Trip(uint32_t now,
  PowerControl_Fault_t fault, const PowerControl_Request_t *request,
  const PowerMonitor_Snapshot_t *measurement)
{
  PowerMonitor_Snapshot_t empty;
  if (measurement == NULL)
  {
    (void)memset(&empty, 0, sizeof(empty));
    empty.last_valid_sample_time_ms = now;
    measurement = &empty;
  }
  unsigned int phase = s_status.output_enabled ? 2U : 0U;
  uint16_t fault_dac = s_applied_dac_code;
  PowerControl_DisableHardware();
  if (s_status.fault == POWER_CONTROL_FAULT_NONE)
  {
    char event[96];
    s_status.fault = fault;
    DebugConsole_OnPowerFault(now,fault,(uint8_t)phase,fault_dac,request,measurement);
    (void)snprintf(event, sizeof(event),
      "[FAULT] t=%lu reason=%s output=OFF\r\n",
      (unsigned long)now, PowerControl_GetFaultName(fault));
    (void)snprintf(s_fault_context, sizeof(s_fault_context),
      "@FAULT,t=%lu,reason=%s,phase=%u,coarse=0,pid=%u,set=%u,dac=%u,vout=%u,vuncal=%u,"
      "vin=%u,iin=%ld,iout=%ld,pout=%lu,temp=%d,valid=%u,age=%lu,seq=%lu\r\n",
      (unsigned long)now, PowerControl_GetFaultName(fault), phase,
      s_status.pid_enabled ? 1U : 0U, request->voltage_setpoint_mv, fault_dac,
      measurement->output_voltage_raw_mv, measurement->output_voltage_uncalibrated_mv,
      measurement->input_voltage_raw_mv, (long)measurement->input_current_raw_ua,
      (long)measurement->output_current_uncalibrated_ua,
      (unsigned long)measurement->output_power_raw_mw, request->temperature_decic,
      measurement->data_valid ? 1U : 0U,
      (unsigned long)(now - measurement->last_valid_sample_time_ms),
      (unsigned long)measurement->sample_sequence);
    DebugConsole_Write(event);
    DebugConsole_Write(s_fault_context);
  }
  s_calibration_active = false;
  s_last_debug_time_ms = now - POWER_CONTROL_DEBUG_PERIOD_MS;
  PowerControl_ParkDac();
}

/* Protection only shuts down: it never adjusts an operating DAC code. */
static void PowerControl_CheckProtections(uint32_t now,
  const PowerControl_Request_t *request,
  const PowerMonitor_Snapshot_t *measurement)
{
  uint32_t timeout = PowerMonitor_GetSamplePeriod() * POWER_CFG_MONITOR_LOSS_PERIODS;
  if (timeout < POWER_CFG_MONITOR_LOSS_MIN_MS) { timeout = POWER_CFG_MONITOR_LOSS_MIN_MS; }
  bool usable = measurement != NULL && measurement->data_valid &&
    measurement->input_online && measurement->output_online &&
    now - measurement->last_valid_sample_time_ms <= timeout;
  bool fresh = usable && measurement->sample_sequence != s_last_sample_sequence;
  if (fresh) { s_last_fresh_time_ms = measurement->last_valid_sample_time_ms; }
  if (request->temperature_valid &&
      request->temperature_decic >= POWER_CFG_OVER_TEMPERATURE_DECIC)
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_OVERTEMPERATURE, request, measurement);
    return;
  }
  /* Missing or frozen frames cannot keep the output alive indefinitely. */
  if (now - s_last_fresh_time_ms >= timeout)
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_MONITOR_OFFLINE, request, measurement);
    return;
  }
  if (!usable)
  {
    s_uv_timer.active = s_ov_timer.active = false;
    return;
  }
  if (!fresh) { return; }
  s_last_sample_sequence = measurement->sample_sequence;
  int32_t output_ua = measurement->output_current_raw_ua;
  if (measurement->output_current_uncalibrated_ua > output_ua)
  { output_ua = measurement->output_current_uncalibrated_ua; }
  if (measurement->input_current_raw_ua > (int32_t)POWER_CFG_INPUT_TRIP_MA * 1000L)
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_INPUT_OVERCURRENT, request, measurement);
    return;
  }
  if (output_ua > (int32_t)s_status.effective_current_limit_ma * 1000L ||
      output_ua > (int32_t)POWER_CFG_OUTPUT_TRIP_MA * 1000L)
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT, request, measurement);
    return;
  }
  uint16_t vout = measurement->output_voltage_raw_mv;
  uint32_t decay_dt = now - s_ov_reference_time_ms;
  if (!s_calibration_active && s_ov_reference_mv > s_setpoint_mv && decay_dt)
  {
    uint32_t tau=POWER_CFG_OUTPUT_DISCHARGE_TAU_MS;
    uint32_t ceiling=(uint32_t)(((uint64_t)s_ov_reference_mv*tau + tau+decay_dt-1U)/(tau+decay_dt));
    s_ov_reference_mv = ceiling > s_setpoint_mv ? (uint16_t)ceiling : s_setpoint_mv;
  }
  s_ov_reference_time_ms=now;
  uint32_t margin = (uint32_t)s_ov_reference_mv * POWER_CFG_OUTPUT_OV_PERMILLE / 1000U;
  if (margin < POWER_CFG_OUTPUT_OV_MARGIN_MV) { margin = POWER_CFG_OUTPUT_OV_MARGIN_MV; }
  bool relative_ov = !s_calibration_active &&
    vout > (uint32_t)s_ov_reference_mv + margin;
  if (vout > POWER_CFG_OUTPUT_ABSOLUTE_OV_MV ||
      PowerControl_Qualified(&s_ov_timer, relative_ov, now, POWER_CFG_OUTPUT_OV_DELAY_MS))
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE, request, measurement);
    return;
  }
  /* The RC ceiling decays even if output stops falling. Actual falling voltage
   * can lower it faster, but can never hold the old reference indefinitely.
   * Passive output capacitors need time to discharge after a downward SET.
   * Follow the falling voltage ceiling without allowing it to rise again. */
  if (!s_calibration_active && vout < s_ov_reference_mv)
  { s_ov_reference_mv = vout > s_setpoint_mv ? vout : s_setpoint_mv; }
  if (PowerControl_Qualified(&s_uv_timer,
      measurement->input_voltage_raw_mv < POWER_CFG_INPUT_UV_MV,
      now, POWER_CFG_INPUT_UV_DELAY_MS))
  {
    PowerControl_Trip(now, POWER_CONTROL_FAULT_INPUT_UNDERVOLTAGE, request, measurement);
    return;
  }
  /* A collapsed output alone is not evidence of a short circuit.
   * Short-circuit current is shut down by the current checks above, on the
   * first valid sample exceeding a limit, without a timer or voltage gate. */
}

void PowerControl_ServiceVoltageTrim(uint32_t now,
  const PowerControl_Request_t *request,const PowerMonitor_Snapshot_t *m)
{
  if (request==NULL || !request->output_requested || !s_status.output_enabled ||
      s_status.fault!=POWER_CONTROL_FAULT_NONE || s_calibration_active ||
      request->voltage_setpoint_mv!=s_trim_target_mv) { return; }
  if (m==NULL || !m->data_valid || !m->input_online || !m->output_online ||
      now-m->last_valid_sample_time_ms>POWER_CFG_TRIM_MAX_SAMPLE_GAP_MS)
  { s_status.pid_enabled=false; s_trim_history=false; s_trim_integral=0.0f; return; }
  if (s_trim_have_sample && m->sample_sequence==s_trim_sequence) { return; }
  uint32_t dt=s_trim_have_sample ? m->last_valid_sample_time_ms-s_trim_time : 0U;
  s_trim_sequence=m->sample_sequence; s_trim_time=m->last_valid_sample_time_ms;
  s_trim_have_sample=true;
  int32_t error=(int32_t)s_trim_target_mv-m->output_voltage_raw_mv;
  int32_t magnitude=error<0 ? -error : error;
  s_status.pid_error_mv=error; s_status.pid_feedback_mv=m->output_voltage_raw_mv;
  s_status.pid_interval_ms=dt; s_status.pid_delta=0;
  s_status.pid_enabled=magnitude<=POWER_CFG_TRIM_BAND_MV;
  if (!s_status.pid_enabled || dt==0U || dt>POWER_CFG_TRIM_MAX_SAMPLE_GAP_MS)
  { s_trim_history=false; s_trim_integral=0.0f; return; }
  float proportional=-POWER_CFG_TRIM_KP*(float)error/s_trim_mv_per_code;
  if (!s_trim_history)
  {
    /* Bumpless gate entry: reconstruct the held operating point. */
    s_trim_integral=(float)((int32_t)s_applied_dac_code-s_trim_base_code)-proportional;
    s_trim_history=true;
  }
  if (magnitude<=POWER_CFG_TRIM_DEADBAND_MV) { return; }
  float integral=s_trim_integral-POWER_CFG_TRIM_KI_PER_SECOND*
    ((float)dt/1000.0f)*(float)error/s_trim_mv_per_code;
  float next=(float)s_trim_base_code+integral+proportional;
  if (next<0.0f) { next=0.0f; }
  else if (next>(float)MCP4725_MAX_CODE) { next=(float)MCP4725_MAX_CODE; }
  /* One-code slew limit and tracking anti-windup in continuous code units. */
  if (next>(float)s_applied_dac_code+1.0f) { next=(float)s_applied_dac_code+1.0f; }
  else if (next<(float)s_applied_dac_code-1.0f) { next=(float)s_applied_dac_code-1.0f; }
  integral=next-(float)s_trim_base_code-proportional;
  /* A 0.65-code decision boundary adds quantization hysteresis. */
  uint16_t code=s_applied_dac_code;
  if (next<=(float)code-0.65f || next>=(float)code+0.65f) { code=(uint16_t)(next+0.5f); }
  if (now-s_trim_write_time<POWER_CFG_TRIM_DAC_INTERVAL_MS) { code=s_applied_dac_code; }
  uint16_t previous=s_applied_dac_code;
  if (code!=previous)
  {
    if (!PowerControl_WriteDac(code))
    { s_trim_history=false; return; }
    s_trim_write_time=now;
  }
  s_trim_integral=integral;
  s_status.pid_delta=(int16_t)((int32_t)code-previous); s_status.pid_updates++;

}

static void PowerControl_PrintDebug(uint32_t now,const PowerMonitor_Snapshot_t *measurement)
{(void)now;(void)measurement;}

uint16_t PowerControl_VoltageToDacCode(uint16_t voltage_mv)
{
  int64_t dac_voltage_uv;
  uint32_t code;
  uint16_t calibrated_code;

  if (voltage_mv < POWER_CFG_VOLTAGE_MIN_MV)
  {
    voltage_mv = POWER_CFG_VOLTAGE_MIN_MV;
  }
  else if (voltage_mv > POWER_CFG_VOLTAGE_MAX_MV)
  {
    voltage_mv = POWER_CFG_VOLTAGE_MAX_MV;
  }

  if (Calibration_MapDac(voltage_mv, &calibrated_code))
  {
    return calibrated_code;
  }

  /* KCL: Vbuf=Vfb+(Rinj/Rgnd)*Vfb-(Rinj/Rup)*(Vout-Vfb). */
  dac_voltage_uv = (int64_t)POWER_CFG_FB_REFERENCE_UV +
    ((int64_t)POWER_CFG_FB_INJECTION_OHM * POWER_CFG_FB_REFERENCE_UV /
     POWER_CFG_FB_LOWER_OHM) -
    ((int64_t)POWER_CFG_FB_INJECTION_OHM *
     ((int64_t)voltage_mv * 1000LL - POWER_CFG_FB_REFERENCE_UV) /
     POWER_CFG_FB_UPPER_OHM);
  if (dac_voltage_uv < 0)
  {
    dac_voltage_uv = 0;
  }
  if (dac_voltage_uv > (int64_t)POWER_CFG_DAC_REFERENCE_UV)
  {
    dac_voltage_uv = POWER_CFG_DAC_REFERENCE_UV;
  }
  code = (uint32_t)((dac_voltage_uv * POWER_CFG_DAC_SCALE_CODES +
                     (POWER_CFG_DAC_REFERENCE_UV / 2UL)) /
                    POWER_CFG_DAC_REFERENCE_UV);
  return (code > MCP4725_MAX_CODE) ? MCP4725_MAX_CODE : (uint16_t)code;
}


bool PowerControl_Init(void)
{
  uint32_t now = HAL_GetTick();
  PowerControl_DisableHardware();
  (void)memset(&s_status, 0, sizeof(s_status));
  s_applied_dac_code = 0U;
  s_setpoint_mv = 0U;
  s_calibration_active = false;
  PowerControl_ResetTrim();
  PowerControl_ResetProtection();
  s_last_dac_retry_time_ms = now;
  s_last_debug_time_ms = now - POWER_CONTROL_DEBUG_PERIOD_MS / 2U;
  s_fault_context[0] = '\0';
  s_status.effective_current_limit_ma = POWER_CFG_CURRENT_MIN_MA;
  return MCP4725_Init(&s_dac, &hi2c2, MCP4725_ADDRESS_7BIT) &&
         PowerControl_WriteDac(0U);
}

void PowerControl_Process(uint32_t now, const PowerControl_Request_t *request,
  const PowerMonitor_Snapshot_t *measurement)
{
  if (request == NULL) { return; }
  uint16_t previous_setpoint = s_setpoint_mv;
  s_setpoint_mv = request->voltage_setpoint_mv;
  s_status.effective_current_limit_ma = request->current_limit_ma;
  s_status.constant_current = false;
  if (!s_dac.ready && now - s_last_dac_retry_time_ms >= POWER_CONTROL_DAC_RETRY_MS)
  {
    s_last_dac_retry_time_ms = now;
    (void)MCP4725_Init(&s_dac, &hi2c2, MCP4725_ADDRESS_7BIT);
  }
  if (!request->output_requested)
  {
    s_calibration_active = false;
    PowerControl_ResetProtection();
    PowerControl_ParkDac();
    PowerControl_PrintDebug(now, measurement);
    return;
  }
  if (s_status.fault != POWER_CONTROL_FAULT_NONE)
  {
    PowerControl_ParkDac();
    PowerControl_PrintDebug(now, measurement);
    return;
  }
  if (!s_protection_active)
  {
    PowerControl_ResetProtection();
    s_protection_active = true;
    s_last_fresh_time_ms = now;
    s_ov_reference_mv = s_setpoint_mv;
    s_ov_reference_time_ms = now;
  }
  else if (s_setpoint_mv != previous_setpoint)
  {
    if (s_setpoint_mv > previous_setpoint || s_setpoint_mv > s_ov_reference_mv)
    { s_ov_reference_mv = s_setpoint_mv; }
    s_ov_reference_time_ms = now;
    s_ov_timer.active = false;
  }
  PowerControl_CheckProtections(now, request, measurement);
  if (s_status.fault == POWER_CONTROL_FAULT_NONE)
  {
    bool rebase=!s_status.output_enabled || s_trim_target_mv!=s_setpoint_mv;
    uint16_t target=s_calibration_active ? s_calibration_code :
                    rebase ? PowerControl_VoltageToDacCode(s_setpoint_mv) : s_applied_dac_code;
    if (s_calibration_active) { PowerControl_ResetTrim(); }
    if (!s_status.output_enabled || target!=s_applied_dac_code || (rebase && !s_calibration_active))
    {
      if (PowerControl_WriteDac(target))
      {
        if (!s_calibration_active) { PowerControl_PrimeTrim(s_setpoint_mv,target); }
        HAL_GPIO_WritePin(P_EN_Port,P_EN_Pin,GPIO_PIN_RESET);
        s_status.output_enabled=true;
      }
    }
    PowerControl_ServiceVoltageTrim(now,request,measurement);
  }
  PowerControl_PrintDebug(now, measurement);
}

void PowerControl_ServiceProtections(uint32_t now,
  const PowerControl_Request_t *request, const PowerMonitor_Snapshot_t *measurement)
{
  if (request != NULL && s_status.output_enabled && s_protection_active &&
      s_status.fault == POWER_CONTROL_FAULT_NONE)
  { PowerControl_CheckProtections(now, request, measurement); }
}

void PowerControl_ServiceCurrentSample(uint32_t now, bool output,
  int32_t calibrated_ua, int32_t uncalibrated_ua, uint16_t voltage_mv,
  const PowerControl_Request_t *request, const PowerMonitor_Snapshot_t *measurement)
{
  if (request == NULL || !s_status.output_enabled || !s_protection_active ||
      s_status.fault != POWER_CONTROL_FAULT_NONE) { return; }
  int32_t current = calibrated_ua > uncalibrated_ua ? calibrated_ua : uncalibrated_ua;
  bool bad = output ? current > (int32_t)s_status.effective_current_limit_ma*1000L ||
                       current > (int32_t)POWER_CFG_OUTPUT_TRIP_MA*1000L :
                      current > (int32_t)POWER_CFG_INPUT_TRIP_MA*1000L;
  if (!bad) { return; }
  PowerMonitor_Snapshot_t context;
  if (measurement != NULL) { context=*measurement; }
  else { (void)memset(&context,0,sizeof(context)); }
  if (output)
  {
    context.output_current_raw_ua=calibrated_ua;
    context.output_current_uncalibrated_ua=uncalibrated_ua;
    context.output_voltage_raw_mv=voltage_mv;
  }
  else { context.input_current_raw_ua=uncalibrated_ua; context.input_voltage_raw_mv=voltage_mv; }
  /* One fresh valid channel is sufficient for current shutdown. The opposite
   * channel may be stale/offline; do not wait for a complete pair. */
  /* Preserve the paired voltage sample age; this callback only certifies the
   * current channel as fresh. Event time is the actual current-read time. */
  PowerControl_Trip(now,output ? POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT :
                    POWER_CONTROL_FAULT_INPUT_OVERCURRENT,request,&context);
}

void PowerControl_ResetVoltageLearning(void)
{
  /* Compatibility API: the PI never learns or modifies calibration records. */
}

bool PowerControl_BeginDacCalibration(uint16_t target_mv)
{
  if (s_calibration_active || s_status.fault != POWER_CONTROL_FAULT_NONE ||
      target_mv < POWER_CFG_VOLTAGE_MIN_MV || target_mv > POWER_CFG_VOLTAGE_MAX_MV)
  { return false; }
  s_calibration_code = PowerControl_VoltageToDacCode(target_mv);
  Calibration_Data_t data; Calibration_Get(&data);
  for (unsigned int i = 0U; i < CALIBRATION_POINT_COUNT; i++)
  {
    if (Calibration_PointTarget(i) == target_mv && (data.point_mask & (1U << i)) != 0U)
    { s_calibration_code = data.points[i].dac_code; break; }
  }
  PowerControl_ResetTrim();
  s_calibration_active = true;
  return true;
}

bool PowerControl_IsDacCalibrationActive(void) { return s_calibration_active; }

bool PowerControl_DacCalibrationReady(const PowerMonitor_Snapshot_t *measurement)
{
  (void)measurement;
  /* ready means the selected code has been written, not analogue stability. */
  return s_calibration_active && s_status.output_enabled &&
         s_status.fault == POWER_CONTROL_FAULT_NONE &&
         s_applied_dac_code == s_calibration_code;
}

bool PowerControl_StepDacCalibration(int16_t delta,
  const PowerMonitor_Snapshot_t *measurement)
{
  if ((delta != -5 && delta != -1 && delta != 1 && delta != 5) ||
      !PowerControl_DacCalibrationReady(measurement)) { return false; }
  int32_t next = (int32_t)s_calibration_code + delta;
  if (next < 0 || next > MCP4725_MAX_CODE) { return false; }
  if (!PowerControl_WriteDac((uint16_t)next)) { return false; }
  s_calibration_code = (uint16_t)next;
  return true;
}

void PowerControl_EndDacCalibration(void)
{
  PowerControl_ParkDac();
  s_calibration_active = false;
  PowerControl_ResetProtection();
}

bool PowerControl_ClearFault(void)
{
  PowerControl_ParkDac();
  PowerControl_ResetProtection();
  s_status.fault = POWER_CONTROL_FAULT_NONE;
  s_fault_context[0] = '\0';
  s_calibration_active = false;
  DebugConsole_Write("[FAULT] cleared; output remains OFF\r\n");
  return true;
}

void PowerControl_GetStatus(PowerControl_Status_t *status)
{
  if (status != NULL) {
    *status = s_status;
    status->ce_gpio_high = HAL_GPIO_ReadPin(P_EN_Port, P_EN_Pin) == GPIO_PIN_SET;
  }
}
const char *PowerControl_GetFaultContext(void) { return s_fault_context; }
const char *PowerControl_GetFaultName(PowerControl_Fault_t fault)
{
  switch (fault)
  {
    case POWER_CONTROL_FAULT_NONE: return "NONE";
    case POWER_CONTROL_FAULT_MONITOR_OFFLINE: return "MON_OFFLINE";
    case POWER_CONTROL_FAULT_INPUT_UNDERVOLTAGE: return "VIN_UV";
    case POWER_CONTROL_FAULT_OUTPUT_OVERVOLTAGE: return "VOUT_OV";
    case POWER_CONTROL_FAULT_OVERTEMPERATURE: return "OTP";
    case POWER_CONTROL_FAULT_INPUT_OVERCURRENT: return "IIN_OC";
    case POWER_CONTROL_FAULT_OUTPUT_OVERCURRENT: return "IOUT_OC";
    default: return "UNKNOWN";
  }
}
