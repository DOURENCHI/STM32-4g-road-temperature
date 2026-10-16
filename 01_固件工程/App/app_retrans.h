/**
 * @file    app_retrans.h
 * @brief   应用层重传队列 + ACK 确认（纯逻辑，不依赖任何硬件）
 *
 * @tag     [2026 重建新增]
 *          2023 年原版是「串口吐出去就不管了」——AT 指令层面有超时判据，
 *          但应用层数据帧没有 ACK。本模块把「已发出」和「对方已收到」分开：
 *          帧发出后进待确认队列，收到 ACK 才清除；超时未确认则重发，
 *          重试次数用尽则移交本地缓存（app_ringbuf）转为离线补传。
 *
 * 三层故障处理合起来才是完整的：
 *   第 1 层  串口/AT 层   —— 指令有没有被模块接受（期望应答 + 超时）
 *   第 2 层  应用层       —— 这一帧数据云端有没有收到（本模块的 ACK）
 *   第 3 层  本地存储层   —— 彻底发不出去时，数据先存下来（app_ringbuf）
 *
 * @note    只包含 <stdint.h>，可在 PC 上独立编译与单元测试。
 */
#ifndef APP_RETRANS_H
#define APP_RETRANS_H

#include <stdint.h>
#include "app_protocol.h"

/** 待确认队列的槽位数。定 4 是为了在 SRAM 占用（4 x 20 字节）与
 *  流水线深度之间取平衡：链路正常时 1 个就够，突发时最多挂 4 帧。 */
#define RETRANS_MAX_SLOTS   4u

/** 帧在队列里的总存活时间上限（毫秒）。
 *  超过这个时间仍未收到 ACK，就认为链路已经不是短暂抖动了，
 *  继续重试只是白耗电——直接交给上层写本地缓存。 */
#define RETRANS_GIVEUP_MS   30000u

/** 单个槽位的状态 */
typedef enum
{
    RETRANS_FREE = 0,       /* 空闲 */
    RETRANS_WAIT_ACK        /* 已发出，等 ACK */
} retrans_state_t;

typedef struct
{
    uint8_t         frame[APP_FRAME_LEN];
    uint32_t        seq;
    uint32_t        first_at_ms;  /* 入队时刻，用于判定帧的总存活时间 */
    uint32_t        sent_at_ms;   /* 最近一次发出时刻，用于判定单次等待超时 */
    uint8_t         retry;
    retrans_state_t state;
} retrans_slot_t;

typedef struct
{
    retrans_slot_t slot[RETRANS_MAX_SLOTS];
    uint32_t       timeout_ms;   /* 单次等待 ACK 的超时 */
    uint8_t        max_retry;    /* 最大重发次数 */
    uint16_t       sent_total;   /* 累计发出的帧数（统计用） */
    uint16_t       resent_total; /* 累计重发次数（统计用） */
    uint16_t       ack_total;    /* 累计确认成功数（统计用） */
    uint16_t       giveup_total; /* 累计放弃并移交缓存的帧数（统计用） */
} app_retrans_t;

/**
 * @brief 初始化
 * @param rq         控制块
 * @param timeout_ms 等待 ACK 的超时（毫秒）
 * @param max_retry  最大重发次数（0 表示不重发，超时直接放弃）
 * @return 0 成功，-1 参数错误
 */
int retrans_init(app_retrans_t *rq, uint32_t timeout_ms, uint8_t max_retry);

/**
 * @brief 把一帧投入待确认队列
 * @param rq      控制块
 * @param frame   已组好的 20 字节帧
 * @param now_ms  当前毫秒时基
 * @return 0 成功；-1 参数错误；-2 队列已满（调用者应转本地缓存）
 */
int retrans_add(app_retrans_t *rq, const uint8_t *frame, uint32_t now_ms);

/**
 * @brief 收到云端 ACK 后清除对应槽位
 * @param seq 被确认的序号
 * @return 0 找到并清除；-1 参数错误；-2 未找到（迟到的重复 ACK，可安全忽略）
 */
int retrans_ack(app_retrans_t *rq, uint32_t seq);

/**
 * @brief 轮询：取出「需要重发」或「已放弃」的一帧
 * @param now_ms    当前毫秒时基
 * @param frame_out 输出帧（重发用）
 * @param seq_out   输出序号
 * @param is_giveup 输出：1 表示这帧已放弃，应移交本地缓存而不是重发
 * @return 1 有输出；0 暂时无事可做；-1 参数错误
 */
int retrans_poll(app_retrans_t *rq, uint32_t now_ms,
                 uint8_t *frame_out, uint32_t *seq_out, int *is_giveup);

/** @return 当前待确认的帧数 */
uint8_t retrans_pending(const app_retrans_t *rq);

#endif /* APP_RETRANS_H */
