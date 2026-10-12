/**
 * @file    display.c
 * @brief   2 位数码管 + 温区指示灯驱动实现（4 相段扫描 + 1 相指示灯，共 5 相）
 * @note
 *   - 段码按“位”组织：buf 中的位为 1 表示该段点亮，位定义见 SEG_A~SEG_G；
 *     buf[0] 为个位、buf[1] 为十位（本工程最多显示 2 位数字）
 *   - 共阴驱动：公共端输出低电平选通，段线输出高电平点亮
 *   - 扫描在 Timer0 1ms 中断内执行，每相 1ms，5 相共 5ms 完成一轮刷新
 *   - 各相真正驱动的引脚（括号内为高电平输出的段线，其余为该相公共端）：
 *       相 0：个位 D/E/F (SEG2/SEG3/SEG4)，公共端 SEG1 低；
 *             个位 G (SEG7)、十位 G (SEG5)，公共端 SEG6 低
 *       相 1：十位 D/E/F (SEG1/SEG3/SEG4)，公共端 SEG2 低
 *       相 2：个位 A/B/C (SEG1/SEG2/SEG4)，公共端 SEG3 低
 *       相 3：十位 A/B/C (SEG1/SEG2/SEG3) + 温度符号 ℃ (SEG9)，公共端 SEG4 低
 *       相 4：温区指示灯 R/G/B (SEG6/SEG8/SEG7)，公共端 SEG5 低；
 *             颜色由 display.value 选择：≥ TEMP_ZONE_HOT(39℃) 红（SEG6/PA6）；
 *             ≥ TEMP_ZONE_NORMAL(35℃) 绿（SEG8/PB3）；否则蓝（SEG7/PA7）
 *   - 相 0 同时使用 SEG1、SEG6 两个公共端，其余相位各只有 1 个公共端
 *   - 每相开始先把 9 根（SEG1~SEG9）引脚全部设为输入（高阻）释放上一相，避免残影
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
#define SEG_NULL                0x00        /* 全灭段码（当前未直接引用，GET_SEG() 对 >9 的输入也返回该值） */

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

static Display_t display;

/** @brief 关闭显示：9 根段/公共引脚全部设为输出并输出低电平（SEG1~SEG9） */
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

    display.temp_buf[0] = 0;                            /* 个位段码清零 */
    display.temp_buf[1] = 0;                            /* 十位段码清零 */
    display.temp_buf[2] = 0;                            /* 百位段码清零 */
    display.timer_buf[0] = 0;                           /* 个位段码清零 */
    display.timer_buf[1] = 0;                           /* 十位段码清零 */
    display.timer_buf[2] = 0;                           /* 百位段码清零 */
    display.timer_buf[3] = 0;                           /* 千位段码清零 */
    display.segment = 0;                                /* 扫描相清零 */
    display.temp = 0;                                   /* 当前水温清零 */
    display.timer = 0;                                  /* 当前计时清零 */
    display.led_cycle = 0;                              /* RGB 灯圈循环切换索引清零 */
    display.blink_cnt = 0;                              /* 闪烁计数清零 */
    display.blink_cont = BLINK_STATE_OFF;               /* 当前闪烁控制清零 */  
    display.battery_level = BATTERY_LEVEL_L0;           /* 当前电池电量清零 */
    display.battery_status = BATTERY_STATUS_NORMAL;     /* 当前电池状态清零 */      
    display.mode = DISPLAY_MODE_OFF;
}

