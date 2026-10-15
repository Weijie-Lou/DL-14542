#ifndef _INPUT_H_
#define _INPUT_H_

/**
 * @file    input.h
 * @brief   触摸 + 充电状态输入驱动接口（每信号独立配置端口/引脚宏）
 * @note
 *   - PA0 = TOUCH （CT8233LF 触摸芯片的 QC 输出）低电平 = 触摸按下
 *   - PA6 = CHRG  （TP4056 的 CHRG#）读高表示充电中
 *   - PA5 = STDBY （TP4056 的 STDBY#）读高表示已充满（读取前临时使能内部上拉）
 *   - 判定细节见 Charge_Scan()：CHRG 读低一律视为未充电（电平极性以本板实际接线为准）
 *   - 每个信号独立配置 xxx_PORT / xxx_PIN，便于移植换端口/引脚
 *   - 消抖计数以“一次扫描”为单位：运行时 TASK_TOUCH_SCAN 每 60ms、
 *     TASK_CHARGE_SCAN 每 100ms 调用一次；休眠时由 WDT 每 60ms 唤醒后调用一次
 */

#include "gpio.h"
#include <stdint.h>

/*============================================================================
 * 引脚配置（每信号独立端口 + 引脚宏）
 *==========================================================================*/

#define TOUCH_PORT      PORTA
#define TOUCH_PIN       GPIO_PIN_0      /* PA0 = Touch */

#define CHRG_PORT       PORTA
#define CHRG_PIN        GPIO_PIN_6      /* PA6 = CHRG */

#define STDBY_PORT      PORTA
#define STDBY_PIN       GPIO_PIN_5      /* PA5 = STDBY */

typedef enum {
    TOUCH_STATE_NONE        = 0,        /* 未触发 */
    TOUCH_STATE_PRESSED     = 1,        /* 已按下 */
    TOUCH_STATE_LONG        = 2,        /* 长按 */
    TOUCH_STATE_RELEASED    = 3,        /* 释放 */
} Touch_State_t;

typedef enum {
    TOUCH_CNT_NONE          = 0,        /* 无计数 */
    TOUCH_CNT_DEBOUNCE_CNT  = 1,        /* 释放消抖计数（按 1 次扫描判定） */
    TOUCH_CNT_SHORT_CNT     = 25,       /* 短按判定计数（25 × 60ms = 1.5s 以内） */
    TOUCH_CNT_LONG_CNT      = 50,       /* 长按判定计数（50 × 60ms = 3s） */
    TOUCH_CNT_MAX           = 250       /* 计数上限（饱和后不再累加） */
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
    CHARGE_DEBOUNCE_CNT     = 5,        /* 消抖计数（5 × 100ms = 500ms） */
    CHARGE_CNT_MAX          = 250       /* 计数上限（饱和后不再累加） */
} Charge_Cnt_t;

/*============================================================================
 * 函数声明
 *==========================================================================*/
/** @brief 初始化输入引脚（TOUCH / CHRG / STDBY 均配置为输入）。 */
void Input_Init(void);

/** @brief 扫描触摸状态：按下时返回长按（持续 3s）或松开时返回短按，未触发返回 NONE。 */
Touch_State_t Touch_Scan(void);

/** @brief 扫描充电状态：返回未充电 / 充电中 / 已充满（带 500ms 消抖）。 */
Charge_State_t Charge_Scan(void);

#endif  /* _INPUT_H_ */
