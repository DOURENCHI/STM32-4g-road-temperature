/**
 * @file    test_dht_decode.c
 * @brief   DHT11 帧解析与校验的单元测试
 */
#include "test_framework.h"
#include "../App/app_dht_decode.h"

/* 用 4 个字节造出 40 位位流，校验和自动补齐。
 * DHT11 是高位先出，所以 raw[0] 的 bit7 是位流第 1 位。 */
static void make_bits(uint8_t *bits, uint8_t hi, uint8_t hd, uint8_t ti, uint8_t td)
{
    uint8_t raw[5];
    int     i;

    raw[0] = hi;
    raw[1] = hd;
    raw[2] = ti;
    raw[3] = td;
    raw[4] = (uint8_t)(((uint16_t)raw[0] + raw[1] + raw[2] + raw[3]) & 0xFFu);

    for (i = 0; i < 40; i++)
    {
        bits[i] = (uint8_t)((raw[i / 8] >> (7 - (i % 8))) & 0x01u);
    }
}

void test_dht_decode(void)
{
    uint8_t      bits[40];
    uint8_t      raw[5];
    dht11_frame_t frame;
    dht11_value_t value;
    int          rc;

    /* ---- 用例 1：正常帧（用论文实测里的一个点：温度 18 ℃、湿度 51 %RH） ---- */
    TF_BEGIN_CASE("正常帧解析");
    make_bits(bits, 51u, 0u, 18u, 0u);
    rc = dht11_decode(bits, &frame);
    CHECK_EQ_INT(rc, 0, "40 位位流解码成功");
    CHECK_EQ_INT(frame.humi_int, 51, "湿度整数 = 51");
    CHECK_EQ_INT(frame.temp_int, 18, "温度整数 = 18");
    CHECK_EQ_INT(frame.humi_dec, 0, "湿度小数 = 0（DHT11 小数位恒为 0）");
    CHECK_EQ_INT(frame.temp_dec, 0, "温度小数 = 0（同上）");
    dht11_frame_to_value(&frame, &value);
    CHECK_EQ_INT(value.temp_int, 18, "物理量：温度 18 ℃");
    CHECK_EQ_INT(value.humi_int, 51, "物理量：湿度 51 %RH");
    TF_END_CASE();

    /* ---- 用例 2：校验和错误必须被挡住 ---- */
    TF_BEGIN_CASE("校验和错误");
    make_bits(bits, 51u, 0u, 18u, 0u);
    bits[39] = (uint8_t)(bits[39] ^ 1u);   /* 翻转校验和的最后一位 */
    rc = dht11_decode(bits, &frame);
    CHECK_EQ_INT(rc, -3, "校验和不匹配时返回 -3");
    TF_END_CASE();

    /* ---- 用例 3：校验和函数本身 ---- */
    TF_BEGIN_CASE("校验和函数");
    raw[0] = 51u; raw[1] = 0u; raw[2] = 18u; raw[3] = 0u; raw[4] = 69u;
    CHECK_EQ_INT(dht11_checksum_ok(raw), 1, "51+0+18+0 = 69 -> 校验通过");
    raw[4] = 70u;
    CHECK_EQ_INT(dht11_checksum_ok(raw), 0, "校验和故意写错 -> 校验失败");
    /* 高字节溢出必须被截断：40+40+40+40 = 160 不溢出，改用 200+200 测截断 */
    raw[0] = 200u; raw[1] = 200u; raw[2] = 0u; raw[3] = 0u;
    raw[4] = (uint8_t)((200u + 200u) & 0xFFu);   /* = 144 */
    CHECK_EQ_INT(raw[4], 144, "求和溢出后取低 8 位");
    CHECK_EQ_INT(dht11_checksum_ok(raw), 1, "按低 8 位比较 -> 通过");
    TF_END_CASE();

    /* ---- 用例 4：负数温度（符号位） ---- */
    TF_BEGIN_CASE("负数温度符号位");
    raw[0] = 40u; raw[1] = 0u; raw[2] = 0x80u | 5u; raw[3] = 0u;
    raw[4] = (uint8_t)((raw[0] + raw[1] + raw[2] + raw[3]) & 0xFFu);
    frame.humi_int = raw[0];
    frame.humi_dec = raw[1];
    frame.temp_int = raw[2];
    frame.temp_dec = raw[3];
    frame.checksum = raw[4];
    dht11_frame_to_value(&frame, &value);
    CHECK_EQ_INT(value.temp_int, -5, "0x85 应解析为 -5 ℃ 而不是 133 ℃");
    TF_END_CASE();

    /* ---- 用例 5：非法位 ---- */
    TF_BEGIN_CASE("非法位被拒绝");
    make_bits(bits, 51u, 0u, 18u, 0u);
    bits[7] = 2u;
    rc = dht11_decode(bits, &frame);
    CHECK_EQ_INT(rc, -2, "位值只能是 0/1，出现 2 应返回 -2");
    TF_END_CASE();

    /* ---- 用例 6：量程判断 ---- */
    TF_BEGIN_CASE("量程判断（官方规格 0~50 ℃ / 20~90 %RH）");
    value.temp_int = 18; value.humi_int = 51;
    CHECK_EQ_INT(dht11_out_of_range(&value), 0, "18 ℃ / 51 %RH 在量程内");

    value.temp_int = 60; value.humi_int = 51;
    CHECK_EQ_INT(dht11_out_of_range(&value), 1, "60 ℃ 超温度量程（夏季沥青路面的真实场景）");

    value.temp_int = 18; value.humi_int = 10;
    CHECK_EQ_INT(dht11_out_of_range(&value), 2, "10 %RH 低于湿度量程下限");

    value.temp_int = 60; value.humi_int = 10;
    CHECK_EQ_INT(dht11_out_of_range(&value), 3, "温度与湿度同时超量程");

    value.temp_int = -5; value.humi_int = 51;
    CHECK_EQ_INT(dht11_out_of_range(&value), 1, "-5 ℃ 低于温度量程下限");
    TF_END_CASE();

    /* ---- 用例 7：位序往返 ---- */
    TF_BEGIN_CASE("位序往返");
    make_bits(bits, 0xA5u, 0x5Au, 0x7Fu, 0x80u);
    rc = dht11_bits_to_bytes(bits, raw);
    CHECK_EQ_INT(rc, 0, "打包成功");
    CHECK_EQ_INT(raw[0], 0xA5, "第 1 字节 = 0xA5（高位先出）");
    CHECK_EQ_INT(raw[1], 0x5A, "第 2 字节 = 0x5A");
    CHECK_EQ_INT(raw[2], 0x7F, "第 3 字节 = 0x7F");
    CHECK_EQ_INT(raw[3], 0x80, "第 4 字节 = 0x80");
    TF_END_CASE();
}
