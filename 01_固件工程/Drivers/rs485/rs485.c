/**
 * @file    rs485.c
 * @brief   RS485 半双工收发方向控制
 * @tag     [未经硬件回归验证]
 */
#include "rs485.h"

#include "stm32f10x.h"
#include "uart.h"       /* 需要用到底层串口的 TC 标志与寄存器 */

void RS485_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    gpio.GPIO_Pin   = RS485_RE_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_Init(RS485_RE_PORT, &gpio);

    RS485_SetRx();      /* 上电默认接收态 */
}

void RS485_SetTx(void)
{
    GPIO_SetBits(RS485_RE_PORT, RS485_RE_PIN);      /* DE 高 = 发送 */
}

void RS485_SetRx(void)
{
    GPIO_ResetBits(RS485_RE_PORT, RS485_RE_PIN);    /* DE 低 = 接收 */
}

void RS485_SendBuf(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((data == 0) || (len == 0u))
    {
        return;
    }

    RS485_SetTx();

    for (i = 0u; i < len; i++)
    {
        uart_send_byte_blocking(data[i]);
    }

    /* 关键：等最后一个字节真正移位完成（TC=1）再切回接收。
     * 只等 TXE 就切回，会把最后一个字节的尾部吃掉——
     * 表现为「云端偶尔收到半截数据」，而且是概率性出现，非常难查。 */
    uart_wait_tx_complete();

    RS485_SetRx();
}
