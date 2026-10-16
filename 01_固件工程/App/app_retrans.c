/**
 * @file    app_retrans.c
 * @brief   应用层重传队列 + ACK 确认（纯逻辑，不依赖任何硬件）
 * @note    可独立编译：gcc -c app_retrans.c
 */
#include "app_retrans.h"

#include <string.h>

int retrans_init(app_retrans_t *rq, uint32_t timeout_ms, uint8_t max_retry)
{
    uint8_t i;

    if (rq == 0)
    {
        return -1;
    }

    for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
    {
        rq->slot[i].state       = RETRANS_FREE;
        rq->slot[i].seq         = 0u;
        rq->slot[i].retry       = 0u;
        rq->slot[i].first_at_ms = 0u;
        rq->slot[i].sent_at_ms  = 0u;
    }

    rq->timeout_ms   = timeout_ms;
    rq->max_retry    = max_retry;
    rq->sent_total   = 0u;
    rq->resent_total = 0u;
    rq->ack_total    = 0u;
    rq->giveup_total = 0u;

    return 0;
}

int retrans_add(app_retrans_t *rq, const uint8_t *frame, uint32_t now_ms)
{
    uint8_t i;

    if ((rq == 0) || (frame == 0))
    {
        return -1;
    }

    for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
    {
        if (rq->slot[i].state == RETRANS_FREE)
        {
            memcpy(rq->slot[i].frame, frame, APP_FRAME_LEN);
            /* 序号直接取自帧体，避免调用方传错导致 ACK 对不上 */
            rq->slot[i].seq        = ((uint32_t)frame[4] << 24) |
                                     ((uint32_t)frame[5] << 16) |
                                     ((uint32_t)frame[6] << 8)  |
                                      (uint32_t)frame[7];
            rq->slot[i].first_at_ms = now_ms;
            rq->slot[i].sent_at_ms  = now_ms;
            rq->slot[i].retry       = 0u;
            rq->slot[i].state       = RETRANS_WAIT_ACK;
            rq->sent_total++;
            return 0;
        }
    }

    return -2;      /* 队列满 */
}

int retrans_ack(app_retrans_t *rq, uint32_t seq)
{
    uint8_t i;

    if (rq == 0)
    {
        return -1;
    }

    for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
    {
        if ((rq->slot[i].state == RETRANS_WAIT_ACK) && (rq->slot[i].seq == seq))
        {
            rq->slot[i].state = RETRANS_FREE;
            rq->ack_total++;
            return 0;
        }
    }

    return -2;      /* 迟到的重复 ACK，忽略即可 */
}

int retrans_poll(app_retrans_t *rq, uint32_t now_ms,
                 uint8_t *frame_out, uint32_t *seq_out, int *is_giveup)
{
    uint8_t i;

    if ((rq == 0) || (frame_out == 0) || (seq_out == 0) || (is_giveup == 0))
    {
        return -1;
    }

    for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
    {
        if (rq->slot[i].state != RETRANS_WAIT_ACK)
        {
            continue;
        }

        /* 未到超时时间，继续等 */
        if ((now_ms - rq->slot[i].sent_at_ms) < rq->timeout_ms)
        {
            continue;
        }

        /*
         * 超时了。两种情况下不再重试，直接把帧交还上层去写本地缓存：
         *   1) 这帧在队列里的总存活时间已超过 RETRANS_GIVEUP_MS
         *      —— 链路不是短暂抖动，继续重试只是白耗电；
         *   2) 重发次数已用尽（max_retry 为 0 时第一次超时就走到这里）。
         * 必须先释放槽位再返回，否则上层拿到帧后槽位仍被占着。
         */
        if (((now_ms - rq->slot[i].first_at_ms) >= RETRANS_GIVEUP_MS) ||
            (rq->slot[i].retry >= rq->max_retry))
        {
            memcpy(frame_out, rq->slot[i].frame, APP_FRAME_LEN);
            *seq_out   = rq->slot[i].seq;
            *is_giveup = 1;
            rq->slot[i].state = RETRANS_FREE;
            rq->giveup_total++;
            return 1;
        }

        /* 正常重发 */
        rq->slot[i].retry++;
        rq->slot[i].sent_at_ms = now_ms;
        rq->resent_total++;

        memcpy(frame_out, rq->slot[i].frame, APP_FRAME_LEN);
        *seq_out   = rq->slot[i].seq;
        *is_giveup = 0;
        return 1;
    }

    return 0;       /* 无事可做 */
}

uint8_t retrans_pending(const app_retrans_t *rq)
{
    uint8_t i;
    uint8_t n = 0u;

    if (rq == 0)
    {
        return 0u;
    }

    for (i = 0u; i < RETRANS_MAX_SLOTS; i++)
    {
        if (rq->slot[i].state == RETRANS_WAIT_ACK)
        {
            n++;
        }
    }

    return n;
}
