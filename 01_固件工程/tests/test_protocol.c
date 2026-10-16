/**
 * @file    test_protocol.c
 * @brief   应用层数据帧组帧/解析的单元测试
 */
#include "test_framework.h"
#include "../App/app_protocol.h"

#include <string.h>

void test_protocol(void)
{
    app_sample_t s;
    app_sample_t back;
    uint8_t      frame[APP_FRAME_LEN];
    uint8_t      bad[APP_FRAME_LEN];
    int          rc;

    /* ---- 用例 1：CRC16/MODBUS 是否符合标准测试向量 ---- */
    TF_BEGIN_CASE("CRC16/MODBUS 标准测试向量");
    {
        /* 这是 CRC16/MODBUS 的公开标准自检向量：
         * 对 ASCII 字符串 "123456789" 计算结果应为 0x4B37。
         * 用它来证明我的实现与工业仪表（Modbus RTU）完全一致——
         * 将来接真实电表/气象站时校验算法不用换。 */
        const uint8_t vec[9] = {'1','2','3','4','5','6','7','8','9'};
        CHECK_EQ_INT(crc16_modbus(vec, 9u), 0x4B37, "\"123456789\" 的 CRC16/MODBUS = 0x4B37");
    }
    TF_END_CASE();

    /* ---- 用例 2：组帧结构 ---- */
    TF_BEGIN_CASE("组帧结构");
    s.seq       = 1234u;
    s.timestamp = 1699999999u;   /* 任意 Unix 秒 */
    s.temp_x10  = 265;           /* 26.5 ℃ */
    s.humi_x10  = 510;           /* 51.0 %RH */
    s.flags     = 0u;

    rc = app_frame_build(&s, frame);
    CHECK_EQ_INT(rc, 0, "组帧返回 0");
    CHECK_EQ_INT(frame[0], 0xAA, "帧头第 1 字节 = 0xAA");
    CHECK_EQ_INT(frame[1], 0x55, "帧头第 2 字节 = 0x55");
    CHECK_EQ_INT(frame[2], 0x01, "版本号 = 0x01");
    CHECK_EQ_INT(frame[16], 0, "保留字节置 0");
    TF_END_CASE();

    /* ---- 用例 3：组帧 -> 解析 往返一致 ---- */
    TF_BEGIN_CASE("组帧 -> 解析 往返一致");
    rc = app_frame_parse(frame, APP_FRAME_LEN, &back);
    CHECK_EQ_INT(rc, 0, "解析返回 0");
    CHECK_EQ_INT(back.seq, 1234, "序号一致");
    CHECK_EQ_INT(back.timestamp, 1699999999u, "时间戳一致");
    CHECK_EQ_INT(back.temp_x10, 265, "温度一致（26.5 ℃）");
    CHECK_EQ_INT(back.humi_x10, 510, "湿度一致（51.0 %RH）");
    CHECK_EQ_INT(back.flags, 0, "标志位一致");
    TF_END_CASE();

    /* ---- 用例 4：篡改负载必须被 CRC 抓住 ---- */
    TF_BEGIN_CASE("篡改负载被 CRC 拦下");
    memcpy(bad, frame, APP_FRAME_LEN);
    bad[12] = (uint8_t)(bad[12] + 1u);    /* 改动温度高字节 */
    rc = app_frame_parse(bad, APP_FRAME_LEN, &back);
    CHECK_EQ_INT(rc, -4, "负载被改动 -> CRC 校验失败返回 -4");

    memcpy(bad, frame, APP_FRAME_LEN);
    bad[18] = (uint8_t)(bad[18] + 1u);    /* 改动 CRC 自身 */
    rc = app_frame_parse(bad, APP_FRAME_LEN, &back);
    CHECK_EQ_INT(rc, -4, "CRC 自身被改动 -> 同样返回 -4");
    TF_END_CASE();

    /* ---- 用例 5：帧头错误 / 长度不足 ---- */
    TF_BEGIN_CASE("帧头与长度校验");
    memcpy(bad, frame, APP_FRAME_LEN);
    bad[0] = 0x00;
    rc = app_frame_parse(bad, APP_FRAME_LEN, &back);
    CHECK_EQ_INT(rc, -2, "帧头不匹配 -> -2");

    rc = app_frame_parse(frame, 10u, &back);
    CHECK_EQ_INT(rc, -3, "长度不足 -> -3");
    TF_END_CASE();

    /* ---- 用例 6：负数温度 ---- */
    TF_BEGIN_CASE("负数温度往返");
    s.temp_x10 = -52;    /* -5.2 ℃ */
    s.humi_x10 = 400;
    s.seq      = 7u;
    s.flags    = APP_FLAG_RETRANSMIT;
    app_frame_build(&s, frame);
    app_frame_parse(frame, APP_FRAME_LEN, &back);
    CHECK_EQ_INT(back.temp_x10, -52, "负温度正确往返（-5.2 ℃）");
    CHECK_EQ_INT(back.flags, APP_FLAG_RETRANSMIT, "补传标志位保持");
    TF_END_CASE();

    /* ---- 用例 7：DHT11 物理量 -> 样本 ---- */
    TF_BEGIN_CASE("DHT11 物理量换算");
    app_sample_from_dht11(18, 0, 51, 0, 100u, 1699999900u, 0u, &s);
    CHECK_EQ_INT(s.temp_x10, 180, "18 ℃ -> temp_x10 = 180");
    CHECK_EQ_INT(s.humi_x10, 510, "51 %RH -> humi_x10 = 510");
    CHECK_EQ_INT(s.seq, 100, "序号透传");
    TF_END_CASE();
}
