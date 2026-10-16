/**
 * @file    uart.h
 * @brief   串口（USART2）收发与「按帧接收」机制
 *
 * @tag     [2023 实做]  串口在论文里是核心：
 *          与 ATK-IDM751C 的全部交互都走串口（经 RS485 电平）。
 *          论文 3.6 节用到的接口名与这里一一对应（见文件末尾的别名表）。
 *
 * @tag     [2026 复盘补做]  两个改进：
 *          1. 用「串口空闲中断（IDLE）」切帧，而不是靠软件延时猜帧尾。
 *             原来靠 delay 判断「这帧应该收完了」，波特率一变或
 *             模块响应稍慢就会切错帧。
 *          2. 加一层环形接收缓冲，中断里只管塞字节，
 *             上层解析与中断彻底解耦，不会因为处理慢而丢字节。
 *
 * @tag     [未经硬件回归验证]
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

/** 接收环形缓冲大小（字节）。定 256 是因为 AT 指令应答最长的也就几十字节，
 *  留 4 倍余量足够吸收一次突发。 */
#define UART_RX_RING_SIZE   256u

/** 单帧缓冲的最大长度（超过则截断，防止畸形数据把内存写穿） */
#define UART_FRAME_MAX      128u

/**
 * @brief 初始化 USART2（对应战舰板的 RS485 电平侧）
 * @param baudrate 波特率，ATK-IDM751C 常用 115200
 * @note  同时会初始化 RX 环形缓冲与 IDLE 中断。
 */
void uart_init(uint32_t baudrate);

/** @brief 阻塞发送一个字节 */
void uart_send_byte_blocking(uint8_t b);

/**
 * @brief 阻塞发送一块数据
 * @param data 数据首地址
 * @param len  长度
 */
void uart_send_buf(const uint8_t *data, uint16_t len);

/**
 * @brief 等待最后一个字节真正发送完成（TC 标志）
 * @note  RS485 半双工切方向之前必须调用，否则会截断尾部字节。
 */
void uart_wait_tx_complete(void);

/**
 * @brief 格式化打印到串口（类似 printf，内部用 vsnprintf 封一层）
 * @return 实际发送的字节数
 */
int uart_printf(const char *fmt, ...);

/**
 * @brief 清空接收缓冲，并丢弃当前半截帧
 * @note  发 AT 指令之前调用它，保证接下来收到的数据一定是这条指令的应答，
 *        而不是上一条命令的残留回声 —— 这是 AT 交互可靠性的前提。
 */
void uart_rx_restart(void);

/**
 * @brief 取出最近收到的一「帧」数据
 * @return 指向帧缓冲的指针（以 '\0' 结尾，可直接当字符串用）；
 *         若自上次调用以来没有收到完整帧，返回 0
 *
 * 切帧依据：串口空闲（IDLE）中断，即总线静默超过一个字节时间即认为帧结束。
 * 这与 AT 指令「一问一答、应答之间有间隔」的通信习惯天然契合。
 */
uint8_t *uart_rx_get_frame(void);

/** @return 接收环形缓冲里当前积压的字节数（调试用） */
uint16_t uart_rx_pending(void);

/* ------------------------------------------------------------------
 * 与论文 3.6 节的接口对照
 *   论文里写的是 atk_idm751c_uart_init / _rx_restart / _printf / _rx_get_frame，
 *   这些在当年是 dt 模块内部的一组静态函数。本工程把它们独立成
 *   uart_* 层（职责更清楚），在 atk_idm751c.c 顶部用宏做了别名映射，
 *   因此对照论文阅读时函数名仍然能一一对上。
 * ------------------------------------------------------------------ */

#endif /* UART_H */