/** @brief 更新显示数值
 *  @param value 待显示数值（TEMP：水温 ℃；INIT：灯色选择值）
 *  @note  数值与 display.value 相同时直接跳过（避免重复刷新）；
 *         TEMP 模式：反复减 10 拆出十位（避免除法），重算 buf[0] 个位与 buf[1] 十位
 *                     段码并清零扫描相（从第一相重新刷新）；
 *                     value ≥ 100 时 ten 会到 10，GET_SEG() 对 >9 返回全灭，
 *                     表现为十位熄灭（本工程有效显示范围 0~99）
 *         INIT / OFF 模式：只更新 display.value（INIT 供相 4 选择指示灯颜色），
 *                     段码保持不变（INIT 仍显示 88）
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
        while (temp >= 100u) { temp -= 100u; housand++; }               /* 再减出百位，余数即十位和个位（避免除法） */            
        while (temp >= 10u) { temp -= 10u; ten++; }                     /* 再减出十位，余数即个位（避免除法） */            
        display.temp_buf[0] = GET_SEG(temp);                            /* 个位段码 */
        display.temp_buf[1] = GET_SEG(ten);                             /* 十位段码 */
        display.temp_buf[2] = GET_SEG(housand);                         /* 百位段码 */
        display.segment = 0;                                            /* 扫描相清零，从第一相重新刷新 */
    }
    
    if (display.timer != timer) {
        uint8_t min_ten = 0, min = 0, sec_ten = 0;
        display.timer = timer;
        display.timer_buf[0] = 0;
        display.timer_buf[1] = 0;
        display.timer_buf[2] = 0;
        display.timer_buf[3] = 0;
        
        while (timer >= 600u) { timer -= 600u; min_ten++; }             /* 减出分钟十位，余数即分钟个位和秒（避免除法） */
        while (timer >= 60u) { timer -= 60u; min++; }                   /* 再减出分钟个位，余数秒（避免除法） */
        while (timer >= 10u) { timer -= 10u; sec_ten++; }               /* 再减出秒十位，余数即秒个位（避免除法） */
        display.timer_buf[0] = GET_SEG(timer);                          /* 秒个位段码 */
        display.timer_buf[1] = GET_SEG(sec_ten);                        /* 秒十位段码 */
        display.timer_buf[2] = GET_SEG(min);                            /* 分钟个位段码 */
        display.timer_buf[3] = GET_SEG(min_ten);                        /* 分钟十位段码 */
        display.segment = 0;                                            /* 扫描相清零，从第一相重新刷新 */
    }

    if (display.battery_level != battery_level) {
        display.battery_level = battery_level;
    }

    if (display.battery_status != battery_status) {
        display.battery_status = battery_status;
    }    
}

/** @brief 切换显示模式并按 value 刷新显示缓冲（模式相同时不动作）
 *  @param mode  DISPLAY_MODE_OFF / DISPLAY_MODE_INIT / DISPLAY_MODE_TEMP
 *  @param value INIT：灯色选择值（≥TEMP_ZONE_HOT 红/≥TEMP_ZONE_NORMAL 绿/否则蓝）；
 *               TEMP：初始水温（℃）
 *  @note  模式相同时直接返回，因此 INIT 期间换灯色需调用 Display_SetDigit()
 */
