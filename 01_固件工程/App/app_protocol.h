/**
 * @file    app_protocol.h
 * @brief   应用层数据帧：组帧 / 解析（纯逻辑，不依赖任何硬件）
 *
 * 标签说明（贯穿整个工程）：
 *   [2023 实做]            —— 论文里有据的实现
 *   [2026 重建新增]        —— 本次重建时新增的内容
 *   [未经硬件回归验证]      —— 硬件已不在手，代码经静态审查 + PC 端单元测试，
 *                             但没有在真实开发板上跑过
 *
 * @note    [2026 重建新增] 本文件的「序号 + 时间戳 + CRC16」帧结构是重建时新增的。
 *          2023 年原版直接把解析出的温湿度拼成字符串经 RS485 透传，
 *          没有序号、没有 CRC、没有补传标记——这是当时最大的工程缺口。
 * @note    本文件只包含 <stdint.h>，不引用 STM32 任何头文件，
 *          因此可以在 PC 上用 gcc 独立编译并跑单元测试（见 ../tests/）。
 */
#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

/** 上行数据帧总长度（字节） */
#define APP_FRAME_LEN       20u

/** 帧头，用于在字节流里重新定位帧起点 */
#define APP_FRAME_HEAD0     0xAAu
#define APP_FRAME_HEAD1     0x55u

/** 帧格式版本号，便于后续升级时向后兼容 */
#define APP_FRAME_VER       0x01u

/** flags 位定义 */
#define APP_FLAG_RETRANSMIT 0x01u   /* 本帧来自本地缓存补传，而非实时采集 */
#define APP_FLAG_SENSOR_ERR 0x02u   /* 采集环节校验失败，温湿度字段无效 */

/**
 * @brief 一次采集得到的样本（应用层内部表示）
 *
 * 温度/湿度用「x10 的整数」表示，避免在 MCU 上引入浮点运算：
 *   temp_x10 = 265 表示 26.5 ℃
 *   humi_x10 = 510 表示 51.0 %RH
 */
typedef struct
{
    uint32_t seq;        /*!< 递增序号，断网补传与云端去重的关键 */
    uint32_t timestamp;  /*!< RTC 提供的 Unix 秒级时间戳 */
    int16_t  temp_x10;   /*!< 温度 x10，单位 0.1 ℃，有符号 */
    uint16_t humi_x10;   /*!< 湿度 x10，单位 0.1 %RH */
    uint8_t  flags;      /*!< APP_FLAG_xxx 的按位或 */
} app_sample_t;

/**
 * @brief CRC16/MODBUS 校验
 * @param data 数据首地址
 * @param len  参与计算的字节数
 * @return 16 位校验值
 */
uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

/**
 * @brief 把一条样本组装成 20 字节上行帧
 * @param s   待发送样本
 * @param out 输出缓冲区，长度必须 >= APP_FRAME_LEN
 * @return 0 成功，-1 参数为空
 *
 * 帧布局（大端）：
 *   [0]      0xAA          帧头
 *   [1]      0x55          帧头
 *   [2]      ver           版本号
 *   [3]      flags         标志位
 *   [4..7]   seq           序号
 *   [8..11]  timestamp     时间戳（Unix 秒）
 *   [12..13] temp_x10      温度 x10
 *   [14..15] humi_x10      湿度 x10
 *   [16..17] rsvd          保留（置 0）
 *   [18..19] crc16         前 18 字节的 CRC16/MODBUS
 */
int app_frame_build(const app_sample_t *s, uint8_t *out);

/**
 * @brief 从字节流解析出一帧
 * @param buf 输入缓冲区
 * @param len 输入长度
 * @param out 解析结果
 * @return 0 成功；-1 参数错误；-2 帧头不匹配；-3 长度不足；-4 CRC 校验失败
 */
int app_frame_parse(const uint8_t *buf, uint16_t len, app_sample_t *out);

/**
 * @brief 由 DHT11 解出的物理量生成应用层样本
 * @param temp_int DHT11 温度整数部分（已处理符号位）
 * @param temp_dec DHT11 温度小数部分
 * @param humi_int DHT11 湿度整数部分
 * @param humi_dec DHT11 湿度小数部分
 * @param seq      序号
 * @param ts       时间戳
 * @param flags    标志位
 * @param out      输出样本
 */
void app_sample_from_dht11(int8_t temp_int, uint8_t temp_dec,
                           uint8_t humi_int, uint8_t humi_dec,
                           uint32_t seq, uint32_t ts, uint8_t flags,
                           app_sample_t *out);

#endif /* APP_PROTOCOL_H */
