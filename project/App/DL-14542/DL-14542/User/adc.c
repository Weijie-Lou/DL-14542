/**
 * @file    adc.c
 * @brief   NY8BM84A 模数转换(ADC)驱动实现
 * @note
 *   - ADMD  ：ADEN[7] 使能 / START[6] 启动 / GCHS[5] 全局通道 / CHS[4:0] 通道选择
 *   - ADCON1：ADFM[4] 结果格式 / ADCK[3:2] 转换时钟 / VHS[1:0] 参考电压 / EOC 转换完成标志
 *   - ADCR  ：PxCON 端口映射[7:6] / 采样脉冲宽度[3:2] / 转换位数[1:0]
 *   - 转换结果：ADD = (ADDH << 8) | ADDL（12 位）
 */

#include "adc.h"

void ADC_Init(ADC_Channel_t ch, ADC_Reference_t ref, ADC_Clock_t clk)
{
    ADMD   = C_ADC_En | C_ADC_CH_Dis | ch;   /* 使能 ADC、关闭全局通道、选择通道 */
    ADCON1 = C_ADFM_Right | ref | clk;       /* 结果右对齐 + 参考电压 + 转换时钟 */
    ADCR   = (ADCR & 0xF0u) | C_Sample_8Clk | C_12Bit;  /* 保留 ADCR[7:4]（含 [7:6] PxCON 端口映射，由 GPIO_SET_ANALOG 维护），设采样宽度[3:2]与位数[1:0] */

    ADMDbits.GCHS = 1;                       /* 使能全局 ADC 通道 */
    PIR2bits.ADIF = 0;                       /* 清 ADC 中断标志 */
}

void ADC_Disable(void)
{
    ADMD = C_ADC_CH_Dis;                     /* ADMD 写 0：关闭 ADEN 使能与通道选择 */
}

uint16_t ADC_Convert(void)
{
    static uint16_t last_result = 0;   /* 上次有效结果（超时保护时返回，避免读数跳变） */
    uint16_t result;
    uint16_t timeout = 0;

    ADMDbits.START = 1;                      /* 启动一次转换 */
    while (ADCON1bits.EOC == 0) {            /* 等待转换完成 */
        if (++timeout == 0u) {               /* 超时保护：约 65535 次空转后放弃，返回上次有效结果 */
            return last_result;
        }
    }
    last_result = result = ((unsigned short)ADDH << 8) | ADDL;   /* 12 位结果 */
    return result;
}