/**
 * @file    display.c
 * @brief   数码管 + 电池图标 + RGB 温区灯圈驱动实现（12 相动态扫描）
 * @note
 *   - 段码按“位”组织：buf 中的位为 1 表示该段点亮，位定义见 SEG_A~SEG_G
 *   - 扫描在 Timer0 1ms 中断内执行，每相 1ms，12 相共 12ms 完成一轮刷新，
 *     相序由 display.segment（0~11）轮询推进
 *   - 共阴驱动：每相把 1 根引脚拉低作该相公共端，该相需点亮的段线输出高电平；
 *     灯圈 LEDR/LEDG/LEDB 为独立引脚，低电平点亮（Display_OFF() 输出高电平熄灭）
 *   - 各相驱动的引脚（括号内为该相拉低的公共端，其余为该相高电平输出的段线）：
 *       相 0：温度个位 temp_buf[0]：A/B/C/D/E/F/G = SEG2/3/4/5/6/7/8（SEG1 公共端）
 *       相 1：温度十位 temp_buf[1]：A/B/C/D/E/F/G = SEG1/2/4/5/6/7/8（SEG3 公共端）
 *       相 2：温度百位 temp_buf[2]：A/B/C/D/E/F/G = SEG1/2/3/5/6/7/8（SEG4 公共端）
 *       相 3：倒计时秒个位 timer_buf[0]：A/B/C/D/E/G/F = SEG1/2/3/4/6/7/8（SEG5 公共端）
 *       相 4：倒计时秒十位 timer_buf[1]：A/B/C/D/E/G/F = SEG1/2/3/4/5/7/8（SEG6 公共端）
 *       相 5：倒计时分个位 timer_buf[2]：A/B/C/D/E/G/F = SEG1/2/3/4/5/6/8（SEG7 公共端）
 *       相 6：倒计时分十位 timer_buf[3]：A/B/C/D/E/F/G = SEG1/2/3/4/5/6/7（SEG8 公共端）
 *       相 7：附加符号 SEG3/SEG4/SEG5（◴ / NC / 小数点）（SEG2 公共端）
 *       相 8：冒号 SEG6/SEG7 + 温度符号 ℃ SEG1（SEG2 公共端，冒号受 BLINK_STATE_TFLG 控制）
 *       相 9：电池图标边框 SEG1/SEG3/SEG4/SEG5/SEG6（SEG9 公共端）
 *       相 10：电池图标边框 SEG6 + 电量格 SEG1~SEG5（按 battery_level 逐格点亮）（SEG10 公共端）
 *       相 11：电池状态灯 SEG7 绿 / SEG2 红（按 battery_status 点亮，低电与充电时闪烁）（SEG9 公共端）
 *   - DISPLAY_MODE_CHARGING 下相 0~8 直接跳过，画面只剩电池图标（相 9~11）
 *   - 每相开始先把 SEG1~SEG10 及 LEDR/G/B 全部设为输入（高阻）释放上一相，避免残影；
 *     点亮时统一“先写电平再切方向”，避免切换瞬间出现毛刺
 *   - 本函数只由 Timer0 中断调用；display 结构由主循环写、中断读，无临界区保护
 */

#include "display.h"
#include "gpio.h"

/* 单个数码管的段码位定义（buf 位序） */
#define SEG_A                   0x01
#define SEG_B                   0x02
#define SEG_C                   0x04
#define SEG_D                   0x08
#define SEG_E                   0x10
#define SEG_F                   0x20
#define SEG_G                   0x40
#define SEG_NULL                0x00        /* 全灭段码（倒计时分钟十位为 0 时用它熄灭该位；GET_SEG() 对 >9 的输入也返回该值） */

/* 0~9 的七段段码表 */
static const uint8_t seg_code[] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,                  /* 0 */
    SEG_B | SEG_C,                                                  /* 1 */
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                          /* 2 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                          /* 3 */
    SEG_B | SEG_C | SEG_F | SEG_G,                                  /* 4 */
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                          /* 5 */
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,                  /* 6 */
    SEG_A | SEG_B | SEG_C,                                          /* 7 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,          /* 8 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,                  /* 9 */
};

