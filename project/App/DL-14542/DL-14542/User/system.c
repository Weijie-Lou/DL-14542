/**
 * @file    system.c
 * @brief   系统初始化 + 周期任务调度 + 状态机（水温显示主逻辑）
 * @note
 *   - Timer0 每 1ms 产生系统节拍（同时完成数码管一相扫描），调度器与状态机在主循环中按节拍运行
 *   - 采用“任务表驱动 + 两级状态机”：sys_mode 描述整轮流程，sys_state 描述子状态（如唤醒首采）
 *   - 流程：INIT（3s：显示 88 + R/G/B 指示灯自检）→ SLEEP（显示关闭，靠看门狗中断周期唤醒做入水检测）
 *           → TEMP（显示水温（℃）+ 温区指示灯；离水（消抖 WATER_CNT_EXIT = 3 次）或运行满
 *             SYSTEM_RUN_TIME(10 分钟) → 回 SLEEP）
 *   - 入水锁存：sys_state 的 SYSTEM_STATE_WATER_INGRESS 位记录“本轮已确认入水”，
 *     避免休眠期间每次看门狗唤醒（约 250ms，水仍连通时每次都返回 INGRESS）都重复启动显示；
 *     该位在确认离水（SLEEP 分支或 TASK_WATER_SCAN）时清除，因此运行超时熄灭后
 *     必须“先取出（确认离水）再放入”才能重新点亮
 *   - 看门狗配置（参 NYIDE 检查清单 DL-8107.htm）：Enable、Event = Interrupt、Time Base = 250ms；
 *     正常运行时由主循环喂狗，休眠期间该中断是唯一唤醒源，必须 CLRWDT() + 清 WDTIF（见 isr()）；
 *     因此休眠时入水检测实际约 250ms 一次（消抖判断见 water.c）
 */

#include "system.h"
#include "gpio.h"
#include "display.h"
#include "temp.h"
#include "input.h"

/* 系统初始化时间 */
#define SYSTEM_INIT_TIME        3000            /* 系统初始化时间，单位：毫秒 */
#define SYSTEM_LED_SELF_TIME    500             /* LED 自检切换时长，单位：毫秒（自检换色现由 zone_tab 直接按 sys_tick/500 计算，本宏暂未被引用） */
#define SYSTEM_RUN_TIME         10              /* 产品运行时长，单位：分钟 */

 /*--- 任务定义 -----------------------------------------------------------*/
typedef enum {
    TASK_TOUCH_SCAN,                            /* 触摸扫描（按下/长按检测） */
    TASK_DISPLAY_UPDATE,                        /* 显示刷新（100ms，把 system.temp_c（℃）送往显示模块） */
    TASK_CHARGE_SCAN,                           /* 充电状态扫描（充电中/充满检测） */
    TASK_NTC_SAMPLE,                            /* NTC 采样（200ms，推进滤波；STARTUP 时取有效值） */
    TASK_TEMP_UPDATE,                           /* 温度更新（2000ms，重读温度（℃）并更新 system.temp_c） */
    TASK_SEC_TIMER,                             /* 每秒计时 1000ms */
    TASK_DELAY,                                 /* 占位（不参与周期执行；自检换色改用 zone_tab，其时间戳当前无人使用） */
    TASK_MAX
} TaskID_e;

/*--- 调度时序 -----------------------------------------------------------*/
/* 说明：调度循环只遍历 i < TASK_DELAY，故 TASK_DELAY 自身不按周期执行；
 *       它只用于占住最后一个任务槽，task_id[TASK_DELAY] 当前无代码使用
 *       （上电自检换色已改为在 SYSTEM_MODE_INIT 中用 zone_tab 直接计算）。
 *       表中该行的周期值仅为占位。 */
const uint16_t TASK_PERIOD[TASK_MAX] = {
    60,                                         /* TASK_TOUCH_SCAN 触摸扫描 */
    100,                                        /* TASK_DISPLAY_UPDATE 显示更新 */
    100,                                        /* TASK_CHARGE_SCAN 充电状态扫描 */
    200,                                        /* TASK_NTC_SAMPLE NTC 温度采样 */
    2000,                                       /* TASK_TEMP_UPDATE 温度更新 */   
    1000,                                       /* TASK_SEC_TIMER 每秒计时 1000ms */
    60000,                                      /* TASK_DELAY 占位值（不参与周期比较） */
};

