/**
 * @file    temp.c
 * @brief   NTC 水温采集实现（ADC 采样 + 查表线性插值，输出 ℃ 整数 0~99）
 * @note
 *   采样链路：ADC(AN9/PB4, 12bit, 参考 VDD) → 丢弃首次值 → 4 次采样平均
 *             → 查表定位 5℃ 档 → 档内线性插值 → 1/8 递推平均滤波 → 输出温度
 *   说明：分压电阻 R_fix 与 NTC 均接 VDD，ADC 参考也是 VDD，属比例测量，
 *         因此 ADC 码值不受电池电压波动影响（配置字 VDD 标称 3.0V）
 */

#include "temp.h"
#include "adc.h"

// ADC 换算表（12bit，参考 VDD 的比例测量；NTC 3950/100K 与 R_fix=100kΩ 分压）
// 排列规律：温度越高 → NTC 阻值越小 → 分压点电压越低 → ADC 码值越小（表值递减）
static const uint16_t ADC_Res[] = {
    953,  /* 0℃ 补偿+0.0℃ */
    1151,  /* 5℃ 补偿+0.0℃ */
    1365,  /* 10℃ 补偿+0.0℃ */
    1590,  /* 15℃ 补偿+0.0℃ */
    1820,  /* 20℃ 补偿+0.0℃ */
    2048,  /* 25℃ 补偿+0.0℃ */
    2271,  /* 30℃ 补偿+0.0℃ */
    2481,  /* 35℃ 补偿+0.0℃ */
    2677,  /* 40℃ 补偿+0.0℃ */
    2821,  /* 45℃ 补偿-1.0℃ */
    2954,  /* 50℃ 补偿-2.0℃ */
    3104,  /* 55℃ 补偿-2.0℃ */
    3236,  /* 60℃ 补偿-2.0℃ */
    3352,  /* 65℃ 补偿-2.0℃ */
    3434,  /* 70℃ 补偿-3.0℃ */
    3523,  /* 75℃ 补偿-3.0℃ */
    3600,  /* 80℃ 补偿-3.0℃ */
    3667,  /* 85℃ 补偿-3.0℃ */
    3723,  /* 90℃ 补偿-3.0℃ */
    3772,  /* 95℃ 补偿-3.0℃ */
    3814,  /* 100℃ 补偿-3.0℃ */
};
#define TEMP_TABLE_CNT      (sizeof(ADC_Res) / sizeof(ADC_Res[0]))  /* 表项数（21 项，0~100℃ 每 5℃ 一项） */
#define TEMP_STEP_C         50u      /* 表项温度步进 5℃ */

/* 温度采样滤波状态 */
typedef struct {
    uint16_t avg;   /* 滤波后的温度值（1/8 递推平均，单位 ℃） */
    uint8_t  diff;  /* 递推平均的余数，避免整除误差累积 */
} Temp_Sample_t;

Temp_Sample_t temp = {0, 0};                    /* 全局便于调试观察，也可改为 static */

void Temp_Enable(void)
{
    GPIO_SET_ANALOG(NTC_PORT, NTC_PIN);                     /* NTC 引脚为模拟输入 */
    GPIO_SET_INPUT(NTC_PORT, NTC_PIN);                      /* NTC 引脚为输入 */

    GPIO_SET_OUTPUT(NTC_ENABLE_PORT, NTC_ENABLE_PIN);       /* NTC 使能引脚为输出 */
    GPIO_OUTPUT_HIGH(NTC_ENABLE_PORT, NTC_ENABLE_PIN);      /* 拉高 NTC 使能引脚 */
}

void Temp_Deinit(void)
{
    GPIO_SET_DIGITAL(NTC_PORT, NTC_PIN);                    /* 关闭 NTC 引脚模拟输入 */
    GPIO_SET_OUTPUT(NTC_PORT, NTC_PIN);                     /* NTC 引脚为输出 */
    GPIO_OUTPUT_LOW(NTC_PORT, NTC_PIN);                     /* 拉低 NTC 引脚 */

    GPIO_SET_OUTPUT(NTC_ENABLE_PORT, NTC_ENABLE_PIN);       /* NTC 使能引脚为输出 */
    GPIO_OUTPUT_LOW(NTC_ENABLE_PORT, NTC_ENABLE_PIN);       /* 拉低 NTC 使能引脚 */
}

