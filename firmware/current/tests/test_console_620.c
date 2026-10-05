#define DEBUG_CONSOLE_HOST_TEST
#define DEBUG_CONSOLE_COMMAND_HOST_TEST
#define DEBUG_CONSOLE_DMA_HOST_TEST
#define main controller_checks_main
#define DebugConsole_Write HostDebugConsole_Write
#include "test_620.c"
#undef DebugConsole_Write
#undef main
#include "../02_Application/Core/SRC/debug_console.c"
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t p,uint32_t s){(void)irq;(void)p;(void)s;}
void HAL_NVIC_EnableIRQ(IRQn_Type irq){(void)irq;}
static UI_ControlRequest_t ui_request;
static OtaUpdater_Status_t ota;
static uint8_t transmitted[10000];static unsigned tx_size,dma_calls;
void UI_GetControlRequest(UI_ControlRequest_t *r){*r=ui_request;}
bool UI_SetVoltageSetpoint(uint16_t v){if(v<3200||v>32000)return false;ui_request.voltage_setpoint_mv=v;return true;}
bool UI_SetCurrentLimit(uint16_t i){if(i<100||i>8000)return false;ui_request.current_limit_ma=i;return true;}
void UI_SetOutputRequested(bool on){ui_request.output_requested=on;}
void UI_SetPowerFault(bool fault){(void)fault;}
void UI_ForceOutputOff(void){ui_request.output_requested=false;}
bool UserSettings_IsSaving(void){return false;}
bool OtaStorage_BootloaderPresent(void){return true;}
bool OtaStorage_ReadMetadata(OtaMetadata_t *m){memset(m,0,sizeof(*m));m->state=OTA_STATE_CONFIRMED;return true;}
bool OtaUpdater_IsBusy(void){return ota.active;}
void OtaUpdater_GetStatus(OtaUpdater_Status_t *s){*s=ota;}
bool OtaUpdater_Begin(uint32_t n,uint32_t c,uint32_t v){ota.length=n;ota.crc32=c;ota.version_code=v;ota.active=true;return true;}
bool OtaUpdater_Write(uint32_t off,const uint8_t *b,uint32_t n){(void)b;if(off!=ota.received)return false;ota.received+=n;return true;}
bool OtaUpdater_Finalize(void){return true;}
bool OtaUpdater_Apply(void){return true;}
bool OtaUpdater_Abort(void){ota.active=false;return true;}
const char *W25Q256_GetEraseDiagnostic(void){return "@FLASH,error=mock";}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u,uint8_t *d,uint16_t n){(void)u;(void)d;(void)n;return HAL_OK;}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u){(void)u;return HAL_OK;}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u){(void)u;return HAL_OK;}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *u,uint8_t *d,uint16_t n)
{(void)u;CHECK(tx_size+n<=sizeof(transmitted));memcpy(transmitted+tx_size,d,n);tx_size+=n;dma_calls++;return HAL_OK;}
static PowerMonitor_Snapshot_t measurement;static TemperatureSensor_Snapshot_t temperature;
static void Drain(DebugConsole_Channel_t *c){while(c->tx_active || c->tx_head!=c->tx_tail){if(c->tx_active)HAL_UART_TxCpltCallback(c->uart);else DebugConsole_KickTx(c);}}
static void Request(DebugConsole_Channel_t *c,uint16_t seq,const uint8_t *p,uint16_t n)
{
 uint8_t frame[458];unsigned len=Wire_Encode(frame,WIRE_COMMAND,seq,p,n);
 for(unsigned i=0;i<len;i++){c->rx_buffer[c->rx_head]=frame[i];c->rx_head=(c->rx_head+1)%DEBUG_CONSOLE_RX_BUFFER_SIZE;}
 PowerControl_Status_t st=Status();DebugConsole_ProcessChannel(c,tick,&measurement,&st,&temperature);Drain(c);
}
int main(void)
{
 memset(flash,255,sizeof(flash));Calibration_Init();CHECK(PowerControl_Init());
 memset(s_channels,0,sizeof(s_channels));s_channels[0].uart=&husart1;s_channels[1].uart=&husart2;
 s_initialized=true;ui_request.voltage_setpoint_mv=3300;ui_request.current_limit_ma=1500;
 DebugConsole_Channel_t *c=&s_channels[0];uint8_t hello[]={1,1,2,3,4};Request(c,1,hello,sizeof(hello));CHECK(c->binary_mode);
 uint8_t calv[]={9,0x40,0x42,0x0f,0,3,0,0,0};Request(c,2,calv,sizeof(calv));CHECK(Data().voltage_gain_ppm==1000000 && Data().voltage_offset_mv==3);
 uint8_t invalid[]={3,0xd0,7};Request(c,3,invalid,sizeof(invalid));CHECK(ui_request.voltage_setpoint_mv==3300);
 uint8_t start[]={15,1,0};Request(c,4,start,sizeof(start));CHECK(PowerControl_IsDacCalibrationActive());
 PowerControl_Request_t r={3300,1500,300,true,true};measurement.data_valid=measurement.input_online=measurement.output_online=true;
 measurement.input_voltage_raw_mv=28000;measurement.sample_sequence=1;measurement.last_valid_sample_time_ms=tick;
 PowerControl_Process(tick,&r,&measurement);CHECK(Status().output_enabled);uint16_t old=Status().dac_code;
 uint8_t step[]={16,0xff,0xff};Request(c,5,step,sizeof(step));CHECK(Status().dac_code==old-1);
 Request(c,5,step,sizeof(step));CHECK(Status().dac_code==old-1); /* byte-for-byte retry must not apply STEP twice */
 step[1]=1;step[2]=0;Request(c,5,step,sizeof(step));CHECK(Status().dac_code==old-1); /* same sequence, different payload */
 uint8_t cap[]={17,0xe4,0x0c};Request(c,6,cap,sizeof(cap));CHECK(!Status().output_enabled && !ui_request.output_requested && Data().point_mask==2);
 uint8_t get[3]={13};Request(c,7,get,1);get[0]=14;get[1]=1;get[2]=0;Request(c,8,get,3);
 uint8_t cal[]={7};Request(c,9,cal,1);temperature.valid=true;temperature.temperature_decic=300;
 uint8_t status[]={2};Request(c,10,status,1);
 unsigned before=tx_size;DebugConsole_SendStatus(c,tick+20,&measurement,&(PowerControl_Status_t){0},&temperature,true);Drain(c);CHECK(tx_size-before==87);
 CHECK(dma_calls>10);FILE *out=fopen("wire_vectors_620.bin","wb");CHECK(out);fwrite(transmitted,1,tx_size,out);fclose(out);
 uint8_t probe[]={24};Request(c,11,probe,1);unsigned n=tx_size;
 uint8_t corrupted[32];unsigned len=Wire_Encode(corrupted,WIRE_COMMAND,12,probe,1);corrupted[len-1]^=1;
 for(unsigned i=0;i<len;i++){c->rx_buffer[c->rx_head]=corrupted[i];c->rx_head=(c->rx_head+1)%DEBUG_CONSOLE_RX_BUFFER_SIZE;}
 PowerControl_Status_t st=Status();DebugConsole_ProcessChannel(c,tick,&measurement,&st,&temperature);Drain(c);CHECK(n==tx_size);
 uint8_t rate[]={21,40,0};Request(c,20,rate,sizeof rate);CHECK(c->telemetry_rate_ms==40);
 CHECK(s_channels[1].telemetry_rate_ms==0); /* One channel cannot retime the other. */
 c->logs_enabled=true;c->last_telemetry_ms=0;tick=0;before=tx_size;
 for(tick=1;tick<=1000;tick++){DebugConsole_Process(tick,&measurement,&st,&temperature);Drain(c);}
 CHECK(tx_size-before==25*87); /* Exactly 25 complete frames / second. */
 rate[1]=100;Request(c,21,rate,sizeof rate);CHECK(c->telemetry_rate_ms==100);
 before=tx_size;
 for(tick=1001;tick<=2000;tick++){DebugConsole_Process(tick,&measurement,&st,&temperature);Drain(c);}
 CHECK(tx_size-before==10*87);
 rate[1]=19;Request(c,22,rate,sizeof rate);CHECK(c->telemetry_rate_ms==100);
 c->last_telemetry_ms=0xffffffecU;rate[1]=40;Request(c,23,rate,sizeof rate);before=tx_size;
 for(unsigned i=1;i<=80;i++){tick=0xffffffecU+i;DebugConsole_Process(tick,&measurement,&st,&temperature);Drain(c);}
 CHECK(tx_size-before==2*87);
 puts("PASS: binary telemetry 40ms=25Hz, configurable 100ms, range checks, independent channel periods and tick wrap");
 puts("PASS: production command dispatch, DMA queue, INFO/CAL/curve/status packing, CRC rejection, duplicate STEP/capture and sequence-conflict rejection");return 0;
}