#define GET_SEG(d)  (((d) >= 10u) ? 0u : seg_code[(d)])     /* 取数字段码（>9 视为熄灭） */

Display_t display;

/** @brief 关闭显示：SEG1~SEG10 设为输出并输出低电平，LEDR/G/B 输出高电平（熄灭） */
void Display_OFF(void)
{
    GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);
    GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);
    GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);
    GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);
    GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);
    GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);
    GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);
    GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);
    GPIO_SET_OUTPUT(SEG9_PORT, SEG9_PIN);
    GPIO_SET_OUTPUT(SEG10_PORT, SEG10_PIN);
    
    GPIO_OUTPUT_LOW(SEG1_PORT, SEG1_PIN);
    GPIO_OUTPUT_LOW(SEG2_PORT, SEG2_PIN);
    GPIO_OUTPUT_LOW(SEG3_PORT, SEG3_PIN);
    GPIO_OUTPUT_LOW(SEG4_PORT, SEG4_PIN);
    GPIO_OUTPUT_LOW(SEG5_PORT, SEG5_PIN);
    GPIO_OUTPUT_LOW(SEG6_PORT, SEG6_PIN);
    GPIO_OUTPUT_LOW(SEG7_PORT, SEG7_PIN);
    GPIO_OUTPUT_LOW(SEG8_PORT, SEG8_PIN);
    GPIO_OUTPUT_LOW(SEG9_PORT, SEG9_PIN);
    GPIO_OUTPUT_LOW(SEG10_PORT, SEG10_PIN);    

    GPIO_SET_OUTPUT(LEDR_PORT, LEDR_PIN);
    GPIO_SET_OUTPUT(LEDG_PORT, LEDG_PIN);
    GPIO_SET_OUTPUT(LEDB_PORT, LEDB_PIN);

    GPIO_OUTPUT_HIGH(LEDR_PORT, LEDR_PIN);
    GPIO_OUTPUT_HIGH(LEDG_PORT, LEDG_PIN);
    GPIO_OUTPUT_HIGH(LEDB_PORT, LEDB_PIN);    
}

/** @brief 初始化显示模块：先关闭显示，再复位缓冲/数值/扫描相，模式置 OFF */
void Display_Init(void)
{      
    Display_OFF();

    display.temp_buf[0] = 0;                            /* 温度个位段码清零 */
    display.temp_buf[1] = 0;                            /* 温度十位段码清零 */
    display.temp_buf[2] = 0;                            /* 温度百位段码清零 */
    display.timer_buf[0] = 0;                           /* 倒计时秒个位段码清零 */
    display.timer_buf[1] = 0;                           /* 倒计时秒十位段码清零 */
    display.timer_buf[2] = 0;                           /* 倒计时分个位段码清零 */
    display.timer_buf[3] = 0;                           /* 倒计时分十位段码清零 */
    display.segment = 0;                                /* 扫描相清零 */
    display.temp = 0;                                   /* 当前温度清零 */
    display.timer = 0;                                  /* 当前倒计时清零 */
    display.led_cycle = 0;                              /* RGB 灯圈循环切换索引清零 */
    display.blink_cnt = 0;                              /* 闪烁计时清零 */
    display.blink_cont = BLINK_STATE_OFF;               /* 闪烁控制清零 */  
    display.battery_level = BATTERY_LEVEL_L0;           /* 当前电池电量清零 */
    display.battery_status = BATTERY_STATUS_NORMAL;     /* 当前电池状态置正常 */      
    display.mode = DISPLAY_MODE_OFF;
}

