/**
 * @file    test_framework.h
 * @brief   极简单元测试框架（不用第三方库，保证在任何机器上都能一键编译）
 *
 * 为什么不用 Unity / CMock：这个工程要在面试现场演示「怎么验证代码」，
 * 依赖越少越不容易出问题。整个测试框架就是下面这几个宏。
 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

extern int g_tests_run;
extern int g_tests_failed;
extern int g_case_failed;   /* 当前用例内失败数 */

#define TF_BEGIN_CASE(name)                     \
    do {                                        \
        g_case_failed = 0;                      \
        printf("\n[用例] %s\n", name);          \
    } while (0)

#define TF_END_CASE()                           \
    do {                                        \
        if (g_case_failed == 0) {               \
            printf("       ^ 全部通过\n");      \
        } else {                                \
            printf("       ^ 本用例失败 %d 项\n", g_case_failed); \
        }                                       \
    } while (0)

#define CHECK(cond, msg)                                        \
    do {                                                        \
        g_tests_run++;                                          \
        if (cond) {                                             \
            printf("       [PASS] %s\n", msg);                  \
        } else {                                                \
            g_tests_failed++;                                   \
            g_case_failed++;                                    \
            printf("       [FAIL] %s   <= 第 %d 行\n", msg, __LINE__); \
        }                                                       \
    } while (0)

#define CHECK_EQ_INT(actual, expect, msg)                                   \
    do {                                                                    \
        long _a = (long)(actual);                                           \
        long _e = (long)(expect);                                           \
        g_tests_run++;                                                      \
        if (_a == _e) {                                                     \
            printf("       [PASS] %s (=%ld)\n", msg, _a);                    \
        } else {                                                            \
            g_tests_failed++;                                               \
            g_case_failed++;                                                \
            printf("       [FAIL] %s  实际=%ld 期望=%ld  <= 第 %d 行\n",     \
                   msg, _a, _e, __LINE__);                                  \
        }                                                                   \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
