/**
 * @file    system.c
 * @brief   系统初始化 + 周期任务调度 + 状态机（温度显示 / 倒计时 / 电量主逻辑）
 * @note
 *   - Timer0 每 1ms 产生系统节拍（同一中断内完成数码管一相扫描），调度器与状态机在主循环中按节拍运行
 *   - 采用“任务表驱动 + 两级状态机”：sys_mode 描述整轮流程，sys_state 保存位标志子状态，
 *     sys_event 缓存触摸/充电等事件并由状态机取用
 *   - 流程：INIT（3s 自检：888 + 88:88，灯圈每 500ms 换色）
 *           → SLEEP（关显示、停 Timer0，看门狗中断每 60ms 唤醒一次，轮询触摸与充电状态）
 *           → TIMER（短按唤醒：显示温度（0.1℃）+ 电量 + 灯圈温区着色；再短按熄屏/唤醒；
 *                    长按启动或复位 15 分钟倒计时（sec_cnt=900）；亮屏满 60s 自动熄屏，
 *                    熄屏且未在倒计时则回 SLEEP）
 *           ⇄ CHARGING（充电中：只显示电池图标；拔出充电器回 SLEEP）
 *   - 看门狗配置（见工程检查清单 DL-14542.htm）：Enable、Event = Interrupt、Time Base = 60ms；
 *     正常运行时由主循环喂狗，休眠期间该中断是唯一唤醒源（触摸未使用 INT0 中断），
 *     因此必须在 isr() 中 CLRWDT() + 清 WDTIF；休眠时触摸/充电实际约 60ms 检测一次
 */
#include "gpio.h"
#include "temp.h"
#include "input.h"
#include "system.h"
#include "battery.h"
#include "display.h"

/* 系统初始化时间 */
#define SYSTEM_INIT_TIME            3000            /* 上电自检时长，单位：毫秒（3s） */
#define SYSTEM_DISPLAY_TIME         60000           /* 亮屏保持时间，单位：毫秒（无操作 60s 后自动熄屏） */
/* 倒计时剩余 5 分钟 */
#define SYSTEM_COUNTDOWN_5_MIN      300             /* 倒计时剩余 5 分钟（秒）：触发一次报警动画 */


 /*--- 任务定义 -----------------------------------------------------------*/
typedef enum {
    TASK_TOUCH_SCAN,                            /* 触摸扫描（按下/长按检测），60ms */
    TASK_DISPLAY_UPDATE,                        /* 显示刷新（100ms，把温度/倒计时/电量/电池状态送往显示模块） */
    TASK_CHARGE_SCAN,                           /* 充电状态扫描（充电中/充满检测），100ms */    
    TASK_NTC_SAMPLE,                            /* NTC 采样（100ms，推进 IIR 滤波；唤醒首采时取有效值写入 system.temp） */
    TASK_SEC_TIMER,                             /* 每秒计时 1000ms（倒计时递减） */
    TASK_TEMP_UPDATE,                           /* 温度更新（2000ms，重读温度并更新 system.temp） */    
    TASK_BATTERY_SCAN,                          /* 电量扫描（2000ms，刷新 system.battery_level / battery_status） */
    TASK_DELAY,                                 /* 占位，不参与周期调度；其时间戳被状态机用作“最近一次刷新显示时刻” */
    TASK_MAX
} TaskID_e;

/*--- 调度时序 -----------------------------------------------------------*/
/* 说明：调度循环只遍历 i < TASK_DELAY，故 TASK_DELAY 自身不按周期执行；
 *       它只是占住最后一个任务槽，但 task_id[TASK_DELAY] 被 System_StateMachine()
 *       当作“最近一次刷新显示时刻”，用于 60s 亮屏超时判断（SYSTEM_DISPLAY_TIME）。
 *       表中该行的周期值不会被使用，仅为占位。 */
