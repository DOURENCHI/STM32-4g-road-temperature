/**
 * @file    test_ringbuf.c
 * @brief   记录式环形缓冲的单元测试（断网缓存的核心）
 */
#include "test_framework.h"
#include "../App/app_ringbuf.h"

#include <string.h>

/* 容量 8 条、每条 4 字节的小缓冲，方便手算 */
static uint8_t      g_storage[8 * 4];
static app_ringbuf_t rb;

/* 造一条记录：首字节是标签，其余填 0 */
static void make_rec(uint8_t *rec, uint8_t tag)
{
    memset(rec, 0, 4);
    rec[0] = tag;
}

void test_ringbuf(void)
{
    uint8_t rec[4];
    uint8_t out[4];
    int     rc;

    /* ---- 用例 1：初始化参数校验 ---- */
    TF_BEGIN_CASE("初始化参数校验");
    CHECK_EQ_INT(ringbuf_init(0, g_storage, 8u, 4u, RINGBUF_OVERWRITE_OLDEST), -1, "空控制块 -> -1");
    CHECK_EQ_INT(ringbuf_init(&rb, 0, 8u, 4u, RINGBUF_OVERWRITE_OLDEST), -1, "空存储区 -> -1");
    CHECK_EQ_INT(ringbuf_init(&rb, g_storage, 0u, 4u, RINGBUF_OVERWRITE_OLDEST), -1, "容量 0 -> -1");
    CHECK_EQ_INT(ringbuf_init(&rb, g_storage, 8u, 0u, RINGBUF_OVERWRITE_OLDEST), -1, "记录长度 0 -> -1");
    CHECK_EQ_INT(ringbuf_init(&rb, g_storage, 8u, 4u, RINGBUF_OVERWRITE_OLDEST), 0, "正常初始化 -> 0");
    TF_END_CASE();

    /* ---- 用例 2：空缓冲的行为 ---- */
    TF_BEGIN_CASE("空缓冲行为");
    CHECK_EQ_INT(ringbuf_count(&rb), 0, "初始条数 = 0");
    CHECK_EQ_INT(ringbuf_free(&rb), 8, "剩余空间 = 8");
    CHECK_EQ_INT(ringbuf_pop(&rb, out), -2, "空缓冲出队 -> -2");
    CHECK_EQ_INT(ringbuf_peek(&rb, out), -2, "空缓冲查看 -> -2");
    TF_END_CASE();

    /* ---- 用例 3：FIFO 顺序 ---- */
    TF_BEGIN_CASE("FIFO 顺序（补传必须按采集顺序，否则云端时间轴会乱）");
    ringbuf_clear(&rb);
    make_rec(rec, 0xA1); ringbuf_push(&rb, rec);
    make_rec(rec, 0xA2); ringbuf_push(&rb, rec);
    make_rec(rec, 0xA3); ringbuf_push(&rb, rec);
    CHECK_EQ_INT(ringbuf_count(&rb), 3, "写入 3 条");

    ringbuf_pop(&rb, out);
    CHECK_EQ_INT(out[0], 0xA1, "第 1 条出队 = 0xA1（最旧的先出）");
    ringbuf_pop(&rb, out);
    CHECK_EQ_INT(out[0], 0xA2, "第 2 条出队 = 0xA2");
    CHECK_EQ_INT(ringbuf_count(&rb), 1, "出队 2 条后剩 1 条");
    TF_END_CASE();

    /* ---- 用例 4：peek 不出队 ---- */
    TF_BEGIN_CASE("peek 不出队（用于确认式补传）");
    ringbuf_clear(&rb);
    make_rec(rec, 0xB1); ringbuf_push(&rb, rec);
    ringbuf_peek(&rb, out);
    CHECK_EQ_INT(out[0], 0xB1, "peek 拿到最旧一条");
    CHECK_EQ_INT(ringbuf_count(&rb), 1, "peek 之后条数不变");

    /* 模拟「发送成功后才移除」：先 peek 发出，收到 ACK 再 pop */
    ringbuf_pop(&rb, out);
    CHECK_EQ_INT(ringbuf_count(&rb), 0, "确认后才真正移除");
    TF_END_CASE();

    /* ---- 用例 5：写满 ---- */
    TF_BEGIN_CASE("写满 8 条");
    ringbuf_clear(&rb);
    {
        uint8_t i;
        int     all_ok = 1;
        for (i = 1u; i <= 8u; i++)
        {
            make_rec(rec, i);
            if (ringbuf_push(&rb, rec) != 1)
            {
                all_ok = 0;
            }
        }
        CHECK(all_ok == 1, "连续写入 8 条，每条都返回 1（未触发覆盖）");
    }
    CHECK_EQ_INT(ringbuf_count(&rb), 8, "条数 = 容量 8");
    CHECK_EQ_INT(ringbuf_free(&rb), 0, "剩余空间 = 0");
    TF_END_CASE();

    /* ---- 用例 6：满时覆盖最旧（默认策略） ---- */
    TF_BEGIN_CASE("满时覆盖最旧（保证新数据不丢）");
    make_rec(rec, 0xC9);
    rc = ringbuf_push(&rb, rec);
    CHECK_EQ_INT(rc, 0, "满时写入返回 0（提示「挤掉了一条」）");
    CHECK_EQ_INT(ringbuf_count(&rb), 8, "条数仍为 8，不越界");
    CHECK_EQ_INT(ringbuf_dropped(&rb), 1, "丢弃计数 = 1");

    /* 现在最旧的一条应该是原来的第 2 条（标签 2），而不是第 1 条 */
    ringbuf_pop(&rb, out);
    CHECK_EQ_INT(out[0], 2, "第 1 条（标签 1）已被覆盖，最旧的变成标签 2");

    /* 队列尾部应该是最新写入的 0xC9，把它一路取到底验证 */
    {
        int guard = 20;
        while ((ringbuf_count(&rb) > 0) && (guard-- > 0))
        {
            ringbuf_pop(&rb, out);
        }
        CHECK_EQ_INT(out[0], 0xC9, "队尾是最新写入的 0xC9");
    }
    TF_END_CASE();

    /* ---- 用例 7：DROP_NEWEST 策略 ---- */
    TF_BEGIN_CASE("DROP_NEWEST 策略（宁可丢新、保住历史）");
    ringbuf_init(&rb, g_storage, 2u, 4u, RINGBUF_DROP_NEWEST);
    make_rec(rec, 1u); ringbuf_push(&rb, rec);
    make_rec(rec, 2u); ringbuf_push(&rb, rec);
    make_rec(rec, 3u); rc = ringbuf_push(&rb, rec);
    CHECK_EQ_INT(rc, -2, "满时返回 -2（本条被丢弃）");
    CHECK_EQ_INT(ringbuf_count(&rb), 2, "条数保持 2");
    ringbuf_pop(&rb, out);
    CHECK_EQ_INT(out[0], 1, "最早的一条仍在，历史数据未被破坏");
    TF_END_CASE();
}
