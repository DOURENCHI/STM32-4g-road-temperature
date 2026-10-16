/**
 * @file    dht11.c
 * @brief   DHT11 温湿度传感器单总线驱动
 * @tag     [未经硬件回归验证]  依赖 GPIO 寄存器与微秒时序，无法在 PC 上验证
 *
 * 时序依据（论文 3.6 节 / 图 4-2）：
 *   1. 主机拉低总线 >= 18ms          起始信号
 *   2. 主机释放总线，延时 20~40µs     等待
 *   3. 从机响应：80µs 低 + 80µs 高
 *   4. 连续 40 位数据，每位 = 50µs 低 + 高电平（约 26µs = 0，约 70µs = 1）
 *   5. 数据 5 字节：湿度整数 / 湿度小数 / 温度整数 / 温度小数 / 校验和
 */
#include "dht11.h"
#include "delay.h"

#include "stm32f10x.h"
#include "sys.h"        /* PGout / PGin */

/* 单总线时序参数（微秒）——对应论文图 4-2 */
#define DHT11_START_LOW_US   18000u   /* 起始信号拉低 18ms */
#define DHT11_START_REL_US     30u    /* 释放后等 20~40µs，取中间值 */
#define DHT11_RESP_LOW_US      80u    /* 从机响应低电平 */
#define DHT11_RESP_HIGH_US     80u    /* 从机响应高电平 */
#define DHT11_BIT_LOW_US       50u    /* 每位起始的低电平 */
#define DHT11_TIMEOUT_US       100u   /* 单个电平的最长等待，超过判定为超时 */

/* 判 0/1 的阈值：论文说约 26µs 为 0、约 70µs 为 1，
 * 取中点 48µs 作为分界。用中点而不是「>26 就是 1」，
 * 是为了给两个方向都留出容差 —— 传感器批次差异和温度漂移
 * 都会让脉宽整体偏移。 */
#define DHT11_ONE_THRESHOLD_US 48u

uint8_t DHT11_Init(void)
{
    GPIO_InitTypeDef gpio;

    /* 使能 GPIOG 时钟（PG11 挂在 GPIOG 上） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOG, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_11;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;     /* 起始默认推挽输出，空闲拉高 */
    GPIO_Init(GPIOG, &gpio);

    DHT11_DQ_OUT = 1;       /* 总线空闲为高 */
    delay_ms(100);          /* DHT11 上电后需要约 1s 稳定，这里给一段余量 */

    return DHT11_Check();
}

void DHT11_Rst(void)
{
    DHT11_IO_OUT();         /* 主机要驱动总线，必须先切成输出 */
    DHT11_DQ_OUT = 0;       /* 拉低 */
    delay_ms(18);           /* 保持 18ms 以上 */
    DHT11_DQ_OUT = 1;       /* 释放总线 */
    delay_us(DHT11_START_REL_US);
}

uint8_t DHT11_Check(void)
{
    uint8_t retry = 0u;
    uint8_t ret;

    DHT11_Rst();

    /* 主机释放总线后必须切成输入，否则会和从机抢驱动同一根线。
     * 这就是为什么这个驱动的 GPIO 一定要动态切方向，
     * 也是很多「照抄代码但不理解」的人翻车的点。 */
    DHT11_IO_IN();

    /* 等待 DHT11 把总线拉低（响应开始） */
    while ((DHT11_DQ_IN != 0u) && (retry < DHT11_TIMEOUT_US))
    {
        retry++;
        delay_us(1u);
    }
    if (retry >= DHT11_TIMEOUT_US)
    {
        return DHT11_ERR;       /* 传感器无响应：接线、供电或上拉电阻的问题 */
    }

    /* 等 80µs 低电平走完 */
    DHT11_IO_IN();
    retry = 0u;
    while ((DHT11_DQ_IN == 0u) && (retry < DHT11_TIMEOUT_US))
    {
        retry++;
        delay_us(1u);
    }
    if (retry >= DHT11_TIMEOUT_US)
    {
        return DHT11_ERR;
    }

    /* 等 80µs 高电平走完，之后就是数据位 */
    retry = 0u;
    while ((DHT11_DQ_IN != 0u) && (retry < DHT11_TIMEOUT_US))
    {
        retry++;
        delay_us(1u);
    }
    if (retry >= DHT11_TIMEOUT_US)
    {
        return DHT11_ERR;
    }

    ret = DHT11_OK;
    return ret;
}