/* 系统节拍更新标志（中断置位 / 主循环清除） */
typedef enum {
    SYSTEM_TICK_IDLE = 0,                       /* 本拍已被处理 */
    SYSTEM_TICK_UPDATE,                         /* 有新的 1ms 节拍待处理 */
} Tick_State_e;

typedef enum {
    SYSTEM_STATE_NONE               = 0x00,
    SYSTEM_STATE_STARTUP            = 0x01,     /* 唤醒后首采：由 NTC 任务立即取一次有效温度 */
    SYSTEM_STATE_CHARGING           = 0x02,     /* 充电中 */
    SYSTEM_STATE_CHARGE_FULL        = 0x04,     /* 充满 */
    SYSTEM_STATE_TOUCH_LONG_PRESS   = 0x08,     /* 触摸长按中 */
} System_State_e;

typedef enum {
    SYSTEM_EVENT_NONE               = 0x00u,
    SYSTEM_EVENT_TOUCH_LONG_PRESS   = 0x01u,    /* 触摸长按事件 */
    SYSTEM_EVENT_CHARGING           = 0x02u,    /* 充电中事件 */
    SYSTEM_EVENT_CHARGE_FULL        = 0x04u,    /* 充满事件 */
    SYSTEM_EVENT_MAX
} System_Event_e;

typedef enum {
    SYSTEM_MODE_INIT,                           /* 上电自检：显示 88 + R/G/B 指示灯自检，持续 3s */
    SYSTEM_MODE_TIMER,                          /* 运行：显示水温（℃）+ 温区指示灯 */
    SYSTEM_MODE_SLEEP,                          /* 休眠：显示关闭 */
    SYSTEM_MODE_MAX
} System_Mode_e;

typedef struct {
    volatile uint16_t           sys_tick;               /* 系统节拍计数（1ms 递增，睡眠时停住） */
             uint16_t           task_id[TASK_MAX];      /* 各任务上次执行的时间戳 */
             uint16_t           temp;                   /* 当前水温（℃） */
             uint16_t           sec_cnt;                /* 秒计数 */
             uint8_t            battery_level;          /* 当前电池电量（%） */
    volatile Tick_State_e       tick_state;             /* 系统节拍更新状态 */
             Battery_Status_e   battery_status;         /* 当前电池状态 */
             System_State_e     sys_state;              /* 当前系统状态（STARTUP 位） */
             System_Event_e     sys_event;              /* 待处理的系统事件 */
             System_Mode_e      sys_mode;               /* 整轮流程模式 */
} System_t;

System_t system = {0};                                  /* 系统运行参数（全局唯一实例） */

/** @brief 中断服务：Timer0 1ms 系统节拍 + 数码管动态扫描 + 看门狗溢出处理 */
void isr(void) __interrupt(0)
{
    if(INTCONbits.T0IF)                                                             // Timer0 溢出
    {
        // Add user code
        TMR0     = 0x83;                                                          // Load 0x83 to TMR0 (Initial Timer0 register)
                                                                                  // Real Timing Duration: 1.0000ms
        INTCONbits.T0IF = 0;                                                        // Clear T0IF flag bit

        system.sys_tick++;                              /* 1ms 节拍累加（主循环据此调度任务） */
        system.tick_state = SYSTEM_TICK_UPDATE;
        
        Display_Scan_ISR();                             /* 数码管动态扫描（每 1ms 一相） */
    }

    if (PIR1bits.WDTIF) {                               /* 看门狗溢出（配置：Event=Interrupt，时基 250ms）；
                                                         * 休眠期间这是唯一的唤醒源 */
        CLRWDT();                                       /* 清 WDT 计数器 */
        PIR1bits.WDTIF = 0;                             /* 清中断标志（不清会反复重入本中断） */
    }
}

