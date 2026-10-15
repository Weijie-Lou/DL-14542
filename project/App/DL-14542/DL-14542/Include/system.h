#ifndef _SYSTEM_H_
#define _SYSTEM_H_

/**
 * @file    system.h
 * @brief   系统初始化 / 任务调度 / 状态机接口
 * @note
 *   - System_Init()：看门狗 / GPIO / LVR / NTC 温度采集 / 输入 / 显示初始化 + 运行参数复位，
 *                    最后启动 Timer0（1ms 节拍）并进入上电自检
 *   - System_Scheduler()：按 TASK_PERIOD[] 周期执行各任务，必须由主循环反复调用
 *   - System_StateMachine()：INIT（自检 3s：888 + 88:88，灯圈每 500ms 换色）
 *                            → SLEEP（关显示、停 Timer0，WDT 每 60ms 唤醒一次轮询触摸/充电）
 *                            ⇄ TIMER（短按唤醒：温度 + 倒计时 + 电量 + 温区灯圈；再短按熄屏/唤醒；
 *                                     长按启动或复位 15 分钟倒计时；亮屏满 60s 自动熄屏，
 *                                     熄屏且未在倒计时则回 SLEEP；插入充电器切 CHARGING）
 *                            ⇄ CHARGING（只显示电池图标；拔出充电器回 SLEEP）
 *   - 主循环需同时调用 System_Scheduler() 与 System_StateMachine()（两者均负责喂狗）
 */

#include "gpio.h"
#include <stdint.h>

/*============================================================================
 * 函数声明
 *==========================================================================*/
/** @brief 应用层初始化：看门狗 / GPIO / LVR / NTC 温度采集 / 输入 / 数码管显示 / Timer0 时基 */
void System_Init(void);
/** @brief 周期任务调度（主循环调用，基于 1ms 系统节拍） */
void System_Scheduler(void);
/** @brief 系统状态机（主循环调用，基于 1ms 系统节拍） */
void System_StateMachine(void);

#endif  /* _SYSTEM_H_ */
