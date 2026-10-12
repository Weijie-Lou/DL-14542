/**
 * @file    main.c
 * @brief   主程序入口（NY8B062F / NYIDE 6.11）
 * @note
 *   - 上电后 System_Init() 初始化看门狗、NTC 温度采集、数码管显示并启动 Timer0
 *   - 主循环：System_Scheduler() 周期任务调度 + System_StateMachine() 系统状态机
 *   - 系统时基由 Timer0 1ms 中断提供，数码管动态扫描与看门狗溢出处理也在该中断内完成
 */
#include "system.h"

void main(void)
{
    System_Init();

    while (1) {
        System_Scheduler();
        System_StateMachine();
    }
}