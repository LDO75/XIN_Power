/**
  ******************************************************************************
  * @file    cst836u.h
  * @brief   CST836U电容触摸控制器驱动接口。
  ******************************************************************************
  */

#ifndef __CST836U_H__
#define __CST836U_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  CST836U_TRANSFORM_NONE = 0,
  CST836U_TRANSFORM_ROTATE_90_CW,
  CST836U_TRANSFORM_ROTATE_90_CCW,
  CST836U_TRANSFORM_ROTATE_180
} CST836U_Transform_t;

typedef enum
{
  CST836U_EVENT_NONE = 0,
  CST836U_EVENT_DOWN,
  CST836U_EVENT_CONTACT,
  CST836U_EVENT_UP
} CST836U_Event_t;

typedef struct
{
  uint16_t x;
  uint16_t y;
  uint8_t id;
  uint8_t point_count;
  CST836U_Event_t event;
} CST836U_Point_t;

/*
 * 触摸玻璃通常按竖屏方向安装，而液晶屏工作在320×240横屏方向。
 * 如果所用屏幕模组已经直接上报320×240坐标，请将此项改为
 * CST836U_TRANSFORM_NONE。
 */
#define CST836U_DEFAULT_TRANSFORM         CST836U_TRANSFORM_ROTATE_90_CW

/**
 * @brief  复位CST836U并通过I2C1探测器件。
 * @retval 器件应答正常工作地址时返回true。
 */
bool CST836U_Init(void);

/**
 * @brief  不复位触摸芯片，仅重新探测I2C应答并恢复驱动状态。
 * @retval 触摸芯片已经恢复应答时返回true。
 */
bool CST836U_TryRecover(void);

/**
 * @brief  通知驱动程序TP_INT外部中断已经发生。
 * @retval 无。
 * @note   可以安全地在中断回调函数中调用本函数。
 */
void CST836U_NotifyInterrupt(void);

/**
 * @brief  在中断上下文之外读取一帧待处理的触摸数据。
 * @param  point 用于保存解析后坐标和事件的结构体。
 * @retval 成功读取一帧数据时返回true，否则返回false。
 */
bool CST836U_GetEvent(CST836U_Point_t *point);

/**
 * @brief  选择从原始触摸坐标到320×240显示坐标的变换方式。
 * @param  transform 所需的坐标变换方式。
 * @retval 无。
 */
void CST836U_SetTransform(CST836U_Transform_t transform);

/**
 * @brief  查询触摸控制器在初始化时是否正确应答。
 * @retval 控制器就绪时返回true。
 */
bool CST836U_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __CST836U_H__ */