/** @brief 更新显示缓冲（温度 / 倒计时 / 电量 / 电池状态），仅数值变化时重算段码
 *  @param temp           温度值（0.1℃ 单位，如 278 = 27.8℃）
 *  @param timer          倒计时剩余秒数（0~900，显示为 MM:SS）
 *  @param battery_level  电池电量（%，相 10 据此点亮电量格）
 *  @param battery_status 电池状态（相 11 据此点亮状态灯 / 闪烁）
 *  @note  INIT 模式直接返回（自检画面由 Display_SetMode() 设置并保持 888 / 88:88）；
 *         与 display 中的当前值相同时跳过，避免重复刷新；
 *         温度与倒计时都用重复减法拆位（避免除法）：temp 先减出百位、再减出十位，
 *         余数即个位；timer 先减出分钟十位与分钟个位，再减出秒十位，余数即秒个位；
 *         拆位得到的档位 >9 时 GET_SEG() 返回 0（该位熄灭）
 */
void Display_SetDigit(uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status)
{
    if (display.mode == DISPLAY_MODE_INIT) return;
    if (display.temp != temp) {        
        uint8_t ten = 0, housand = 0;
        display.temp = temp;        
        display.temp_buf[0] = 0;
        display.temp_buf[1] = 0;
        display.temp_buf[2] = 0;
        while (temp >= 100u) { temp -= 100u; housand++; }               /* 先减出百位，余数即十位和个位（避免除法） */            
        while (temp >= 10u) { temp -= 10u; ten++; }                     /* 再减出十位，余数即个位（避免除法） */            
        display.temp_buf[0] = GET_SEG(temp);                            /* 温度个位段码 */
        display.temp_buf[1] = GET_SEG(ten);                             /* 温度十位段码 */
        display.temp_buf[2] = GET_SEG(housand);                         /* 温度百位段码 */
    }
    
    if (display.timer != timer) {
        uint8_t min_ten = 0, min = 0, sec_ten = 0;
        display.timer = timer;
        display.timer_buf[0] = 0;
        display.timer_buf[1] = 0;
        display.timer_buf[2] = 0;
        display.timer_buf[3] = 0;
        
        while (timer >= 600u) { timer -= 600u; min_ten++; }             /* 减出分钟十位，余数即分钟个位和秒（避免除法） */
        while (timer >= 60u) { timer -= 60u; min++; }                   /* 再减出分钟个位，余数即秒（避免除法） */
        while (timer >= 10u) { timer -= 10u; sec_ten++; }               /* 再减出秒十位，余数即秒个位（避免除法） */
        display.timer_buf[0] = GET_SEG(timer);                          /* 倒计时秒个位段码 */
        display.timer_buf[1] = GET_SEG(sec_ten);                        /* 倒计时秒十位段码 */
        display.timer_buf[2] = GET_SEG(min);                            /* 倒计时分个位段码 */
        if (min_ten == 0) {
            display.timer_buf[3] = SEG_NULL;                            /* 分钟十位为 0 时熄灭，不显示前导 0 */
        } else {
            display.timer_buf[3] = GET_SEG(min_ten);                    /* 倒计时分十位段码 */
        }        
    }

    if (display.battery_level != battery_level) {
        display.battery_level = battery_level;                          /* 电量变化，相 10 据此刷新电量格 */
    }

    if (display.battery_status != battery_status) {
        display.battery_status = battery_status;                        /* 状态变化，相 11 据此刷新状态灯 */
    }    
}

/** @brief 切换显示模式并按入参重建画面（模式相同时不动作）
 *  @param mode           DISPLAY_MODE_OFF / INIT / TIMER / TIMER_ALARM / CHARGING
 *  @param temp           温度值（0.1℃）
 *  @param timer          倒计时剩余秒数
 *  @param battery_level  电池电量（%）
 *  @param battery_status 电池状态
 *  @note  模式变化时清空段码缓冲、扫描相（从第 1 相重新刷新）与闪烁控制，随后：
 *         OFF 调 Display_OFF() 彻底关断；INIT 固定显示 888 + 88:88 并保持
 *         （此后 Display_SetDigit() 直接返回），灯圈颜色由 blink_cont 计时自动轮换；
 *         其余模式交给 Display_SetDigit() 用入参刷新一次
 */
