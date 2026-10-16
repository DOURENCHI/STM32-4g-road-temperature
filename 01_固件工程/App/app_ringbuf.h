/**
 * @file    app_ringbuf.h
 * @brief   记录式环形缓冲：断网期间的数据本地留存（纯逻辑，不依赖任何硬件）
 *
 * @tag     [2026 重建新增]
 *          2023 年原版没有断网补传——网络断了，那段时间的数据就永久丢了。
 *          本模块就是补齐这个缺口的核心件。
 *
 * @note    设计取向：存储区由调用者提供（PC 测试时给内存数组，
 *          固件里指向内部 Flash 的扇区缓冲或外挂 W25Qxx），
 *          因此本文件与「存在哪里」完全解耦，可独立测试。
 * @note    只包含 <stdint.h>，可在 PC 上独立编译与单元测试。
 */
#ifndef APP_RINGBUF_H
#define APP_RINGBUF_H

#include <stdint.h>

/** 环形缓冲满时的策略 */
typedef enum
{
    RINGBUF_OVERWRITE_OLDEST = 0,   /* 覆盖最旧的一条，保证新数据不丢 */
    RINGBUF_DROP_NEWEST      = 1    /* 丢弃最新的一条，保证历史数据完整 */
} ringbuf_policy_t;

/** 记录式环形缓冲控制块 */
typedef struct
{
    uint8_t         *buf;        /*!< 外部提供的存储区 */
    uint16_t         cap;        /*!< 可容纳的记录条数 */
    uint16_t         rec_size;   /*!< 单条记录字节数 */
    uint16_t         head;       /*!< 下一条写入的槽位下标 */
    uint16_t         count;      /*!< 当前有效记录数 */
    uint16_t         dropped;    /*!< 因容量不足而丢失的记录累计数 */
    ringbuf_policy_t policy;     /*!< 满时策略 */
} app_ringbuf_t;

/**
 * @brief 初始化
 * @param rb       控制块
 * @param buf      外部存储区首地址，大小必须 >= cap * rec_size
 * @param cap      记录条数容量
 * @param rec_size 单条记录字节数
 * @param policy   满时策略
 * @return 0 成功，-1 参数非法
 */
int ringbuf_init(app_ringbuf_t *rb, uint8_t *buf, uint16_t cap,
                 uint16_t rec_size, ringbuf_policy_t policy);

/**
 * @brief 写入一条记录
 * @return 1 写入成功；0 写入成功但挤掉了最旧的一条（发生覆盖）；
 *          -1 参数错误；-2 缓冲区已满且策略为 DROP_NEWEST（丢弃了本条）
 */
int ringbuf_push(app_ringbuf_t *rb, const uint8_t *rec);

/**
 * @brief 取出最旧的一条并从缓冲区移除（FIFO，保证补传顺序与采集顺序一致）
 * @return 0 成功；-1 参数错误；-2 缓冲区为空
 */
int ringbuf_pop(app_ringbuf_t *rb, uint8_t *rec);

/**
 * @brief 查看最旧的一条但不出队（用于「发送成功后才移除」的确认式补传）
 * @return 0 成功；-1 参数错误；-2 缓冲区为空
 */
int ringbuf_peek(const app_ringbuf_t *rb, uint8_t *rec);

/** @return 当前有效记录数 */
uint16_t ringbuf_count(const app_ringbuf_t *rb);

/** @return 还能再写多少条而不触发覆盖 */
uint16_t ringbuf_free(const app_ringbuf_t *rb);

/** @return 累计丢弃（含被覆盖）的记录数 */
uint16_t ringbuf_dropped(const app_ringbuf_t *rb);

/** @brief 清空（补传完成后调用） */
void ringbuf_clear(app_ringbuf_t *rb);

#endif /* APP_RINGBUF_H */
