/**
  ******************************************************************************
  * @file    debug_console.c
  * @brief   双串口命令、日志广播和结构化遥测实现。
  ******************************************************************************
  */

#include "debug_console.h"

#include "calibration.h"
#include "firmware_version.h"
#include "ota_updater.h"
#include "ota_storage.h"
#include "power_config.h"
#include "ui_page.h"
#include "usart.h"
#include "w25q256.h"
#include "user_settings.h"
#include "wire_protocol.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_CONSOLE_CHANNEL_COUNT             2U
#define DEBUG_CONSOLE_RX_BUFFER_SIZE         1024U
#define DEBUG_CONSOLE_TX_BUFFER_SIZE         1024U
#define DEBUG_CONSOLE_LINE_SIZE               448U
#define DEBUG_CONSOLE_TELEMETRY_SIZE          576U
#define DEBUG_CONSOLE_BT_TX_CHUNK_MAX         240U
#define DEBUG_CONSOLE_DEFAULT_RATE_MS          40U
#define DEBUG_CONSOLE_MIN_RATE_MS              20U
#define DEBUG_CONSOLE_MAX_RATE_MS            5000U
#define DEBUG_CONSOLE_RX_BYTES_PER_PASS       256U
#define DEBUG_CONSOLE_COMMANDS_PER_PASS         2U
#define DEBUG_CONSOLE_REPLY_RESERVE           320U
#define DEBUG_CONSOLE_RX_RECOVERY_MS           20U

typedef enum
{
  DEBUG_CHANNEL_USB = 0,
  DEBUG_CHANNEL_BLUETOOTH
} DebugConsole_ChannelId_t;

typedef struct
{
  uint16_t sequence, request_length, response_length;
  bool valid;
  uint8_t request[OTA_TRANSFER_CHUNK_MAX+5U];
  uint8_t response[544];
} DebugConsole_Replay_t;
typedef struct
{
  WireParser_t parser;
  DebugConsole_Replay_t replay[4];
  uint32_t hello_nonce,last_telemetry_ms,telemetry_rate_ms;
  uint16_t reply_sequence,event_sequence;
  uint8_t command_id,replay_next,replay_current;
  bool binary_mode,legacy_mode,capturing;
  UART_HandleTypeDef *uart;
  uint8_t rx_interrupt_byte;
  uint8_t rx_buffer[DEBUG_CONSOLE_RX_BUFFER_SIZE];
  uint8_t tx_buffer[DEBUG_CONSOLE_TX_BUFFER_SIZE];
  char line_buffer[DEBUG_CONSOLE_LINE_SIZE];
  volatile uint16_t rx_head;
  volatile uint16_t rx_tail;
  volatile uint16_t tx_head;
  volatile uint16_t tx_tail;
  volatile uint16_t tx_active_length;
  uint16_t max_tx_chunk;
  uint16_t line_length;
  bool discarding_line;
  volatile uint32_t rx_overflow_count;
  volatile uint32_t tx_overflow_count;
  volatile uint32_t uart_error_count;
  volatile uint32_t rx_recovery_count;
  volatile uint32_t tx_timeout_count;
  volatile uint32_t rx_error_time_ms;
  volatile uint32_t tx_start_time_ms;
  volatile bool rx_restart_pending;
  volatile bool rx_resync_pending;
  volatile bool tx_active;
  bool logs_enabled;
} DebugConsole_Channel_t;

static DebugConsole_Channel_t s_channels[DEBUG_CONSOLE_CHANNEL_COUNT];
static DebugConsole_Channel_t *s_ota_owner;
static DebugConsole_Channel_t *s_curve_owner;
static uint8_t s_curve_index;
static UI_ControlRequest_t s_curve_previous_request;
static bool s_curve_restore_pending;
static bool s_initialized;
static uint8_t s_last_fault_packet[48];
static uint16_t s_last_fault_length;

static void DebugConsole_RestoreCurveRequest(void)
{
  UI_ControlRequest_t request;
  PowerControl_Status_t live;
  if (!s_curve_restore_pending || PowerControl_IsDacCalibrationActive()) { return; }
  UI_GetControlRequest(&request);
  PowerControl_GetStatus(&live);
  if (request.output_requested || live.output_enabled) { return; }
  (void)UI_SetVoltageSetpoint(s_curve_previous_request.voltage_setpoint_mv);
  (void)UI_SetCurrentLimit(s_curve_previous_request.current_limit_ma);
  s_curve_restore_pending = false;
  s_curve_owner = NULL;
}

static void DebugConsole_SendPacket(DebugConsole_Channel_t *channel,uint8_t type,
  uint16_t sequence,const uint8_t *payload,uint16_t length,bool telemetry);
static void DebugConsole_WriteRaw(DebugConsole_Channel_t *channel,const uint8_t *bytes,
  uint16_t length,bool telemetry);

static uint32_t DebugConsole_EnterCritical(void);
static void DebugConsole_ExitCritical(uint32_t primask);
static DebugConsole_Channel_t *DebugConsole_FindChannel(
  UART_HandleTypeDef *uart);
static void DebugConsole_KickTx(DebugConsole_Channel_t *channel);
static void DebugConsole_WriteChannel(DebugConsole_Channel_t *channel,
                                      const char *text,
                                      bool obey_log_switch);
static bool DebugConsole_PopRx(DebugConsole_Channel_t *channel,
                               uint8_t *value);
static void DebugConsole_SendAck(DebugConsole_Channel_t *channel,
                                 const char *command,
                                 const char *detail);
static void DebugConsole_SendError(DebugConsole_Channel_t *channel,
                                   const char *command,
                                   const char *reason);
static void DebugConsole_SendInfo(DebugConsole_Channel_t *channel);
static void DebugConsole_SendCalibration(
  DebugConsole_Channel_t *channel,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status);
static void DebugConsole_SendStatus(
  DebugConsole_Channel_t *channel,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature,
  bool obey_log_switch);
static void DebugConsole_Uppercase(char *text);
static int DebugConsole_HexNibble(char value);
static void DebugConsole_HandleFirmwareCommand(DebugConsole_Channel_t *channel,
                                                const char *command,
                                                const PowerControl_Status_t *power_status);
static bool DebugConsole_ParseUnsigned(const char *command,
                                       const char *prefix,
                                       uint32_t *value);
static bool DebugConsole_CalibrationAllowed(
  const PowerControl_Status_t *power_status);
static void DebugConsole_HandleCommand(
  DebugConsole_Channel_t *channel,
  char *line,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature);
static void DebugConsole_ProcessChannel(
  DebugConsole_Channel_t *channel,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature);

/** @brief 进入短临界区并返回进入前的全局中断状态。 */
static uint32_t DebugConsole_EnterCritical(void)
{
#ifdef DEBUG_CONSOLE_HOST_TEST
  return 0U;
#else
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
#endif
}

/** @brief 按进入前状态退出短临界区。 */
static void DebugConsole_ExitCritical(uint32_t primask)
{
#ifdef DEBUG_CONSOLE_HOST_TEST
  (void)primask;
#else
  if (primask == 0U)
  {
    __enable_irq();
  }
#endif
}

/* SR then DR clears PE/FE/NE/ORE even when RXNE is NOT set. The vendor HAL
 * does not clear this case; immediate RX rearming caused an IRQ storm. */
static void DebugConsole_ClearRxErrors(UART_HandleTypeDef *uart)
{
  __HAL_UART_CLEAR_PEFLAG(uart);
#ifdef DEBUG_CONSOLE_HOST_TEST
  uart->Instance->SR &= ~(USART_SR_PE | USART_SR_FE | USART_SR_NE |
                         USART_SR_ORE | USART_SR_RXNE);
#endif
}

/** @brief 根据HAL句柄找到对应的调试通道。 */
static DebugConsole_Channel_t *DebugConsole_FindChannel(
  UART_HandleTypeDef *uart)
{
  uint8_t index;

  for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
  {
    if (s_channels[index].uart == uart)
    {
      return &s_channels[index];
    }
  }
  return NULL;
}

