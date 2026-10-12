#ifndef _DISPLAY_H_
#define _DISPLAY_H_

/**
 * @file    display.h
 * @brief   2 位数码管 + 温区指示灯 R/G/B 驱动接口（9 根 I/O，5 相动态扫描）
 * @note
 *   - 9 根 I/O（SEG1~SEG9）分 5 相动态扫描，每相 1ms，5ms 刷新一轮
 *   - 相 0~3 扫描两位数字（个位/十位）的 A~G 段，相 3 附带点亮温度符号 ℃
 *   - 相 4 点亮温区指示灯 R/G/B，颜色由 display.value 决定（见 Temp_Zone_e）
 *   - 无闪烁功能，所有模式均为常亮；扫描在 Timer0 1ms 中断中执行
 *   - 驱动方式为共阴（段线输出高电平、公共端输出低电平）
 *   - 显示位数：仅个位/十位（Display_t.buf），显示值有效范围 0~99
 */

#include "gpio.h"
#include <stdint.h>

/* 扫描引脚（非连续编号，按硬件实际布线定义）
 * 注意：每根引脚在不同扫描相中承担不同角色（公共端 / 段线），
 *       具体相序映射见 display.c 文件头说明。
 *   SEG1~SEG4 ：个位/十位的 A~F 段线（同一根线在不同相位角色互换），
 *               并在相 0~3 中依次兼任该相的选通公共端（相 0→SEG1、相 1→SEG2、
 *               相 2→SEG3、相 3→SEG4）
 *   SEG5      ：十位 G 段线（相 0）；相 4 兼任 R/G/B 指示灯的公共端
 *   SEG6      ：个位/十位 G 段的公共端（相 0）；相 4 兼任红色指示灯段线
 *   SEG7      ：个位 G 段线（相 0）；相 4 兼任蓝色指示灯段线
 *   SEG8      ：相 4 兼任绿色指示灯段线（其余相位不使用）
 *   SEG9      ：温度符号 ℃ 段线（相 3，由 SEG4 选通）
 */
#define SEG1_PORT   PORTB
#define SEG1_PIN    GPIO_PIN_1
#define SEG2_PORT   PORTA
#define SEG2_PIN    GPIO_PIN_7
#define SEG3_PORT   PORTA
#define SEG3_PIN    GPIO_PIN_3
#define SEG4_PORT   PORTB
#define SEG4_PIN    GPIO_PIN_4
#define SEG5_PORT   PORTB
#define SEG5_PIN    GPIO_PIN_5
#define SEG6_PORT   PORTB
#define SEG6_PIN    GPIO_PIN_6
#define SEG7_PORT   PORTB
#define SEG7_PIN    GPIO_PIN_7
#define SEG8_PORT   PORTC
#define SEG8_PIN    GPIO_PIN_1
#define SEG9_PORT   PORTB
#define SEG9_PIN    GPIO_PIN_0
#define SEG10_PORT  PORTC
#define SEG10_PIN   GPIO_PIN_0

#define LEDR_PORT   PORTA
#define LEDR_PIN    GPIO_PIN_4
#define LEDG_PORT   PORTB
#define LEDG_PIN    GPIO_PIN_3
#define LEDB_PORT   PORTB
#define LEDB_PIN    GPIO_PIN_2

typedef enum {
    TEMP_ZONE_LOW       = 0u,           /* 低温区上限（≤35.9℃，蓝灯）*/
    TEMP_ZONE_NORMAL    = 360u,         /* 常温区下限（36.0~39.9℃，绿灯）*/
    TEMP_ZONE_HOT       = 400u,         /* 高温区下限（≥40.0℃，红灯）*/
} Temp_Zone_e;

/* 电池电量 */
typedef enum {
    BATTERY_LEVEL_L0        = 0u,       /* 低电量 */
    BATTERY_LEVEL_L1        = 10u,      /* 电量 10% */
    BATTERY_LEVEL_L2        = 30u,      /* 电量 30% */
    BATTERY_LEVEL_L3        = 50u,      /* 电量 50% */
    BATTERY_LEVEL_L4        = 70u,      /* 电量 70% */
    BATTERY_LEVEL_L5        = 90u,      /* 电量 90% */
} Battery_Level_e;