void Temp_Init(void)
{
    temp.avg = 0;                                           /* 复位滤波状态，使下一次采样直接生效 */
    temp.diff = 0;

    Temp_Enable();
}

/*============================================================================
 * 温度读取（℃ 整数）
 *==========================================================================*/
/** @brief 读取水温：ADC 采样 → 平均 → 查表插值 → 递推滤波，返回 ℃ 整数（0~99）
 *  @note  查表插值以 5℃ 档为基准，档内按 ADC 码值线性修正，结果为整度值（分辨率 1℃）；
 *         输出上限钳在 99℃（2 位数码管无法显示 100，避免十位段码溢出） */
uint8_t Temp_Read(void)
{
    uint8_t  i;
    uint16_t adc = 0, sum = 0, frac = 0, temp_val = 0;  
    /* AN9 通道（PB4），参考电压 VDD，ADC 时钟 Fcpu/16，12bit 转换
     * 首次转换结果丢弃：切换通道后的第一个值未稳定 */
    ADC_Init(NTC_ADC_CH, NTC_ADC_REF, NTC_ADC_CLK);
	ADC_Convert();

    for (i = 0; i < TEMP_ADC_SAMPLE_CNT; i++) {
        sum += ADC_Convert();  /* 累加 TEMP_ADC_SAMPLE_CNT 次采样 */
    }
    ADC_Disable();
    
    if (sum != 0) {
        adc = sum / TEMP_ADC_SAMPLE_CNT;  /* 取算术平均值（未剔除最大值/最小值） */

        if (adc <= ADC_Res[0]) temp_val = 0;                                        /* 低于表下限（0℃）→ 输出 0 */
        else {
            if (adc >= ADC_Res[TEMP_TABLE_CNT - 1]) temp_val = 1000;                /* 高于表上限：先取 1000，随后由输出上限钳到 999 */
            else {
                /* 查表 + 线性插值：定位 adc 所在的 5℃ 区间 [i-1, i]，基准温度取 i*5℃，
                * 按 (ADC_Res[i]-adc)/(ADC_Res[i]-ADC_Res[i-1]) 的比例向下修正，
                * 全程使用整数运算（frac 放大 5 倍后与档差比较递减），规避除法与 NTC 非线性 */
                for (i = 1; i < TEMP_TABLE_CNT; i++) {
                    temp_val += TEMP_STEP_C;
                    if (adc < ADC_Res[i]) {
                        frac = (uint16_t)(ADC_Res[i] - adc);
                        adc = (uint16_t)(ADC_Res[i] - ADC_Res[i - 1]);          /* 温度档差 */            
                        frac = (frac << 2) + frac;                              /* frac * 5 */
                        while (frac >= adc) { frac -= adc; temp_val -= 10;}
                        frac = (frac << 3) + (frac << 1);                       /* frac * 10 */
                        while (frac >= adc) { frac -= adc; temp_val--;}         /* 按 10 倍精度修正 */
                        break;
                    }
                }
            }
        }
    } else {
        temp_val = 0;  /* 如果 sum 为 0，则温度值直接置为 0 */
    }   

    /* 1/8 递推平均（IIR 低通）：avg = (avg*7 + diff + 新值) / 8
     * avg == 0 时（首次或外部复位后）直接用新值作初值，跳过滤波 */
    if (temp.avg == 0) temp.avg = temp_val;                         /* 首次直接赋值（跳过滤波） */
    temp.avg *= 7;                                                  /* 旧值权重 7/8 */
    temp.avg = temp.avg + temp.diff + temp_val;                     /* 旧值×7 + 上次余数 + 新值 */
    temp.diff = (uint8_t)(temp.avg % 8);                            /* 保存余数，补偿整除截断误差 */
    temp.avg >>= 3;                                                 /* 除以 8 → 1/8 递推平均（IIR 低通） */
    
    if (temp.avg >= 999u) temp.avg = 999u;                          /* 输出上限饱和（999：3 位数码管无法显示 1000） */
    return temp.avg;
}