/** @brief 若当前串口空闲，则启动环形缓冲区中的下一段连续发送。 */
static void DebugConsole_KickTx(DebugConsole_Channel_t *channel)
{
  uint16_t length;
  uint16_t tail;
  uint32_t primask;

  if ((channel == NULL) || (channel->uart == NULL))
  {
    return;
  }

  primask = DebugConsole_EnterCritical();
  if (channel->tx_active || (channel->tx_head == channel->tx_tail))
  {
    DebugConsole_ExitCritical(primask);
    return;
  }

  tail = channel->tx_tail;
  if (channel->tx_head > tail)
  {
    length = (uint16_t)(channel->tx_head - tail);
  }
  else
  {
    length = (uint16_t)(DEBUG_CONSOLE_TX_BUFFER_SIZE - tail);
  }
  if ((channel->max_tx_chunk > 0U) && (length > channel->max_tx_chunk))
  {
    length = channel->max_tx_chunk;
  }
  channel->tx_active = true;
  channel->tx_active_length = length;
  channel->tx_start_time_ms = HAL_GetTick();
  DebugConsole_ExitCritical(primask);

#if defined(DEBUG_CONSOLE_HOST_TEST) && !defined(DEBUG_CONSOLE_DMA_HOST_TEST)
  if (HAL_UART_Transmit_IT(channel->uart,
#else
  if (HAL_UART_Transmit_DMA(channel->uart,
#endif
                           &channel->tx_buffer[tail],
                           length) != HAL_OK)
  {
    primask = DebugConsole_EnterCritical();
    channel->tx_active = false;
    channel->tx_active_length = 0U;
    DebugConsole_ExitCritical(primask);
  }
}

static void DebugConsole_WriteRaw(DebugConsole_Channel_t *channel,const uint8_t *bytes,
  uint16_t length,bool telemetry)
{
  uint32_t primask=DebugConsole_EnterCritical();
  uint16_t used=(uint16_t)((channel->tx_head+DEBUG_CONSOLE_TX_BUFFER_SIZE-channel->tx_tail)%DEBUG_CONSOLE_TX_BUFFER_SIZE);
  uint16_t available=(uint16_t)(DEBUG_CONSOLE_TX_BUFFER_SIZE-used-1U);
  if (available<length || (telemetry && available<length+DEBUG_CONSOLE_REPLY_RESERVE))
  {channel->tx_overflow_count++;DebugConsole_ExitCritical(primask);return;}
  for(unsigned i=0U;i<length;i++)
  {channel->tx_buffer[channel->tx_head]=bytes[i];channel->tx_head=(uint16_t)((channel->tx_head+1U)%DEBUG_CONSOLE_TX_BUFFER_SIZE);}
  DebugConsole_ExitCritical(primask);
  DebugConsole_KickTx(channel);
}
static void DebugConsole_SendPacket(DebugConsole_Channel_t *channel,uint8_t type,
  uint16_t sequence,const uint8_t *payload,uint16_t length,bool telemetry)
{
  uint8_t packet[WIRE_MAX_FRAME];
  uint16_t count=Wire_Encode(packet,type,sequence,payload,length);
  if (!count) {return;}
  if(channel->capturing)
  {
    DebugConsole_Replay_t *r=&channel->replay[channel->replay_current];
    if(r->response_length+count<=sizeof(r->response))
    {(void)memcpy(r->response+r->response_length,packet,count);r->response_length+=count;}
    else {r->valid=false;}
  }
  DebugConsole_WriteRaw(channel,packet,count,telemetry);
}

/** @brief 将字符串放入指定通道的非阻塞发送环形缓冲区。 */
static void DebugConsole_WriteChannel(DebugConsole_Channel_t *channel,
                                      const char *text,
                                      bool obey_log_switch)
{
  if(channel && text && channel->binary_mode)
  {
    if(obey_log_switch){return;} /* no periodic text formatting/traffic in binary mode */
    uint16_t length=(uint16_t)strlen(text);
    if(length>WIRE_MAX_PAYLOAD){length=WIRE_MAX_PAYLOAD;}
    DebugConsole_SendPacket(channel,WIRE_TEXT,channel->reply_sequence,
      (const uint8_t *)text,length,false); return;
  }
  if(channel && !channel->legacy_mode){return;}

  uint16_t next_head;
  size_t text_length;
  uint16_t used;
  uint16_t available;
  uint32_t primask;

  if ((channel == NULL) || (text == NULL) ||
      (obey_log_switch && !channel->logs_enabled))
  {
    return;
  }

  text_length = strlen(text);
  primask = DebugConsole_EnterCritical();
  used = (uint16_t)((channel->tx_head + DEBUG_CONSOLE_TX_BUFFER_SIZE -
                    channel->tx_tail) % DEBUG_CONSOLE_TX_BUFFER_SIZE);
  available = (uint16_t)(DEBUG_CONSOLE_TX_BUFFER_SIZE - 1U - used);
  /* 整行入队或整行丢弃，避免缓冲满时截断协议帧；日志为命令回复让出空间。 */
  if ((text_length > available) ||
      (obey_log_switch &&
       (text_length + DEBUG_CONSOLE_REPLY_RESERVE > available)))
  {
    channel->tx_overflow_count++;
    DebugConsole_ExitCritical(primask);
    return;
  }
  while (*text != '\0')
  {
    next_head = (uint16_t)(channel->tx_head + 1U);
    if (next_head >= DEBUG_CONSOLE_TX_BUFFER_SIZE)
    {
      next_head = 0U;
    }
    if (next_head == channel->tx_tail)
    {
      channel->tx_overflow_count++;
      break;
    }
    channel->tx_buffer[channel->tx_head] = (uint8_t)*text;
    channel->tx_head = next_head;
    text++;
  }
  DebugConsole_ExitCritical(primask);
  DebugConsole_KickTx(channel);
}

/** @brief 从指定通道接收环形缓冲区取出一个字节。 */
static bool DebugConsole_PopRx(DebugConsole_Channel_t *channel,
                               uint8_t *value)
{
  uint16_t next_tail;
  uint32_t primask;

  if ((channel == NULL) || (value == NULL))
  {
    return false;
  }

  primask = DebugConsole_EnterCritical();
  if (channel->rx_head == channel->rx_tail)
  {
    DebugConsole_ExitCritical(primask);
    return false;
  }
  *value = channel->rx_buffer[channel->rx_tail];
  next_tail = (uint16_t)(channel->rx_tail + 1U);
  if (next_tail >= DEBUG_CONSOLE_RX_BUFFER_SIZE)
  {
    next_tail = 0U;
  }
  channel->rx_tail = next_tail;
  DebugConsole_ExitCritical(primask);
  return true;
}

/** @brief 发送一条结构化成功响应。 */
static void DebugConsole_SendAck(DebugConsole_Channel_t *channel,
                                 const char *command,
                                 const char *detail)
{
  if(channel->binary_mode)
  {
    uint8_t payload[161];payload[0]=channel->command_id;
    uint16_t length=(uint16_t)strlen(detail);if(length>160U){length=160U;}
    (void)memcpy(payload+1,detail,length);
    DebugConsole_SendPacket(channel,WIRE_ACK,channel->reply_sequence,payload,length+1U,false);return;
  }

  char text[160];

  (void)snprintf(text, sizeof(text), "@ACK,cmd=%s,detail=%s\r\n",
                 command, detail);
  DebugConsole_WriteChannel(channel, text, false);
}

/** @brief 发送一条结构化错误响应。 */
static void DebugConsole_SendError(DebugConsole_Channel_t *channel,
                                   const char *command,
                                   const char *reason)
{
  if(channel->binary_mode)
  {
    uint8_t payload[161];payload[0]=channel->command_id;
    uint16_t length=(uint16_t)strlen(reason);if(length>160U){length=160U;}
    (void)memcpy(payload+1,reason,length);
    DebugConsole_SendPacket(channel,WIRE_ERROR,channel->reply_sequence,payload,length+1U,false);return;
  }

  char text[160];

  (void)snprintf(text, sizeof(text), "@ERR,cmd=%s,reason=%s\r\n",
                 command, reason);
  DebugConsole_WriteChannel(channel, text, false);
}

#if !defined(DEBUG_CONSOLE_HOST_TEST) || defined(DEBUG_CONSOLE_COMMAND_HOST_TEST)
/** @brief 发送一帧可直接由电脑上位机解析的状态遥测。 */
static void DebugConsole_SendStatus(
  DebugConsole_Channel_t *channel,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature,
  bool obey_log_switch)
{
  if(channel && channel->binary_mode && measurement && power_status && temperature)
  {
    if(obey_log_switch && !channel->logs_enabled){return;}
    UI_ControlRequest_t req;UI_GetControlRequest(&req);
    PowerControl_Status_t live;PowerControl_GetStatus(&live);
    uint8_t payload[80],*p=payload;
    Wire_Put32(&p,system_time_ms);Wire_Put32(&p,measurement->sample_sequence);
    Wire_Put32(&p,measurement->last_valid_sample_time_ms);
    Wire_Put16(&p,measurement->input_voltage_mv);Wire_Put32(&p,(uint32_t)measurement->input_current_ua);
    Wire_Put32(&p,measurement->input_power_mw);Wire_Put16(&p,measurement->output_voltage_mv);
    Wire_Put32(&p,(uint32_t)measurement->output_current_ua);Wire_Put32(&p,measurement->output_power_mw);
    Wire_Put16(&p,(uint16_t)temperature->temperature_decic);
    Wire_Put16(&p,req.voltage_setpoint_mv);Wire_Put16(&p,req.current_limit_ma);
    Wire_Put16(&p,live.effective_current_limit_ma);Wire_Put16(&p,live.dac_code);
    Wire_Put16(&p,measurement->output_voltage_uncalibrated_mv);
    Wire_Put32(&p,(uint32_t)measurement->output_current_uncalibrated_ua);
    Wire_Put16(&p,measurement->output_voltage_raw_mv);
    uint16_t flags=(req.output_requested?1U:0U)|(live.output_enabled?2U:0U)|
      (measurement->data_valid?4U:0U)|(live.ce_gpio_high?8U:0U)|
      (measurement->input_online?16U:0U)|(measurement->output_online?32U:0U)|
      (measurement->ripple_valid?64U:0U)|(temperature->valid?128U:0U)|
      (live.pid_enabled?256U:0U)|512U; /* bits8 active PI, bit9 gated-PI firmware */
    Wire_Put16(&p,flags);*p++=(uint8_t)live.fault;
    Wire_Put16(&p,measurement->ripple_vpp_mv);Wire_Put16(&p,measurement->sample_hz);
    Wire_Put16(&p,measurement->sample_maxgap_ms);
    Wire_Put32(&p,live.dac_write_count);Wire_Put32(&p,live.dac_write_time_ms);
    Wire_Put16(&p,(uint16_t)channel->rx_overflow_count);Wire_Put16(&p,(uint16_t)channel->tx_overflow_count);
    Wire_Put16(&p,(uint16_t)measurement->input_error_count);Wire_Put16(&p,(uint16_t)measurement->output_error_count);
    Wire_Put16(&p,temperature->raw_adc);
    DebugConsole_SendPacket(channel,WIRE_TELEMETRY,obey_log_switch ? ++channel->event_sequence : channel->reply_sequence,
      payload,(uint16_t)(p-payload),obey_log_switch);
    if(!obey_log_switch && live.fault!=POWER_CONTROL_FAULT_NONE && s_last_fault_length)
    {DebugConsole_SendPacket(channel,WIRE_FAULT,channel->reply_sequence,s_last_fault_packet,s_last_fault_length,false);}
    return;
  }
  if(channel && !channel->legacy_mode){return;}

  UI_ControlRequest_t request;
  static char text[DEBUG_CONSOLE_TELEMETRY_SIZE];
  int length;

  if ((channel == NULL) || (measurement == NULL) ||
      (power_status == NULL) || (temperature == NULL))
  {
    return;
  }

  PowerControl_Status_t live;
  PowerControl_GetStatus(&live);
  power_status = &live;
  UI_GetControlRequest(&request);
  length = snprintf(
    text,
    sizeof(text),
    "@TEL,t=%lu,vin=%u,iin=%ld,pin=%lu,vout=%u,iout=%ld,pout=%lu,"
    "temp=%d,setv=%u,seti=%u,ilim=%u,req=%u,out=%u,cc=%u,dac=%u,valid=%u,"
    "vraw=%u,iraw=%ld,pid=%u,err=%ld,delta=%d,updates=%lu,vfb=%u,pidrev=%lu,pdt=%lu,seq=%lu,samplet=%lu,"
    "fault=%s,rxdrop=%lu,txdrop=%lu,vfast=%u,cegpio=%u,dw=%lu,dwt=%lu\r\n",
    (unsigned long)system_time_ms,
    (unsigned int)measurement->input_voltage_mv,
    (long)measurement->input_current_ua,
    (unsigned long)measurement->input_power_mw,
    (unsigned int)measurement->output_voltage_mv,
    (long)measurement->output_current_ua,
    (unsigned long)measurement->output_power_mw,
    (int)temperature->temperature_decic,
    (unsigned int)request.voltage_setpoint_mv,
    (unsigned int)request.current_limit_ma,
    (unsigned int)power_status->effective_current_limit_ma,
    request.output_requested ? 1U : 0U,
    power_status->output_enabled ? 1U : 0U,
    power_status->constant_current ? 1U : 0U,
    (unsigned int)power_status->dac_code,
    measurement->data_valid ? 1U : 0U,
    (unsigned int)measurement->output_voltage_uncalibrated_mv,
    (long)measurement->output_current_uncalibrated_ua,
    power_status->pid_enabled ? 1U : 0U,
    (long)power_status->pid_error_mv, (int)power_status->pid_delta,
    (unsigned long)power_status->pid_updates, power_status->pid_feedback_mv,
    (unsigned long)power_status->pid_revision,
    (unsigned long)power_status->pid_interval_ms,
    (unsigned long)measurement->sample_sequence,
    (unsigned long)measurement->last_valid_sample_time_ms,
    PowerControl_GetFaultName(power_status->fault),
    (unsigned long)channel->rx_overflow_count,
    (unsigned long)channel->tx_overflow_count,
    measurement->output_voltage_raw_mv,
    power_status->ce_gpio_high ? 1U : 0U,
    (unsigned long)power_status->dac_write_count,
    (unsigned long)power_status->dac_write_time_ms);

  if (length > 0)
  {
    text[sizeof(text) - 1U] = '\0';
    DebugConsole_WriteChannel(channel, text, obey_log_switch);
  }
  if (!obey_log_switch && (power_status->fault != POWER_CONTROL_FAULT_NONE))
  { DebugConsole_WriteChannel(channel, PowerControl_GetFaultContext(), false); }
}


#endif

#if !defined(DEBUG_CONSOLE_HOST_TEST) || defined(DEBUG_CONSOLE_COMMAND_HOST_TEST)
/** @brief 发送固件、协议和物理通道信息。 */
static void DebugConsole_SendInfo(DebugConsole_Channel_t *channel)
{
  if(channel->binary_mode)
  {
    uint8_t payload[32],*p=payload;
    *p++=XIN_POWER_FIRMWARE_MAJOR;*p++=XIN_POWER_FIRMWARE_MINOR;
    *p++=XIN_POWER_FIRMWARE_PATCH;*p++=2U; /* Shared version, hardware V2. */
    Wire_Put16(&p,POWER_CFG_VOLTAGE_MIN_MV);Wire_Put16(&p,POWER_CFG_VOLTAGE_MAX_MV);
    Wire_Put16(&p,POWER_CFG_CURRENT_MIN_MA);Wire_Put16(&p,POWER_CFG_CURRENT_MAX_MA);
    Wire_Put16(&p,POWER_CFG_SAMPLE_PERIOD_MS);Wire_Put16(&p,20U);Wire_Put16(&p,channel->telemetry_rate_ms ? channel->telemetry_rate_ms : DEBUG_CONSOLE_DEFAULT_RATE_MS);
    Wire_Put16(&p,POWER_CFG_FB_INJECTION_OHM);Wire_Put16(&p,POWER_CFG_DAC_HARDWARE_TAG);
    *p++=CALIBRATION_POINT_COUNT;*p++=(W25Q256_IsReady()?1U:0U)|(OtaStorage_BootloaderPresent()?2U:0U);
    Wire_Put32(&p,115200U);
    OtaMetadata_t metadata;
    *p++=UserSettings_IsSaving()?8U:
      (OtaStorage_ReadMetadata(&metadata)?(uint8_t)metadata.state:0U);
    DebugConsole_SendPacket(channel,WIRE_INFO,channel->reply_sequence,payload,(uint16_t)(p-payload),false);return;
  }

  char text[448];
  OtaMetadata_t metadata;
  const char *ota_state = UserSettings_IsSaving() ? "SETTINGS_BUSY" : "NONE";

  if (!UserSettings_IsSaving() && OtaStorage_ReadMetadata(&metadata))
  {
    if (metadata.state == OTA_STATE_VERIFIED)
    {
      ota_state = "VERIFIED";
    }
    else if (metadata.state == OTA_STATE_STAGED)
    {
      ota_state = "STAGED";
    }
    else if (metadata.state == OTA_STATE_COPYING)
    {
      ota_state = "COPYING";
    }
    else if (metadata.state == OTA_STATE_TRIAL)
    {
      ota_state = "TRIAL";
    }
    else if (metadata.state == OTA_STATE_CONFIRMED)
    {
      ota_state = "CONFIRMED";
    }
    else if (metadata.state == OTA_STATE_ROLLBACK)
    {
      ota_state = "ROLLBACK";
    }
    else if (metadata.state == OTA_STATE_CANCELLED)
    {
      ota_state = "CANCELLED";
    }
  }

  (void)snprintf(text,
                 sizeof(text),
                 "@INFO,fw=" XIN_POWER_FIRMWARE_VERSION
                 ",proto=" XIN_POWER_PROTOCOL_VERSION
                 ",hw=" XIN_POWER_HARDWARE_ID
                 ",control=DAC_PI,caldiag=2,build=v623_gated_pi,sample_ms=2,ct_us=332,avg=1,src=BYPASS,offdac=0,rup=100000,rgnd=7620,rinj=5110"
                 ",usb=USART1,bt=VG6328A_USART2,"
                 "baud=115200,ref=3000mV,imax=8000mA,flash=W25Q256,flashok=%u,bl=%u,ota=%s\r\n",
                 W25Q256_IsReady() ? 1U : 0U,
                 OtaStorage_BootloaderPresent() ? 1U : 0U,
                 ota_state);
  DebugConsole_WriteChannel(channel, text, false);
}

/** @brief 返回当前校准系数、原始测量值和DAC状态。 */
static void DebugConsole_SendCalibration(
  DebugConsole_Channel_t *channel,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status)
{
  if(channel->binary_mode)
  {
    Calibration_Data_t cal;Calibration_Get(&cal);uint8_t payload[40],*p=payload;
    *p++=(cal.measurement_valid?1U:0U)|(cal.voltage_valid?2U:0U)|(cal.current_valid?4U:0U)|
      (cal.dac_valid?8U:0U)|(cal.loaded_from_flash?16U:0U);
    Wire_Put32(&p,(uint32_t)cal.voltage_gain_ppm);Wire_Put32(&p,(uint32_t)cal.voltage_offset_mv);
    Wire_Put32(&p,(uint32_t)cal.current_gain_ppm);Wire_Put32(&p,(uint32_t)cal.current_offset_ua);
    Wire_Put16(&p,cal.dac_voltage_1_mv);Wire_Put16(&p,cal.dac_code_1);
    Wire_Put16(&p,cal.dac_voltage_2_mv);Wire_Put16(&p,cal.dac_code_2);
    Wire_Put16(&p,measurement?measurement->output_voltage_uncalibrated_mv:0U);
    Wire_Put32(&p,measurement?(uint32_t)measurement->output_current_uncalibrated_ua:0U);
    Wire_Put16(&p,power_status?power_status->dac_code:0U);
    DebugConsole_SendPacket(channel,WIRE_CALIBRATION,channel->reply_sequence,payload,(uint16_t)(p-payload),false);return;
  }

  Calibration_Data_t calibration;
  static char text[256];

  Calibration_Get(&calibration);
  (void)snprintf(
    text,
    sizeof(text),
    "@CAL,valid=%u,vvalid=%u,ivalid=%u,dacvalid=%u,saved=%u,schema=2,vgain=%ld,voff=%ld,"
    "igain=%ld,ioff=%ld,v1=%u,c1=%u,v2=%u,c2=%u,vraw=%u,iraw=%ld,dac=%u\r\n",
    calibration.measurement_valid ? 1U : 0U,
    calibration.voltage_valid ? 1U : 0U,
    calibration.current_valid ? 1U : 0U,
    calibration.dac_valid ? 1U : 0U,
    calibration.loaded_from_flash ? 1U : 0U,
    (long)calibration.voltage_gain_ppm,
    (long)calibration.voltage_offset_mv,
    (long)calibration.current_gain_ppm,
    (long)calibration.current_offset_ua,
    (unsigned int)calibration.dac_voltage_1_mv,
    (unsigned int)calibration.dac_code_1,
    (unsigned int)calibration.dac_voltage_2_mv,
    (unsigned int)calibration.dac_code_2,
    (unsigned int)((measurement != NULL) ?
                   measurement->output_voltage_uncalibrated_mv : 0U),
    (long)((measurement != NULL) ?
           measurement->output_current_uncalibrated_ua : 0),
    (unsigned int)((power_status != NULL) ? power_status->dac_code : 0U));
  DebugConsole_WriteChannel(channel, text, false);
}

/** @brief 将命令副本转成大写，方便不区分大小写地解析。 */
static void DebugConsole_Uppercase(char *text)
{
  while ((text != NULL) && (*text != '\0'))
  {
    *text = (char)toupper((unsigned char)*text);
    text++;
  }
}

static int DebugConsole_HexNibble(char value)
{
  if ((value >= '0') && (value <= '9'))
  {
    return value - '0';
  }
  if ((value >= 'A') && (value <= 'F'))
  {
    return value - 'A' + 10;
  }
  return -1;
}

/** @brief Stop-and-wait firmware transfer over USB or Bluetooth SPP. */
static void DebugConsole_HandleFirmwareCommand(DebugConsole_Channel_t *channel,
                                                const char *command,
                                                const PowerControl_Status_t *power_status)
{
  OtaUpdater_Status_t status;
  unsigned long length;
  unsigned long crc;
  unsigned long version;
  unsigned long offset;
  char trailing;
  const char *cursor;
  char *after_number;
  uint8_t bytes[OTA_TRANSFER_CHUNK_MAX];
  uint32_t count;
  int high;
  int low;
  char detail[64];

  /* Both USART1/USB and paired USART2/Bluetooth SPP stage into W25Q256.
   * The bootloader consumes the same verified image after either link resets. */
  if (strcmp(command, "FW STATUS") == 0)
  {
    OtaUpdater_GetStatus(&status);
    (void)snprintf(detail, sizeof(detail), "%lu/%lu/%u",
                   (unsigned long)status.received,
                   (unsigned long)status.length,
                   status.verified ? 1U : 0U);
    DebugConsole_SendAck(channel, "FW_STATUS", detail);
    return;
  }
  if (sscanf(command, "FW BEGIN %lu %lx %lu %c",
             &length, &crc, &version, &trailing) == 3)
  {
    UI_ControlRequest_t request;
    UI_GetControlRequest(&request);
    if ((s_ota_owner != NULL) && (s_ota_owner != channel))
    {
      DebugConsole_SendError(channel, "FW_BEGIN", "LINK_BUSY");
    }
    else if (OtaUpdater_IsBusy())
    {
      DebugConsole_SendError(channel, "FW_BEGIN", "SESSION_ACTIVE");
    }
    else if (request.output_requested || (power_status == NULL) ||
        power_status->output_enabled)
    {
      DebugConsole_SendError(channel, "FW_BEGIN", "OUTPUT_MUST_BE_OFF");
    }
    else if (!OtaUpdater_Begin((uint32_t)length, (uint32_t)crc,
                                (uint32_t)version))
    {
      DebugConsole_SendError(channel, "FW_BEGIN", "FLASH_OR_RANGE");
    }
    else
    {
      s_ota_owner = channel;
      UI_ForceOutputOff();
      DebugConsole_SendAck(channel, "FW_BEGIN", "READY");
    }
    return;
  }
  if (strncmp(command, "FW DATA ", 8U) == 0)
  {
    if (s_ota_owner != channel)
    {
      DebugConsole_SendError(channel, "FW_DATA", "LINK_NOT_OWNER");
      return;
    }
    cursor = command + 8U;
    offset = strtoul(cursor, &after_number, 10);
    if ((after_number == cursor) || (*after_number != ' '))
    {
      DebugConsole_SendError(channel, "FW_DATA", "FORMAT");
      return;
    }
    cursor = after_number + 1;
    length = strlen(cursor);
    if ((length == 0U) || ((length & 1U) != 0U) ||
        ((length / 2U) > OTA_TRANSFER_CHUNK_MAX))
    {
      DebugConsole_SendError(channel, "FW_DATA", "LENGTH");
      return;
    }
    count = (uint32_t)(length / 2U);
    for (length = 0U; length < count; length++)
    {
      high = DebugConsole_HexNibble(cursor[length * 2U]);
      low = DebugConsole_HexNibble(cursor[length * 2U + 1U]);
      if ((high < 0) || (low < 0))
      {
        DebugConsole_SendError(channel, "FW_DATA", "HEX");
        return;
      }
      bytes[length] = (uint8_t)((high << 4) | low);
    }
    if (!OtaUpdater_Write((uint32_t)offset, bytes, count))
    {
      DebugConsole_SendError(channel, "FW_DATA", "OFFSET_OR_FLASH");
    }
    else
    {
      OtaUpdater_GetStatus(&status);
      (void)snprintf(detail, sizeof(detail), "%lu",
                     (unsigned long)status.received);
      DebugConsole_SendAck(channel, "FW_DATA", detail);
    }
    return;
  }
  if (strcmp(command, "FW END") == 0)
  {
    if (s_ota_owner != channel)
    {
      DebugConsole_SendError(channel, "FW_END", "LINK_NOT_OWNER");
      return;
    }
    if (OtaUpdater_Finalize())
    {
      DebugConsole_SendAck(channel, "FW_END", "VERIFIED");
    }
    else
    {
      DebugConsole_SendError(channel, "FW_END", "IMAGE_VERIFY_FAILED");
    }
    return;
  }
  if (strcmp(command, "FW APPLY") == 0)
  {
    if (s_ota_owner != channel)
    {
      DebugConsole_SendError(channel, "FW_APPLY", "LINK_NOT_OWNER");
      return;
    }
    if (OtaUpdater_Apply())
    {
      DebugConsole_SendAck(channel, "FW_APPLY", "REBOOTING");
    }
    else
    {
      DebugConsole_SendError(channel, "FW_APPLY", "NOT_VERIFIED");
    }
    return;
  }
  if (strcmp(command, "FW ABORT") == 0)
  {
    if (OtaUpdater_Abort())
    {
      s_ota_owner = NULL;
      DebugConsole_SendAck(channel, "FW_ABORT", "CANCELLED");
    }
    else
    {
      DebugConsole_SendError(channel, "FW_ABORT", "METADATA_WRITE_FAILED");
    }
    return;
  }
  DebugConsole_SendError(channel, "FW", "USE_FW_STATUS");
}

/** @brief 解析“命令前缀 + 无符号整数”格式并拒绝多余字符。 */
static bool DebugConsole_ParseUnsigned(const char *command,
                                       const char *prefix,
                                       uint32_t *value)
{
  const char *number;
  char *end;
  unsigned long parsed;
  size_t prefix_length;

  if ((command == NULL) || (prefix == NULL) || (value == NULL))
  {
    return false;
  }
  prefix_length = strlen(prefix);
  if (strncmp(command, prefix, prefix_length) != 0)
  {
    return false;
  }
  number = command + prefix_length;
  while (*number == ' ')
  {
    number++;
  }
  if (!isdigit((unsigned char)*number))
  {
    return false;
  }
  parsed = strtoul(number, &end, 10);
  while (*end == ' ')
  {
    end++;
  }
  if (*end != '\0')
  {
    return false;
  }
  *value = (uint32_t)parsed;
  return true;
}

/** @brief 只有输出请求和真实输出都关闭时才允许修改或保存校准。 */
static bool DebugConsole_CalibrationAllowed(
  const PowerControl_Status_t *power_status)
{
  UI_ControlRequest_t request;

  if (power_status == NULL)
  {
    return false;
  }
  UI_GetControlRequest(&request);
  return !request.output_requested && !power_status->output_enabled;
}

static void DebugConsole_SendCurveStatus(DebugConsole_Channel_t *channel,
  const PowerMonitor_Snapshot_t *measurement)
{
  if(channel->binary_mode)
  {
    PowerControl_Status_t live;PowerControl_GetStatus(&live);uint8_t payload[10],*p=payload;
    *p++=PowerControl_IsDacCalibrationActive()?1U:0U;*p++=s_curve_index;
    Wire_Put16(&p,Calibration_PointTarget(s_curve_index));Wire_Put16(&p,live.dac_code);
    Wire_Put16(&p,measurement?measurement->output_voltage_raw_mv:0U);
    *p++=PowerControl_DacCalibrationReady(measurement)?1U:0U;*p++=(uint8_t)live.fault;
    DebugConsole_SendPacket(channel,WIRE_CAL_STATE,channel->reply_sequence,payload,(uint16_t)(p-payload),false);return;
  }

  PowerControl_Status_t live;
  char text[176];
  PowerControl_GetStatus(&live);
  (void)snprintf(text, sizeof(text),
    "@CSTATE,active=%u,id=%u,target=%u,dac=%u,vout=%u,ready=%u,fault=%s\r\n",
    PowerControl_IsDacCalibrationActive() ? 1U : 0U, s_curve_index,
    Calibration_PointTarget(s_curve_index), live.dac_code,
    measurement != NULL ? measurement->output_voltage_raw_mv : 0U,
    PowerControl_DacCalibrationReady(measurement) ? 1U : 0U,
    PowerControl_GetFaultName(live.fault));
  DebugConsole_WriteChannel(channel, text, false);
}

static void DebugConsole_HandleCurve(DebugConsole_Channel_t *channel,
  const char *command, const PowerMonitor_Snapshot_t *measurement)
{
  Calibration_Data_t data;
  PowerControl_Status_t live;
  uint32_t value;
  char text[160];
  Calibration_Get(&data);
  if (strcmp(command, "CAL POINT GET") == 0)
  {
    if(channel->binary_mode)
    {
      uint8_t payload[7],*p=payload;*p++=5U;Wire_Put32(&p,data.point_mask);
      *p++=data.loaded_from_flash?1U:0U;*p++=CALIBRATION_POINT_COUNT;
      DebugConsole_SendPacket(channel,WIRE_CURVE,channel->reply_sequence,payload,(uint16_t)(p-payload),false);return;
    }
    (void)snprintf(text, sizeof(text), "@CURVE,schema=5,mask=%lu,saved=%u,total=16\r\n",
      (unsigned long)data.point_mask, data.loaded_from_flash ? 1U : 0U);
    DebugConsole_WriteChannel(channel, text, false);
    return;
  }
  if (DebugConsole_ParseUnsigned(command, "CAL POINT GET ", &value))
  {
    if (value >= CALIBRATION_POINT_COUNT)
    { DebugConsole_SendError(channel, "CAL_POINT_GET", "INDEX_0_15"); return; }
    if(channel->binary_mode)
    {
      uint8_t payload[10],*p=payload;*p++=(uint8_t)value;
      Wire_Put16(&p,Calibration_PointTarget((uint8_t)value));
      *p++=(data.point_mask&(1UL<<value))?1U:0U;
      Wire_Put16(&p,data.points[value].actual_mv);Wire_Put16(&p,data.points[value].dac_code);
      *p++=data.loaded_from_flash?1U:0U;
      DebugConsole_SendPacket(channel,WIRE_POINT,channel->reply_sequence,payload,(uint16_t)(p-payload),false);return;
    }
    (void)snprintf(text, sizeof(text),
      "@CPOINT,id=%lu,target=%u,valid=%u,actual=%u,dac=%u,saved=%u\r\n",
      (unsigned long)value, Calibration_PointTarget((uint8_t)value),
      (data.point_mask & (1U << value)) != 0U ? 1U : 0U,
      data.points[value].actual_mv, data.points[value].dac_code,
      data.loaded_from_flash ? 1U : 0U);
    DebugConsole_WriteChannel(channel, text, false);
    return;
  }
  if (strcmp(command, "CAL POINT STATUS") == 0)
  { DebugConsole_SendCurveStatus(channel, measurement); return; }
  /* STOP never writes Flash; any control port may request a safe shutdown. */
  if (strcmp(command, "CAL POINT STOP") == 0)
  {
    UI_SetOutputRequested(false);
    PowerControl_EndDacCalibration();
    DebugConsole_RestoreCurveRequest();
    s_curve_owner = NULL;
    DebugConsole_SendAck(channel, "CAL_POINT_STOP", "OUTPUT_OFF");
    return;
  }
  if (OtaUpdater_IsBusy() || UserSettings_IsSaving())
  { DebugConsole_SendError(channel, "CAL_POINT", "FLASH_BUSY_RETRY"); return; }
  if (PowerControl_IsDacCalibrationActive() && channel != s_curve_owner)
  { DebugConsole_SendError(channel, "CAL_POINT", "OWNED_BY_OTHER_PORT"); return; }
  if (DebugConsole_ParseUnsigned(command, "CAL POINT START ", &value))
  {
    PowerControl_GetStatus(&live);
    if (live.fault != POWER_CONTROL_FAULT_NONE)
    { DebugConsole_SendError(channel, "CAL_POINT_START", "FAULT_ACTIVE_USE_CLEAR"); return; }
    if (value >= CALIBRATION_POINT_COUNT ||
        !PowerControl_BeginDacCalibration(Calibration_PointTarget((uint8_t)value)))
    { DebugConsole_SendError(channel, "CAL_POINT_START", "INDEX_OR_ACTIVE_OR_FAULT"); return; }
    s_curve_index = (uint8_t)value;
    s_curve_owner = channel;
    UI_GetControlRequest(&s_curve_previous_request);
    s_curve_restore_pending = true;
    (void)UI_SetVoltageSetpoint(Calibration_PointTarget(s_curve_index));
    UI_SetOutputRequested(true);
    DebugConsole_SendAck(channel, "CAL_POINT_START", "DIRECT_DAC");
    return;
  }
  int delta = 0;
  if (strcmp(command, "CAL POINT STEP -5") == 0) { delta = -5; }
  else if (strcmp(command, "CAL POINT STEP -1") == 0) { delta = -1; }
  else if (strcmp(command, "CAL POINT STEP 1") == 0) { delta = 1; }
  else if (strcmp(command, "CAL POINT STEP 5") == 0) { delta = 5; }
  if (delta != 0)
  {
    if (!PowerControl_StepDacCalibration((int16_t)delta, measurement))
    { DebugConsole_SendError(channel, "CAL_POINT_STEP", "INACTIVE_OR_CODE_RANGE_OR_DAC_WRITE"); }
    else { DebugConsole_SendAck(channel, "CAL_POINT_STEP", "DAC_APPLIED"); }
    return;
  }
  if (DebugConsole_ParseUnsigned(command, "CAL POINT CAPTURE ", &value))
  {
    if (!PowerControl_DacCalibrationReady(measurement) ||
        value == 0U || value > UINT16_MAX)
    { DebugConsole_SendError(channel, "CAL_POINT_CAPTURE", "INACTIVE_OR_INVALID_METER_VALUE"); return; }
    PowerControl_GetStatus(&live);
    UI_SetOutputRequested(false);
    PowerControl_EndDacCalibration();
    DebugConsole_RestoreCurveRequest();
    s_curve_owner = NULL;
    if (!Calibration_SavePoint(s_curve_index, (uint16_t)value, live.dac_code))
    {
      char detail[160];
      (void)snprintf(detail, sizeof(detail),
        "@CSAVE,ok=0,id=%u,actual=%lu,dac=%u,reason=%s\r\n",
        (unsigned)s_curve_index, (unsigned long)value, (unsigned)live.dac_code,
        Calibration_GetSaveError());
      DebugConsole_WriteChannel(channel, detail, false);
      if (strcmp(Calibration_GetSaveError(), "FLASH_ERASE_FAILED") == 0)
      { DebugConsole_WriteChannel(channel, W25Q256_GetEraseDiagnostic(), false); }
      DebugConsole_SendError(channel, "CAL_POINT_CAPTURE", Calibration_GetSaveError());
      return;
    }
    DebugConsole_SendAck(channel, "CAL_POINT_CAPTURE", "SAVED_OUTPUT_OFF");
    return;
  }
  DebugConsole_SendError(channel, "CAL_POINT", "USE_GET_START_STEP_CAPTURE_STOP_STATUS");
}

/** @brief 解析并执行一条来自USB或蓝牙的完整命令。 */
static void DebugConsole_HandleCommand(
  DebugConsole_Channel_t *channel,
  char *line,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature)
{
  char command[DEBUG_CONSOLE_LINE_SIZE];
  char detail[64];
  char *start;
  char *end;
  uint32_t value;
  long signed_value_1;
  long signed_value_2;
  unsigned int unsigned_value_1;
  unsigned int unsigned_value_2;
  unsigned int unsigned_value_3;
  unsigned int unsigned_value_4;

  start = line;
  while ((*start == ' ') || (*start == '\t'))
  {
    start++;
  }
  end = start + strlen(start);
  while ((end > start) && ((end[-1] == ' ') || (end[-1] == '\t')))
  {
    end--;
  }
  *end = '\0';
  if (*start == '\0')
  {
    return;
  }

  (void)strncpy(command, start, sizeof(command) - 1U);
  command[sizeof(command) - 1U] = '\0';
  DebugConsole_Uppercase(command);

  if (strncmp(command, "PID ", 4U) == 0)
  { DebugConsole_SendError(channel, "PID", "DAC_MODE"); return; }
  if (strncmp(command, "CAL POINT ", 10U) == 0)
  {
    DebugConsole_HandleCurve(channel, command, measurement);
    return;
  }
  if (PowerControl_IsDacCalibrationActive() &&
      (strncmp(command, "SET ", 4U) == 0 || strncmp(command, "FW ", 3U) == 0 ||
       strncmp(command, "CAL SET ", 8U) == 0 || strcmp(command, "CAL SAVE") == 0 ||
       strcmp(command, "CAL RESET") == 0 || strcmp(command, "OUT ON") == 0 ||
       strcmp(command, "OUT 1") == 0))
  { DebugConsole_SendError(channel, "CAL_POINT", "ACTIVE_STOP_FIRST"); return; }

  /* Never share an in-progress NOR erase/program with OTA or calibration. */
  if (UserSettings_IsSaving() &&
      ((strncmp(command, "FW ", 3U) == 0) ||
       (strcmp(command, "CAL SAVE") == 0) ||
       (strcmp(command, "CAL RESET") == 0)))
  {
    DebugConsole_SendError(channel, "FLASH", "SETTINGS_BUSY_RETRY");
    return;
  }

  if (strncmp(command, "FW ", 3U) == 0)
  {
    DebugConsole_HandleFirmwareCommand(channel, command, power_status);
    return;
  }

  if (strcmp(command, "PING") == 0)
  {
    DebugConsole_SendAck(channel, "PING", "PONG");
  }
  else if ((strcmp(command, "INFO") == 0) ||
           (strcmp(command, "GET INFO") == 0))
  {
    DebugConsole_SendInfo(channel);
  }
  else if ((strcmp(command, "STATUS") == 0) ||
           (strcmp(command, "GET STATUS") == 0) ||
           (strcmp(command, "SYNC") == 0))
  {
    DebugConsole_SendStatus(channel, system_time_ms, measurement,
                            power_status, temperature, false);
  }
  else if (DebugConsole_ParseUnsigned(command, "SET V ", &value) ||
           DebugConsole_ParseUnsigned(command, "SET VOLT ", &value) ||
           DebugConsole_ParseUnsigned(command, "SET VOLTAGE ", &value))
  {
    if ((value < POWER_CFG_VOLTAGE_MIN_MV) ||
        (value > POWER_CFG_VOLTAGE_MAX_MV))
    {
      DebugConsole_SendError(channel, "SET_V", "RANGE_3200_32000MV");
    }
    else if (!UI_SetVoltageSetpoint((uint16_t)value))
    {
      DebugConsole_SendError(channel, "SET_V", "REJECTED");
    }
    else
    {
      (void)snprintf(detail, sizeof(detail), "%lumV", (unsigned long)value);
      DebugConsole_SendAck(channel, "SET_V", detail);
    }
  }
  else if (DebugConsole_ParseUnsigned(command, "SET I ", &value) ||
           DebugConsole_ParseUnsigned(command, "SET CURR ", &value) ||
           DebugConsole_ParseUnsigned(command, "SET CURRENT ", &value))
  {
    if ((value < POWER_CFG_CURRENT_MIN_MA) ||
        (value > POWER_CFG_CURRENT_MAX_MA))
    {
      DebugConsole_SendError(channel, "SET_I", "RANGE");
    }
    else if (!UI_SetCurrentLimit((uint16_t)value))
    {
      DebugConsole_SendError(channel, "SET_I", "REJECTED");
    }
    else
    {
      (void)snprintf(detail, sizeof(detail), "%lumA", (unsigned long)value);
      DebugConsole_SendAck(channel, "SET_I", detail);
    }
  }
  else if ((strcmp(command, "OUT ON") == 0) ||
           (strcmp(command, "OUT 1") == 0))
  {
    if (OtaUpdater_IsBusy())
    {
      DebugConsole_SendError(channel, "OUT", "FW_UPDATE_ACTIVE");
    }
    else if ((power_status != NULL) &&
        (power_status->fault != POWER_CONTROL_FAULT_NONE))
    {
      UI_SetOutputRequested(false);
      DebugConsole_SendError(channel, "OUT", "FAULT_ACTIVE_USE_CLEAR");
    }
    else
    {
      UI_SetOutputRequested(true);
      DebugConsole_SendAck(channel, "OUT", "ON");
    }
  }
  else if ((strcmp(command, "OUT OFF") == 0) ||
           (strcmp(command, "OUT 0") == 0))
  {
    UI_SetOutputRequested(false);
    /* Apply the physical CE shutdown before acknowledging OUT OFF. */
    PowerControl_EndDacCalibration();
    DebugConsole_SendAck(channel, "OUT", "OFF");
  }
  else if ((strcmp(command, "CLEAR") == 0) ||
           (strcmp(command, "CLEAR FAULT") == 0))
  {
    UI_SetOutputRequested(false);
    (void)PowerControl_ClearFault();
    UI_SetPowerFault(false);
    DebugConsole_SendAck(channel, "CLEAR", "FAULT_CLEARED_OUTPUT_OFF");
  }
  else if ((strcmp(command, "LOG ON") == 0) ||
           (strcmp(command, "LOG 1") == 0))
  {
    channel->logs_enabled = true;
    DebugConsole_SendAck(channel, "LOG", "ON");
  }
  else if ((strcmp(command, "LOG OFF") == 0) ||
           (strcmp(command, "LOG 0") == 0))
  {
    channel->logs_enabled = false;
    DebugConsole_SendAck(channel, "LOG", "OFF");
  }
  else if (DebugConsole_ParseUnsigned(command, "LOG RATE ", &value))
  {
    if ((value < DEBUG_CONSOLE_MIN_RATE_MS) ||
        (value > DEBUG_CONSOLE_MAX_RATE_MS))
    {
      DebugConsole_SendError(channel, "LOG_RATE", "RANGE_20_5000MS");
    }
    else
    {
      channel->telemetry_rate_ms = value;
      (void)snprintf(detail, sizeof(detail), "%lums", (unsigned long)value);
      DebugConsole_SendAck(channel, "LOG_RATE", detail);
    }
  }
  else if ((strcmp(command, "CAL GET") == 0) ||
           (strcmp(command, "CAL RAW") == 0))
  {
    DebugConsole_SendCalibration(channel, measurement, power_status);
  }
  else if (strcmp(command, "DIAG") == 0)
  {
    uint8_t index;
    for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
    {
      DebugConsole_Channel_t *item = &s_channels[index];
      char diagnostic[192];
      (void)snprintf(diagnostic, sizeof(diagnostic),
                     "@DIAG,port=%s,rx_drop=%lu,tx_drop=%lu,uart_err=%lu,rx_recover=%lu,tx_timeout=%lu,rx_pending=%u\r\n",
                     (index == DEBUG_CHANNEL_USB) ? "USB" : "BT",
                     (unsigned long)item->rx_overflow_count,
                     (unsigned long)item->tx_overflow_count,
                     (unsigned long)item->uart_error_count,
                     (unsigned long)item->rx_recovery_count,
                     (unsigned long)item->tx_timeout_count,
                     (unsigned int)((item->rx_head + DEBUG_CONSOLE_RX_BUFFER_SIZE -
                                     item->rx_tail) % DEBUG_CONSOLE_RX_BUFFER_SIZE));
      DebugConsole_WriteChannel(channel, diagnostic, false);
    }
  }
  else if (sscanf(command, "CAL SET V %ld %ld",
                  &signed_value_1, &signed_value_2) == 2)
  {
    if (!DebugConsole_CalibrationAllowed(power_status))
    {
      DebugConsole_SendError(channel, "CAL_SET_V", "OUTPUT_MUST_BE_OFF");
    }
    else if (!Calibration_SetVoltage((int32_t)signed_value_1,
                                     (int32_t)signed_value_2))
    {
      DebugConsole_SendError(channel, "CAL_SET_V", "RANGE");
    }
    else
    {
      PowerControl_ResetVoltageLearning();
      DebugConsole_SendAck(channel, "CAL_SET_V", "RAM_UPDATED");
    }
  }
  else if (sscanf(command, "CAL SET I %ld %ld",
                  &signed_value_1, &signed_value_2) == 2)
  {
    if (!DebugConsole_CalibrationAllowed(power_status))
    {
      DebugConsole_SendError(channel, "CAL_SET_I", "OUTPUT_MUST_BE_OFF");
    }
    else if (!Calibration_SetCurrent((int32_t)signed_value_1,
                                     (int32_t)signed_value_2))
    {
      DebugConsole_SendError(channel, "CAL_SET_I", "RANGE");
    }
    else
    {
      DebugConsole_SendAck(channel, "CAL_SET_I", "RAM_UPDATED");
    }
  }
  else if (sscanf(command, "CAL SET DAC %u %u %u %u",
                  &unsigned_value_1, &unsigned_value_2,
                  &unsigned_value_3, &unsigned_value_4) == 4)
  {
    if (!DebugConsole_CalibrationAllowed(power_status))
    {
      DebugConsole_SendError(channel, "CAL_SET_DAC", "OUTPUT_MUST_BE_OFF");
    }
    else if ((unsigned_value_1 > UINT16_MAX) ||
             (unsigned_value_2 > UINT16_MAX) ||
             (unsigned_value_3 > UINT16_MAX) ||
             (unsigned_value_4 > UINT16_MAX) ||
             !Calibration_SetDac((uint16_t)unsigned_value_1,
                                 (uint16_t)unsigned_value_2,
                                 (uint16_t)unsigned_value_3,
                                 (uint16_t)unsigned_value_4))
    {
      DebugConsole_SendError(channel, "CAL_SET_DAC", "RANGE_OR_ORDER");
    }
    else
    {
      PowerControl_ResetVoltageLearning();
      DebugConsole_SendAck(channel, "CAL_SET_DAC", "RAM_UPDATED");
    }
  }
  else if (strcmp(command, "CAL SAVE") == 0)
  {
    if (!DebugConsole_CalibrationAllowed(power_status))
    {
      DebugConsole_SendError(channel, "CAL_SAVE", "OUTPUT_MUST_BE_OFF");
    }
    else if (!Calibration_Save())
    {
      DebugConsole_SendError(channel, "CAL_SAVE", "W25Q256_WRITE_FAILED");
    }
    else
    {
      DebugConsole_SendAck(channel, "CAL_SAVE", "W25Q256_OK");
    }
  }
  else if (strcmp(command, "CAL RESET") == 0)
  {
    if (!DebugConsole_CalibrationAllowed(power_status))
    {
      DebugConsole_SendError(channel, "CAL_RESET", "OUTPUT_MUST_BE_OFF");
    }
    else
    {
      Calibration_ResetDefaults();
      PowerControl_ResetVoltageLearning();
      DebugConsole_SendAck(channel, "CAL_RESET", "RAM_DEFAULTS");
    }
  }
  else if (strcmp(command, "HELP") == 0)
  {
    DebugConsole_WriteChannel(
      channel,
      "@HELP,PING|INFO|STATUS|SET V mV|SET I mA|OUT ON/OFF|"
      "LOG ON/OFF|LOG RATE ms|CLEAR|DIAG|CAL GET/RAW/SET/SAVE/RESET|"
      "CAL POINT GET [id]/START id/STEP +/-1 or +/-5/CAPTURE mV/STOP/STATUS|"
      "FW STATUS/BEGIN/DATA/END/APPLY/ABORT (USB/BT SPP)\r\n",
      false);
  }
  else
  {
    DebugConsole_SendError(channel, "UNKNOWN", "USE_HELP");
  }
}

static void DebugConsole_HandlePacket(DebugConsole_Channel_t *channel,const WirePacket_t *packet,
  uint32_t now,const PowerMonitor_Snapshot_t *measurement,const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature)
{
  if(packet->type!=WIRE_COMMAND || packet->length==0U || packet->length>OTA_TRANSFER_CHUNK_MAX+5U){return;}
  uint8_t op=packet->payload[0];const uint8_t *a=packet->payload+1U;uint16_t n=packet->length-1U;
  channel->binary_mode=true;channel->legacy_mode=false;
  channel->reply_sequence=packet->sequence;channel->command_id=op;
  if(op==CMD_HELLO && n==4U && Wire_U32(a)!=channel->hello_nonce)
  {channel->hello_nonce=Wire_U32(a);(void)memset(channel->replay,0,sizeof(channel->replay));}
  for(unsigned i=0U;i<4U;i++)
  {
    DebugConsole_Replay_t *r=&channel->replay[i];
    if(!r->valid || r->sequence!=packet->sequence){continue;}
    if(r->request_length==packet->length && !memcmp(r->request,packet->payload,packet->length))
    {DebugConsole_WriteRaw(channel,r->response,r->response_length,false);return;}
    DebugConsole_SendError(channel,"SEQUENCE","SEQUENCE_CONFLICT");return;
  }
  char command[DEBUG_CONSOLE_LINE_SIZE];command[0]='\0';
  const char *simple=NULL,*numeric=NULL;
  switch(op)
  {
    case CMD_HELLO: if(n!=4U){break;} channel->logs_enabled=true;simple="INFO";break;
    case CMD_STATUS:simple="STATUS";break;case CMD_CLEAR:simple="CLEAR";break;
    case CMD_CAL_GET:simple="CAL GET";break;case CMD_CAL_RAW:simple="CAL RAW";break;
    case CMD_CAL_SAVE:simple="CAL SAVE";break;case CMD_CAL_RESET:simple="CAL RESET";break;
    case CMD_CURVE_GET:simple="CAL POINT GET";break;case CMD_POINT_STOP:simple="CAL POINT STOP";break;
    case CMD_POINT_STATE:simple="CAL POINT STATUS";break;case CMD_DIAG:simple="DIAG";break;
    case CMD_HELP:simple="HELP";break;case CMD_PING:simple="PING";break;
    case CMD_FW_STATUS:simple="FW STATUS";break;case CMD_FW_END:simple="FW END";break;
    case CMD_FW_APPLY:simple="FW APPLY";break;case CMD_FW_ABORT:simple="FW ABORT";break;
    case CMD_REBOOT:simple="REBOOT";break;
    case CMD_SET_V:numeric="SET V %u";break;case CMD_SET_I:numeric="SET I %u";break;
    case CMD_POINT_GET:numeric="CAL POINT GET %u";break;case CMD_POINT_START:numeric="CAL POINT START %u";break;
    case CMD_POINT_CAPTURE:numeric="CAL POINT CAPTURE %u";break;
    case CMD_POINT_STEP:if(n==2U){(void)snprintf(command,sizeof(command),"CAL POINT STEP %d",(int)(int16_t)Wire_U16(a));}break;
    case CMD_OUT:if(n==1U && a[0]<=1U){simple=a[0]?"OUT ON":"OUT OFF";}break;
    case CMD_LOG:if(n==1U && a[0]<=1U){simple=a[0]?"LOG ON":"LOG OFF";}break;
    case CMD_RATE:if(n==2U){(void)snprintf(command,sizeof(command),"LOG RATE %u",Wire_U16(a));}break;
    case CMD_CAL_V:case CMD_CAL_I:
      if(n==8U){(void)snprintf(command,sizeof(command),op==CMD_CAL_V?"CAL SET V %ld %ld":"CAL SET I %ld %ld",(long)(int32_t)Wire_U32(a),(long)(int32_t)Wire_U32(a+4));}break;
    case CMD_FW_BEGIN:
      if(n==12U){(void)snprintf(command,sizeof(command),"FW BEGIN %lu %08lX %lu",(unsigned long)Wire_U32(a),(unsigned long)Wire_U32(a+4),(unsigned long)Wire_U32(a+8));}break;
    case CMD_FW_DATA:
      if(n>4U && n<=OTA_TRANSFER_CHUNK_MAX+4U)
      {
        unsigned count=(unsigned)snprintf(command,sizeof(command),"FW DATA %lu ",(unsigned long)Wire_U32(a));
        const char hex[]="0123456789ABCDEF";
        for(unsigned i=4U;i<n;i++){command[count++]=hex[a[i]>>4];command[count++]=hex[a[i]&15U];}
        command[count]='\0';
      }break;
    default:break;
  }
  if(simple && ((op==CMD_HELLO && n==4U) || (op==CMD_OUT || op==CMD_LOG) ||
                (op==CMD_RATE && n==2U) || n==0U))
  {(void)snprintf(command,sizeof(command),"%s",simple);}
  if(numeric && n==2U){(void)snprintf(command,sizeof(command),numeric,(unsigned)Wire_U16(a));}
  if(!command[0]){DebugConsole_SendError(channel,"PACKET","INVALID_COMMAND_PAYLOAD");return;}
  channel->replay_current=channel->replay_next;channel->replay_next=(uint8_t)((channel->replay_next+1U)%4U);
  DebugConsole_Replay_t *r=&channel->replay[channel->replay_current];
  r->valid=true;r->sequence=packet->sequence;r->request_length=packet->length;r->response_length=0U;
  (void)memcpy(r->request,packet->payload,packet->length);
  channel->capturing=true;
  DebugConsole_HandleCommand(channel,command,now,measurement,power_status,temperature);
  channel->capturing=false;
  if(!r->response_length){r->valid=false;}
}

/** @brief 从一个通道的字节流中组装并执行换行结尾的命令。 */
static void DebugConsole_ProcessChannel(
  DebugConsole_Channel_t *channel,
  uint32_t system_time_ms,
  const PowerMonitor_Snapshot_t *measurement,
  const PowerControl_Status_t *power_status,
  const TemperatureSensor_Snapshot_t *temperature)
{
  uint8_t value;
  uint16_t processed = 0U;
  uint8_t commands = 0U;
  uint32_t primask;

  /* 溢出后丢弃不完整命令，直到下一换行；不能把两条命令拼成一条。 */
  primask = DebugConsole_EnterCritical();
  if (channel->rx_resync_pending)
  {
    channel->rx_tail = channel->rx_head;
    channel->rx_resync_pending = false;
    channel->line_length = 0U;
    channel->discarding_line = true;
    channel->parser.used=0U;
  }
  DebugConsole_ExitCritical(primask);

  /* 每轮限量处理，持续串口流也必须让出主循环给UI、采样和保护。 */
  while ((processed < DEBUG_CONSOLE_RX_BYTES_PER_PASS) &&
         (commands < DEBUG_CONSOLE_COMMANDS_PER_PASS) &&
         DebugConsole_PopRx(channel, &value))
  {
    processed++;
    if(channel->binary_mode || channel->parser.used || value==0xA5U)
    {
      WirePacket_t packet;
      if(Wire_Feed(&channel->parser,value,system_time_ms,&packet))
      {commands++;DebugConsole_HandlePacket(channel,&packet,system_time_ms,measurement,power_status,temperature);}
      continue;
    }
    if (value == '\r')
    {
      continue;
    }
    if (value == '\n')
    {
      if (channel->discarding_line)
      {
        channel->discarding_line = false;
        channel->line_length = 0U;
        continue;
      }
      channel->line_buffer[channel->line_length] = '\0';
      channel->legacy_mode=true;
      commands++;
      DebugConsole_HandleCommand(channel,
                                 channel->line_buffer,
                                 system_time_ms,
                                 measurement,
                                 power_status,
                                 temperature);
      channel->line_length = 0U;
      continue;
    }
    if (channel->discarding_line)
    {
      continue;
    }
    if ((value < 0x20U) || (value > 0x7EU))
    {
      continue;
    }
    if (channel->line_length >= (DEBUG_CONSOLE_LINE_SIZE - 1U))
    {
      channel->line_length = 0U;
      channel->parser.used = 0U;
      channel->discarding_line = true;
      DebugConsole_SendError(channel, "LINE", "TOO_LONG");
      continue;
    }
    channel->line_buffer[channel->line_length++] = (char)value;
  }
}

bool DebugConsole_Init(void)
{
  uint8_t index;
  bool ready = true;

  (void)memset(s_channels, 0, sizeof(s_channels));
  s_channels[DEBUG_CHANNEL_USB].uart = &husart1;
  s_channels[DEBUG_CHANNEL_BLUETOOTH].uart = &husart2;
  s_channels[DEBUG_CHANNEL_USB].max_tx_chunk =
    (uint16_t)(DEBUG_CONSOLE_TX_BUFFER_SIZE - 1U);
  /* VG6328A规格要求单个串口透传包不超过255字节，留15字节余量。 */
  s_channels[DEBUG_CHANNEL_BLUETOOTH].max_tx_chunk =
    DEBUG_CONSOLE_BT_TX_CHUNK_MAX;
  HAL_NVIC_SetPriority(USART1_IRQn, 2U, 0U);
  HAL_NVIC_SetPriority(USART2_IRQn, 2U, 0U);
  for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
  {
    s_channels[index].logs_enabled = true;
    s_channels[index].telemetry_rate_ms = DEBUG_CONSOLE_DEFAULT_RATE_MS;
    s_channels[index].last_telemetry_ms = HAL_GetTick();
    if (HAL_UART_Receive_IT(s_channels[index].uart,
                            &s_channels[index].rx_interrupt_byte,
                            1U) != HAL_OK)
    {
      ready = false;
    }
  }
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  s_initialized = true;
  DebugConsole_Write("[DEBUG] USB=USART1 BT=VG6328A/USART2 115200 8N1\r\n");
  return ready;
}
#endif

/* Bounded foreground recovery: no retries or waits inside UART interrupts.
 * A disconnected/noisy BT module must never hold up the power/UI tasks. */
void DebugConsole_Service(uint32_t now)
{
  uint8_t index;
  if (!s_initialized) { return; }
  for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
  {
    DebugConsole_Channel_t *channel = &s_channels[index];
    uint32_t primask;
    if (channel->rx_restart_pending &&
        ((now - channel->rx_error_time_ms) >= DEBUG_CONSOLE_RX_RECOVERY_MS))
    {
      primask = DebugConsole_EnterCritical();
      /* These channels use IT only (no RX DMA); AbortReceive does not wait. */
      (void)HAL_UART_AbortReceive(channel->uart);
      DebugConsole_ClearRxErrors(channel->uart);
      channel->rx_tail = channel->rx_head;
      channel->line_length = 0U;
      channel->parser.used = 0U;
      channel->discarding_line = true;
      channel->rx_resync_pending = false;
      channel->rx_restart_pending = false;
      if (HAL_UART_Receive_IT(channel->uart,
                              &channel->rx_interrupt_byte, 1U) == HAL_OK)
      {
        channel->rx_recovery_count++;
      }
      else
      {
        channel->rx_restart_pending = true;
        channel->rx_error_time_ms = now;
      }
      DebugConsole_ExitCritical(primask);
    }
    if (channel->tx_active)
    {
      uint32_t baud = channel->uart->Init.BaudRate;
      uint32_t deadline = 40U +
        ((uint32_t)channel->tx_active_length * 10000U) /
        ((baud != 0U) ? baud : 115200U);
      if ((now - channel->tx_start_time_ms) > deadline)
      {
        primask = DebugConsole_EnterCritical();
        (void)HAL_UART_AbortTransmit(channel->uart);
        channel->tx_tail = channel->tx_head; /* discard a broken protocol frame */
        channel->tx_active_length = 0U;
        channel->tx_active = false;
        channel->tx_timeout_count++;
        DebugConsole_ExitCritical(primask);
      }
    }
    DebugConsole_KickTx(channel);
  }
}

#if !defined(DEBUG_CONSOLE_HOST_TEST) || defined(DEBUG_CONSOLE_COMMAND_HOST_TEST)
void DebugConsole_Process(uint32_t system_time_ms,
                          const PowerMonitor_Snapshot_t *measurement,
                          const PowerControl_Status_t *power_status,
                          const TemperatureSensor_Snapshot_t *temperature)
{
  uint8_t index;

  if (!s_initialized)
  {
    return;
  }

  /* Also restore after a local OFF or a latched fault/idle timeout. Never ON. */
  DebugConsole_RestoreCurveRequest();

  for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
  {
    DebugConsole_ProcessChannel(&s_channels[index],
                                system_time_ms,
                                measurement,
                                power_status,
                                temperature);
    /* HAL忙时保留队列，下轮再尝试，而不是等下一条日志才能继续发送。 */
    DebugConsole_KickTx(&s_channels[index]);
  }

  if (!OtaUpdater_IsBusy())
  {
    for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
    {
      DebugConsole_Channel_t *channel=&s_channels[index];
      uint32_t period=channel->telemetry_rate_ms ? channel->telemetry_rate_ms : DEBUG_CONSOLE_DEFAULT_RATE_MS;
      if (!channel->binary_mode && period<100U) { period=100U; }
      if(system_time_ms-channel->last_telemetry_ms<period){continue;}
      channel->last_telemetry_ms=system_time_ms;
      DebugConsole_SendStatus(&s_channels[index],
                              system_time_ms,
                              measurement,
                              power_status,
                              temperature,
                              true);
    }
  }
}

void DebugConsole_OnPowerFault(uint32_t now,PowerControl_Fault_t fault,uint8_t phase,
  uint16_t dac,const PowerControl_Request_t *request,const PowerMonitor_Snapshot_t *m)
{
  uint8_t *p=s_last_fault_packet;
  Wire_Put32(&p,now);*p++=(uint8_t)fault;*p++=phase;
  Wire_Put16(&p,request->voltage_setpoint_mv);Wire_Put16(&p,dac);
  Wire_Put16(&p,m->output_voltage_raw_mv);Wire_Put16(&p,m->output_voltage_uncalibrated_mv);
  Wire_Put16(&p,m->input_voltage_raw_mv);Wire_Put32(&p,(uint32_t)m->input_current_raw_ua);
  Wire_Put32(&p,(uint32_t)m->output_current_uncalibrated_ua);Wire_Put32(&p,m->output_power_raw_mw);
  Wire_Put16(&p,(uint16_t)request->temperature_decic);
  *p++=(m->data_valid?1U:0U)|(m->input_online?2U:0U)|(m->output_online?4U:0U);
  Wire_Put32(&p,now-m->last_valid_sample_time_ms);Wire_Put32(&p,m->sample_sequence);
  Wire_Put16(&p,request->current_limit_ma);
  s_last_fault_length=(uint16_t)(p-s_last_fault_packet);
  if(!s_initialized){return;}
  for(unsigned i=0U;i<DEBUG_CONSOLE_CHANNEL_COUNT;i++)
  {if(s_channels[i].binary_mode){DebugConsole_SendPacket(&s_channels[i],WIRE_FAULT,
    ++s_channels[i].event_sequence,s_last_fault_packet,s_last_fault_length,false);}}
}

void DebugConsole_Write(const char *text)
{
  uint8_t index;

  if (!s_initialized || (text == NULL) || OtaUpdater_IsBusy())
  {
    return;
  }
  for (index = 0U; index < DEBUG_CONSOLE_CHANNEL_COUNT; index++)
  {
    DebugConsole_WriteChannel(&s_channels[index], text, true);
  }
}

#endif
/**
 * @brief  HAL单字节接收完成回调，将数据放入对应通道环形缓冲。
 * @param  huart 发生接收完成事件的UART句柄。
 * @retval 无。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  DebugConsole_Channel_t *channel = DebugConsole_FindChannel(huart);
  uint16_t next_head;

  if ((channel == NULL) || channel->rx_restart_pending)
  {
    return;
  }
  next_head = (uint16_t)(channel->rx_head + 1U);
  if (next_head >= DEBUG_CONSOLE_RX_BUFFER_SIZE)
  {
    next_head = 0U;
  }
  if (next_head == channel->rx_tail)
  {
    channel->rx_overflow_count++;
    channel->rx_resync_pending = true;
  }
  else
  {
    channel->rx_buffer[channel->rx_head] = channel->rx_interrupt_byte;
    channel->rx_head = next_head;
  }
  (void)HAL_UART_Receive_IT(channel->uart,
                           &channel->rx_interrupt_byte,
                           1U);
}

/**
 * @brief  HAL中断发送完成回调，释放已发送区间并继续下一段。
 * @param  huart 发生发送完成事件的UART句柄。
 * @retval 无。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  DebugConsole_Channel_t *channel = DebugConsole_FindChannel(huart);
  uint32_t primask;

  if (channel == NULL)
  {
    return;
  }
  primask = DebugConsole_EnterCritical();
  channel->tx_tail = (uint16_t)(channel->tx_tail +
                                channel->tx_active_length);
  if (channel->tx_tail >= DEBUG_CONSOLE_TX_BUFFER_SIZE)
  {
    channel->tx_tail = (uint16_t)(channel->tx_tail -
                                  DEBUG_CONSOLE_TX_BUFFER_SIZE);
  }
  channel->tx_active_length = 0U;
  channel->tx_active = false;
  DebugConsole_ExitCritical(primask);
  DebugConsole_KickTx(channel);
}

/**
 * @brief  HAL串口错误回调，恢复接收并保留尚未发送的日志。
 * @param  huart 发生错误的UART句柄。
 * @retval 无。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  DebugConsole_Channel_t *channel = DebugConsole_FindChannel(huart);

  if (channel == NULL)
  {
    return;
  }
  channel->uart_error_count++;
  __HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);
  __HAL_UART_DISABLE_IT(huart, UART_IT_PE);
  __HAL_UART_DISABLE_IT(huart, UART_IT_ERR);
  DebugConsole_ClearRxErrors(huart);
  channel->rx_restart_pending = true;
  channel->rx_resync_pending = true;
  channel->rx_error_time_ms = HAL_GetTick();
}
