#ifndef _DISPLAY_H_
#define _DISPLAY_H_

/**
 * @file    display.h
 * @brief   数码管 + 电池图标 + RGB 温区灯圈驱动接口（12 相动态扫描）
 * @note
 *   - 显示内容：3 位温度数码管 + 4 位倒计数码管（MM:SS）+ 冒号 / ℃ / 小数点等附加符号
 *               + 电池图标（边框 / 电量格 / 状态灯）+ RGB 温区灯圈
 *   - 12 相动态扫描（display.segment = 0~11），每相 1ms，12ms 完成一轮刷新；
 *     扫描在 Timer0 1ms 中断中执行（Display_Scan_ISR）
 *   - 驱动方式为共阴：每相把 1 根引脚拉低作该相公共端，该相点亮的段线输出高电平；
 *     灯圈 LEDR/LEDG/LEDB 为独立引脚、低电平点亮，不参与 SEG 复用
 *   - 每相开始先把 SEG1~SEG10 与 LEDR/LEDG/LEDB 全部置为输入（高阻）释放上一相，避免残影
 *   - 温度值以 0.1℃ 为单位（278 = 27.8℃），量程 0~99.9℃，占 temp_buf[3]
 *   - 槽位与相序映射、各模式显示差异详见 display.c 文件头
 */

#include "gpio.h"
#include <stdint.h>

/* 扫描引脚（非连续编号，按硬件实际布线定义）
 * 注意：每根引脚在不同扫描相中承担不同角色（段线 / 公共端），
 *       完整相序映射见 display.c 文件头说明。
 *   SEG1~SEG8 ：数码管段线（3 位温度 + 4 位倒计时 + ℃ / 冒号 / 小数点等符号共用），
 *               并按硬件布线在相 0~6 中依次兼任该相的选通公共端
 *   SEG9      ：电池图标边框 / 状态灯的公共端（相 9、相 11）
 *   SEG10     ：电池图标电量格的公共端（相 10）
 *   LEDR/G/B  ：RGB 温区灯圈，独立引脚，低电平点亮（Display_OFF() 输出高电平熄灭）
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
    TEMP_ZONE_LOW       = 0u,           /* 低温区上限（< 36.0℃，灯圈蓝色）*/
    TEMP_ZONE_NORMAL    = 360u,         /* 常温区下限（36.0~39.9℃，灯圈绿色）*/
    TEMP_ZONE_HOT       = 400u,         /* 高温区下限（≥ 40.0℃，灯圈红色）*/
} Temp_Zone_e;                          /* 单位 0.1℃，与 display.temp（Temp_Read() 输出）同量纲 */

/* 电池电量（%，来自 Battery 的 BAT_Voltage()，相 10 按此逐格点亮电量格） */
typedef enum {
    BATTERY_LEVEL_L0        = 0u,       /* 电量 0%（低于 L1，电量格全灭）*/
    BATTERY_LEVEL_L1        = 10u,      /* 电量 10% */
    BATTERY_LEVEL_L2        = 30u,      /* 电量 30% */
    BATTERY_LEVEL_L3        = 50u,      /* 电量 50% */
    BATTERY_LEVEL_L4        = 70u,      /* 电量 70% */
    BATTERY_LEVEL_L5        = 90u,      /* 电量 90% */
} Battery_Level_e;

/* 电池状态（相 11 按此点亮状态灯：绿 / 红，低电与充电时闪烁） */
typedef enum {
    BATTERY_STATUS_NORMAL      = 0u,       /* 电池状态正常（绿灯） */
    BATTERY_STATUS_CHARGING,               /* 电池充电中（红灯闪烁） */
    BATTERY_STATUS_LOW,                    /* 电池电量低（绿灯闪烁） */
    BATTERY_STATUS_CHARGE_FULL,            /* 电池已充满（绿灯） */
    BATTERY_STATUS_INIT,                   /* 初始状态（红绿同时点亮 = 上电自检） */
} Battery_Status_e;

/* 闪烁控制（display.blink_cont 的位标志，每 500ms 翻转一次） */
typedef enum {
    BLINK_STATE_OFF         = 0x00u,       /* 不闪烁 */
    BLINK_STATE_TFLG        = 0x01u,       /* 时间标记(:)闪烁控制（相 8） */
    BLINK_STATE_TIMER_ALARM = 0x02u,       /* 定时报警标志（当前只置位/清除，未参与显示判断） */
    BLINK_STATE_BAT_LOW     = 0x04u,       /* 电量低闪烁控制（相 11 绿灯） */
    BLINK_STATE_CHARGING    = 0x08u,       /* 充电闪烁控制（相 11 红灯） */
} Blink_Cont_e;

