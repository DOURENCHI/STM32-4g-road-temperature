/**
 * @file    app_dht_decode.c
 * @brief   DHT11 40 位帧的解析与校验（纯逻辑，不依赖任何硬件）
 * @note    可独立编译：gcc -c app_dht_decode.c
 */
#include "app_dht_decode.h"

int dht11_checksum_ok(const uint8_t *raw)
{
    uint16_t sum;

    if (raw == 0)
    {
        return -1;
    }

    sum = (uint16_t)raw[0] + (uint16_t)raw[1] + (uint16_t)raw[2] + (uint16_t)raw[3];
    sum &= 0x00FFu;

    return (sum == (uint16_t)raw[4]) ? 1 : 0;
}

int dht11_bits_to_bytes(const uint8_t *bits, uint8_t *raw)
{
    uint8_t  i;
    uint8_t  byte_val = 0u;
    uint8_t  byte_idx = 0u;

    if ((bits == 0) || (raw == 0))
    {
        return -1;
    }

    for (i = 0u; i < DHT11_FRAME_BITS; i++)
    {
        if (bits[i] > 1u)
        {
            return -2;      /* 非法位：只允许 0 或 1 */
        }

        byte_val = (uint8_t)((byte_val << 1) | bits[i]);

        if (((i + 1u) % 8u) == 0u)
        {
            raw[byte_idx] = byte_val;
            byte_idx++;
            byte_val = 0u;
        }
    }

    return 0;
}

int dht11_decode(const uint8_t *bits, dht11_frame_t *out)
{
    uint8_t raw[DHT11_FRAME_BYTES];
    int     rc;

    if ((bits == 0) || (out == 0))
    {
        return -1;
    }

    rc = dht11_bits_to_bytes(bits, raw);
    if (rc != 0)
    {
        return rc;      /* -2：非法位 */
    }

    if (dht11_checksum_ok(raw) != 1)
    {
        return -3;      /* 校验和错误 */
    }

    out->humi_int = raw[0];
    out->humi_dec = raw[1];
    out->temp_int = raw[2];
    out->temp_dec = raw[3];
    out->checksum = raw[4];

    return 0;
}

void dht11_frame_to_value(const dht11_frame_t *frame, dht11_value_t *value)
{
    uint8_t t;

    if ((frame == 0) || (value == 0))
    {
        return;
    }

    /*
     * 温度字节的最高位是符号位。
     * DHT11 官方量程是 0~50 ℃，正常读数不会用到这一位；
     * 但纸面规格与实测存在出入时，先按符号位处理比直接当无符号更安全
     * ——否则 -5 ℃ 会被误读成 251 ℃。
     */
    t = frame->temp_int;
    if ((t & 0x80u) != 0u)
    {
        value->temp_int = (int8_t)(-(int8_t)(t & 0x7Fu));
    }
    else
    {
        value->temp_int = (int8_t)t;
    }

    value->temp_dec = frame->temp_dec;
    value->humi_int = frame->humi_int;
    value->humi_dec = frame->humi_dec;
}

int dht11_out_of_range(const dht11_value_t *value)
{
    int rc = 0;

    if (value == 0)
    {
        return -1;
    }

    /* 温度量程 0~50 ℃ */
    if ((value->temp_int < 0) || (value->temp_int > 50))
    {
        rc |= 1;
    }

    /* 湿度量程 20~90 %RH */
    if ((value->humi_int < 20u) || (value->humi_int > 90u))
    {
        rc |= 2;
    }

    return rc;
}
