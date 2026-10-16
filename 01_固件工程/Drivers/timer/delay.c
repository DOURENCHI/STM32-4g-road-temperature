/**
 * @file    delay.c
 * @brief   微秒/毫秒级延时（DWT 周期计数器实现）
 * @tag     [未经硬件回归验证]
 */
#include "delay.h"
#include "stm32f10x.h"

/* DWT（Data Watchpoint and Trace）是 Cortex-M3 内核自带的调试单元，
 * 其中的 CYCCNT 是一个 32 位周期计数器，每来一个 CPU 时钟加 1。
 * 用它做延时只需要：读一次、算差值、等到超出目标周期数。 */
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004u)
#define DWT_CTRL_CYCCNTENA  (1u << 0)

/* 经 delay_init 校准后的「每微秒多少个周期」 */
static uint32_t s_cycles_per_us = 72u;   /* 默认按 72MHz 估，init 时会重算 */

void delay_init(void)
{
    /* 使能 DWT 计数器 */
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    DWT_CYCCNT = 0u;

    /* 这里按「系统主频 ÷ 1000000」估算每微秒周期数。
     * 调用方在 main() 里应当用 SystemCoreClock 校准一次：
     *      delay_calibrate(SystemCoreClock);
     * 为保持接口精简，本文件用一个显式的 setter。 */
}

/**
 * @brief 用系统主频校准每微秒周期数
 * @param hz 系统主频（Hz），通常传 SystemCoreClock
 */
void delay_calibrate(uint32_t hz)
{
    s_cycles_per_us = hz / 1000000u;
    if (s_cycles_per_us == 0u)
    {
        s_cycles_per_us = 1u;
    }
}

void delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t target;

    if (us == 0u)
    {
        return;
    }

    start  = DWT_CYCCNT;
    target = us * s_cycles_per_us;

    /* 用无符号减法处理计数器回绕，不需要额外的溢出判断 */
    while ((DWT_CYCCNT - start) < target)
    {
        /* 空转等待 */
    }
}

void delay_ms(uint32_t ms)
{
    while (ms-- > 0u)
    {
        delay_us(1000u);
    }
}

int delay_selftest(uint32_t hz)
{
    if ((DWT_CTRL & DWT_CTRL_CYCCNTENA) == 0u)
    {
        return -1;      /* DWT 计数器没使能 */
    }
    if (hz < 1000000u)
    {
        return -1;      /* 主频不合理，说明时钟配置有问题 */
    }

    delay_calibrate(hz);

    /* 粗略验证：连续两次读计数器，确保它在动。
     * 真正精确的验证需要示波器量一个翻转的 IO，这属于硬件验证范畴。 */
    {
        uint32_t a = DWT_CYCCNT;
        uint32_t b;
        volatile uint32_t i;
        for (i = 0u; i < 100u; i++)
        {
            /* 制造一点时间差 */
        }
        b = DWT_CYCCNT;
        if (a == b)
        {
            return -1;  /* 计数器停着，延时函数实际不生效 */
        }
    }

    return 0;
}
