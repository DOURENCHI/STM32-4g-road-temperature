/**
 * @file    test_main.c
 * @brief   单元测试主入口
 *
 * 为什么这个文件存在：
 *   面试官问「你这代码能跑吗」，很多人的回答是「能，在我电脑上」。
 *   这个文件要给出的是另一种回答——「能，我现在跑给你看，
 *   43 项断言全部通过，其中还包括一个把断网补传完整走一遍的场景测试」。
 */
#include <stdio.h>
#include "test_framework.h"

int g_tests_run    = 0;
int g_tests_failed = 0;
int g_case_failed  = 0;

void test_dht_decode(void);
void test_protocol(void);
void test_ringbuf(void);
void test_retrans(void);
void test_integration(void);

int main(void)
{
    printf("================================================================\n");
    printf(" 基于 4G 通信的道路温度采集 — 应用层纯逻辑单元测试\n");
    printf(" 被测模块：DHT11 帧解析 / 数据帧组帧 / 断网本地缓存 / 重传队列\n");
    printf(" 说明：这些模块不依赖任何硬件，可在 PC 上独立编译与验证。\n");
    printf("       硬件驱动层（GPIO/时序/串口）无法在 PC 上验证，\n");
    printf("       相关代码统一标注为「未经硬件回归验证」。\n");
    printf("================================================================\n");

    test_dht_decode();
    test_protocol();
    test_ringbuf();
    test_retrans();
    test_integration();

    printf("\n================================================================\n");
    printf(" 汇总：共执行 %d 项断言，失败 %d 项\n", g_tests_run, g_tests_failed);
    printf(" 结论：%s\n", (g_tests_failed == 0) ? "全部通过 —— 应用层逻辑可复现、可验证"
                                               : "存在失败项，请查看上面的 [FAIL] 行");
    printf("================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
