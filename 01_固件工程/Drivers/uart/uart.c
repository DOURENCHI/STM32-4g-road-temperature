/**
 * @file    uart.c
 * @brief   串口（USART2）收发与「按帧接收」机制
 * @tag     [未经硬件回归验证]
 *
 * 为什么用 USART2：战舰板上 RS485 与 RS232(COM2) 共用 USART2（PA2/PA3），
 * 由 P7 跳线选择接到哪一侧。本项目把它接到 RS485 侧，与 ATK-IDM751C 通信。
 */
#include "uart.h"

#include <stdarg.h>
#include <stdio.h>

#include "stm32f10x.h"

/* 接收环形缓冲：中断里只管往这里塞，上层慢慢取 */
static volatile uint8_t  s_ring[UART_RX_RING_SIZE];
static volatile uint16_t s_ring_head = 0u;
static volatile uint16_t s_ring_tail = 0u;

/* 切好的一帧 */
static volatile uint8_t  s_frame[UART_FRAME_MAX];
static volatile uint16_t s_frame_len   = 0u;
static volatile uint8_t  s_frame_ready = 0u;

/* 中断接收字节 */
static void rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_ring_head + 1u) % UART_RX_RING_SIZE);

    if (next == s_ring_tail)
    {
        return;     /* 缓冲满，丢弃这个字节。上层处理太慢才会走到这里 */
    }

    s_ring[s_ring_head] = b;
    s_ring_head = next;
}

/* 把环形缓冲里的积压搬成一帧（在 IDLE 中断里调用） */
static void frame_capture(void)
{
    uint16_t n = 0u;
    uint16_t h = s_ring_head;
    uint16_t t = s_ring_tail;

    while ((t != h) && (n < (UART_FRAME_MAX - 1u)))
    {
        s_frame[n] = s_ring[t];
        n++;
        t = (uint16_t)((t + 1u) % UART_RX_RING_SIZE);
    }
    s_frame[n]   = 0u;      /* 方便直接当字符串匹配 */
    s_frame_len  = n;
    s_frame_ready = (n > 0u) ? 1u : 0u;

    s_ring_tail = h;        /* 已搬走 */
}

void uart_init(uint32_t baudrate)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    /* PA2 作为 USART2_TX（复用推挽），PA3 作为 USART2_RX（浮空输入） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = baudrate;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);

    /* 空闲中断（IDLE）用来判定「一帧收完了」，
     * 比软件延时猜帧尾可靠得多 */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority        = 1u;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART2, ENABLE);

    uart_rx_restart();
}

void uart_send_byte_blocking(uint8_t b)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
    {
        /* 等发送数据寄存器空 */
    }
    USART_SendData(USART2, (uint16_t)b);
}

void uart_wait_tx_complete(void)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
    {
        /* 等最后一个字节移位完成 */
    }
}

void uart_send_buf(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (data == 0)
    {
        return;
    }

    for (i = 0u; i < len; i++)
    {
        uart_send_byte_blocking(data[i]);
    }
}

int uart_printf(const char *fmt, ...)
{
    char    buf[128];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return -1;
    }
    if (n > (int)sizeof(buf) - 1)
    {
        n = (int)sizeof(buf) - 1;   /* 被截断了 */
    }

    uart_send_buf((const uint8_t *)buf, (uint16_t)n);

    return n;
}

void uart_rx_restart(void)
{
    s_ring_tail   = s_ring_head;    /* 丢掉积压 */
    s_frame_len   = 0u;
    s_frame_ready = 0u;
}

uint8_t *uart_rx_get_frame(void)
{
    if (s_frame_ready == 0u)
    {
        return 0;
    }

    s_frame_ready = 0u;     /* 取走即消费，避免同一帧被匹配两次 */
    return (uint8_t *)s_frame;
}

uint16_t uart_rx_pending(void)
{
    uint16_t h = s_ring_head;
    uint16_t t = s_ring_tail;

    if (h >= t)
    {
        return (uint16_t)(h - t);
    }
    return (uint16_t)(UART_RX_RING_SIZE - t + h);
}

/**
 * @brief USART2 中断服务程序
 *
 * 两件事：
 *   RXNE —— 收到一个字节，塞进环形缓冲
 *   IDLE —— 总线静默，说明这一帧到头了，把缓冲搬成一帧
 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        rx_push((uint8_t)(USART_ReceiveData(USART2) & 0xFFu));
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }

    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* 清 IDLE 标志的固定套路：先读 SR，再读 DR */
        (void)USART2->SR;
        (void)USART2->DR;
        frame_capture();
    }
}
