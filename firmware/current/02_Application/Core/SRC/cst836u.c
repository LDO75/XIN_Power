/**
  ******************************************************************************
  * @file    cst836u.c
  * @brief   CST836U电容触摸控制器驱动实现。
  ******************************************************************************
  */

#include "cst836u.h"
#include "gpio.h"
#include "i2c.h"
#include "lcd.h"

#include <stddef.h>

#define CST836U_I2C_ADDRESS_7BIT          0x15U
#define CST836U_I2C_ADDRESS_HAL           (CST836U_I2C_ADDRESS_7BIT << 1U)
#define CST836U_REPORT_REGISTER           0x00U
#define CST836U_REPORT_LENGTH             15U
#define CST836U_I2C_TIMEOUT_MS             5U
#define CST836U_INITIAL_READY_TRIALS        5U
#define CST836U_RECOVER_READY_TRIALS        1U
#define CST836U_READ_RETRY_INTERVAL_MS     10U
#define CST836U_OFFLINE_ERROR_COUNT         3U

static volatile bool s_cst836u_interrupt_pending = false;
static bool s_cst836u_ready = false;
static CST836U_Transform_t s_cst836u_transform = CST836U_DEFAULT_TRANSFORM;
static uint16_t s_cst836u_last_x = 0U;
static uint16_t s_cst836u_last_y = 0U;
static uint32_t s_cst836u_last_read_attempt_ms = 0U;
static uint8_t s_cst836u_consecutive_error_count = 0U;

static HAL_StatusTypeDef CST836U_ReadRegister(uint8_t register_address,
                                               uint8_t *data,
                                               uint16_t length);
static void CST836U_TransformCoordinates(uint16_t raw_x,
                                         uint16_t raw_y,
                                         uint16_t *display_x,
                                         uint16_t *display_y);

/**
 * @brief  从指定CST836U寄存器开始读取若干字节。
 * @param  register_address 起始寄存器地址。
 * @param  data 接收缓冲区。
 * @param  length 待读取字节数。
 * @retval HAL传输状态。
 */