void Display_SetMode(Display_Mode_e mode, uint16_t temp, uint16_t timer, Battery_Level_e battery_level, Battery_Status_e battery_status)
{
    if (display.mode != mode) {
        display.mode = mode;
        display.temp = 0;
        display.timer = 0;
        display.temp_buf[0] = GET_SEG(0);                                /* 个位段码 */
        display.temp_buf[1] = GET_SEG(0);                                /* 十位段码 */
        display.temp_buf[2] = GET_SEG(0);                                /* 百位段码 */
        display.timer_buf[0] = GET_SEG(0);
        display.timer_buf[1] = GET_SEG(0);
        display.timer_buf[2] = GET_SEG(0);
        display.timer_buf[3] = GET_SEG(0);
        display.segment = 0;                                            /* 扫描相清零 */
        display.led_cycle = 0;                                          /* RGB 灯圈循环切换索引清零 */
        display.blink_cnt = 0;                                          /* 闪烁计数清零 */
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

/** @brief 动态扫描（Timer0 1ms 中断内调用）：按相序刷新段码 / 指示灯，无闪烁控制 */
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
    GPIO_SET_INPUT(LEDR_PORT, LEDR_PIN);
    GPIO_SET_INPUT(LEDG_PORT, LEDG_PIN);
    GPIO_SET_INPUT(LEDB_PORT, LEDB_PIN);

    display.blink_cnt++;
    if (display.blink_cnt >= 500) {
        display.blink_cnt = 0;
        switch (display.mode) {
            case DISPLAY_MODE_TIMER:
                display.blink_cont ^= BLINK_STATE_TFLG;
                break;
            case DISPLAY_MODE_TIMER_ALARM:
                display.blink_cont ^= BLINK_STATE_TFLG;
                display.led_cycle++;                                          /* RGB 灯圈循环切换索引 */
                if (display.led_cycle >= 18) {
                    display.led_cycle = 0;
                    display.mode = DISPLAY_MODE_TIMER;
                }
                break;
            case DISPLAY_MODE_INIT:
                display.led_cycle++;                                          /* RGB 灯圈循环切换索引 */
                if (display.led_cycle >= 6) {
                    display.led_cycle = 0;
                }
                break;
            default:
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
                break;
        }
    }

    switch (display.segment) 
	{
		case 0:
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
		case 1:          
            if ((display.battery_status & BLINK_STATE_TFLG) == 0) {
                GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);        // :
                GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);        // :
            }
            GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);            // ℃
            GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);            // ◴
            GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);            // dot

			GPIO_OUTPUT_LOW(SEG2_PORT, SEG2_PIN);
            GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);  			
			break;
		case 2:
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
		case 3:
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
		case 4:
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
        case 5:
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
        case 6:
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
        case 7:
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
        case 8:
            switch (display.battery_status) {
                case BATTERY_STATUS_LOW:
                    if (display.battery_status & BLINK_STATE_BAT_LOW) break;
                    GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);    // 绿灯
                    break;
                case BATTERY_STATUS_CHARGING:
                    if (display.battery_status & BLINK_STATE_CHARGING) break;
                    GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);    // 红灯
                    break;
                case BATTERY_STATUS_CHARGED:
                case BATTERY_STATUS_NORMAL:
                    GPIO_OUTPUT_HIGH(SEG7_PORT, SEG7_PIN); GPIO_SET_OUTPUT(SEG7_PORT, SEG7_PIN);    // 绿灯
                    break;    
                default:
                    break;
            }
			GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);            // 电池图标边框
            GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);            // 电池图标边框

            GPIO_OUTPUT_LOW(SEG9_PORT, SEG9_PIN);			
			GPIO_SET_OUTPUT(SEG9_PORT, SEG9_PIN); 			
			break;
        case 9:
            switch (display.battery_level)
            {
                case BATTERY_LEVEL_L5:
                    GPIO_OUTPUT_HIGH(SEG1_PORT, SEG1_PIN); GPIO_SET_OUTPUT(SEG1_PORT, SEG1_PIN);
                case BATTERY_LEVEL_L4:
                    GPIO_OUTPUT_HIGH(SEG2_PORT, SEG2_PIN); GPIO_SET_OUTPUT(SEG2_PORT, SEG2_PIN);
                case BATTERY_LEVEL_L3:
                    GPIO_OUTPUT_HIGH(SEG3_PORT, SEG3_PIN); GPIO_SET_OUTPUT(SEG3_PORT, SEG3_PIN);
                case BATTERY_LEVEL_L2:
                    GPIO_OUTPUT_HIGH(SEG4_PORT, SEG4_PIN); GPIO_SET_OUTPUT(SEG4_PORT, SEG4_PIN);
                case BATTERY_LEVEL_L1:
                    GPIO_OUTPUT_HIGH(SEG5_PORT, SEG5_PIN); GPIO_SET_OUTPUT(SEG5_PORT, SEG5_PIN);
                    break;
                default:
                    break;
            }
            GPIO_OUTPUT_HIGH(SEG6_PORT, SEG6_PIN); GPIO_SET_OUTPUT(SEG6_PORT, SEG6_PIN);            // 电池图标边框

            GPIO_OUTPUT_LOW(SEG10_PORT, SEG10_PIN);			
			GPIO_SET_OUTPUT(SEG10_PORT, SEG10_PIN); 			
			break;
        case 10:
            if (display.mode == DISPLAY_MODE_TIMER) {
                if (display.temp >= TEMP_ZONE_HOT) {
                    GPIO_OUTPUT_HIGH(LEDR_PORT, LEDR_PIN); GPIO_SET_OUTPUT(LEDR_PORT, LEDR_PIN);            // 红色灯圈
                } else {
                    if (display.temp >= TEMP_ZONE_NORMAL) {
                        GPIO_OUTPUT_HIGH(LEDG_PORT, LEDG_PIN); GPIO_SET_OUTPUT(LEDG_PORT, LEDG_PIN);        // 绿色灯圈
                    } else {
                        GPIO_OUTPUT_LOW(LEDB_PORT, LEDB_PIN); GPIO_SET_OUTPUT(LEDB_PORT, LEDB_PIN);         // 蓝色灯圈
                    }
                }
            }

            if ((display.mode == DISPLAY_MODE_TIMER_ALARM) || (display.mode == DISPLAY_MODE_INIT)) {
                switch (display.led_cycle % 3) {
                    case 0:
                        GPIO_OUTPUT_LOW(LEDB_PORT, LEDB_PIN); GPIO_SET_OUTPUT(LEDB_PORT, LEDB_PIN);         // 蓝色灯圈
                        break;
                    case 1:
                        GPIO_OUTPUT_HIGH(LEDG_PORT, LEDG_PIN); GPIO_SET_OUTPUT(LEDG_PORT, LEDG_PIN);        // 绿色灯圈
                        break;
                    case 2:
                        GPIO_OUTPUT_HIGH(LEDR_PORT, LEDR_PIN); GPIO_SET_OUTPUT(LEDR_PORT, LEDR_PIN);        // 红色灯圈
                        break;
                }
            }
            break;
	}

    display.segment++;                                  /* 推进到下一扫描相 */
    if (display.segment > 10) display.segment = 0;
}
