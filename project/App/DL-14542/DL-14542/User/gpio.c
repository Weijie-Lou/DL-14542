/**
 * @file    gpio.c
 * @brief   GPIO 初始化与引脚属性/模拟功能配置（NY8BM84A，PORTA/PORTB/PORTC）
 * @note
 *   - 方向寄存器 IOSTx（地址 = PORTx + 0x80）：位 = 1 输入，位 = 0 输出
 *   - 引脚属性寄存器 PORTxCON30/PORTxCON74：每引脚 2 位（输入 PH/PL，输出 PP/OD）
 *   - 模拟功能由 PxCON 控制（ADCR[7:6] 选择 PxCON 映射到哪个端口），位 = 1 为纯模拟引脚
 */

 #include "gpio.h"

 /* 上电默认状态：PORTA/PORTB/PORTC 全部输出低、方向寄存器全部设为输出，
  * 引脚属性 = 输入浮动 / 输出推挽（无上下拉）；
  * 其余外设所需引脚由各自初始化函数配置（如 PA2 = NTC 输入、PA1 = NTC 供电使能） */
 void GPIO_Init(void) {
     PORTA = 0x00; /* PORTA 全部输出低 */
     PORTB = 0x00; /* PORTB 全部输出低 */
     PORTC = 0x00; /* PORTC 全部输出低 */
     IOSTA = 0x00; /* PORTA 全部设为输出（位 0 = 输出） */
     IOSTB = 0x00; /* PORTB 全部设为输出 */
     IOSTC = 0x00; /* PORTC 全部设为输出 */

     PORTACON30 = 0x00; /* PORTA0-3：输入浮动 / 输出推挽 */
     PORTACON74 = 0x00; /* PORTA4-7：输入浮动 / 输出推挽 */
     PORTBCON30 = 0x00; /* PORTB0-3：输入浮动 / 输出推挽 */
     PORTBCON74 = 0x00; /* PORTB4-7：输入浮动 / 输出推挽 */
     PORTCCON30 = 0x00; /* PORTC0-3：输入浮动 / 输出推挽 */
     PORTCCON74 = 0x00; /* PORTC4-7：输入浮动 / 输出推挽 */
}

/*============================================================================
 * 内部辅助：识别寄存器对应的端口
 *==========================================================================*/
static GPIO_Port GPIO_ResolvePort(const volatile __rpage unsigned char *port)
{
    if (port == &PORTA) 
        return GPIO_PORTA;
    if (port == &PORTB) 
        return GPIO_PORTB;
    return GPIO_PORTC;
}

/*============================================================================
 * 内部辅助：返回引脚对应的 PORTxCON 寄存器（pin0-3 → CON30，pin4-7 → CON74）
 *==========================================================================*/
static volatile __rpage unsigned char *GPIO_GetConReg(GPIO_Port port, GPIO_Pin pin)
{
    if (port == GPIO_PORTA) {
        return (pin < GPIO_PIN_4) ? &PORTACON30 : &PORTACON74;
    }
    if (port == GPIO_PORTB) {
        return (pin < GPIO_PIN_4) ? &PORTBCON30 : &PORTBCON74;
    }
    return (pin < GPIO_PIN_4) ? &PORTCCON30 : &PORTCCON74;
}

/* 设置引脚上下拉：操作 PORTxCON30/74 中该引脚的 2 位（输入 PH/PL，输出 PP/OD） */
void GPIO_SET_PULL_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, GPIO_Pull pull)
{
    GPIO_Port p = GPIO_ResolvePort(port);
    volatile __rpage unsigned char *reg = GPIO_GetConReg(p, pin);
    unsigned char shift = (unsigned char)(((unsigned char)pin & 0x03u) * 2u);

    *reg = (unsigned char)((*reg & (unsigned char)~(0x03u << shift))
                           | ((unsigned char)pull << shift));
}

/* 设置引脚唤醒使能：PORTA 用 AWUCON，PORTB/PORTC 共用 BCDWUCON（SELCON[4:3] 选择映射） */
void GPIO_SET_WAKEUP_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, unsigned char enable)
{
    GPIO_Port p = GPIO_ResolvePort(port);
    unsigned char mask = (unsigned char)(1u << pin);

    if (p == GPIO_PORTA) {
        /* PORTA 唤醒由 AWUCON 直接控制 */
        AWUCON = (unsigned char)(enable ? (AWUCON | mask)
                                       : (AWUCON & (unsigned char)~mask));
    } else {
        /* PORTB/PORTC 共用 BCDWUCON，由 SELCON[4:3] 选择映射 */
        SELCON = (unsigned char)((SELCON & (unsigned char)~(0x18u))
                                 | ((p == GPIO_PORTC) ? C_PCWUCON_SEL : C_PBWUCON_SEL));
        BCDWUCON = (unsigned char)(enable ? (BCDWUCON | mask)
                                         : (BCDWUCON & (unsigned char)~mask));
    }
}
/* 模拟/数字切换：先用 ADCR[7:6] 选定端口对应的 PxCON，再改该引脚的模拟位（1 = 纯模拟） */

void GPIO_SET_ANALOG_IMPL(volatile __rpage unsigned char *port, GPIO_Pin pin, unsigned char enable)
{
    GPIO_Port p = GPIO_ResolvePort(port);    
    /* ADCR[7:6] 选择 PxCON 映射到的端口 */
    switch (p) {        
        case GPIO_PORTA: 
            ADCR = (unsigned char)((ADCR & 0x4Fu) | C_Select_PACON); 
            break;
        case GPIO_PORTB: 
            ADCR = (unsigned char)((ADCR & 0x4Fu) | C_Select_PBCON); 
            break;
        case GPIO_PORTC: 
            ADCR = (unsigned char)((ADCR & 0x4Fu) | C_Select_PCCON); 
            break;
        default:
            break;
    }
    
    /* PxCON 对应位：1 = 纯模拟引脚 */
    if (enable)
        PxCON = (unsigned char)(PxCON | (1u << pin));
    else
        PxCON = (unsigned char)(PxCON & (unsigned char)~(1u << pin));
}
