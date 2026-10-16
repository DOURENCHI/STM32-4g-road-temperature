/**
 * @file    app_protocol.c
 * @brief   应用层数据帧：组帧 / 解析（纯逻辑，不依赖任何硬件）
 * @tag     [2026 重建新增] 序号 + 时间戳 + CRC16 的帧结构
 * @note    可独立编译：gcc -c app_protocol.c
 */
#include "app_protocol.h"

/* ---------- 内部小工具：大端读写 ---------- */
static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ---------- CRC16/MODBUS ----------
 * 多项式 0x8005（反射写法 0xA001），初值 0xFFFF。
 * 选它是因为它在工业仪表里最常见：将来如果换成 Modbus RTU 电表/气象站，
 * 校验算法不用换。
 */
uint16_t crc16_modbus(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  b;

    if (data == 0)
    {
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 0x0001u) != 0u)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/* ---------- 组帧 ---------- */
int app_frame_build(const app_sample_t *s, uint8_t *out)
{
    uint16_t crc;

    if ((s == 0) || (out == 0))
    {
        return -1;
    }

    out[0] = APP_FRAME_HEAD0;
    out[1] = APP_FRAME_HEAD1;
    out[2] = APP_FRAME_VER;
    out[3] = s->flags;

    put_u32_be(&out[4],  s->seq);
    put_u32_be(&out[8],  s->timestamp);
    put_u16_be(&out[12], (uint16_t)s->temp_x10);
    put_u16_be(&out[14], s->humi_x10);

    out[16] = 0u;   /* 保留字节，将来放电量/信号强度 */
    out[17] = 0u;

    crc = crc16_modbus(out, 18u);
    put_u16_be(&out[18], crc);

    return 0;
}

/* ---------- 解析 ---------- */
int app_frame_parse(const uint8_t *buf, uint16_t len, app_sample_t *out)
{
    uint16_t crc_calc;
    uint16_t crc_recv;

    if ((buf == 0) || (out == 0))
    {
        return -1;
    }
    if (len < APP_FRAME_LEN)
    {
        return -3;
    }
    if ((buf[0] != APP_FRAME_HEAD0) || (buf[1] != APP_FRAME_HEAD1))
    {
        return -2;
    }

    crc_calc = crc16_modbus(buf, 18u);
    crc_recv = get_u16_be(&buf[18]);
    if (crc_calc != crc_recv)
    {
        return -4;
    }

    out->flags     = buf[3];
    out->seq       = get_u32_be(&buf[4]);
    out->timestamp = get_u32_be(&buf[8]);
    out->temp_x10  = (int16_t)get_u16_be(&buf[12]);
    out->humi_x10  = get_u16_be(&buf[14]);

    return 0;
}

/* ---------- DHT11 物理量 -> 应用层样本 ---------- */
void app_sample_from_dht11(int8_t temp_int, uint8_t temp_dec,
                           uint8_t humi_int, uint8_t humi_dec,
                           uint32_t seq, uint32_t ts, uint8_t flags,
                           app_sample_t *out)
{
    if (out == 0)
    {
        return;
    }

    /* DHT11 的小数位恒为 0（这是它与 DHT22 的关键差别），
     * 这里仍然按 x10 折算，是为了将来换成 DHT22 时上层代码不用改。 */
    out->seq       = seq;
    out->timestamp = ts;
    out->temp_x10  = (int16_t)((int16_t)temp_int * 10 + (int16_t)temp_dec);
    out->humi_x10  = (uint16_t)((uint16_t)humi_int * 10u + (uint16_t)humi_dec);
    out->flags     = flags;
}
