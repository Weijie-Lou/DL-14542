#ifndef _ADC_H_
#define _ADC_H_

/**
 * @file    adc.h
 * @brief   NY8BM84A 模数转换(ADC)驱动接口
 * @note
 *   - 12 位分辨率；通道选择见 ADC_Channel_t（对应 ADMD[4:0] 的 CHS 值）
 *   - 参考电压见 ADC_Reference_t（对应 ADCON1：EVHENB[7] 外部参考 / VHS[1:0] 内部电压）
 *   - 转换时钟见 ADC_Clock_t（对应 ADCON1 的 ADCK[3:2] 位，0x00/0x04/0x08/0x0C 分别 Fcpu/16、/8、/1、/2）
 *   - 转换结果：ADD = (ADDH << 8) | ADDL，范围 0~4095
 */

#include <ny8.h>
#include "ny8_constant.h"
#include <stdint.h>

/*============================================================================
 * 通道定义（ADMD[4:0] 选择值，与官方 C_ADC_* 宏对应）
 *==========================================================================*/

typedef enum {
    ADC_CH_AN0      = C_ADC_PA0,       /* AN0  -> PORTA0     */
    ADC_CH_AN1      = C_ADC_PA1,       /* AN1  -> PORTA1     */
    ADC_CH_AN2      = C_ADC_PA2,       /* AN2  -> PORTA2     */
    ADC_CH_AN3      = C_ADC_PA3,       /* AN3  -> PORTA3     */
    ADC_CH_AN4      = C_ADC_PA4,       /* AN4  -> PORTA4     */
    ADC_CH_AN6      = C_ADC_PA6,       /* AN6  -> PORTA6     */
    ADC_CH_AN7      = C_ADC_PA7,       /* AN7  -> PORTA7     */
    ADC_CH_AN8      = C_ADC_PB0,       /* AN8  -> PORTB0     */
    ADC_CH_AN9      = C_ADC_PB1,       /* AN9  -> PORTB1     */
    ADC_CH_AN10     = C_ADC_PB2,       /* AN10 -> PORTB2     */
    ADC_CH_AN11     = C_ADC_PB3,       /* AN11 -> PORTB3     */
    ADC_CH_AN12     = C_ADC_PB4,       /* AN12 -> PORTB4     */
    ADC_CH_AN13     = C_ADC_PB5,       /* AN13 -> PORTB5     */
    ADC_CH_AN14     = C_ADC_PB6,       /* AN14 -> PORTB6     */
    ADC_CH_AN15     = C_ADC_PB7,       /* AN15 -> PORTB7     */
    ADC_CH_PC0      = C_ADC_PC0,       /* PC0  -> PORTC0     */
    ADC_CH_PC1      = C_ADC_PC1,       /* PC1  -> PORTC1     */
    ADC_CH_PC2      = C_ADC_PC2,       /* PC2  -> PORTC2     */
    ADC_CH_PC3      = C_ADC_PC3,       /* PC3  -> PORTC3     */
    ADC_CH_PC4      = C_ADC_PC4,       /* PC4  -> PORTC4     */
    ADC_CH_PC5      = C_ADC_PC5,       /* PC5  -> PORTC5     */
    ADC_CH_1_4_VDD  = C_Quarter_VDD,   /* 1/4 VDD internal   */
    ADC_CH_VSS      = C_ADC_VSS        /* VSS pad            */
} ADC_Channel_t;

/*============================================================================
 * 参考电压定义（ADCON1：Bit7=EVHENB 外部参考，Bit[1:0]=VHS 内部电压）
 *==========================================================================*/

typedef enum {
    ADC_REF_2V      = C_Vrefh_2V,       /* 0x00 internal 2.0V  */
    ADC_REF_3V      = C_Vrefh_3V,       /* 0x01 internal 3.0V  */
    ADC_REF_4V      = C_Vrefh_4V,       /* 0x02 internal 4.0V  */
    ADC_REF_VDD     = C_Vrefh_VDD,      /* 0x03 internal VDD   */
    ADC_REF_EXT     = C_Vrefh_External  /* 0x80 external (PA0) */
} ADC_Reference_t;

/*============================================================================
 * ADC 转换时钟（ADCON1 Bit[3:2]）
 *==========================================================================*/

typedef enum {
    ADC_CLK_FOSC_DIV16  = C_ADC_CLK_Div16,  /* 0x00 Fcpu/16 */
    ADC_CLK_FOSC_DIV8   = C_ADC_CLK_Div8,   /* 0x04 Fcpu/8  */
    ADC_CLK_FOSC_DIV1   = C_ADC_CLK_Div1,   /* 0x08 Fcpu/1  */
    ADC_CLK_FOSC_DIV2   = C_ADC_CLK_Div2    /* 0x0C Fcpu/2  */
} ADC_Clock_t;

/*============================================================================
 * 函数声明
 *==========================================================================*/

/** @brief 初始化 ADC（通道 + 参考电压 + 时钟，并置 GCHS 使能） */
void ADC_Init(ADC_Channel_t ch, ADC_Reference_t ref, ADC_Clock_t clk);
/** @brief 关闭 ADC（ADMD 写 0：同时清 ADEN 使能与通道选择） */
void ADC_Disable(void);
/** @brief 阻塞式转换，返回 12 位结果（查询 EOC 等待完成）
 *  @note  带超时保护：长时间等不到 EOC 时返回上一次的有效结果，避免读数跳变 */
uint16_t ADC_Convert(void);

#endif  /* _ADC_H_ */