void Display_SetMode(Display_Mode_e mode, uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status)
{
    if (display.mode != mode) {
        display.mode = mode;
        display.temp = 0;
        display.timer = 0;
        display.temp_buf[0] = GET_SEG(0);                                /* 温度个位段码 */
        display.temp_buf[1] = GET_SEG(0);                                /* 温度十位段码 */
        display.temp_buf[2] = GET_SEG(0);                                /* 温度百位段码 */
        display.timer_buf[0] = GET_SEG(0);                               /* 倒计时秒个位段码 */
        display.timer_buf[1] = GET_SEG(0);                               /* 倒计时秒十位段码 */
        display.timer_buf[2] = GET_SEG(0);                               /* 倒计时分个位段码 */
        display.timer_buf[3] = SEG_NULL;                                 /* 倒计时分十位段码（熄灭） */
        display.segment = 0;                                            /* 扫描相清零 */
        display.led_cycle = 0;                                          /* RGB 灯圈循环切换索引清零 */
        display.blink_cnt = 0;                                          /* 闪烁计时清零 */
        display.blink_cont = BLINK_STATE_OFF;                           /* 闪烁控制清零 */
        display.battery_level = BATTERY_LEVEL_L0;
        display.battery_status = BATTERY_STATUS_NORMAL;
        
        switch (mode) {
            case DISPLAY_MODE_OFF:
                Display_OFF();
                break;
            case DISPLAY_MODE_INIT:
                display.temp_buf[0] = GET_SEG(8);                        /* 上电自检显示 888（个位/十位/百位） */
                display.temp_buf[1] = GET_SEG(8);
                display.temp_buf[2] = GET_SEG(8);                        
                display.timer_buf[0] = GET_SEG(8);                       /* 上电自检显示 88:88 */
                display.timer_buf[1] = GET_SEG(8);
                display.timer_buf[2] = GET_SEG(8);
                display.timer_buf[3] = GET_SEG(8);
                display.temp = temp;
                display.timer = timer;
                display.battery_level = battery_level;
                display.battery_status = battery_status;
                break;
            default:
                Display_SetDigit(temp, timer, battery_level, battery_status);
                break;
        }
    }
}

/** @brief 动态扫描（Timer0 1ms 中断内调用）：按相序刷新段码 / 电池图标 / 灯圈，并推进闪烁
 *  @note  OFF 模式直接返回（省电）；每相开始先把全部段线与灯圈引脚置为输入（高阻）释放上一相；
 *         每 500ms 翻转一次闪烁标志：倒计时冒号（TIMER，倒计时为 0 时常亮）、
 *         报警灯圈三色循环（TIMER_ALARM，18 次即 9s 后自动回到 TIMER）、
 *         自检灯圈换色（INIT，6 次即 3s 一轮）、低电（绿）与充电（红）状态灯闪烁
 */