typedef enum {
    DISPLAY_MODE_OFF = 0,                   /* 显示关闭（段线与灯圈全部关断，省电） */
    DISPLAY_MODE_INIT,                      /* 上电自检：888 + 88:88，灯圈每 500ms 换色（共 3s） */
    DISPLAY_MODE_TIMER,                     /* 温度 + 倒计时（MM:SS，冒号闪烁）+ 电量；灯圈按温区着色 */
    DISPLAY_MODE_TIMER_ALARM,               /* 倒计时报警：冒号闪烁 + 灯圈三色循环（18 次后自动回 TIMER） */
    DISPLAY_MODE_TIMER_END,                 /* 倒计时结束（预留，当前未使用） */
    DISPLAY_MODE_CHARGING,                  /* 充电模式：相 0~8 跳过，只显示电池图标（相 9~11） */
} Display_Mode_e;

typedef struct {
    uint16_t            blink_cnt;          /* 闪烁计时（1ms 递增，满 500 = 500ms 翻转一次闪烁标志） */
    uint16_t            temp;               /* 当前温度值（0.1℃ 单位，如 278 = 27.8℃） */
    uint16_t            timer;              /* 当前倒计时剩余秒数（0~900） */
    uint8_t             battery_level;      /* 当前电池电量（%） */
    uint8_t             temp_buf[3];        /* 温度段码缓冲：[0] 个位、[1] 十位、[2] 百位 */
    uint8_t             timer_buf[4];       /* 倒计时段码缓冲：[0] 秒个位、[1] 秒十位、[2] 分个位、[3] 分十位 */
    uint8_t             segment;            /* 当前扫描相（0~11，12 相循环） */
    uint8_t             led_cycle;          /* RGB 灯圈循环切换索引（INIT 0~5，TIMER_ALARM 0~17） */
    Blink_Cont_e        blink_cont;         /* 当前闪烁控制 */
    Battery_Status_e    battery_status;     /* 当前电池状态 */
    Display_Mode_e      mode;               /* 显示模式 */
} Display_t;

/* 注意：display 结构由主循环（Display_SetMode/SetDigit）写入、
 *       Timer0 中断（Display_Scan_ISR）读取，两者无临界区保护 */

/*============================================================================
 * 函数声明
 *==========================================================================*/

/** @brief 初始化显示模块（关断所有段线/灯圈引脚并复位缓冲、数值、扫描相，模式置 OFF） */
void Display_Init(void);
/** @brief 切换显示模式并重建画面；模式相同时直接返回，不做任何刷新
 *  @param mode           DISPLAY_MODE_OFF / INIT / TIMER / TIMER_ALARM / CHARGING
 *  @param temp           温度值（0.1℃）
 *  @param timer          倒计时剩余秒数
 *  @param battery_level  电池电量（%）
 *  @param battery_status 电池状态
 *  @note  模式变化时清空段码缓冲、扫描相与闪烁控制；OFF 调 Display_OFF() 彻底关断，
 *         INIT 固定显示 888 + 88:88（此后 Display_SetDigit() 不再生效），
 *         其余模式由 Display_SetDigit() 用入参刷新一次 */
void Display_SetMode(Display_Mode_e mode, uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status);
/** @brief 更新显示缓冲（温度 / 倒计时 / 电量 / 电池状态），仅数值变化时重算段码
 *  @param temp           温度值（0.1℃）
 *  @param timer          倒计时剩余秒数（MM:SS）
 *  @param battery_level  电池电量（%）
 *  @param battery_status 电池状态
 *  @note  INIT 模式直接返回（自检画面由 Display_SetMode() 设置并保持）；
 *         温度与倒计时用重复减法拆位以避免除法，结果写入 temp_buf / timer_buf */
void Display_SetDigit(uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status);
/** @brief 关闭显示（SEG1~SEG10 输出低电平，LEDR/G/B 输出高电平熄灭） */
void Display_OFF(void);
/** @brief 动态扫描刷新（12 相轮询：相 0~8 扫段码与符号，相 9~11 扫电池图标，灯圈每 3 相刷一次），
 *         由 Timer0 1ms 中断调用 */
void Display_Scan_ISR(void);

#endif  /* _DISPLAY_H_ */
