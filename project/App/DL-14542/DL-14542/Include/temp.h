#ifndef _TEMP_H_
#define _TEMP_H_

/**
 * @file    temp.h
 * @brief   NTC 温度采集模块（12bit ADC 采样 + 查表线性插值，输出 0.1℃ 单位的值）
 * @note
 *   - 硬件：NTC(3950/100K/1%) 与 R_fix(100k) 分压，分压点接 PA2/AN2，C1(10nF) 滤波；
 *           PA1 为分压供电使能脚（高有效），由 Temp_Enable()/Temp_Disable() 控制
 *   - ADC：12bit，参考电压 VDD，时钟 Fcpu/16，采样时间 8 Clk
 *           （分压与 ADC 参考同源，属比例测量，ADC 码值不受电池电压波动影响）
 *   - 量程 0~100℃，表间隔 5℃（共 21 项），查表 + 线性插值
 *   - 越界处理：低于 0℃ 输出 0，≥99.9℃ 饱和输出 999（即 99.9℃）
 *   - 软件滤波：丢弃首次转换 → 4 次采样平均 → 1/16 递推平均（IIR 低通）
 *   - 温度值统一以 0.1℃ 为单位返回（0~999），供显示与温区判断使用
 */

#include "gpio.h"
#include <stdint.h>

#define NTC_ENABLE_PORT			PORTA                  /* NTC 分压供电控制（PA1） */
#define NTC_ENABLE_PIN			GPIO_PIN_1             /* 输出高 = 分压电路通电 */

#define NTC_PORT                PORTA                  /* NTC 采样端口 */
#define NTC_PIN                 GPIO_PIN_2             /* NTC 采样引脚（PA2） */
#define NTC_ADC_CH              C_ADC_PA2             /* ADC 通道 AN2 */
#define NTC_ADC_REF             C_Vrefh_VDD            /* ADC 参考电压 = VDD */
#define NTC_ADC_CLK             C_ADC_CLK_Div16        /* ADC 时钟 = Fcpu / 16 */

/*============================================================================
 * 函数声明
 *==========================================================================*/

/** @brief 初始化温度采集：复位滤波参数并使能采样电路（NTC 引脚为输入、拉高 NTC 使能） */
void Temp_Init(void);
/** @brief 使能温度采集：配置 NTC 引脚为输入并拉高使能引脚 */
void Temp_Enable(void);
/** @brief 关闭温度采集：配置 NTC 引脚为输出并拉低使能引脚（省电：避免分压电路常通耗电） */
void Temp_Disable(void);
/** @brief 读取当前温度：返回滤波后的温度值（单位 0.1℃，0~999） */
uint16_t Temp_Read(void);

#endif  /* _TEMP_H_ */
