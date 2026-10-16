/**
 * @file    main.c
 * @brief   主程序：采集 -> 组帧 -> 上报 -> 确认 -> 补传 的完整调度
 *
 * 标签：
 *   [2023 实做]          采集、组帧、经 RS485 送 DTU、透传上云这条主链
 *   [2026 重建新增]      ACK 确认、断网本地缓存、恢复后补传、失败即停
 *   [未经硬件回归验证]   整个 main 都依赖真实硬件，没有在开发板上跑过
 *
 * 与 2023 年原版的差别：
 *   原版：采集 -> 拼字符串 -> 串口吐出去 -> 结束。发出去了就认为成了。
 *   现在：采集 -> 组帧(序号+时间戳+CRC) -> 发出并挂进待确认队列
 *         -> 收到 ACK 才清除 -> 超时重发 -> 重试用尽转本地缓存
 *         -> 链路恢复后先补完历史再上新的。
 *   变化的核心是：把「已发出」和「已被收到」分开。
 */
#include "stm32f10x.h"

#include "app_protocol.h"
#include "app_dht_decode.h"
#include "app_ringbuf.h"
#include "app_retrans.h"

#include "delay.h"
#include "dht11.h"
#include "uart.h"
#include "rs485.h"
#include "atk_idm751c.h"

/* ------------------------------------------------------------------
 * 配置
 * ------------------------------------------------------------------ */
#define SAMPLE_PERIOD_MS     60000u     /* 采样周期：1 分钟，与论文实测节奏一致 */
#define LOCAL_CACHE_RECS     200u       /* 本地缓存可存 200 条（约 3 小时 20 分） */
#define ACK_TIMEOUT_MS       3000u      /* 单次等待云端 ACK 的超时 */
#define ACK_MAX_RETRY        2u         /* 最多重发 2 次，之后转本地缓存 */

/* 原子云的设备编号与密码。真实使用时填自己的 */
#define CLOUD_DEV_ID         "YOUR_DEVICE_ID"
#define CLOUD_DEV_PWD        "YOUR_DEVICE_PWD"

/* ------------------------------------------------------------------
 * 本地缓存载体
 *
 * ⚠️ 这里用 SRAM 数组占位，掉电会丢。
 *    实际工程应该放到「内部 Flash 的末尾扇区」或「外挂 W25Qxx SPI Flash」，
 *    否则断网又断电时数据还是保不住。
 *    之所以这么写，是因为在没有硬件的情况下无法验证 Flash 读写时序，
 *    与其写一段没验证过的 Flash 驱动冒充"完整实现"，不如把接口留干净、把
 *    不确定性明确标出来。存储层只要满足 ringbuf_init() 要求的
 *    「一块连续、可读写、容量够」的缓冲区即可，替换成本很低。
 * ------------------------------------------------------------------ */
static uint8_t       g_cache_storage[LOCAL_CACHE_RECS * APP_FRAME_LEN];
static app_ringbuf_t g_cache;
static app_retrans_t g_retrans;

static uint32_t g_seq        = 0u;      /* 采样序号，单调递增 */
static uint32_t g_tick_ms    = 0u;      /* 软毫秒时基 */
static uint32_t g_last_sample_ms = 0u;  /* 上次采样时刻 */
static uint32_t g_last_heartbeat_ms = 0u;
static uint8_t  g_online     = 0u;      /* 链路是否可用（收到过注册包/ACK） */
static uint32_t g_sample_ok  = 0u;      /* 统计：采集成功次数 */
static uint32_t g_sample_err = 0u;      /* 统计：采集失败次数 */
static uint32_t g_cached     = 0u;      /* 统计：转入本地缓存的帧数 */

/* ------------------------------------------------------------------ */
static void on_boot_fail(const char *stage)
{
    /* 失败即停 —— 而不是带着错误往下跑。
     * 带着错误继续跑的系统，最后表现为「云端偶尔没数据」，
     * 排查成本远高于当场停机并指出失败在哪一步。 */
    uart_printf("[BOOT] FAILED at: %s\r\n", stage);
    while (1)
    {
        delay_ms(500);
    }
}

/* 把一帧数据交给本地缓存（离线时，或重试用尽时） */
static void move_to_local_cache(const uint8_t *frame)
{
    if (ringbuf_push(&g_cache, frame) >= 0)
    {
        g_cached++;
    }
}

