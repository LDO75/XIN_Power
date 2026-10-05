/* Exercises the production callbacks/service with FE/NE/ORE and no RXNE.
 * Peripheral flag side effects and UART IT are mocked, not real BT hardware. */
#define DEBUG_CONSOLE_HOST_TEST
#include "../02_Application/Core/SRC/debug_console.c"
#include <assert.h>

UART_HandleTypeDef husart1, husart2;
static USART_TypeDef usb_registers, bt_registers;
static uint32_t tick, receives, aborts, tx_aborts;
static UI_ControlRequest_t mock_request;
static bool mock_cal_active, mock_output;
bool PowerControl_IsDacCalibrationActive(void) { return mock_cal_active; }
void PowerControl_GetStatus(PowerControl_Status_t *p)
{ memset(p,0,sizeof(*p)); p->output_enabled=mock_output; }
void UI_GetControlRequest(UI_ControlRequest_t *p) { *p=mock_request; }
bool UI_SetVoltageSetpoint(uint16_t v) { mock_request.voltage_setpoint_mv=v; return true; }
bool UI_SetCurrentLimit(uint16_t i) { mock_request.current_limit_ma=i; return true; }
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart,
                                     uint8_t *data, uint16_t length)
{
  (void)data;
  assert(length == 1U);
  receives++;
  uart->RxState = HAL_UART_STATE_BUSY_RX;
  uart->Instance->CR1 |= USART_CR1_RXNEIE | USART_CR1_PEIE;
  uart->Instance->CR3 |= USART_CR3_EIE;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
  aborts++;
  uart->RxState = HAL_UART_STATE_READY;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
  tx_aborts++;
  uart->gState = HAL_UART_STATE_READY;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                      uint8_t *data, uint16_t length)
{
  (void)uart; (void)data; (void)length;
  return HAL_OK;
}
int main(void)
{
  husart1.Instance = &usb_registers;
  husart2.Instance = &bt_registers;
  husart1.Init.BaudRate = husart2.Init.BaudRate = 115200U;
  s_channels[0].uart = &husart1;
  s_channels[1].uart = &husart2;
  s_initialized = true;
  DebugConsole_Channel_t *bt = &s_channels[1];
  for (unsigned i = 0; i < 200; i++)
  {
    tick = i * 50U;
    bt_registers.CR1 |= USART_CR1_RXNEIE | USART_CR1_PEIE;
    bt_registers.CR3 |= USART_CR3_EIE;
    bt_registers.SR = USART_SR_FE | USART_SR_NE | USART_SR_ORE; /* RXNE=0 */
    husart2.RxState = HAL_UART_STATE_BUSY_RX;
    uint32_t before = receives;
    HAL_UART_ErrorCallback(&husart2);
    assert(bt_registers.SR == 0U);
    assert(!(bt_registers.CR1 & (USART_CR1_RXNEIE | USART_CR1_PEIE)));
    assert(!(bt_registers.CR3 & USART_CR3_EIE));
    assert(bt->rx_restart_pending && receives == before);
    DebugConsole_Service(tick + 19U);
    assert(receives == before);
    DebugConsole_Service(tick + 20U);
    assert(receives == before + 1U && !bt->rx_restart_pending);
    assert(bt_registers.CR1 & USART_CR1_RXNEIE);
    bt->rx_interrupt_byte = 'P';
    HAL_UART_RxCpltCallback(&husart2);
    assert(bt->rx_head != bt->rx_tail);
  }
  assert(bt->uart_error_count == 200U && bt->rx_recovery_count == 200U);
  assert(aborts == 200U && s_channels[0].uart_error_count == 0U);
  bt->tx_active = true;
  bt->tx_active_length = 240U;
  bt->tx_head = 250U;
  bt->tx_tail = 10U;
  bt->tx_start_time_ms = tick;
  DebugConsole_Service(tick + 100U);
  assert(!bt->tx_active && bt->tx_tail == bt->tx_head);
  assert(bt->tx_timeout_count == 1U && tx_aborts == 1U);
  /* Unsigned millisecond wrap also recovers at 20ms, not after reconnect. */
  tick = UINT32_MAX - 5U;
  bt_registers.SR = USART_SR_FE;
  HAL_UART_ErrorCallback(&husart2);
  DebugConsole_Service(14U);
  assert(!bt->rx_restart_pending && bt->rx_recovery_count == 201U);
  puts("UART FE/NE/ORE recovery, cooldown, TX timeout and tick wrap: PASS");
  s_curve_previous_request.voltage_setpoint_mv=12000;
  s_curve_previous_request.current_limit_ma=1500;
  mock_request.voltage_setpoint_mv=32000; mock_request.current_limit_ma=100;
  s_curve_restore_pending=true; mock_cal_active=true;
  DebugConsole_RestoreCurveRequest(); assert(s_curve_restore_pending);
  mock_cal_active=false; mock_output=true;
  DebugConsole_RestoreCurveRequest(); assert(s_curve_restore_pending);
  mock_output=false; mock_request.output_requested=true;
  DebugConsole_RestoreCurveRequest(); assert(s_curve_restore_pending);
  mock_request.output_requested=false;
  DebugConsole_RestoreCurveRequest(); assert(!s_curve_restore_pending);
  assert(mock_request.voltage_setpoint_mv==12000 && mock_request.current_limit_ma==1500);
  assert(!mock_request.output_requested);
  puts("Calibration restores original setpoints only after output is OFF: PASS");
  return 0;
}