const uint16_t TASK_PERIOD[TASK_MAX] = {
    60,                                         /* TASK_TOUCH_SCAN 触摸扫描 */
    100,                                        /* TASK_DISPLAY_UPDATE 显示更新 */
    100,                                        /* TASK_CHARGE_SCAN 充电状态扫描 */    
    100,                                        /* TASK_NTC_SAMPLE NTC 温度采样 */
    1000,                                       /* TASK_SEC_TIMER 每秒计时 1000ms */
    2000,                                       /* TASK_TEMP_UPDATE 温度更新 */       
    2000,                                       /* TASK_BATTERY_SCAN 电量扫描 */
    60000,                                      /* TASK_DELAY 占位值（不参与周期比较） */
};

/* 系统状态位标志（中断/任务置位，状态机或调度器清除） */
typedef enum {
    SYSTEM_STATE_NONE               = 0x00u,
    SYSTEM_STATE_TICK_UPDATE        = 0x01u,    /* 有新的 1ms 节拍待处理（中断置位，调度器清除） */
    SYSTEM_STATE_TIMER_STARTUP      = 0x02u,    /* 倒计时运行中（长按启动/复位，减到 0 或短按退出） */
    SYSTEM_STATE_CHARGING           = 0x04u,    /* 充电中（记录充电边沿，避免重复触发事件） */
    SYSTEM_STATE_CHARGE_FULL        = 0x08u,    /* 已充满（记录边沿，避免重复触发事件） */
    SYSTEM_STATE_TEMP_SAMPLE        = 0x10u,    /* 唤醒后首次采样标志（下一次 NTC 采样取有效值并清除） */
    SYSTEM_STATE_DISPLAY_OFF        = 0x20u,    /* 熄屏状态（Timer0 仍在运行，短按可重新点亮） */
} System_State_e;

typedef enum {
    SYSTEM_EVENT_NONE               = 0x00u,
    SYSTEM_EVENT_TOUCH_SHORT_PRESS  = 0x01u,    /* 触摸短按事件 */
    SYSTEM_EVENT_TOUCH_LONG_PRESS   = 0x02u,    /* 触摸长按事件 */
    SYSTEM_EVENT_CHARGING           = 0x04u,    /* 插入充电器事件 */
    SYSTEM_EVENT_CHARGE_FULL        = 0x08u,    /* 充电完成事件 */
    SYSTEM_EVENT_CHARGE_NONE        = 0x10u,    /* 拔出充电器事件 */
    SYSTEM_EVENT_TIMER_END          = 0x20u,    /* 倒计时结束事件 */
} System_Event_e;

typedef enum {
    SYSTEM_MODE_INIT,                           /* 上电自检（3s） */
    SYSTEM_MODE_TIMER,                          /* 运行：温度 + 15 分钟倒计时 + 电量 + 温区灯圈 */
    SYSTEM_MODE_CHARGING,                       /* 充电模式（只显示电池图标） */
    SYSTEM_MODE_SLEEP,                          /* 休眠（停 Timer0，靠 WDT 中断唤醒轮询） */
    SYSTEM_MODE_MAX
} System_Mode_e;

typedef struct {
    volatile uint16_t           sys_tick;               /* 系统节拍计数（1ms 递增，睡眠时停住） */
             uint16_t           task_id[TASK_MAX];      /* 各任务上次执行的时间戳 */
             uint16_t           temp;                   /* 当前温度（0.1℃ 单位，来自 Temp_Read()） */
             uint16_t           sec_cnt;                /* 倒计时剩余秒数（0~900） */
             uint8_t            battery_level;          /* 当前电池电量（%） */
    		 Battery_Status_e   battery_status;         /* 当前电池状态 */
    volatile System_State_e     sys_state;              /* 当前系统状态（位标志） */
    volatile System_Event_e     sys_event;              /* 待处理的系统事件 */
    volatile System_Mode_e      sys_mode;               /* 整轮流程模式 */
} System_t;

System_t system = {0};                                  /* 系统运行参数（全局唯一实例） */

