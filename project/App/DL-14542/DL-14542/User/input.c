/**
 * @file    input.c
 * @brief   触摸 + 充电状态输入驱动实现（复用 gpio.h，均低电平有效）
 * @note
 *   - 每个信号独立配置（INPUT_xxx_PORT / INPUT_xxx_PIN 宏），端口/引脚可各自移植。
 *   - gpio.h 映射宏为间接展开，端口宏（如 INPUT_TOUCH1_PORT）可直接作为参数。
 */

#include "input.h"

void Input_Init(void)
{
    GPIO_SET_INPUT(TOUCH_PORT, TOUCH_PIN);
    GPIO_SET_INPUT(CHRG_PORT, CHRG_PIN);
    GPIO_SET_INPUT(STDBY_PORT, STDBY_PIN);
}

Touch_State_t Touch_Scan(void)
{
    Touch_State_t state = TOUCH_STATE_NONE;
    static uint8_t pressed_cnt = TOUCH_CNT_NONE;            /* 按下消抖计数 */    
    static uint8_t released_cnt = TOUCH_CNT_NONE;           /* 松开消抖计数 */
    
    if (GPIO_READ(TOUCH_PORT, TOUCH_PIN) == 0) {
        if (pressed_cnt < TOUCH_CNT_MAX) pressed_cnt++; 
        if (pressed_cnt == TOUCH_CNT_LONG_CNT) state = TOUCH_STATE_LONG;                        /* 长按计数饱和 */

        released_cnt = TOUCH_CNT_NONE;                                                          /* 按下计数清零 */
    } else {
        if (released_cnt < TOUCH_CNT_MAX) released_cnt++;
        if (released_cnt == TOUCH_CNT_DEBOUNCE_CNT) state = TOUCH_STATE_RELEASED;               /* 释放判定 */    
        
        if (pressed_cnt <= TOUCH_CNT_SHORT_CNT && pressed_cnt >= TOUCH_CNT_DEBOUNCE_CNT) {            
            state = TOUCH_STATE_PRESSED;                                                        /* 短按 */
        }     
        pressed_cnt = TOUCH_CNT_NONE;                                                           /* 松开计数清零 */
    }

    return  state;
}

Charge_State_t Charge_Scan(void)
{
    Charge_State_t state = CHARGE_STATE_NONE;
    static uint8_t chrg_cnt = CHARGE_CNT_NONE;          /* 消抖计数 */
    static uint8_t stdby_cnt = CHARGE_CNT_NONE;         /* 消抖计数 */
    
    if (GPIO_READ(CHRG_PORT, CHRG_PIN) == 0) {       
        if (chrg_cnt < CHARGE_CNT_MAX) chrg_cnt++; 
        if (chrg_cnt >= CHARGE_DEBOUNCE_CNT) state = CHARGE_STATE_CHARGING;     /* 消抖计数 */

        if (GPIO_READ(STDBY_PORT, STDBY_PIN) == 0) {
            if (stdby_cnt < CHARGE_CNT_MAX) stdby_cnt++; 
            if (stdby_cnt >= CHARGE_DEBOUNCE_CNT) state = CHARGE_STATE_FULL;    /* 消抖计数 */
        } else {
            stdby_cnt = CHARGE_CNT_NONE;                /* 松开计数清零 */
        }
    } else {
        chrg_cnt = CHARGE_CNT_NONE;                     /* 松开计数清零 */
    }
    
    return state;                                       /* CHRG# 高 + STDBY# 高 = 未充电 */
}
