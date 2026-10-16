/**
 * @file    app_ringbuf.c
 * @brief   记录式环形缓冲（纯逻辑，不依赖任何硬件）
 * @note    可独立编译：gcc -c app_ringbuf.c
 */
#include "app_ringbuf.h"

#include <string.h>

int ringbuf_init(app_ringbuf_t *rb, uint8_t *buf, uint16_t cap,
                 uint16_t rec_size, ringbuf_policy_t policy)
{
    if ((rb == 0) || (buf == 0) || (cap == 0u) || (rec_size == 0u))
    {
        return -1;
    }

    rb->buf      = buf;
    rb->cap      = cap;
    rb->rec_size = rec_size;
    rb->head     = 0u;
    rb->count    = 0u;
    rb->dropped  = 0u;
    rb->policy   = policy;

    return 0;
}

uint16_t ringbuf_count(const app_ringbuf_t *rb)
{
    return (rb == 0) ? 0u : rb->count;
}

uint16_t ringbuf_free(const app_ringbuf_t *rb)
{
    if (rb == 0)
    {
        return 0u;
    }
    return (uint16_t)(rb->cap - rb->count);
}

uint16_t ringbuf_dropped(const app_ringbuf_t *rb)
{
    return (rb == 0) ? 0u : rb->dropped;
}

int ringbuf_push(app_ringbuf_t *rb, const uint8_t *rec)
{
    uint16_t slot;

    if ((rb == 0) || (rec == 0))
    {
        return -1;
    }

    if (rb->count == rb->cap)
    {
        /* 已满 */
        if (rb->policy == RINGBUF_DROP_NEWEST)
        {
            rb->dropped++;
            return -2;
        }

        /* 覆盖最旧：head 正好指向最旧的那条（因为 count == cap 时
         * 最旧记录就在 head 位置），直接覆盖并前移 head 即可 */
        slot = rb->head;
        memcpy(&rb->buf[(uint32_t)slot * rb->rec_size], rec, rb->rec_size);
        rb->head = (uint16_t)((rb->head + 1u) % rb->cap);
        rb->dropped++;
        return 0;
    }

    slot = rb->head;
    memcpy(&rb->buf[(uint32_t)slot * rb->rec_size], rec, rb->rec_size);
    rb->head = (uint16_t)((rb->head + 1u) % rb->cap);
    rb->count++;

    return 1;
}

int ringbuf_pop(app_ringbuf_t *rb, uint8_t *rec)
{
    uint16_t oldest;

    if ((rb == 0) || (rec == 0))
    {
        return -1;
    }
    if (rb->count == 0u)
    {
        return -2;
    }

    /* head 是「下一条要写」的位置，所以最旧的一条在 head 往前数 count 格 */
    oldest = (uint16_t)((rb->head + rb->cap - rb->count) % rb->cap);

    memcpy(rec, &rb->buf[(uint32_t)oldest * rb->rec_size], rb->rec_size);
    rb->count--;

    return 0;
}

int ringbuf_peek(const app_ringbuf_t *rb, uint8_t *rec)
{
    uint16_t oldest;

    if ((rb == 0) || (rec == 0))
    {
        return -1;
    }
    if (rb->count == 0u)
    {
        return -2;
    }

    oldest = (uint16_t)((rb->head + rb->cap - rb->count) % rb->cap);
    memcpy(rec, &rb->buf[(uint32_t)oldest * rb->rec_size], rb->rec_size);

    return 0;
}

void ringbuf_clear(app_ringbuf_t *rb)
{
    if (rb == 0)
    {
        return;
    }
    rb->head    = 0u;
    rb->count   = 0u;
    rb->dropped = 0u;
}