/** @brief 中断服务：Timer0 1ms 系统节拍 + 数码管动态扫描 + 看门狗溢出处理
 *  @note  TMR0 初值 0x83（125 次计数），IRC 8MHz、指令周期 2T = 0.25µs，预分频 1:32
 *         → 125 × 32 × 0.25µs = 1ms，即系统节拍同时也是扫描相周期
 */
void isr(void) __interrupt(0)
{
    if(INTCONbits.T0IF)                                                             // Timer0 溢出
    {
        // Add user code
        TMR0     = 0x83;                                                          // Load 0x83 to TMR0 (Initial Timer0 register)
                                                                                  // Real Timing Duration: 1.0000ms
        INTCONbits.T0IF = 0;                                                        // Clear T0IF flag bit

        system.sys_tick++;                              /* 1ms 节拍累加（主循环据此调度任务） */
        system.sys_state |= SYSTEM_STATE_TICK_UPDATE;
        
        Display_Scan_ISR();                             /* 数码管动态扫描（每 1ms 一相） */
    }

    if (PIR1bits.WDTIF) {                               /* 看门狗溢出（配置：Event = Interrupt，时基 60ms）；
                                                         * 休眠期间这是唯一的唤醒源（触摸未用 INT0） */
        CLRWDT();                                       /* 清 WDT 计数器 */
        PIR1bits.WDTIF = 0;                             /* 清中断标志（不清会反复重入本中断） */
    }
}

/** @brief 初始化 Timer0：1ms 溢出中断（TMR0=0x83、预分频 1:32、指令时钟源）
 *  @note  睡眠唤醒后需重新调用本函数恢复节拍
 */
void Timer0_Init(void)
{
    INTCONbits.GIE = 0;                                                           /* 关闭总中断 */
    INTCONbits.PEIE = 1;                                                          /* 使能外设中断 */

    T0MD     = C_TMR0_Dis | C_PS0_TMR0 | C_PS0_Div32;                              /* 先关闭 Timer0 */
                                                                                  /* PS0 预分频分配给 Timer0 */
                                                                                  /* 预分频比 = 1:32 */
                                                                                  /* Timer0 时钟源 = 指令时钟（T0CS = 0） */

    TMR0     = 0x83;                                                              /* 装载 TMR0 初值 0x83（余 125 次计数） */
                                                                                  /* 溢出周期 1.0000ms */

    INTCONbits.T0IE = 1;                                                          /* 使能 Timer0 溢出中断 */
    T0MDbits.T0EN = 1;                                                            /* 使能 Timer0 */
    INTCONbits.GIE = 1;                                                           /* 使能总中断 */
}


/** @brief 关闭 Timer0 节拍（进入休眠前调用）
 *  @note  仅关闭 T0EN，不清 T0IF：唤醒后 Timer0_Init() 会重装 TMR0 并重新使能；
 *         isr() 依据 T0IF 判断，故停摆期间不会产生节拍
 */
void Timer0_Stop(void)
{
    T0MDbits.T0EN = 0;                               /* 停止 Timer0（睡眠期间停节拍，由 WDT 中断唤醒后重新打开） */
}


//--------------- WDT INIT ----------------------------------------------
/* 看门狗：睡眠期间由 WDT 中断周期唤醒 CPU（每 60ms 一次，检查清单 WDT Time Base = 60ms），
 * 主循环被唤醒后继续执行调度器与状态机（休眠中只轮询触摸/充电），无操作则再次进入 sleep */
void WDT_Init(void)
{
    PCONbits.WDTEN  = 1;                             /* 使能 WDT */
    PIE1bits.WDTIE  = 1;                             /* 使能 WDT 中断（sleep 唤醒源） */
    INTCONbits.PEIE = 1;                             /* 使能外设中断 */
}
/* 关闭LVR */
void LVR_Disable(void)
{
    PCONbits.LVREN = 0;                             /* 关闭低压检测器 */
}
/* 开启 LVR */
void LVR_Enable(void)
{
    PCONbits.LVREN = 1;                             /* 开启低压检测器 */
}

