#ifndef __BATTERY_H
#define __BATTERY_H

/**
 * @file    battery.h
 * @brief   电池电量采集接口（内部 1/4 VDD 通道 + 2V 基准 ADC，输出电量百分比）
 * @note
 *   - BAT_Voltage() 内部完成 ADC 初始化、采样与关闭，无需外部保持 ADC 常开（省电）
 *   - ADC 码值 ≈ VDD × 512（1/4 VDD 分压 ÷ 2V 基准 × 4096），
 *     折算基准见 battery.c 的 BATTERY_ADC_LEVEL0/50/100
 */

#include <ny8.h>
#include "ny8_constant.h"
#include "gpio.h"

#define BAT_ADC_CHANNEL             C_Quarter_VDD           /* ADC 通道 = 内部 1/4 VDD */
#define BAT_ADC_REF                 C_Vrefh_2V              /* ADC 参考电压 = 内部 2V */
#define BAT_ADC_CLK                 C_ADC_CLK_Div16         /* ADC 时钟 = Fcpu / 16 */
#define BAT_ADC_SAMPLE_CNT          4                       // ADC采样平滑次数（求平均）

/* @brief 获取电池电量百分比（0~100，内部含 1/8 递推平均） */
uint8_t BAT_Voltage(void);

#endif
