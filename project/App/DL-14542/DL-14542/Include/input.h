#ifndef _INPUT_H_
#define _INPUT_H_

/**
 * @file    input.h
 * @brief   触摸 + 充电状态输入驱动接口（每信号独立配置端口/引脚宏，均低电平有效）
 * @note
 *   - RA4 = Touch1（CT8233LF U2 的 QC） 低电平 = 触摸按下
 *   - RA3 = Touch2（CT8233LF U1 的 QC） 低电平 = 触摸按下
 *   - RA1 = CHRG  （TP4056 的 CHRG#）   低电平 = 充电中 充电电流1A
 *   - RA0 = STDBY （TP4056 的 STDBY#）  低电平 = 充满
 *   - 每个信号独立配置 INPUT_xxx_PORT / INPUT_xxx_PIN，便于移植换端口/引脚。
 */

#include "gpio.h"
#include <stdint.h>

/*============================================================================
 * 引脚配置（每信号独立端口 + 引脚宏）
 *==========================================================================*/

#define TOUCH_PORT   PORTA
#define TOUCH_PIN    GPIO_PIN_0     /* PA0 = Touch */

#define CHRG_PORT     PORTA
#define CHRG_PIN      GPIO_PIN_6    /* PA6 = CHRG */

#define STDBY_PORT    PORTA
#define STDBY_PIN     GPIO_PIN_5    /* PA5 = STDBY */

typedef enum {
    TOUCH_STATE_NONE        = 0,        /* 未触发 */
    TOUCH_STATE_PRESSED     = 1,        /* 已按下 */
    TOUCH_STATE_LONG        = 2,        /* 长按 */
    TOUCH_STATE_RELEASED    = 3,        /* 释放 */
} Touch_State_t;

typedef enum {
    TOUCH_CNT_NONE          = 0,        /* 无计数 */
    TOUCH_CNT_DEBOUNCE_CNT  = 5,        /* 消抖计数 */
    TOUCH_CNT_SHORT_CNT     = 25,       /* 短按判定计数 */
    TOUCH_CNT_LONG_CNT      = 50,       /* 长按判定计数 */
    TOUCH_CNT_MAX           = 250       /* 计数上限 */
} Touch_Cnt_t;

/* 充电端口状态枚举 */
typedef enum {
    CHARGE_STATE_NONE       = 0,        /* 未充电 */
    CHARGE_STATE_CHARGING   = 1,        /* 充电中（CHRG 低有效）*/
    CHARGE_STATE_FULL       = 2         /* 已充满（STDBY 低有效）*/
} Charge_State_t;

/* 充电端口消抖计数枚举 */
typedef enum {
    CHARGE_CNT_NONE         = 0,        /* 无计数 */
    CHARGE_DEBOUNCE_CNT     = 5,        /* 消抖计数 */
    CHARGE_CNT_MAX          = 250       /* 计数上限 */
} Charge_Cnt_t;

/*============================================================================
 * 函数声明
 *==========================================================================*/
/** @brief 初始化输入引脚（数字模式 + 输入 + 内部上拉）。 */
void Input_Init(void);

/** @brief 查询 Touch 是否按下（返回 1=按下，低电平有效）。 */
Touch_State_t Touch_Scan(void);

/** @brief 查询充电状态（返回充电状态枚举值）。 */
Charge_State_t Charge_Scan(void);

#endif  /* _INPUT_H_ */