/* 采集一次，返回 0 成功 */
static uint8_t do_sample(app_sample_t *out)
{
    uint8_t bits[40];
    dht11_frame_t frame;
    dht11_value_t value;

    /* 走「读原始位流 -> 解析」这条路，
     * 而不是直接用 DHT11_Read_Data()，
     * 这样解析失败时能拿到原始位流看是哪一位错了。 */
    if (DHT11_Read_RawBits(bits) != DHT11_OK)
    {
        return DHT11_ERR;
    }

    if (dht11_decode(bits, &frame) != 0)
    {
        return DHT11_ERR_CRC;
    }

    dht11_frame_to_value(&frame, &value);

    /* 量程检查：超出官方 0~50℃ 时上报异常标记，
     * 而不是把可疑数据当正常值发上去 */
    if (dht11_out_of_range(&value) != 0)
    {
        out->flags |= APP_FLAG_SENSOR_ERR;
    }

    app_sample_from_dht11(value.temp_int, value.temp_dec,
                          value.humi_int, value.humi_dec,
                          g_seq, g_tick_ms / 1000u, out->flags, out);
    return DHT11_OK;
}

/* 上报一帧：在线则发出去并挂进待确认队列，离线则直接进本地缓存 */
static void report_frame(const app_sample_t *s)
{
    uint8_t frame[APP_FRAME_LEN];

    if (app_frame_build(s, frame) != 0)
    {
        return;
    }

    if (g_online == 0u)
    {
        move_to_local_cache(frame);
        return;
    }

    atk_idm751c_send_payload(frame, APP_FRAME_LEN);

    if (retrans_add(&g_retrans, frame, g_tick_ms) != 0)
    {
        /* 待确认队列满了：说明链路虽然"在线"但确认来得很慢。
         * 与其无限堆积，不如先落到本地缓存，等链路真的好了再补。 */
        move_to_local_cache(frame);
    }
}

/* 处理下行数据：注册包 / 云端 ACK */
static void handle_downlink(void)
{
    uint8_t *frame = uart_rx_get_frame();

    if (frame == 0)
    {
        return;
    }

    /* 是否已经注册上原子云（固定返回帧，论文 3.3.2 节） */
    if (atk_idm751c_is_cloud_registered(frame) != 0u)
    {
        if (g_online == 0u)
        {
            uart_printf("[CLOUD] registered, link online\r\n");
        }
        g_online = 1u;
        return;
    }

    /* 云端回执格式（[2026 重建新增] 自定义）："ACK <seq>"
     * 用最低限度的解析，不引入 sscanf —— 嵌入式里它太占空间。 */
    if ((frame[0] == 'A') && (frame[1] == 'C') && (frame[2] == 'K'))
    {
        uint32_t seq = 0u;
        uint16_t i = 4u;
        uint8_t  got = 0u;

        while (frame[i] >= '0' && frame[i] <= '9')
        {
            seq = seq * 10u + (uint32_t)(frame[i] - '0');
            i++;
            got = 1u;
        }
        if (got == 1u)
        {
            (void)retrans_ack(&g_retrans, seq);
        }
    }
}

/* 轮询重传队列 */
static void do_retrans(void)
{
    uint8_t  frame[APP_FRAME_LEN];
    uint32_t seq;
    int      is_giveup;
    int      rc;

    rc = retrans_poll(&g_retrans, g_tick_ms, frame, &seq, &is_giveup);
    if (rc != 1)
    {
        return;
    }

    if (is_giveup == 1)
    {
        /* 重试用尽：这帧彻底发不出去了，转本地缓存等链路恢复。
         * 注意顺序——先入缓存，绝不直接丢弃。 */
        move_to_local_cache(frame);
        uart_printf("[RETRANS] give up seq=%lu -> local cache\r\n",
                    (unsigned long)seq);
        return;
    }

    atk_idm751c_send_payload(frame, APP_FRAME_LEN);
}

