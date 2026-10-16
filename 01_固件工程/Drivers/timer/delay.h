/**
 * @file    delay.h
 * @brief   微秒/毫秒级延时（DWT 周期计数器实现）
 *
 * @tag     [2026 复盘补做]  标注原因：
 *          2023 年的原代码用的是正点原子例程里的 delay_us()，其原理是
 *          SysTick 轮询；这里改用 Cortex-M3 内核自带的 DWT 周期计数器。
 *          两者都能用，差别在于 DWT 不受 SysTick 被占用/被改写的影响
 *          —— 而 DHT11 的时序对延时精度极其敏感（26µs 与 70µs 要区分开）。
 *
 * @tag     [未经硬件回归验证]  本文件依赖 STM32 寄存器，无法在 PC 上验证。
 *          硬件不在手，因此没有在真实开发板上测过。
 *
 * @note    DHT11 为什么需要微秒级延时：
 *          每一个数据位靠「高电平持续多久」来区分 0 和 1——
 *          约 26µs 是 0，约 70µs 是 1。毫秒级延时根本分辨不出来。
 */
#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>

/**
 * @brief 初始化延时模块（使能 DWT 周期计数器）
 * @note  必须在 SystemInit() 之后、任何 delay_us() 调用之前执行一次。
 *        返回后应立刻自检一次（见 delay_selftest）。
 */
void delay_init(void);

/**
 * @brief 微秒级延时
 * @param us 延时长度（微秒）
 */
void delay_us(uint32_t us);

/**
 * @brief 毫秒级延时
 * @param ms 延时长度（毫秒）
 */
void delay_ms(uint32_t ms);

/**
 * @brief 用系统主频校准「每微秒周期数」
 * @param hz 系统主频（Hz），通常直接传 SystemCoreClock
 * @note  主频改了却忘了重新校准，会导致所有微秒延时整体偏一个系数，
 *        DHT11 会持续校验失败，表现得像「传感器坏了」。
 */
void delay_calibrate(uint32_t hz);

/**
 * @brief 延时精度自检
 * @param hz 系统主频（STM32F103 在 8MHz 晶振 x PLL9 下为 72000000）
 * @return 0 正常；-1 DWT 未使能（说明 delay_init 没调或内核不支持）
 * @note  这个自检是给「换一块板子/改一次时钟配置」用的：
 *        主频算错会导致所有微秒延时整体偏一个系数，
 *        DHT11 会持续校验失败而表现得像「传感器坏了」。
 */
int delay_selftest(uint32_t hz);

#endif /* DELAY_H */