static HAL_StatusTypeDef CST836U_ReadRegister(uint8_t register_address,
                                               uint8_t *data,
                                               uint16_t length)
{
  HAL_StatusTypeDef status;

  status = HAL_I2C_Master_Transmit(&hi2c1,
                                   CST836U_I2C_ADDRESS_HAL,
                                   &register_address,
                                   1U,
                                   CST836U_I2C_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_I2C_Master_Receive(&hi2c1,
                                CST836U_I2C_ADDRESS_HAL,
                                data,
                                length,
                                CST836U_I2C_TIMEOUT_MS);
}

/**
 * @brief  将触摸控制器原始坐标转换为液晶屏坐标。
 * @param  raw_x 原始触摸X坐标。
 * @param  raw_y 原始触摸Y坐标。
 * @param  display_x 转换后的显示X坐标。
 * @param  display_y 转换后的显示Y坐标。
 * @retval 无。
 */
static void CST836U_TransformCoordinates(uint16_t raw_x,
                                         uint16_t raw_y,
                                         uint16_t *display_x,
                                         uint16_t *display_y)
{
  int32_t x;
  int32_t y;

  switch (s_cst836u_transform)
  {
    case CST836U_TRANSFORM_ROTATE_90_CW:
      x = raw_y;
      y = (int32_t)LCD_HEIGHT - 1 - raw_x;
      break;

    case CST836U_TRANSFORM_ROTATE_90_CCW:
      x = (int32_t)LCD_WIDTH - 1 - raw_y;
      y = raw_x;
      break;

    case CST836U_TRANSFORM_ROTATE_180:
      x = (int32_t)LCD_WIDTH - 1 - raw_x;
      y = (int32_t)LCD_HEIGHT - 1 - raw_y;
      break;

    case CST836U_TRANSFORM_NONE:
    default:
      x = raw_x;
      y = raw_y;
      break;
  }

  if (x < 0)
  {
    x = 0;
  }
  else if (x >= (int32_t)LCD_WIDTH)
  {
    x = (int32_t)LCD_WIDTH - 1;
  }

  if (y < 0)
  {
    y = 0;
  }
  else if (y >= (int32_t)LCD_HEIGHT)
  {
    y = (int32_t)LCD_HEIGHT - 1;
  }

  *display_x = (uint16_t)x;
  *display_y = (uint16_t)y;
}

/**
 * @brief  复位CST836U并通过I2C1探测器件。
 * @retval 器件应答正常工作地址时返回true。
 */
bool CST836U_Init(void)
{
  HAL_GPIO_WritePin(TP_RST_Port, TP_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(TP_RST_Port, TP_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(50U);

  s_cst836u_interrupt_pending = false;
  s_cst836u_consecutive_error_count = 0U;
  s_cst836u_last_read_attempt_ms = HAL_GetTick();
  s_cst836u_ready =
    (HAL_I2C_IsDeviceReady(&hi2c1,
                           CST836U_I2C_ADDRESS_HAL,
                           CST836U_INITIAL_READY_TRIALS,
                           CST836U_I2C_TIMEOUT_MS) == HAL_OK);
  return s_cst836u_ready;
}

/**
 * @brief  不复位触摸芯片，仅重新探测I2C应答并恢复驱动状态。
 * @retval 触摸芯片已经恢复应答时返回true。
 */
bool CST836U_TryRecover(void)
{
  if (s_cst836u_ready)
  {
    return true;
  }

  if (HAL_I2C_IsDeviceReady(&hi2c1,
                            CST836U_I2C_ADDRESS_HAL,
                            CST836U_RECOVER_READY_TRIALS,
                            CST836U_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return false;
  }

  s_cst836u_interrupt_pending = false;
  s_cst836u_consecutive_error_count = 0U;
  s_cst836u_last_read_attempt_ms = HAL_GetTick();
  s_cst836u_ready = true;
  return true;
}

/**
 * @brief  通知驱动程序TP_INT外部中断已经发生。
 * @retval 无。
 * @note   可以安全地在中断回调函数中调用本函数。
 */
void CST836U_NotifyInterrupt(void)
{
  s_cst836u_interrupt_pending = true;
}

/**
 * @brief  在中断上下文之外读取一帧待处理的触摸数据。
 * @param  point 用于保存解析后坐标和事件的结构体。
 * @retval 成功读取一帧数据时返回true，否则返回false。
 */
bool CST836U_GetEvent(CST836U_Point_t *point)
{
  uint8_t report[CST836U_REPORT_LENGTH] = {0U};
  uint8_t raw_event;
  uint16_t raw_x;
  uint16_t raw_y;
  uint32_t now;

  if ((point == NULL) || !s_cst836u_ready)
  {
    return false;
  }

  if (!s_cst836u_interrupt_pending &&
      (HAL_GPIO_ReadPin(TP_INT_Port, TP_INT_Pin) == GPIO_PIN_SET))
  {
    return false;
  }

  now = HAL_GetTick();
  if ((now - s_cst836u_last_read_attempt_ms) <
      CST836U_READ_RETRY_INTERVAL_MS)
  {
    return false;
  }
  s_cst836u_last_read_attempt_ms = now;

  s_cst836u_interrupt_pending = false;
  if (CST836U_ReadRegister(CST836U_REPORT_REGISTER,
                           report,
                           sizeof(report)) != HAL_OK)
  {
    if (s_cst836u_consecutive_error_count < UINT8_MAX)
    {
      s_cst836u_consecutive_error_count++;
    }
    if (s_cst836u_consecutive_error_count >= CST836U_OFFLINE_ERROR_COUNT)
    {
      s_cst836u_ready = false;
    }
    return false;
  }

  s_cst836u_consecutive_error_count = 0U;

  point->point_count = report[2] & 0x0FU;
  if (point->point_count == 0U)
  {
    point->x = s_cst836u_last_x;
    point->y = s_cst836u_last_y;
    point->id = 0U;
    point->event = CST836U_EVENT_UP;
    return true;
  }

  raw_event = (report[3] >> 6U) & 0x03U;
  raw_x = (uint16_t)(((uint16_t)(report[3] & 0x0FU) << 8U) | report[4]);
  raw_y = (uint16_t)(((uint16_t)(report[5] & 0x0FU) << 8U) | report[6]);

  CST836U_TransformCoordinates(raw_x, raw_y, &point->x, &point->y);
  point->id = (report[5] >> 4U) & 0x0FU;

  if (raw_event == 0U)
  {
    point->event = CST836U_EVENT_DOWN;
  }
  else if (raw_event == 1U)
  {
    point->event = CST836U_EVENT_UP;
  }
  else
  {
    point->event = CST836U_EVENT_CONTACT;
  }

  s_cst836u_last_x = point->x;
  s_cst836u_last_y = point->y;
  return true;
}

/**
 * @brief  选择从原始触摸坐标到320×240显示坐标的变换方式。
 * @param  transform 所需的坐标变换方式。
 * @retval 无。
 */
void CST836U_SetTransform(CST836U_Transform_t transform)
{
  s_cst836u_transform = transform;
}

/**
 * @brief  查询触摸控制器在初始化时是否正确应答。
 * @retval 控制器就绪时返回true。
 */
bool CST836U_IsReady(void)
{
  return s_cst836u_ready;
}