/* 补传：链路恢复后，把积压的缓存按顺序补上去 */
static void flush_local_cache(void)
{
    uint8_t buf[APP_FRAME_LEN];
    uint16_t guard = LOCAL_CACHE_RECS;      /* 防止死循环 */

    /* 必须先补完历史、再上新的。
     * 一旦先发当前帧，云端序号会"先跳过去再跳回来"，
     * 时间轴上出现倒流，后处理时极难查。 */
    while ((g_online != 0u) && (ringbuf_count(&g_cache) > 0u) && (guard-- > 0u))
    {
        app_sample_t hist;

        if (ringbuf_pop(&g_cache, buf) != 0)
        {
            break;
        }
        if (app_frame_parse(buf, APP_FRAME_LEN, &hist) != 0)
        {
            continue;       /* 坏帧直接丢 */
        }

        hist.flags |= APP_FLAG_RETRANSMIT;      /* 打上补传标记 */
        app_frame_build(&hist, buf);
        atk_idm751c_send_payload(buf, APP_FRAME_LEN);
    }
}

/* 心跳与状态打印（可挂在这里看设备到底卡在哪一环） */
static void do_heartbeat(void)
{
    if ((g_tick_ms - g_last_heartbeat_ms) < 30000u)
    {
        return;
    }
    g_last_heartbeat_ms = g_tick_ms;

    uart_printf("[STAT] t=%lus online=%u ok=%lu err=%lu cached=%lu pending=%u\r\n",
                (unsigned long)(g_tick_ms / 1000u),
                (unsigned)g_online,
                (unsigned long)g_sample_ok,
                (unsigned long)g_sample_err,
                (unsigned long)g_cached,
                (unsigned)retrans_pending(&g_retrans));
}

int main(void)
{
    app_sample_t s;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* ---- 1. 时基 ---- */
    delay_init();
    delay_calibrate(SystemCoreClock);
    if (delay_selftest(SystemCoreClock) != 0)
    {
        /* 延时不准，DHT11 一定会读错。先在这里拦住。 */
        uart_init(115200u);
        on_boot_fail("delay_selftest");
    }

    /* ---- 2. 串口与 RS485 方向控制 ---- */
    uart_init(115200u);
    RS485_Init();

    /* ---- 3. 本地缓存与重传队列 ---- */
    (void)ringbuf_init(&g_cache, g_cache_storage, LOCAL_CACHE_RECS,
                       APP_FRAME_LEN, RINGBUF_OVERWRITE_OLDEST);
    (void)retrans_init(&g_retrans, ACK_TIMEOUT_MS, ACK_MAX_RETRY);

    uart_printf("\r\n===== road temperature node booting =====\r\n");

    /* ---- 4. 传感器 ---- */
    if (DHT11_Init() != DHT11_OK)
    {
        on_boot_fail("DHT11_Init");
    }
    uart_printf("[BOOT] DHT11 ok\r\n");

    /* ---- 5. 4G DTU ---- */
    if (atk_idm751c_init(115200u) != ATK_IDM751C_EOK)
    {
        on_boot_fail("atk_idm751c_init");
    }
    uart_printf("[BOOT] DTU ok\r\n");

    if (atk_idm751c_set_cloud_account(CLOUD_DEV_ID, CLOUD_DEV_PWD)
            != ATK_IDM751C_EOK)
    {
        on_boot_fail("cloud account");
    }

    if (atk_idm751c_enter_transparent_mode() != ATK_IDM751C_EOK)
    {
        on_boot_fail("transparent mode");
    }

    uart_printf("[BOOT] all modules ready, entering main loop\r\n");

    /* ---- 主循环 ---- */
    while (1)
    {
        delay_ms(10u);
        g_tick_ms += 10u;

        /* 1) 下行：注册包 / ACK */
        handle_downlink();

        /* 2) 补传：链路一恢复就把积压的历史清掉 */
        flush_local_cache();

        /* 3) 采样：到点就采 */
        if ((g_tick_ms - g_last_sample_ms) >= SAMPLE_PERIOD_MS)
        {
            g_last_sample_ms += SAMPLE_PERIOD_MS;

            s.seq       = g_seq;
            s.timestamp = g_tick_ms / 1000u;
            s.temp_x10  = 0;
            s.humi_x10  = 0;
            s.flags     = 0u;

            if (do_sample(&s) == DHT11_OK)
            {
                g_sample_ok++;
                report_frame(&s);
            }
            else
            {
                /* 采集失败不上报脏数据，只记数。
                 * 连续失败到一定次数应该报警，这部分留给后续扩展。 */
                g_sample_err++;
            }

            g_seq++;
        }

        /* 4) 重传轮询 */
        do_retrans();

        /* 5) 心跳 */
        do_heartbeat();
    }
}