/*============================================================================
 * 睡眠
 *   进入前需先关显示、关 LVR/NTC 并停止 Timer0（见各状态机分支）；
 *   唤醒源只有 WDT 中断（60ms 周期），醒来后由主循环轮询触摸与充电状态
 *==========================================================================*/
void Sleep_Enter(void)
{
    asm("nop");
    asm("nop");
    asm("sleep");
    asm("nop");
    asm("nop");
}

/** @brief 系统初始化：看门狗 / GPIO / LVR / 温度采集 / 输入 / 显示初始化 + 运行参数复位 + 启动 1ms 时基 */
void System_Init(void)
{
    WDT_Init();                                     /* 看门狗使能（休眠唤醒源，紧随其后的初始化也不能超时） */
    GPIO_Init();                                    /* 初始化 GPIO（全部端口设为输出低） */
    LVR_Enable();                                   /* 开启低压检测器 */
    Temp_Init();                                    /* NTC 采样引脚使能 + 滤波参数复位 */
    Input_Init();                                   /* 触摸 + 充电状态输入初始化 */
    Display_Init();                                 /* 关闭显示并复位显示缓冲 */

    system.sys_tick = 0;
    system.temp = 0;
    system.sec_cnt = 0;
    system.battery_level = BATTERY_LEVEL_L5;
    system.battery_status = BATTERY_STATUS_INIT;
    system.sys_state = SYSTEM_STATE_NONE;
    system.sys_event = SYSTEM_EVENT_NONE;
    system.sys_mode = SYSTEM_MODE_INIT;

    for (uint8_t i = 0; i < TASK_MAX; i++) {
        system.task_id[i] = 0;                      /* 所有任务时间戳清零 */
    }

    Display_SetMode(DISPLAY_MODE_INIT, system.temp, system.sec_cnt, system.battery_level, system.battery_status);        /* 进入自检模式：显示 888 + 88:88，灯圈由扫描中断每 500ms 换色 */
    Timer0_Init();                                  /* 启动 1ms 节拍，数码管扫描随之开始 */
}

/** @brief 周期任务调度：每个 1ms 节拍检查一次各任务是否到期
 *  @note  休眠模式下只喂狗不跑任务；时间比较用无符号回绕减法，漏掉几个节拍也不会丢任务
 */
