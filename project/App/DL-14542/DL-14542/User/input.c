/**
 * @file    input.c
 * @brief   触摸 + 充电状态输入驱动实现（复用 gpio.h）
 * @note
 *   - 每个信号独立配置（TOUCH_/CHRG_/STDBY_ 端口/引脚宏），端口/引脚可各自移植。
 *   - gpio.h 映射宏为间接展开，端口宏（如 TOUCH_PORT）可直接作为参数。
 */

#include "input.h"

/** @brief 初始化输入引脚：TOUCH / CHRG / STDBY 均设为输入（不使用内部上下拉） */
void Input_Init(void)
{
    // GPIO_SET_DIGITAL(TOUCH_PORT, TOUCH_PIN);
    GPIO_SET_INPUT(TOUCH_PORT, TOUCH_PIN);

    // GPIO_SET_DIGITAL(CHRG_PORT, CHRG_PIN);
    GPIO_SET_INPUT(CHRG_PORT, CHRG_PIN);

    // GPIO_SET_DIGITAL(STDBY_PORT, STDBY_PIN);    
    GPIO_SET_INPUT(STDBY_PORT, STDBY_PIN);
}

/** @brief 扫描触摸：低电平期间累加按下计数，松开时按计数判定短按/释放
 *  @return TOUCH_STATE_LONG（按下满 50 次 ≈ 3s）/ TOUCH_STATE_PRESSED（松开时计数 ≤ 25 ≈ 1.5s）
 *          / TOUCH_STATE_RELEASED（未达一次扫描周期的极短触碰）/ TOUCH_STATE_NONE
 */
Touch_State_t Touch_Scan(void)
{
    Touch_State_t state = TOUCH_STATE_NONE;
    static uint8_t pressed_cnt = TOUCH_CNT_NONE;                    /* 按下消抖计数 */    
    static uint8_t released_cnt = TOUCH_CNT_NONE;                   /* 松开消抖计数 */

    if (GPIO_READ(TOUCH_PORT, TOUCH_PIN) == 0) {
        released_cnt = TOUCH_CNT_NONE;                              /* 按下计数清零 */
        if (pressed_cnt < TOUCH_CNT_MAX) pressed_cnt++; 
        if (pressed_cnt == TOUCH_CNT_LONG_CNT) {
        	state = TOUCH_STATE_LONG;                               /* 按下持续到长按阈值（50 次） */
        }
    } else {
        if (pressed_cnt >= TOUCH_CNT_DEBOUNCE_CNT ) {            
            if (pressed_cnt <= TOUCH_CNT_SHORT_CNT) {
                state = TOUCH_STATE_PRESSED;                        /* 短按（松开时计数 1~25） */
            }
        } else {
            if (released_cnt < TOUCH_CNT_MAX) released_cnt++;
            if (released_cnt == TOUCH_CNT_DEBOUNCE_CNT) 
            	state = TOUCH_STATE_RELEASED;			            /* 极短触碰（按下不足一次扫描周期） */    
        }    
        pressed_cnt = TOUCH_CNT_NONE;                               /* 松开计数清零 */
    }

    return  state;
}

/** @brief 扫描充电状态：CHRG 为高再判 STDBY（均带 5 次 ≈ 500ms 消抖）
 *  @return CHARGE_STATE_FULL（CHRG、STDBY 均为高）/ CHARGE_STATE_CHARGING（仅 CHRG 高）
 *          / CHARGE_STATE_NONE（CHRG 为低，同时清零两个计数）
 */
Charge_State_t Charge_Scan(void)
{
    Charge_State_t state = CHARGE_STATE_NONE;
    static uint8_t chrg_cnt = CHARGE_CNT_NONE;          /* CHRG 消抖计数 */
    static uint8_t stdby_cnt = CHARGE_CNT_NONE;         /* STDBY 消抖计数 */

    GPIO_SET_PULL(STDBY_PORT, STDBY_PIN, GPIO_PULL_UP);     /* STDBY 为开漏输出，读取期间临时上拉 */
    
    if (GPIO_READ(CHRG_PORT, CHRG_PIN) != 0) {       
        if (chrg_cnt < CHARGE_CNT_MAX) chrg_cnt++; 
        if (chrg_cnt >= CHARGE_DEBOUNCE_CNT) 
        	state = CHARGE_STATE_CHARGING;              /* 消抖后判定充电中 */

        if (GPIO_READ(STDBY_PORT, STDBY_PIN) != 0) {
            if (stdby_cnt < CHARGE_CNT_MAX) stdby_cnt++; 
            if (stdby_cnt >= CHARGE_DEBOUNCE_CNT) 
            	state = CHARGE_STATE_FULL;              /* 消抖后判定已充满（覆盖充电中） */
        } else {
            stdby_cnt = CHARGE_CNT_NONE;                /* STDBY 为低：清零充满计数 */
        }
    } else {
        chrg_cnt = CHARGE_CNT_NONE;                     /* CHRG 为低：未充电，两个计数都清零 */
        stdby_cnt = CHARGE_CNT_NONE;
        state = CHARGE_STATE_NONE;                      /* 未充电 */
    }

    GPIO_SET_PULL(STDBY_PORT, STDBY_PIN, GPIO_PULL_NONE);
    
    return state;                                       /* 充电中 / 已充满 / 未充电 */
}
