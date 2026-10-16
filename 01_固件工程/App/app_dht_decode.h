/**
 * @file    app_dht_decode.h
 * @brief   DHT11 40 位帧的解析与校验（纯逻辑，不依赖任何硬件）
 *
 * @note    [2023 实做] 40 位帧格式、校验和规则在论文 3.1.1 与驱动头文件里有据。
 * @note    [2026 复盘补做] 把「时序测量」与「位流解析」拆开——原来两者揉在
 *          DHT11_Read_Data() 里，读错时无法判断是时序问题还是解析问题。
 *          拆开后，解析部分可以在 PC 上穷举测试。
 * @note    本文件只包含 <stdint.h>，可在 PC 上独立编译与单元测试。
 */
#ifndef APP_DHT_DECODE_H
#define APP_DHT_DECODE_H

#include <stdint.h>

/** DHT11 一次传输的位数 */
#define DHT11_FRAME_BITS    40u
/** DHT11 一次传输的字节数 = 40 / 8 */
#define DHT11_FRAME_BYTES   5u

/** 原始 5 字节帧：湿度整数 / 湿度小数 / 温度整数 / 温度小数 / 校验和 */
typedef struct
{
    uint8_t humi_int;
    uint8_t humi_dec;
    uint8_t temp_int;
    uint8_t temp_dec;
    uint8_t checksum;
} dht11_frame_t;

/** 校验通过后换算出的物理量（温度最高位为符号位） */
typedef struct
{
    int8_t  temp_int;    /*!< 温度整数部分，可为负 */
    uint8_t temp_dec;    /*!< 温度小数部分（DHT11 恒为 0） */
    uint8_t humi_int;    /*!< 湿度整数部分 */
    uint8_t humi_dec;    /*!< 湿度小数部分（DHT11 恒为 0） */
} dht11_value_t;

/**
 * @brief 校验一帧原始数据是否有效
 * @param raw 5 字节原始帧
 * @return 1 有效，0 无效，-1 指针为空
 *
 * 规则：前 4 字节相加取低 8 位，与第 5 字节（校验和）比较。
 */
int dht11_checksum_ok(const uint8_t *raw);

/**
 * @brief 把 40 个「位」打包成 5 个字节
 * @param bits 40 个元素，每个元素必须是 0 或 1
 * @param raw  输出 5 字节
 * @return 0 成功，-1 指针为空，-2 出现非 0/1 的非法位
 *
 * 位序：高位在前（MSB first），与 DHT11 单总线时序一致。
 */
int dht11_bits_to_bytes(const uint8_t *bits, uint8_t *raw);

/**
 * @brief 从 40 位位流直接解出一帧
 * @param bits 40 个位
 * @param out  输出帧
 * @return 0 成功，-1 指针为空，-2 非法位，-3 校验和错误
 */
int dht11_decode(const uint8_t *bits, dht11_frame_t *out);

/**
 * @brief 帧 -> 物理量（处理温度符号位）
 * @param frame 已通过校验的帧
 * @param value 输出物理量
 */
void dht11_frame_to_value(const dht11_frame_t *frame, dht11_value_t *value);

/**
 * @brief 按官方规格判断读数是否落在量程内
 * @param value 物理量
 * @return 0 在量程内；1 温度超量程；2 湿度超量程；3 两者都超
 *
 * 官方规格（论文表 3-2 第 9 条）：温度 0~50 ℃，湿度 20~90 %RH。
 * 这个函数存在的意义：把「量程边界」变成代码里的显式判断，
 * 而不是等面试官问「夏天沥青路面 60 ℃ 你怎么办」才发现没想过。
 */
int dht11_out_of_range(const dht11_value_t *value);

#endif /* APP_DHT_DECODE_H */
