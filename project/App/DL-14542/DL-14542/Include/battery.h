#ifndef __BATTERY_H
#define __BATTERY_H

#include <ny8.h>
#include "ny8_constant.h"
#include "gpio.h"

#define BAT_ADC_CHANNEL             C_Quarter_VDD           /* ADC 通道 1/4 VDD */
#define BAT_ADC_REF                 C_Vrefh_2V              /* ADC 参考电压 = 2V */
#define BAT_ADC_CLK                 C_ADC_CLK_Div16         /* ADC 时钟 = Fcpu / 16 */
#define BAT_ADC_SAMPLE_CNT          4                       // ADC采样平滑次数

/* @brief 获取电池电压 */
unsigned short BAT_Voltage(void);

#endif