/** @brief 初始化 Timer0：1ms 溢出中断（TMR0=0x83，预分频 1:32） */
void Timer0_Init(void)
{
    INTCONbits.GIE = 0;                                                           /* 关闭总中断 */
    INTCONbits.PEIE = 1;                                                          /* 使能外设中断 */

    T0MD     = C_TMR0_Dis | C_PS0_TMR0 | C_PS0_Div8;                              /* 先关闭 Timer0 */
                                                                                  /* PS0 预分频分配给 Timer0 */
                                                                                  /* 预分频比 = 1:8 */
                                                                                  /* Timer0 时钟源 = 指令时钟 */

    TMR0     = 0x83;                                                              /* 装载 TMR0 初值 0x83 */
                                                                                  /* 溢出周期 250.0000us */

    INTCONbits.T0IE = 1;                                                          /* 使能 Timer0 溢出中断 */
    T0MDbits.T0EN = 1;                                                            /* 使能 Timer0 */
    INTCONbits.GIE = 1;                                                           /* 使能总中断 */
}


/** @brief 关闭 Timer0 节拍（进入休眠前调用），并清 T0IF 防止重启后立即误触发 */
void Timer0_Stop(void)
{
    T0MDbits.T0EN = 0;                               /* 停止 Timer0（睡眠期间停节拍，由 INT0 唤醒后重新打开） */
}


//--------------- WDT INIT ----------------------------------------------
/* 看门狗：睡眠期间由 WDT 中断周期唤醒 CPU（约 60ms 一次，配置字 WDT Time Base = 60ms），
 * 主循环中被唤醒后继续执行 System_Process()，无操作则再次进入 sleep */
void WDT_Init(void)
{
    PCONbits.WDTEN  = 1;                             /* 使能 WDT */
    PIE1bits.WDTIE  = 1;                             /* 使能 WDT 中断（sleep 唤醒源） */
    INTCONbits.PEIE = 1;                             /* 使能外设中断 */
}

/*============================================================================
 * 睡眠
 *   进入前需先关显示并停止 Timer0（见 System_AutoShutdown）；
 *   唤醒源：WDT 中断、INT0 上升沿（触摸）
 *==========================================================================*/
void Sleep_Enter(void)
{
    asm("nop");
    asm("nop");
    asm("sleep");
    asm("nop");
    asm("nop");
}

/** @brief 系统初始化：看门狗 / 温度采集 / 显示初始化 + 运行参数复位 + 启动 1ms 时基 */
void System_Init(void)
{
    WDT_Init();                                     /* 看门狗使能（休眠唤醒源，紧随其后的初始化也不能超时） */
    Temp_Init();                                    /* NTC 采样引脚使能 + 滤波参数复位 */
    Display_Init();                                 /* 关闭显示并复位显示缓冲 */

    system.sys_tick = 0;
    system.tick_state = SYSTEM_TICK_IDLE;
    system.temp = 0;
    system.sec_cnt = 0;
    system.battery_level = BATTERY_LEVEL_L0;
    system.battery_status = BATTERY_STATUS_NORMAL;
    system.sys_state = SYSTEM_STATE_NONE;
    system.sys_event = SYSTEM_EVENT_NONE;
    system.sys_mode = SYSTEM_MODE_INIT;

    for (uint8_t i = 0; i < TASK_MAX; i++) {
        system.task_id[i] = 0;                      /* 所有任务时间戳清零 */
    }

    Display_SetMode(DISPLAY_MODE_INIT, system.temp, system.sec_cnt, system.battery_level, system.battery_status);        /* 进入自检模式：显示 88（灯色接着由 zone_tab 每 500ms 轮换） */
    Timer0_Init();                                  /* 启动 1ms 节拍，数码管扫描随之开始 */
}

/** @brief 周期任务调度：每个 1ms 节拍检查一次各任务是否到期
 *  @note  休眠模式下只喂狗不跑任务；时间比较用无符号回绕减法，漏掉几个节拍也不会丢任务
 */
