/**
 * @file    dht11.h
 * @brief   DHT11 温湿度传感器单总线驱动
 *
 * @tag     [2023 实做]  核心部分有据：
 *          - 单总线三线接法（VCC / GND / DATA）：论文 3.1.2 节
 *          - DATA 接 PG11：论文 3.6 节驱动头文件明确写了 GPIOG->CRH + PGout(11)
 *          - GPIO 输入/输出方向切换的两个宏：原样保留论文的寄存器写法
 *          - 40 位帧格式与校验和：论文 3.1.1 节
 *
 * @tag     [2026 重建新增]  两处改动：
 *          1. 新增 DHT11_Read_RawBits()：把「测时序」和「解数据」拆开。
 *             原实现把两者揉在 DHT11_Read_Data() 里，一旦读数出错，
 *             你无法判断是时序没测准还是解析写错了。拆开之后，
 *             解析部分（app_dht_decode.c）可以在 PC 上穷举测试。
 *          2. 读数增加「量程检查」，超量程时明确上报异常而不是照原样上传。
 *
 * @tag     [未经硬件回归验证]  依赖 GPIO 寄存器与时序，无法在 PC 上验证。
 *
 * @note    为什么用寄存器而不是 HAL 库操作 GPIO：
 *          单总线要求在几十微秒的尺度上切换引脚方向，
 *          寄存器操作是确定性的几条指令，而库函数调用链路长、耗时不确定。
 *          原论文的宏写法就是这个原因，保留不改。
 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

/* ------------------------------------------------------------------
 * IO 方向设置
 * 【来源】驱动宏与论文 3.6 节一致，原样保留：
 *   GPIOG->CRH 控制 PG8~PG15，每个引脚占 4 位，PG11 对应 bits[15:12]。
 *     0x8 = 1000b -> CNF=10（上下拉输入）、MODE=00（输入模式）
 *     0x3 = 0011b -> CNF=00（通用推挽输出）、MODE=11（输出 50MHz）
 * 先清掉 PG11 那 4 位（& 0xFFFF0FFF）再写入新配置，避免影响同一寄存器里
 * 其它引脚（PG8~PG10、PG12~PG15）的配置。
 * ------------------------------------------------------------------ */
#define DHT11_IO_IN()   do { GPIOG->CRH &= 0xFFFF0FFFu; GPIOG->CRH |= 0x8u << 12; } while (0)
#define DHT11_IO_OUT()  do { GPIOG->CRH &= 0xFFFF0FFFu; GPIOG->CRH |= 0x3u << 12; } while (0)

/* 数据端口 PG11 的读/写。PGout()/PGin() 来自正点原子的 sys.h；
 * 若移植到别的板子，直接换成对应的位带操作或 BSRR 即可。 */
#define DHT11_DQ_OUT    PGout(11)
#define DHT11_DQ_IN     PGin(11)

/* 返回值约定（与论文保持一致） */
#define DHT11_OK        0u
#define DHT11_ERR       1u      /* 传感器无响应 */
#define DHT11_ERR_CRC   2u      /* 校验和错误 */
#define DHT11_ERR_RANGE 3u      /* 读数超出官方量程 [2026 重建新增] */

/**
 * @brief 初始化 DHT11 的数据引脚
 * @return DHT11_OK / DHT11_ERR
 */
uint8_t DHT11_Init(void);

/**
 * @brief 复位（发送起始信号）并等待 DHT11 响应
 * @return DHT11_OK / DHT11_ERR
 *
 * 时序：主机拉低总线 >= 18ms -> 释放并切成输入 -> 等待
 *       DHT11 回 80µs 低 + 80µs 高。
 */
uint8_t DHT11_Check(void);

/**
 * @brief 发送起始信号
 */
void DHT11_Rst(void);

/**
 * @brief 读一个位
 * @return 0 或 1；读超时返回 0xFF（调用方应据此判定失败）
 *
 * 判据：每位的开始都是 50µs 低电平，之后
 *       约 26µs 高电平表示 0，约 70µs 高电平表示 1。
 */
uint8_t DHT11_Read_Bit(void);

/**
 * @brief 读一个字节（8 位，高位先出）
 */
uint8_t DHT11_Read_Byte(void);

/**
 * @brief [2026 重建新增] 读出 40 位原始位流，解析交给上层
 * @param bits40 输出缓冲，长度必须 >= 40，元素为 0/1
 * @return DHT11_OK / DHT11_ERR
 *
 * 把「测量」与「解析」分开的好处：拿到 40 位原始数据后，
 * 既可以用 PC 上的 app_dht_decode() 解析并校验，
 * 也可以在读数异常时把原始位流打印出来，直接看出是哪一位错了。
 */
uint8_t DHT11_Read_RawBits(uint8_t *bits40);

/**
 * @brief 读一次温湿度（兼容论文原接口）
 * @param temp 输出温度，单位 ℃（整数，DHT11 分辨率就是 1℃）
 * @param humi 输出湿度，单位 %RH（整数）
 * @return DHT11_OK / DHT11_ERR / DHT11_ERR_CRC / DHT11_ERR_RANGE
 */
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);

#endif /* DHT11_H */