void Display_Scan_ISR(void)
{
    if (display.mode == DISPLAY_MODE_OFF) return;

    GPIO_SET_INPUT(SEG1_PORT, SEG1_PIN);        /* 先释放上一相的段引脚，避免残影 */
    GPIO_SET_INPUT(SEG2_PORT, SEG2_PIN);
    GPIO_SET_INPUT(SEG3_PORT, SEG3_PIN);
    GPIO_SET_INPUT(SEG4_PORT, SEG4_PIN);
    GPIO_SET_INPUT(SEG5_PORT, SEG5_PIN);
    GPIO_SET_INPUT(SEG6_PORT, SEG6_PIN);
    GPIO_SET_INPUT(SEG7_PORT, SEG7_PIN);
    GPIO_SET_INPUT(SEG8_PORT, SEG8_PIN);
    GPIO_SET_INPUT(SEG9_PORT, SEG9_PIN);
    GPIO_SET_INPUT(SEG10_PORT, SEG10_PIN);

    GPIO_SET_INPUT(LEDR_PORT, LEDR_PIN);        /* 灯圈引脚同样先释放，仅在相 0/3/6/9 重新驱动 */
    GPIO_SET_INPUT(LEDG_PORT, LEDG_PIN);
    GPIO_SET_INPUT(LEDB_PORT, LEDB_PIN);

    display.blink_cnt++;                        /* 每 500ms 翻转一次闪烁标志（本函数 1ms 调用一次） */
    if (display.blink_cnt >= 500) {
        display.blink_cnt = 0;
        switch (display.mode) {
            case DISPLAY_MODE_TIMER:
                if (display.timer == 0) {
                    display.blink_cont &= ~BLINK_STATE_TFLG;
                    break;
                }
                display.blink_cont ^= BLINK_STATE_TFLG;
                break;
            case DISPLAY_MODE_TIMER_ALARM:
                display.blink_cont ^= BLINK_STATE_TFLG;                       /* 冒号由常亮改为闪烁 */
                display.blink_cont ^= BLINK_STATE_TIMER_ALARM;
                display.led_cycle++;                                          /* RGB 灯圈循环切换索引（每 500ms 换一色） */
                if (display.led_cycle >= 18) {                                /* 报警 18 × 500ms = 9s 后自动转常显 */
                    display.led_cycle = 0;
                    display.blink_cont = BLINK_STATE_OFF;
                    display.mode = DISPLAY_MODE_TIMER;
                }
                break;
            case DISPLAY_MODE_INIT:
                display.led_cycle++;                                          /* RGB 灯圈循环切换索引（每 500ms 换一色） */
                if (display.led_cycle >= 6) {                                 /* 6 × 500ms = 3s，与自检时长一致 */
                    display.led_cycle = 0;
                }
                break;
            default:
            	display.blink_cont &= ~(BLINK_STATE_TFLG | BLINK_STATE_TIMER_ALARM);
                break;
        }

        switch (display.battery_status) {
            case BATTERY_STATUS_CHARGING:
                display.blink_cont ^= BLINK_STATE_CHARGING;
                break;
            case BATTERY_STATUS_LOW:
                display.blink_cont ^= BLINK_STATE_BAT_LOW;
                break;
            default:
                display.blink_cont &= ~(BLINK_STATE_CHARGING | BLINK_STATE_BAT_LOW);
                break;
        }
    }
    
    if ((display.segment % 3) == 0) {           /* 灯圈仅在相 0/3/6/9 驱动，其余相位保持高阻（约 1/3 占空比） */
        if (display.mode == DISPLAY_MODE_TIMER) {                       /* 温度显示期间按温区着色 */
            if (display.temp >= TEMP_ZONE_HOT) {
                GPIO_OUTPUT_LOW(LEDR_PORT, LEDR_PIN); GPIO_SET_OUTPUT(LEDR_PORT, LEDR_PIN);            // 红色灯圈
            } else {
                if (display.temp >= TEMP_ZONE_NORMAL) {
                    GPIO_OUTPUT_LOW(LEDG_PORT, LEDG_PIN); GPIO_SET_OUTPUT(LEDG_PORT, LEDG_PIN);        // 绿色灯圈
                } else {
                    GPIO_OUTPUT_LOW(LEDB_PORT, LEDB_PIN); GPIO_SET_OUTPUT(LEDB_PORT, LEDB_PIN);         // 蓝色灯圈
                }
            }
        }

        if ((display.mode == DISPLAY_MODE_TIMER_ALARM) || (display.mode == DISPLAY_MODE_INIT)) {
            switch (display.led_cycle % 3) {                            /* 报警 / 自检期间三色循环 */
                case 0:
                    GPIO_OUTPUT_LOW(LEDB_PORT, LEDB_PIN); GPIO_SET_OUTPUT(LEDB_PORT, LEDB_PIN);         // 蓝色灯圈
                    break;
                case 1:
                    GPIO_OUTPUT_LOW(LEDG_PORT, LEDG_PIN); GPIO_SET_OUTPUT(LEDG_PORT, LEDG_PIN);        // 绿色灯圈
                    break;
                case 2:
                    GPIO_OUTPUT_LOW(LEDR_PORT, LEDR_PIN); GPIO_SET_OUTPUT(LEDR_PORT, LEDR_PIN);        // 红色灯圈
                    break;
            }
        }
    }    

    switch (display.segment) 
	{
		case 0:                                     /* 温度个位 temp_buf[0]（公共端 SEG1） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
			if (display.temp_buf[0] & SEG_A) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}            
			if (display.temp_buf[0] & SEG_B) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}
            if (display.temp_buf[0] & SEG_C) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}   
            if (display.temp_buf[0] & SEG_D) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}   
            if (display.temp_buf[0] & SEG_E) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}   
            if (display.temp_buf[0] & SEG_F) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}   
            if (display.temp_buf[0] & SEG_G) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);}   
            GPIO_OUTPUT_LOW(SEG1_PORT, SEG1_PIN);
            GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN); 
			break;		
		case 1:                                     /* 温度十位 temp_buf[1]（公共端 SEG3） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
			if (display.temp_buf[1] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}            
			if (display.temp_buf[1] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.temp_buf[1] & SEG_C) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}   
            if (display.temp_buf[1] & SEG_D) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}   
            if (display.temp_buf[1] & SEG_E) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}   
            if (display.temp_buf[1] & SEG_F) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}   
            if (display.temp_buf[1] & SEG_G) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);} 
			GPIO_OUTPUT_LOW(SEG3_PORT, SEG3_PIN);
            GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN); 			
			break;
		case 2:                                     /* 温度百位 temp_buf[2]（公共端 SEG4） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
			if (display.temp_buf[2] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}            
			if (display.temp_buf[2] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.temp_buf[2] & SEG_C) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}   
            if (display.temp_buf[2] & SEG_D) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}   
            if (display.temp_buf[2] & SEG_E) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}   
            if (display.temp_buf[2] & SEG_F) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}   
            if (display.temp_buf[2] & SEG_G) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);} 
			GPIO_OUTPUT_LOW(SEG4_PORT, SEG4_PIN);
            GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);	
			break;
		case 3:                                     /* 倒计时秒个位 timer_buf[0]（公共端 SEG5） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
            //if (display.blink_cont & BLINK_STATE_TIMER_ALARM) break;
			if (display.timer_buf[0] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}
            if (display.timer_buf[0] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.timer_buf[0] & SEG_C) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}
            if (display.timer_buf[0] & SEG_D) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}
            if (display.timer_buf[0] & SEG_E) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}
            if (display.timer_buf[0] & SEG_G) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}
            if (display.timer_buf[0] & SEG_F) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);}
            GPIO_OUTPUT_LOW(SEG5_PORT, SEG5_PIN);			
			GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN); 			
			break;
        case 4:                                     /* 倒计时秒十位 timer_buf[1]（公共端 SEG6） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
            //if (display.blink_cont & BLINK_STATE_TIMER_ALARM) break;
			if (display.timer_buf[1] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}
            if (display.timer_buf[1] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.timer_buf[1] & SEG_C) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}
            if (display.timer_buf[1] & SEG_D) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}
            if (display.timer_buf[1] & SEG_E) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}
            if (display.timer_buf[1] & SEG_G) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}
            if (display.timer_buf[1] & SEG_F) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);}
            GPIO_OUTPUT_LOW(SEG6_PORT, SEG6_PIN);			
			GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN); 			
			break;
        case 5:                                     /* 倒计时分个位 timer_buf[2]（公共端 SEG7） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
            //if (display.blink_cont & BLINK_STATE_TIMER_ALARM) break;
			if (display.timer_buf[2] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}
            if (display.timer_buf[2] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.timer_buf[2] & SEG_C) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}
            if (display.timer_buf[2] & SEG_D) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}
            if (display.timer_buf[2] & SEG_E) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}
            if (display.timer_buf[2] & SEG_G) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}
            if (display.timer_buf[2] & SEG_F) {GPIO_OUTPUT_HIGH(SEG8_PORT, SEG8_PIN); GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN);}
            GPIO_OUTPUT_LOW(SEG7_PORT, SEG7_PIN);			
			GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN); 			
			break;
        case 6:                                     /* 倒计时分十位 timer_buf[3]（公共端 SEG8） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
            //if (display.blink_cont & BLINK_STATE_TIMER_ALARM) break;
			if (display.timer_buf[3] & SEG_A) {GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);}
            if (display.timer_buf[3] & SEG_B) {GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);}
            if (display.timer_buf[3] & SEG_C) {GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);}
            if (display.timer_buf[3] & SEG_D) {GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);}
            if (display.timer_buf[3] & SEG_E) {GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);}
            if (display.timer_buf[3] & SEG_F) {GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);}
            if (display.timer_buf[3] & SEG_G) {GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);}
            GPIO_OUTPUT_LOW(SEG8_PORT, SEG8_PIN);			
			GPIO_SET_OUTPUT(SEG8_PORT, SEG8_PIN); 			
			break;
        case 7:                                     /* 附加符号 ◴ / NC / 小数点（公共端 SEG2） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;                        
            GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);        // ◴
            GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);        // NC
            GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);        // dot            

            GPIO_OUTPUT_LOW(SEG2_PORT, SEG2_PIN);
            GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);
            break;
        case 8:                                     /* 冒号 : 与温度符号 ℃（公共端 SEG2，冒号受 BLINK_STATE_TFLG 控制） */
            if (display.mode == DISPLAY_MODE_CHARGING) break;
            if ((display.blink_cont & BLINK_STATE_TFLG) == 0) {
                GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);        // :
                GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);        // :
                GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);        // ℃
                GPIO_OUTPUT_LOW(SEG2_PORT, SEG2_PIN);
                GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);              
            }			 			
			break;
        case 9:                                     /* 电池图标边框（公共端 SEG9，CHARGING 模式下仍显示） */            
			GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);            // 电池图标边框

            GPIO_OUTPUT_LOW(SEG9_PORT, SEG9_PIN);			
			GPIO_SET_OUTPUT(SEG9_PORT, SEG9_PIN); 			
			break;
        case 10:                                    /* 电池电量格（公共端 SEG10，按 battery_level 逐格点亮） */
            if (display.battery_level >= BATTERY_LEVEL_L1) {
                GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);
            }
            if (display.battery_level >= BATTERY_LEVEL_L2) {
                GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);
            }
            if (display.battery_level >= BATTERY_LEVEL_L3) {
                GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);
            }
            if (display.battery_level >= BATTERY_LEVEL_L4) {
                GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);
            }
            if (display.battery_level >= BATTERY_LEVEL_L5) {
                GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);
            }
            GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);            // 电池图标边框

            GPIO_OUTPUT_LOW(SEG10_PORT, SEG10_PIN);			
			GPIO_SET_OUTPUT(SEG10_PORT, SEG10_PIN); 		
			break;
        case 11:                                    /* 电池状态灯（公共端 SEG9，绿/红按 battery_status 闪烁） */
            switch (display.battery_status) {
                case BATTERY_STATUS_CHARGE_FULL:
                case BATTERY_STATUS_NORMAL:
                    GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);    // 绿灯
                    break;  
                case BATTERY_STATUS_INIT:
                    GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);    // 绿灯
                    GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);    // 红灯
                    break;
                case BATTERY_STATUS_LOW:
                    if (display.blink_cont & BLINK_STATE_BAT_LOW) break;
                    GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);    // 绿灯
                    break;
                case BATTERY_STATUS_CHARGING:
                    if (display.blink_cont & BLINK_STATE_CHARGING) break;
                    GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);    // 红灯
                    break;
                default:
                    break;
            }
            GPIO_OUTPUT_LOW(SEG9_PORT, SEG9_PIN);			
			GPIO_SET_OUTPUT(SEG9_PORT, SEG9_PIN); 			
			break;
        default:
            break;
	}

    display.segment++;                                  /* 推进到下一扫描相 */
    if (display.segment > 11) display.segment = 0;
}
