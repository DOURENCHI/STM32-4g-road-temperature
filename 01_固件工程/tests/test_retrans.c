/**
 * @file    test_retrans.c
 * @brief   重传队列 + ACK 确认的单元测试
 */
#include "test_framework.h"
#include "../App/app_retrans.h"
#include "../App/app_protocol.h"

#include <string.h>

/* 造一帧序号为 seq 的合法帧 */
static void make_frame(uint8_t *frame, uint32_t seq)
{
    app_sample_t s;
    s.seq       = seq;
    s.timestamp = 1700000000u + seq;
    s.temp_x10  = 250;
    s.humi_x10  = 500;
    s.flags     = 0u;
    app_frame_build(&s, frame);
}

void test_retrans(void)
{
    app_retrans_t rq;
    uint8_t       frame[APP_FRAME_LEN];
    uint8_t       out[APP_FRAME_LEN];
    uint32_t      seq_out;
    int           is_giveup;
    int           rc;

    /* ---- 用例 1：加入 -> ACK 清除 ---- */
    TF_BEGIN_CASE("加入 -> ACK 清除");
    retrans_init(&rq, 500u, 2u);
    make_frame(frame, 100u);
    CHECK_EQ_INT(retrans_add(&rq, frame, 0u), 0, "入队成功");
    CHECK_EQ_INT(retrans_pending(&rq), 1, "待确认帧数 = 1");
    CHECK_EQ_INT(retrans_ack(&rq, 100u), 0, "收到 ACK，清除对应槽位");
    CHECK_EQ_INT(retrans_pending(&rq), 0, "待确认帧数 = 0");
    CHECK_EQ_INT(rq.ack_total, 1, "累计确认数 = 1");
    TF_END_CASE();

    /* ---- 用例 2：迟到的重复 ACK 必须被安全忽略 ---- */
    TF_BEGIN_CASE("迟到的重复 ACK 被忽略");
    rc = retrans_ack(&rq, 100u);
    CHECK_EQ_INT(rc, -2, "重复 ACK 返回 -2（可安全忽略，不是错误）");
    rc = retrans_ack(&rq, 999u);
    CHECK_EQ_INT(rc, -2, "从未发过的序号同样返回 -2");
    TF_END_CASE();

    /* ---- 用例 3：超时前不重发 ---- */
    TF_BEGIN_CASE("超时前安静等待");
    retrans_init(&rq, 500u, 2u);
    make_frame(frame, 200u);
    retrans_add(&rq, frame, 1000u);
    rc = retrans_poll(&rq, 1200u, out, &seq_out, &is_giveup);   /* 才过 200ms */
    CHECK_EQ_INT(rc, 0, "未超时 -> poll 返回 0（无事可做）");
    CHECK_EQ_INT(retrans_pending(&rq), 1, "仍挂在待确认队列里");
    TF_END_CASE();

    /* ---- 用例 4：超时触发重发 ---- */
    TF_BEGIN_CASE("超时触发重发");
    rc = retrans_poll(&rq, 1600u, out, &seq_out, &is_giveup);   /* 过了 600ms > 500ms */
    CHECK_EQ_INT(rc, 1, "超时 -> poll 返回 1");
    CHECK_EQ_INT(is_giveup, 0, "这是重发，不是放弃");
    CHECK_EQ_INT(seq_out, 200, "重发的序号正确");
    CHECK_EQ_INT(rq.slot[0].retry, 1, "重试计数 = 1");
    CHECK_EQ_INT(rq.resent_total, 1, "累计重发数 = 1");
    TF_END_CASE();

    /* ---- 用例 5：重发次数用尽 -> 转本地缓存 ---- */
    TF_BEGIN_CASE("重发次数用尽后放弃");
    rc = retrans_poll(&rq, 2200u, out, &seq_out, &is_giveup);   /* retry 1 -> 2 */
    CHECK_EQ_INT(rc, 1, "第 2 次超时 -> 重发");
    CHECK_EQ_INT(is_giveup, 0, "仍是重发");

    rc = retrans_poll(&rq, 2800u, out, &seq_out, &is_giveup);   /* retry 已达 max_retry = 2 */
    CHECK_EQ_INT(rc, 1, "第 3 次超时 -> 判定放弃并交还上层");
    CHECK_EQ_INT(is_giveup, 1, "is_giveup = 1（上层应写入本地缓存）");
    CHECK_EQ_INT(seq_out, 200, "交还的序号正确");
    CHECK_EQ_INT(rq.giveup_total, 1, "累计放弃数 = 1");
    CHECK_EQ_INT(retrans_pending(&rq), 0, "槽位已释放");

    rc = retrans_poll(&rq, 2900u, out, &seq_out, &is_giveup);
    CHECK_EQ_INT(rc, 0, "槽位释放后不再重复上报同一帧");
    TF_END_CASE();

    /* ---- 用例 6：队列满 ---- */
    TF_BEGIN_CASE("队列满时返回 -2（调用者应转本地缓存）");
    retrans_init(&rq, 500u, 2u);
    {
        uint32_t i;
        int      all_ok = 1;
        for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
        {
            make_frame(frame, 300u + i);
            if (retrans_add(&rq, frame, 0u) != 0)
            {
                all_ok = 0;
            }
        }
        CHECK(all_ok == 1, "前 4 帧全部入队成功");
    }
    make_frame(frame, 399u);
    CHECK_EQ_INT(retrans_add(&rq, frame, 0u), -2, "第 5 帧入队失败 -> -2");
    CHECK_EQ_INT(retrans_pending(&rq), RETRANS_MAX_SLOTS, "队列仍满挂着 4 帧");
    TF_END_CASE();

    /* ---- 用例 7：帧总存活时间超限 -> 直接放弃 ---- */
    TF_BEGIN_CASE("帧存活时间超限 -> 不再重试");
    retrans_init(&rq, 100u, 200u);      /* 超时很短、重试次数很多 */
    make_frame(frame, 500u);
    retrans_add(&rq, frame, 0u);
    /* 一直在重发，但时间已经推过 RETRANS_GIVEUP_MS */
    rc = retrans_poll(&rq, RETRANS_GIVEUP_MS + 1u, out, &seq_out, &is_giveup);
    CHECK_EQ_INT(rc, 1, "poll 有输出");
    CHECK_EQ_INT(is_giveup, 1, "总存活时间超限 -> 判定放弃，不再无休止重试");
    CHECK_EQ_INT(rq.giveup_total, 1, "累计放弃数 = 1");
    TF_END_CASE();

    /* ---- 用例 8：序号取自帧体 ---- */
    TF_BEGIN_CASE("序号从帧体里取，不依赖调用者传参");
    retrans_init(&rq, 500u, 2u);
    make_frame(frame, 0x01020304u);
    retrans_add(&rq, frame, 0u);
    CHECK_EQ_INT(rq.slot[0].seq, 0x01020304u, "序号 0x01020304 解析正确（大端）");
    CHECK_EQ_INT(retrans_ack(&rq, 0x01020304u), 0, "用该序号可以正常确认");
    TF_END_CASE();
}
