#include "battery.h"
#include "adc.h"

/* BAT 电量百分比*/

typedef struct {
	uint16_t    BAT_Adc;    
	uint8_t     BAT_Percent;
    uint8_t     BAT_diff;
} Battery_t;

Battery_t battery = {0};

uint8_t BAT_Voltage(void)
{
    uint16_t adc = 0, sum = 0;
    
    ADC_Init(BAT_ADC_CHANNEL, BAT_ADC_REF, BAT_ADC_CLK);
    ADC_Convert();
    
    for (unsigned char i = 0; i < BAT_ADC_SAMPLE_CNT; i++) {
        sum += ADC_Convert();
    }
    ADC_Disable();

    if (sum != 0) {
       adc = sum / BAT_ADC_SAMPLE_CNT;  /* 取算术平均值（未剔除最大值/最小值） */ 
    }
    sum >>= 2;

    if (battery.BAT_Adc == 0) battery.BAT_Adc = bat_AdcVal;
    battery.BAT_Adc *= 7;
    battery.BAT_Adc = battery.BAT_Adc + battery.BAT_diff + bat_AdcVal;
    battery.BAT_diff = battery.BAT_Adc % 8;
    battery.BAT_Adc >>= 3;
    return battery.BAT_Adc;
}