void System_Scheduler(void)
{
    // Add user code for task scheduling
    CLRWDT();                                           /* 喂狗（休眠模式下是本函数唯一动作） */
    if (system.sys_state & SYSTEM_STATE_TICK_UPDATE) {
        system.sys_state &= ~SYSTEM_STATE_TICK_UPDATE;
        if (system.sys_mode == SYSTEM_MODE_SLEEP) return;

        for (uint8_t i = 0; i < TASK_DELAY; i++) {
            if ((uint16_t)(system.sys_tick - system.task_id[i]) < TASK_PERIOD[i]) continue;  /* 未到周期，跳过 */
            system.task_id[i] = system.sys_tick;    /* 记录本次执行时间戳 */
            switch (i) {
                case TASK_TOUCH_SCAN:
                    if (system.sys_mode == SYSTEM_MODE_INIT) break;
                    switch (Touch_Scan()) {
                        case TOUCH_STATE_PRESSED: 
                            system.sys_event |= SYSTEM_EVENT_TOUCH_SHORT_PRESS;
                            break;
                        case TOUCH_STATE_LONG:
                            system.sys_event |= SYSTEM_EVENT_TOUCH_LONG_PRESS;
                            break;                
                        default:
                            break;
                    }
                    break;
                case TASK_DISPLAY_UPDATE:
                    if (system.sys_mode == SYSTEM_MODE_INIT) break;
                    Display_SetDigit(system.temp, system.sec_cnt, system.battery_level, system.battery_status);     /* 把温度/倒计时/电量/电池状态送往显示模块 */
                    break;
                case TASK_CHARGE_SCAN:
                    if (system.sys_mode == SYSTEM_MODE_INIT) break;
                    switch (Charge_Scan()) {                                                /* 只在状态变化时产生事件，避免重复触发 */
                        case CHARGE_STATE_CHARGING:
                            if (system.sys_state & SYSTEM_STATE_CHARGING) break;
                            system.sys_state |= SYSTEM_STATE_CHARGING;
                            system.sys_state &= ~SYSTEM_STATE_CHARGE_FULL;
                            system.sys_event |= SYSTEM_EVENT_CHARGING;
                            system.battery_status = BATTERY_STATUS_CHARGING;
                            break;
                        case CHARGE_STATE_FULL:
                            if (system.sys_state & SYSTEM_STATE_CHARGE_FULL) break;
                            system.sys_state |= SYSTEM_STATE_CHARGE_FULL;
                            system.sys_event |= SYSTEM_EVENT_CHARGE_FULL;
                            system.battery_status = BATTERY_STATUS_CHARGE_FULL;
                            break; 
                        case CHARGE_STATE_NONE:
                            if (system.sys_state & SYSTEM_STATE_CHARGING) {
                                system.sys_state &= ~SYSTEM_STATE_CHARGING;
                                system.sys_state &= ~SYSTEM_STATE_CHARGE_FULL;
                                system.sys_event |= SYSTEM_EVENT_CHARGE_NONE;
                                system.battery_status = BATTERY_STATUS_NORMAL;
                            }
                            break;
                        default:
                            break;
                    }
                    break;
               case TASK_NTC_SAMPLE:
                    if (system.sys_state & SYSTEM_STATE_TEMP_SAMPLE) {                   
                        system.sys_state &= ~SYSTEM_STATE_TEMP_SAMPLE;
                        system.temp = Temp_Read();
                    } else {
                        Temp_Read();                                                /* 高频采样，仅推进 IIR 滤波，结果不直接使用 */
                    }
                    break;
                case TASK_TEMP_UPDATE:
                    system.temp = Temp_Read();                                      /* 每 2s 更新一次有效温度 */
                    break;
                case TASK_BATTERY_SCAN:                
                    system.battery_level = BAT_Voltage();       /* 读取电量（%，内部含 1/8 递推平均） */
                    if (system.sys_state & SYSTEM_STATE_CHARGING) break;    /* 充电中状态由充电扫描决定 */
                    if (system.battery_level >= BATTERY_LEVEL_L2) {
                        system.battery_status = BATTERY_STATUS_NORMAL;
                    } else {
                        system.battery_status = BATTERY_STATUS_LOW;
                    }
                    break;
                case TASK_SEC_TIMER:
                    if (system.sys_mode != SYSTEM_MODE_TIMER) break;
                    if (system.sys_state & SYSTEM_STATE_TIMER_STARTUP) {
                        if (system.sec_cnt != 0) {
                            system.sec_cnt--;
                        } else {
                            system.sys_state &= ~SYSTEM_STATE_TIMER_STARTUP;
                            system.sys_event |= SYSTEM_EVENT_TIMER_END;
                        }
                    }                   
                    break;
                default:
                    break;
            }
            break;
        }
    }
}