/* 电池状态 */
typedef enum {
    BATTERY_STATUS_NORMAL      = 0u,       /* 电池状态正常 */
    BATTERY_STATUS_CHARGING    = 1u,       /* 电池充电中 */
    BATTERY_STATUS_LOW         = 2u,       /* 电池电量低 */
    BATTERY_STATUS_CHARGED     = 3u,       /* 电池已充满 */
} Battery_Status_e;

/* 闪烁控制 */
typedef enum {
    BLINK_STATE_OFF         = 0x00u,       /* 不闪烁 */
    BLINK_STATE_TFLG        = 0x01u,       /* 时间标记(:)闪烁控制 */
    BLINK_STATE_TIMER_ALARM = 0x02u,       /* 定时报警闪烁控制 */
    BLINK_STATE_BAT_LOW     = 0x04u,       /* 电量低闪烁控制 */
    BLINK_STATE_CHARGING    = 0x08u,       /* 充电闪烁控制 */
} Blink_Cont_e;

typedef enum {
    DISPLAY_MODE_OFF = 0,                   /* 显示关闭（9 根引脚全部拉低，省电） */
    DISPLAY_MODE_INIT,                      /* 上电自检：显示 888，相 4 指示灯按 value 换色 */
    DISPLAY_MODE_TIMER,                     /* 显示倒计时（分钟:秒） */
    DISPLAY_MODE_TIMER_ALARM,               /* 倒计时报警 */
    DISPLAY_MODE_TIMER_END,                 /* 倒计时结束 */
} Display_Mode_e;

typedef struct {
    uint16_t            blink_cnt;          /* 闪烁计数 */
    uint16_t            temp;               /* 当前显示数值：TEMP = 水温 ℃；INIT = 灯色选择值 */
    uint16_t            timer;              /* 当前显示数值：TEMP = 水温 ℃；INIT = 灯色选择值 */
    uint8_t             battery_level;      /* 当前电池电量 */
    uint8_t             temp_buf[3];        /* 显示段码缓冲：buf[0] 个位、buf[1] 十位 */
    uint8_t             timer_buf[4];       /* 显示段码缓冲：buf[0] 个位、buf[1] 十位 */
    uint8_t             segment;            /* 当前扫描相（0~4，5 相循环） */
    uint8_t             led_cycle;          /* RGB 灯圈循环切换索引 */
    Blink_Cont_e        blink_cont;         /* 当前闪烁控制 */
    Battery_Status_e    battery_status;     /* 当前电池状态 */
    Display_Mode_e      mode;               /* 显示模式 */
} Display_t;

/* 注意：display 结构由主循环（Display_SetMode/SetDigit）写入、
 *       Timer0 中断（Display_Scan_ISR）读取，两者无临界区保护 */

/*============================================================================
 * 函数声明
 *==========================================================================*/

/** @brief 初始化显示模块（全部引脚拉低并复位缓冲/数值/扫描相，模式置 OFF） */
void Display_Init(void);
/** @brief 切换显示模式并按 value 刷新缓冲；模式相同时直接返回，不做任何刷新
 *  @param mode  DISPLAY_MODE_OFF / DISPLAY_MODE_INIT / DISPLAY_MODE_TEMP
 *  @param value INIT：灯色选择值；TEMP：初始水温（℃）
 *  @note  INIT 模式下模式不会再次变化，故自检期间换灯色请调用 Display_SetDigit() */
void Display_SetMode(Display_Mode_e mode, uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status);
/** @brief 更新显示数值
 *  @param value 待显示数值（TEMP：水温 ℃；INIT：灯色选择值）
 *  @note  TEMP 模式：数值变化时重算十/个位段码并复位扫描相；
 *         INIT 模式：只更新 value（相 4 据此换灯色），显示内容保持 88；
 *         OFF 模式：不动作 */
void Display_SetDigit(uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status);
/** @brief 关闭显示（9 根段/公共引脚全部输出低电平） */
void Display_OFF(void);
/** @brief 动态扫描刷新（5 相轮询：相 0~3 扫段码，相 4 扫指示灯），由 Timer0 1ms 中断调用 */
void Display_Scan_ISR(void);

#endif  /* _DISPLAY_H_ */
