/**
 * @file    battery.c
 * @brief   电池电量采集实现（内部 1/4 VDD 通道 + 2V 基准 ADC → 电量百分比）
 * @note
 *   - 采样链路：ADC(1/4 VDD, 12bit, 参考 2V) → 丢弃首次值 → 4 次采样平均
 *               → 分段折算百分比 → 1/8 递推平均
 *   - ADC 码值 ≈ VDD × 512（即 (VDD/4) / 2V × 4096），故折算基准为：
 *       1650 → 0%（VDD ≈ 3.22V）  1900 → 50%（≈ 3.71V）  2150 → 100%（≈ 4.20V）
 *   - 电量步进 5 个 ADC 码对应 1%，与上表一致（250 码 = 50%）
 */

#include "battery.h"
#include "adc.h"
#include <stdint.h>

/* BAT 电量ADC对应参数 */
#define BATTERY_ADC_LEVEL0          1650        /* 0% 电量对应的 ADC 值（VDD ≈ 3.22V） */
#define BATTERY_ADC_LEVEL50         1900        /* 50% 电量对应的 ADC 值（VDD ≈ 3.71V） */
#define BATTERY_ADC_LEVEL100        2150        /* 100% 电量对应的 ADC 值（VDD ≈ 4.20V） */
#define BATTERY_PERCENT_STEP        5           /* 每 1% 电量对应的 ADC 码数 */


typedef struct {	
	uint16_t	BAT_Percent;    /* 平滑后的电量百分比（%） */
    uint8_t     BAT_diff;       /* 1/8 递推平均的余数，避免整除误差累积 */
} Battery_t;

Battery_t battery = {0, 0};

/** @brief 读取电池电量：ADC 采样平均 → 分段折算百分比 → 1/8 递推平均
 *  @return 电量百分比（0~100）
 *  @note   采样链路与滤波状态保存在 battery 结构体内，多次调用之间保持连续平滑 */
uint8_t BAT_Voltage(void)
{
	uint8_t percent = 0;
	uint16_t sum = 0, adc = 0;
	
	ADC_Init(BAT_ADC_CHANNEL, BAT_ADC_REF, BAT_ADC_CLK);
    ADC_Convert();                                          /* 丢弃首次转换结果（切换通道后未稳定） */
    
    for (uint8_t i = 0; i < BAT_ADC_SAMPLE_CNT; i++) {
        sum += ADC_Convert();
    }
    ADC_Disable();                                          /* 采样结束立即关闭 ADC，省电 */

    if (sum != 0) {
        adc = sum >> 2;  /* 4 次采样求平均（>>2 即 /4，未剔除最大值/最小值） */
        if (adc <= BATTERY_ADC_LEVEL0) {                    /* 低于 0% 基点：电量为 0 */
            percent = 0;
        } else if (adc >= BATTERY_ADC_LEVEL100) {           /* 高于 100% 基点：电量饱和 */
            percent = 100;
        } else {
            adc = adc - BATTERY_ADC_LEVEL0;                 /* 折算到 0%~100% 区间内 */
            while (adc >= (BATTERY_PERCENT_STEP * 10)) {    /* 先按 10% 步进快减 */
                adc -= (BATTERY_PERCENT_STEP * 10);
                percent += 10;
            }
            while (adc >= BATTERY_PERCENT_STEP) {           /* 再按 1% 步进细调 */
                adc -= BATTERY_PERCENT_STEP;
                percent += 1;
            }
        }

        if (battery.BAT_Percent == 0) battery.BAT_Percent = percent;  /* 首次直接赋值（跳过滤波） */
        battery.BAT_Percent *= 7;                                     /* 旧值权重 7/8 */
        battery.BAT_Percent = battery.BAT_Percent + battery.BAT_diff + percent;   /* 旧值×7 + 上次余数 + 新值 */
        battery.BAT_diff = (uint8_t)battery.BAT_Percent % 8;          /* 保存余数，补偿整除截断误差 */
        battery.BAT_Percent >>= 3;                                    /* 除以 8 → 1/8 递推平均（IIR 低通） */
    }
    return battery.BAT_Percent;
}