/** @brief 系统状态机：处理整轮流程（sys_mode）与子状态（sys_state） */
void System_StateMachine(void)
{
    CLRWDT();                                               /* 主循环同样承担喂狗 */
    switch (system.sys_mode) {
        /* 上电自检：显示 888 + 88:88，灯圈红/绿/蓝每 500ms 换色；满 3s 后关显示并进入休眠 */
        case SYSTEM_MODE_INIT:
            if (system.sys_tick >= SYSTEM_INIT_TIME) {
                system.sys_event = SYSTEM_EVENT_NONE;
                system.sys_mode = SYSTEM_MODE_SLEEP;
                system.battery_status = BATTERY_STATUS_NORMAL;
                Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                LVR_Disable();                                  /* 关 LVR（省电） */
                Temp_Disable();                                 /* 关 NTC 采样电路（省电） */
                Timer0_Stop();                                  /* 停 1ms 节拍，改为看门狗中断唤醒 */
                break;
            } 
            break;
        case SYSTEM_MODE_SLEEP:
            if (Charge_Scan() == CHARGE_STATE_CHARGING) {                   /* 插入充电器：切充电显示 */
                if (system.sys_state & SYSTEM_STATE_CHARGING) break;
                system.sys_state |= SYSTEM_STATE_CHARGING;
                system.sys_state &= ~SYSTEM_STATE_CHARGE_FULL;
                system.sys_event = SYSTEM_EVENT_NONE;
                system.sys_mode = SYSTEM_MODE_CHARGING;
                system.battery_status = BATTERY_STATUS_CHARGING;
                Display_SetMode(DISPLAY_MODE_CHARGING, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                LVR_Enable();                                       /* 开启低压检测器 */
                Timer0_Init();                                      /* 启动 1ms 节拍，数码管扫描随之开始 */
                break;
            }
        
            switch (Touch_Scan()) {
                case TOUCH_STATE_PRESSED:                               /* 短按唤醒：显示温度与电量，不启动倒计时 */
                    system.sys_state |= SYSTEM_STATE_TEMP_SAMPLE;
                    system.sys_event = SYSTEM_EVENT_NONE;
                    system.sys_mode = SYSTEM_MODE_TIMER;
                    system.sec_cnt = 0;
                    system.sys_state &= ~SYSTEM_STATE_DISPLAY_OFF;
                    system.task_id[TASK_DELAY] = system.sys_tick;       /* 记录本次刷新时刻，作为 60s 熄屏计时起点 */
                    Display_SetMode(DISPLAY_MODE_TIMER, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    LVR_Enable();                                   /* 开启低压检测器 */
                    Temp_Enable();
                    Timer0_Init();                                  /* 启动 1ms 节拍，数码管扫描随之开始 */
                    break;                    
                case TOUCH_STATE_LONG:                                  /* 长按唤醒：启动 15 分钟倒计时并进入报警动画 */
                    system.sys_state |= SYSTEM_STATE_TEMP_SAMPLE;
                    system.sys_state |= SYSTEM_STATE_TIMER_STARTUP;
                    system.sys_event = SYSTEM_EVENT_NONE;
                    system.sys_mode = SYSTEM_MODE_TIMER;
                    system.task_id[TASK_DELAY] = system.sys_tick; 
                    system.task_id[TASK_SEC_TIMER] = system.sys_tick;
                    system.sec_cnt = 900;                               /* 15 分钟 */
                    Display_SetMode(DISPLAY_MODE_TIMER_ALARM, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    LVR_Enable();                                   /* 开启低压检测器 */
                    Temp_Enable();
                    Timer0_Init();                                  /* 启动 1ms 节拍，数码管扫描随之开始 */
                    break;
                default:                                                /* 无按键：继续睡眠，等待下一次 WDT 唤醒 */
                    Sleep_Enter();
                    break;
            }
            break;
        case SYSTEM_MODE_TIMER:
            if (system.sys_event & SYSTEM_EVENT_TOUCH_LONG_PRESS) {      /* 长按：启动 / 停止 15 分钟倒计时 */
                system.sys_event &= ~SYSTEM_EVENT_TOUCH_LONG_PRESS;
                system.task_id[TASK_DELAY] = system.sys_tick;

                if (system.sys_state & SYSTEM_STATE_TIMER_STARTUP) {
                    system.sys_state &= ~SYSTEM_STATE_TIMER_STARTUP;
                    system.sec_cnt = 0;
                    Display_SetMode(DISPLAY_MODE_TIMER, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                } else {
                    system.sys_state |= SYSTEM_STATE_TIMER_STARTUP;
                    system.task_id[TASK_SEC_TIMER] = system.sys_tick;
                    system.sec_cnt = 900;
                    Display_SetMode(DISPLAY_MODE_TIMER_ALARM, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                }
            }

            if (system.sys_event & SYSTEM_EVENT_TOUCH_SHORT_PRESS) {     /* 短按：倒计时中熄灭/点亮，否则回休眠 */
                system.sys_event &= ~SYSTEM_EVENT_TOUCH_SHORT_PRESS;
                if (system.sys_state & SYSTEM_STATE_TIMER_STARTUP) {
                    if (system.sys_state & SYSTEM_STATE_DISPLAY_OFF) {
                        system.sys_state &= ~SYSTEM_STATE_DISPLAY_OFF;
                        system.task_id[TASK_DELAY] = system.sys_tick;                
                        Display_SetMode(DISPLAY_MODE_TIMER, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    } else {
                        system.sys_state |= SYSTEM_STATE_DISPLAY_OFF;
                        Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    }
                } else {
                    system.sec_cnt = 0;
                    system.sys_event = SYSTEM_EVENT_NONE;
                    system.sys_mode = SYSTEM_MODE_SLEEP;
                    system.sys_state |= SYSTEM_STATE_DISPLAY_OFF;
                    Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    LVR_Disable();                                  /* 关 LVR（省电） */
                    Temp_Disable();                                 /* 关 NTC 采样电路（省电） */
                    Timer0_Stop();                                  /* 停 1ms 节拍，改为看门狗中断唤醒 */
                    break;
                }                
            }            

            if (system.sys_event & SYSTEM_EVENT_TIMER_END) {            /* 倒计时减到 0：进入报警动画 */
                system.sys_event &= ~SYSTEM_EVENT_TIMER_END;
                system.sys_state &= ~SYSTEM_STATE_DISPLAY_OFF;
                system.task_id[TASK_DELAY] = system.sys_tick;                
                Display_SetMode(DISPLAY_MODE_TIMER_ALARM, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
            }

            if (system.sec_cnt == SYSTEM_COUNTDOWN_5_MIN) {             /* 剩余 5 分钟：同样提示一次报警动画 */
                system.sys_state &= ~SYSTEM_STATE_DISPLAY_OFF;
                system.task_id[TASK_DELAY] = system.sys_tick;                
                Display_SetMode(DISPLAY_MODE_TIMER_ALARM, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
            }

            if (system.sys_event & SYSTEM_EVENT_CHARGING) {             /* 插入充电器：切充电显示 */
                system.sys_event &= ~SYSTEM_EVENT_CHARGING;
                system.sys_mode = SYSTEM_MODE_CHARGING;
                Display_SetMode(DISPLAY_MODE_CHARGING, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                break;
            }

            if ((system.sys_state & SYSTEM_STATE_DISPLAY_OFF) == 0) {   /* 亮屏超时判断（时间基准为最近一次刷新显示的时刻） */
                if ((uint16_t)(system.sys_tick - system.task_id[TASK_DELAY]) >= SYSTEM_DISPLAY_TIME) {
                    system.sys_state |= SYSTEM_STATE_DISPLAY_OFF;
                    Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                    if ((system.sys_state & SYSTEM_STATE_TIMER_STARTUP) == 0) {
                        system.sec_cnt = 0;
                        system.sys_event = SYSTEM_EVENT_NONE;
                        system.sys_mode = SYSTEM_MODE_SLEEP;                        
                        LVR_Disable();                                  /* 关 LVR（省电） */
                        Temp_Disable();                                 /* 关 NTC 采样电路（省电） */
                        Timer0_Stop();                                  /* 停 1ms 节拍，改为看门狗中断唤醒 */
                    }
                }
            }
            break;
        case SYSTEM_MODE_CHARGING:
            if (system.sys_event & SYSTEM_EVENT_CHARGE_NONE) {          /* 拔出充电器：关显示回休眠 */
                system.sys_event &= ~SYSTEM_EVENT_CHARGE_NONE;
                system.sys_mode = SYSTEM_MODE_SLEEP;
                Display_SetMode(DISPLAY_MODE_OFF, system.temp, system.sec_cnt, system.battery_level, system.battery_status);
                LVR_Disable();                                          /* 关 LVR（省电） */
                Temp_Disable();                                         /* 关 NTC 采样电路（省电） */              
                Timer0_Stop();                                          /* 停 1ms 节拍，改为看门狗中断唤醒 */
                break;
            }
            break;
        default:
            break;
    }
}