void System_Scheduler(void)
{
    // Add user code for task scheduling
    CLRWDT();                                           /* 喂狗（休眠模式下是本函数唯一动作） */
    if (system.tick_state == SYSTEM_TICK_UPDATE) {
        system.tick_state = SYSTEM_TICK_IDLE;
        if (system.sys_mode == SYSTEM_MODE_SLEEP) return;

        for (uint8_t i = 0; i < TASK_DELAY; i++) {
            if ((uint16_t)(system.sys_tick - system.task_id[i]) < TASK_PERIOD[i]) continue;  /* 未到周期，跳过 */
            system.task_id[i] = system.sys_tick;    /* 记录本次执行时间戳 */
            switch (i) {
                case TASK_TOUCH_SCAN:
                    switch (Touch_Scan()) {
                        case TOUCH_STATE_LONG:
                            if (system.sys_state & SYSTEM_STATE_TOUCH_LONG_PRESS) break;
                            system.sys_state |= SYSTEM_STATE_TOUCH_LONG_PRESS;
                            system.sys_event |= SYSTEM_EVENT_TOUCH_LONG_PRESS;
                            break;
                        case TOUCH_STATE_RELEASED:
                            if (system.sys_state & SYSTEM_STATE_TOUCH_LONG_PRESS) {
                                system.sys_state &= ~SYSTEM_STATE_TOUCH_LONG_PRESS;
                            }
                            break;                    
                        default:
                            break;
                    }
                    break;
                case TASK_DISPLAY_UPDATE:
                    if (system.sys_mode == SYSTEM_MODE_INIT) break;
                    Display_SetDigit(system.temp, system.sec_cnt, system.battery_level, system.battery_status);     /* 把当前水温送往显示模块 */
                    break;
                case TASK_CHARGE_SCAN:
                    switch (Charge_Scan()) {
                        case CHARGE_STATE_CHARGING:
                            if (system.sys_state & SYSTEM_STATE_CHARGING) break;
                            system.sys_state |= SYSTEM_STATE_CHARGING;
                            system.sys_state &= ~SYSTEM_STATE_CHARGE_FULL;
                            system.sys_event |= SYSTEM_EVENT_CHARGING;
                            break;
                        case CHARGE_STATE_FULL:
                            if (system.sys_state & SYSTEM_STATE_CHARGE_FULL) break;
                            system.sys_state |= SYSTEM_STATE_CHARGE_FULL;
                            system.sys_state &= ~SYSTEM_STATE_CHARGING;
                            system.sys_event |= SYSTEM_EVENT_CHARGE_FULL;
                            break;                    
                        default:
                            break;
                    }
                    break;
               case TASK_NTC_SAMPLE:
                    if (system.sys_state & SYSTEM_STATE_STARTUP) {
                        /* 刚唤醒：立即取一次有效温度，避免长时间显示上次的旧值 */
                        system.temp = Temp_Read();
                        system.sys_state &= ~SYSTEM_STATE_STARTUP;
                    } else {
                        Temp_Read();                                                /* 高频采样，仅推进 IIR 滤波，结果不直接使用 */
                    }
                    break;
                case TASK_TEMP_UPDATE:
                    system.temp = Temp_Read();                                      /* 每 2s 更新一次有效温度 */
                    break;
                default:
                    break;
            }
        }
    }
}

/** @brief 系统状态机：处理整轮流程（sys_mode）与子状态（sys_state） */
void System_StateMachine(void)
{
    CLRWDT();                                           /* 主循环同样承担喂狗 */
    switch (system.sys_mode) {
        /* 上电自检：显示 88，温区指示灯红/绿/蓝每 500ms 换色；满 3s 后关显示并进入休眠 */
        case SYSTEM_MODE_INIT:
            if (system.sys_tick >= SYSTEM_INIT_TIME) {
                system.sys_event = SYSTEM_EVENT_NONE;
                system.sys_mode = SYSTEM_MODE_SLEEP;
                Temp_Deinit();                          /* 关 NTC 采样电路（省电） */
                Timer0_Stop();                        /* 停 1ms 节拍，改为看门狗中断唤醒 */
                Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                break;
            } 
            break;
        default:
            break;
    }
}