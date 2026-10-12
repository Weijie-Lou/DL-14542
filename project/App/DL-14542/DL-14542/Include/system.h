#ifndef _SYSTEM_H_
#define _SYSTEM_H_

/**
 * @file    system.h
 * @brief   系统初始化 / 任务调度 / 状态机接口
 * @note
 *   - System_Init()：看门狗、NTC 温度采集、显示初始化 + 运行参数复位，最后启动 Timer0（1ms 节拍）
 *   - System_Scheduler()：按 TASK_PERIOD[] 周期执行各任务，必须由主循环反复调用
 *   - System_StateMachine()：INIT（自检 3s，显示 88 + 灯色轮换）
 *                            → SLEEP（关显示，看门狗中断唤醒做入水检测）
 *                            ⇄ TEMP（显示水温（℃），离水或运行满 10 分钟回 SLEEP）
 *   - 主循环需同时调用 System_Scheduler() 与 System_StateMachine()（两者均负责喂狗）
 */

#include "gpio.h"
#include <stdint.h>

/*============================================================================
 * 函数声明
 *==========================================================================*/
/** @brief 应用层初始化：看门狗 / NTC 温度采集 / 数码管显示 / Timer0 时基 */
void System_Init(void);
/** @brief 周期任务调度（主循环调用，基于 1ms 系统节拍） */
void System_Scheduler(void);
/** @brief 系统状态机（主循环调用，基于 1ms 系统节拍） */
void System_StateMachine(void);

#endif  /* _SYSTEM_H_ */