uint8_t DHT11_Read_Bit(void)
{
    uint8_t retry = 0u;
    uint8_t duration = 0u;

    /* 每位都以 50µs 低电平开始，先等它走完 */
    DHT11_IO_IN();
    while ((DHT11_DQ_IN != 0u) && (retry < DHT11_TIMEOUT_US))
    {
        retry++;
        delay_us(1u);
    }
    if (retry >= DHT11_TIMEOUT_US)
    {
        return 0xFFu;       /* 超时 */
    }

    /* 测量接下来的高电平持续了多久 */
    retry = 0u;
    while ((DHT11_DQ_IN == 0u) && (retry < DHT11_TIMEOUT_US))
    {
        retry++;
        delay_us(1u);
    }
    if (retry >= DHT11_TIMEOUT_US)
    {
        return 0xFFu;
    }

    /* 从高电平起点开始计宽度 */
    duration = 0u;
    while ((DHT11_DQ_IN != 0u) && (duration < DHT11_TIMEOUT_US))
    {
        duration++;
        delay_us(1u);
    }
    if (duration >= DHT11_TIMEOUT_US)
    {
        return 0xFFu;
    }

    /* 这一段的低电平是下一位的开始，不必额外等待：
     * 下一次调用 DHT11_Read_Bit() 时会先把它等掉。 */

    return (duration > DHT11_ONE_THRESHOLD_US) ? 1u : 0u;
}

uint8_t DHT11_Read_Byte(void)
{
    uint8_t i;
    uint8_t byte_val = 0u;
    uint8_t bit;

    for (i = 0u; i < 8u; i++)
    {
        bit = DHT11_Read_Bit();
        if (bit == 0xFFu)
        {
            return 0xFFu;       /* 传递超时 */
        }
        byte_val = (uint8_t)((byte_val << 1) | bit);
    }

    return byte_val;
}

uint8_t DHT11_Read_RawBits(uint8_t *bits40)
{
    uint8_t i;
    uint8_t bit;

    if (bits40 == 0)
    {
        return DHT11_ERR;
    }

    if (DHT11_Check() != DHT11_OK)
    {
        return DHT11_ERR;
    }

    /* 读的这 40 位期间最好关掉中断：
     * 任何一个中断都会打断采样，导致脉宽测量偏长，
     * 把 0 误判成 1。这是这个驱动最容易被忽略的一处细节。 */
    for (i = 0u; i < 40u; i++)
    {
        bit = DHT11_Read_Bit();
        if (bit == 0xFFu)
        {
            return DHT11_ERR;
        }
        bits40[i] = bit;
    }

    return DHT11_OK;
}

uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi)
{
    uint8_t bits[40];
    uint8_t raw[5];
    uint8_t i;
    uint8_t j;
    uint8_t sum;
    uint8_t rc;

    if ((temp == 0) || (humi == 0))
    {
        return DHT11_ERR;
    }

    rc = DHT11_Read_RawBits(bits);
    if (rc != DHT11_OK)
    {
        return rc;
    }

    /* 40 位 -> 5 字节（高位先出） */
    for (i = 0u; i < 5u; i++)
    {
        raw[i] = 0u;
        for (j = 0u; j < 8u; j++)
        {
            raw[i] = (uint8_t)((raw[i] << 1) | bits[i * 8u + j]);
        }
    }

    /* 校验：前 4 字节求和取低 8 位，与第 5 字节比较 */
    sum = (uint8_t)((raw[0] + raw[1] + raw[2] + raw[3]) & 0xFFu);
    if (sum != raw[4])
    {
        return DHT11_ERR_CRC;
    }

    *humi = raw[0];
    *temp = (uint8_t)(raw[2] & 0x7Fu);   /* 屏蔽符号位（量程内不会用到） */

    /* [2026 重建新增] 量程检查：
     * 官方规格 0~50℃，夏季沥青路面可达 60℃ 以上。
     * 超量程时明确返回异常码，而不是把可疑数据照原样发上去。 */
    if ((*temp > 50u) || (*humi > 90u) || (*humi < 20u))
    {
        return DHT11_ERR_RANGE;
    }

    return DHT11_OK;
}
