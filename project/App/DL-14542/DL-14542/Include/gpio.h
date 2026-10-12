/**
 * @file    gpio.h
 * @brief   GPIO 操作接口：方向、电平、上下拉、唤醒、模拟/数字切换
 * @note
 *   - 端口参数直接传寄存器（PORTA/PORTB/PORTC，ny8.h 中的 sfr 宏）
 *   - 方向寄存器通过 IOST() 推导（= 数据寄存器地址 + 0x80）
 *   - SET_PULL/SET_WAKEUP/SET_ANALOG 以宏包装 _IMPL 函数，由实现函数识别端口
 */
#ifndef __GPIO_H
#define __GPIO_H

#include <ny8.h>
#include "ny8_constant.h"
#include <stdint.h>

typedef enum {
     GPIO_PIN_0 = 0,
     GPIO_PIN_1,
     GPIO_PIN_2,
     GPIO_PIN_3,
     GPIO_PIN_4,
     GPIO_PIN_5,
     GPIO_PIN_6,
     GPIO_PIN_7
 } GPIO_Pin;

typedef enum {
     GPIO_PORTA = 0,
     GPIO_PORTB,
     GPIO_PORTC
 } GPIO_Port;

/* 引脚属性：对应 PORTxCON 每引脚 2 位（输入:PH/PL，输出:PP/OD） */
typedef enum {
     GPIO_PULL_NONE    = 0x00,   /* 输入浮动 / 推挽输出 */
     GPIO_PULL_DOWN    = 0x01,   /* 输入下拉           */
     GPIO_PULL_UP      = 0x02,   /* 输入上拉 / 输出开漏 */
     GPIO_PULL_UP_DOWN = 0x03    /* 输入上下拉          */
 } GPIO_Pull;

//  I/O Control Register (方向寄存器 = 数据寄存器地址 + 0x80)
//  注意：不能用 ## 拼接映射 —— PORTA/PORTB/PORTC 在 ny8.h 中会展开为
//  sfr(0x..) 表达式（带括号），token 拼接会形成无效的 _IOST_( 。
#define IOST(port)  (*(volatile __rpage unsigned char *)((unsigned int)(&(port)) + 0x80))

 #define GPIO_SET_INPUT(port, pin)     (IOST(port) |= (1 << pin))       /* 设为输入 */
 #define GPIO_SET_OUTPUT(port, pin)    (IOST(port) &= ~(1 << pin))      /* 设为输出 */

 #define GPIO_OUTPUT_HIGH(port, pin)    (port |= (1 << pin))            /* 输出高 */
 #define GPIO_OUTPUT_LOW(port, pin)     (port &= ~(1 << pin))           /* 输出低 */
 #define GPIO_TOGGLE(port, pin)         (port ^= (1 << pin))            /* 翻转输出 */
 #define GPIO_READ(port, pin)           ((port & (1 << pin)) ? 1 : 0)   /* 读引脚电平 */

/** @brief GPIO 寄存器初始化 */
void GPIO_Init(void);

/* GPIO_SET_PULL / GPIO_SET_WAKEUP / GPIO_SET_ANALOG：
 * 以宏形式提供，端口参数与 GPIO_SET_INPUT 等宏一致，直接传寄存器（如 PORTA/PORTB/PORTC），
 * 内部经 & 取地址后由实现函数识别端口。
 * 说明：GPIO_SET_WAKEUP 当前工程未使用（睡眠唤醒依赖 WDT + INT0）。 */
void GPIO_SET_PULL_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, GPIO_Pull pull);
#define GPIO_SET_PULL(port, pin, pull)      GPIO_SET_PULL_IMPL(&(port), (pin), (pull))

/* enable = 1 允许该引脚唤醒；PORTA 用 AWUCON，PORTB/PORTC 共用 BCDWUCON（由 SELCON[4:3] 选择映射） */
void GPIO_SET_WAKEUP_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, unsigned char enable);
#define GPIO_SET_WAKEUP(port, pin, enable)  GPIO_SET_WAKEUP_IMPL(&(port), (pin), (enable))

/* enable = 1 设为纯模拟引脚，0 恢复数字口；端口映射由 ADCR[7:6] 选择对应的 PxCON */
void GPIO_SET_ANALOG_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, unsigned char enable);
#define GPIO_SET_ANALOG(port, pin)          GPIO_SET_ANALOG_IMPL(&(port), (pin), (1))
#define GPIO_SET_DIGITAL(port, pin)         GPIO_SET_ANALOG_IMPL(&(port), (pin), (0))

#endif
