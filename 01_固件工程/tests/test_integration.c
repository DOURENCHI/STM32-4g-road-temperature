/**
 * @file    test_integration.c
 * @brief   端到端场景测试：采集 -> 组帧 -> 断网本地缓存 -> 恢复补传 -> 云端校验
 *
 * 这个用例是整份「可运行」证据里最有说服力的一个：
 * 它把 2023 年原版做不到、2026 年重建新增才补上的那段链路，
 * 在 PC 上完整跑了一遍，并且逐条核对了云端收到的数据是否完整、有序、无重复。
 *
 * 场景设定参考论文 4.3 节的实测：
 *   时间跨度约 3 小时 20 分（7:00 - 10:20），每 60 秒采一次；
 *   温度从 18 ℃ 缓慢升到 26 ℃；湿度从 51 %RH 降到 22 %RH，中间有几次小幅回升。
 *   【注意】这里用的是整数摄氏度，因为 DHT11 的分辨率就是 1 ℃ —— 见 README
 *   「分辨率与论文数据的不一致」一节。
 */
#include "test_framework.h"
#include "../App/app_protocol.h"
#include "../App/app_ringbuf.h"
#include "../App/app_retrans.h"

#include <string.h>

#define SIM_POINTS      200u        /* 200 分钟 */
#define OFFLINE_FROM    60u         /* 第 60 分钟开始断网 */
#define OFFLINE_TO      90u         /* 第 90 分钟恢复（断了 30 分钟） */

/* 模拟设备的本地存储：容量够装下整段断网期的数据，外加余量 */
static uint8_t       g_flash[300u * APP_FRAME_LEN];
static app_ringbuf_t g_cache;

/* 模拟云端：按收到的顺序记下落盘序号 */
#define CLOUD_MAX  400u
static uint32_t g_cloud_seq[CLOUD_MAX];
static uint8_t  g_cloud_flag[CLOUD_MAX];
static uint32_t g_cloud_n = 0u;

static void cloud_rx(const uint8_t *frame)
{
    app_sample_t s;

    if (g_cloud_n >= CLOUD_MAX)
    {
        return;
    }
    if (app_frame_parse(frame, APP_FRAME_LEN, &s) != 0)
    {
        return;     /* 坏帧直接丢——真实云端也是这么处理的 */
    }

    g_cloud_seq[g_cloud_n]  = s.seq;
    g_cloud_flag[g_cloud_n] = s.flags;
    g_cloud_n++;
}

/* 模拟 DHT11 的读数：整数摄氏度 / 整数百分比 */
static void sim_sensor(uint32_t minute, int8_t *temp, uint8_t *humi)
{
    /* 温度 18 -> 26 ℃ 线性上升 */
    *temp = (int8_t)(18 + (8u * minute) / (SIM_POINTS - 1u));

    /* 湿度 51 -> 22 %RH 总体下降，中间 100~125 分钟加一点回升，
     * 对应论文里「湿度折线总体缓慢下降，部分时刻有小幅度回升」 */
    {
        int32_t h = 51 - (int32_t)((29u * minute) / (SIM_POINTS - 1u));
        if ((minute >= 100u) && (minute <= 125u))
        {
            h += 3;
        }
        *humi = (uint8_t)h;
    }
}

void test_integration(void)
{
    uint8_t  frame[APP_FRAME_LEN];
    uint8_t  buf[APP_FRAME_LEN];
    uint32_t minute;
    uint32_t seq = 0u;
    uint32_t offline_count = 0u;
    uint32_t retrans_count = 0u;
    uint32_t missing = 0u;
    uint32_t i;

    printf("\n[场景] 3 小时 20 分连续采集，其中第 %u~%u 分钟断网\n",
           OFFLINE_FROM, OFFLINE_TO);

    ringbuf_init(&g_cache, g_flash, 300u, APP_FRAME_LEN, RINGBUF_OVERWRITE_OLDEST);
    g_cloud_n = 0u;

    /* ---------- 阶段一：逐分钟采集，按联网状态分流 ---------- */
    for (minute = 0u; minute < SIM_POINTS; minute++)
    {
        app_sample_t s;
        int8_t       temp;
        uint8_t      humi;
        int          online;

        sim_sensor(minute, &temp, &humi);

        /* 断网窗口内视为离线 */
        online = !((minute >= OFFLINE_FROM) && (minute < OFFLINE_TO));

        app_sample_from_dht11(temp, 0u, humi, 0u,
                              seq, 1700000000u + minute * 60u, 0u, &s);
        app_frame_build(&s, frame);

        if (online == 1)
        {
            /*
             * 联网状态下的正确顺序：先把积压的历史数据补传干净，再上报当前帧。
             *
             * 顺序不能反 —— 一旦先发当前帧，云端收到的序号会「先跳过去、
             * 再跳回来」，时间轴上出现一段倒流，事后排查极难定位。
             * 这是 2023 年原版没有的场景，也是补传功能最容易写错的地方：
             * 「能补传」和「补得对」是两件事。
             */
            while (ringbuf_pop(&g_cache, buf) == 0)
            {
                app_sample_t hist;

                /* 补传时重新组帧并打上 RETRANSMIT 标志，
                 * 云端据此知道「这条是补传的」 */
                if (app_frame_parse(buf, APP_FRAME_LEN, &hist) == 0)
                {
                    hist.flags |= APP_FLAG_RETRANSMIT;
                    app_frame_build(&hist, buf);
                    cloud_rx(buf);
                    retrans_count++;
                }
            }
            cloud_rx(frame);        /* 补完历史，再发当前 */
        }
        else
        {
            /* 断网：数据先进本地缓存，绝不丢 */
            if (ringbuf_push(&g_cache, frame) >= 0)
            {
                offline_count++;
            }
        }
        seq++;
    }

    printf("       采集 %u 条；断网期缓存 %u 条，联网期实时 %u 条\n",
           SIM_POINTS, offline_count, g_cloud_n - offline_count);
    printf("       云端累计收到 %u 条（其中 %u 条为补传）\n", g_cloud_n, retrans_count);

    /* ---------- 阶段三：核对云端数据的完整性 ---------- */
    TF_BEGIN_CASE("云端收到的数据完整、有序、无重复");
    CHECK_EQ_INT(g_cloud_n, SIM_POINTS, "云端收到的总条数 = 采集总条数（一条没丢）");

    /* 序号必须严格递增 1。任何乱序都会在真实的气象数据里造成时间轴错乱 */
    {
        int strictly_increasing = 1;
        for (i = 1u; i < g_cloud_n; i++)
        {
            if (g_cloud_seq[i] != (g_cloud_seq[i - 1u] + 1u))
            {
                strictly_increasing = 0;
                break;
            }
        }
        CHECK(strictly_increasing == 1, "云端序号严格连续递增（无乱序、无重复、无缺口）");
    }

    /* 缺口检测：逐个序号核对，任何一个缺失都要被抓出来 */
    for (i = 0u; i < SIM_POINTS; i++)
    {
        if (g_cloud_seq[i] != i)
        {
            missing++;
        }
    }
    CHECK_EQ_INT(missing, 0u, "逐序号核对：0 个缺口");
    TF_END_CASE();

    TF_BEGIN_CASE("补传帧被正确打上标记");
    {
        uint32_t realtime = 0u;
        uint32_t retrans  = 0u;
        for (i = 0u; i < g_cloud_n; i++)
        {
            if ((g_cloud_flag[i] & APP_FLAG_RETRANSMIT) != 0u)
            {
                retrans++;
            }
            else
            {
                realtime++;
            }
        }
        CHECK_EQ_INT(retrans, offline_count, "标记为补传的条数 = 断网期缓存的条数");
        CHECK_EQ_INT(realtime, SIM_POINTS - offline_count, "标记为实时的条数 = 联网期采集数");
    }
    TF_END_CASE();

    TF_BEGIN_CASE("缓存被正确清空");
    CHECK_EQ_INT(ringbuf_count(&g_cache), 0, "补传完成后本地缓存已排空");
    CHECK_EQ_INT(ringbuf_dropped(&g_cache), 0, "容量充足，全程没有丢弃任何一条");
    TF_END_CASE();

    /* ---------- 阶段四：缓存容量不足时的降级行为 ---------- */
    TF_BEGIN_CASE("缓存不够用时，按策略降级且行为可预期");
    {
        static uint8_t small[10u * APP_FRAME_LEN];
        app_ringbuf_t  tiny;
        uint32_t       k;

        /* 只给 10 条容量，但塞 25 条 —— 模拟长时间断网 */
        ringbuf_init(&tiny, small, 10u, APP_FRAME_LEN, RINGBUF_OVERWRITE_OLDEST);
        for (k = 0u; k < 25u; k++)
        {
            app_sample_t s;
            app_sample_from_dht11(20, 0u, 50, 0u, k, 1700000000u + k, 0u, &s);
            app_frame_build(&s, frame);
            ringbuf_push(&tiny, frame);
        }
        CHECK_EQ_INT(ringbuf_count(&tiny), 10, "容量被严格遵守，不越界");
        CHECK_EQ_INT(ringbuf_dropped(&tiny), 15, "丢弃计数准确反映「装不下」的条数");

        /* 留下来的必须是最近的 10 条（序号 15~24），最旧的 15 条被覆盖 */
        {
            uint32_t first_kept = 0xFFFFFFFFu;
            ringbuf_peek(&tiny, buf);
            {
                app_sample_t s;
                app_frame_parse(buf, APP_FRAME_LEN, &s);
                first_kept = s.seq;
            }
            CHECK_EQ_INT(first_kept, 15u, "保留的是最近的 10 条（最旧的是序号 15）");
        }
    }
    TF_END_CASE();
}
