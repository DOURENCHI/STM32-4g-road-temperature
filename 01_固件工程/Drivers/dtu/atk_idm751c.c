/**
 * @file    atk_idm751c.c
 * @brief   ATK-IDM751C（4G Cat.1 DTU）AT 指令交互模块
 * @tag     [未经硬件回归验证]
 *
 * 与论文 3.6 节的对应关系：
 *   本文件在顶部用宏把 uart 层的接口映射成论文里用的名字，
 *   这样对照论文阅读时，函数名仍然能一一对上。
 */
#include "atk_idm751c.h"
#include "uart.h"
#include "delay.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------
 * 论文 3.6 节的接口别名映射
 *   论文里 atk_idm751c_uart_xxx 是模块内部的静态函数；
 *   本工程把它拆成独立的 uart 层（职责更清楚），这里做别名保持可对照。
 * ------------------------------------------------------------------ */
#define atk_idm751c_uart_init(baud)        uart_init(baud)
#define atk_idm751c_uart_rx_restart()      uart_rx_restart()
#define atk_idm751c_uart_printf(...)       uart_printf(__VA_ARGS__)
#define atk_idm751c_uart_rx_get_frame()    uart_rx_get_frame()
#define atk_idm751c_uart_send_buf(d, l)    uart_send_buf((d), (l))

/* ------------------------------------------------------------------
 * AT 指令表
 *
 * ⚠️ 这些字符串**必须**用你手上 ATK-IDM751C 的《AT 指令手册》核对后再用。
 *    指令名参照同系列 4G Cat.1 DTU 的通用形态整理，
 *    不代表与 ATK-IDM751C 逐字一致。详见头文件顶部的说明。
 *
 * 为什么集中成一张表：换模块型号时只改这一处，
 * 上层业务代码一行都不用动。
 * ------------------------------------------------------------------ */
#define ATK_CMD_TEST            "AT"            /* 连通性测试 */
#define ATK_ACK_OK              "OK"

#define ATK_CMD_ENTER_CONFIG    "AT+ENTM"       /* 进出配置/透传模式的切换 */
#define ATK_ACK_ENTER_CONFIG    "OK"

#define ATK_CMD_SET_REG_ID      "AT+REGID="     /* 注册包 / 设备编号（前缀） */
#define ATK_CMD_SET_REG_PWD     "AT+REGKEY="    /* 设备密码（前缀） */

/* ------------------------------------------------------------------
 * 进入配置模式的辅助流程
 *
 * 通用 DTU 的惯例是「总线静默 200ms 后发 +++，收到 'a' 后 3 秒内回 'a'，
 * 再收到 +ok 才算进入 AT 指令模式」。
 * 这一段按同系列模块的通用做法实现；ATK-IDM751C 的实际进入方式
 * 请以手册为准（有的型号是直接发 AT 即可）。
 * ------------------------------------------------------------------ */
#define ATK_ENTER_GUARD_MS      200u    /* +++ 之前必须静默 */
#define ATK_ENTER_REPLY_MS      3000u   /* 回应 'a' 的窗口 */

static uint8_t wait_ack(const char *ack, uint32_t timeout_ms);

uint8_t atk_idm751c_send_cmd_to_dtu(const char *cmd, const char *ack, uint32_t timeout)
{
    uint8_t *ret;

    if (cmd != 0)
    {
        /* 先清接收缓冲再发指令：
         * 保证接下来收到的数据一定是这条指令的应答，
         * 而不是上一条命令的残留回声。这是 AT 交互可靠性的前提。 */
        atk_idm751c_uart_rx_restart();
        atk_idm751c_uart_printf("%s\r\n", cmd);
    }

    /* 不需要等应答（关键字为空或超时为 0）：发完即返回成功 */
    if ((ack == 0) || (timeout == 0u))
    {
        return ATK_IDM751C_EOK;
    }

    while (timeout > 0u)
    {
        ret = atk_idm751c_uart_rx_get_frame();
        if (ret != 0)
        {
            if (strstr((const char *)ret, ack) != 0)
            {
                return ATK_IDM751C_EOK;     /* 收到期望应答 */
            }
            /* 收到了一帧但不是期望的应答：重启接收继续等 */
            atk_idm751c_uart_rx_restart();
        }
        timeout--;
        delay_ms(1u);
    }

    return ATK_IDM751C_ETIMEOUT;
}

static uint8_t wait_ack(const char *ack, uint32_t timeout_ms)
{
    uint8_t *ret = 0;

    while (timeout_ms > 0u)
    {
        ret = atk_idm751c_uart_rx_get_frame();
        if ((ret != 0) && (strstr((const char *)ret, ack) != 0))
        {
            return ATK_IDM751C_EOK;
        }
        timeout_ms--;
        delay_ms(1u);
    }

    return ATK_IDM751C_ETIMEOUT;
}

uint8_t atk_idm751c_at_test(void)
{
    return atk_idm751c_send_cmd_to_dtu(ATK_CMD_TEST, ATK_ACK_OK,
                                       ATK_IDM751C_DEFAULT_TIMEOUT_MS);
}

uint8_t atk_idm751c_dtu_enter_configmode(void)
{
    /* 通用做法：静默 -> 发 +++ -> 等 'a' -> 回 'a' -> 等 +ok */
    delay_ms(ATK_ENTER_GUARD_MS);

    atk_idm751c_uart_rx_restart();
    atk_idm751c_uart_printf("+++");

    if (wait_ack("a", ATK_ENTER_REPLY_MS) != ATK_IDM751C_EOK)
    {
        /* 有的模块不带 +++ 机制，或者本来就已经在指令模式。
         * 这里不直接判死，交给后面的 AT 连通性测试去确认。 */
        return ATK_IDM751C_EOK;
    }

    atk_idm751c_uart_rx_restart();
    atk_idm751c_uart_printf("a");

    if (wait_ack("ok", ATK_ENTER_REPLY_MS) != ATK_IDM751C_EOK)
    {
        return ATK_IDM751C_ERROR;
    }

    return ATK_IDM751C_EOK;
}

uint8_t atk_idm751c_set_cloud_account(const char *dev_id, const char *dev_pwd)
{
    char cmd[64];
    uint8_t rc;

    if ((dev_id == 0) || (dev_pwd == 0))
    {
        return ATK_IDM751C_ERROR;
    }
    if ((strlen(dev_id) >= ATK_CLOUD_ID_MAX_LEN) ||
        (strlen(dev_pwd) >= ATK_CLOUD_PWD_MAX_LEN))
    {
        return ATK_IDM751C_ERROR;
    }

    /* 设备编号 */
    memset(cmd, 0, sizeof(cmd));
    (void)snprintf(cmd, sizeof(cmd), "%s%s", ATK_CMD_SET_REG_ID, dev_id);
    rc = atk_idm751c_send_cmd_to_dtu(cmd, ATK_ACK_OK, ATK_IDM751C_DEFAULT_TIMEOUT_MS);
    if (rc != ATK_IDM751C_EOK)
    {
        return rc;
    }

    /* 设备密码 */
    memset(cmd, 0, sizeof(cmd));
    (void)snprintf(cmd, sizeof(cmd), "%s%s", ATK_CMD_SET_REG_PWD, dev_pwd);
    rc = atk_idm751c_send_cmd_to_dtu(cmd, ATK_ACK_OK, ATK_IDM751C_DEFAULT_TIMEOUT_MS);
    if (rc != ATK_IDM751C_EOK)
    {
        return rc;
    }

    return ATK_IDM751C_EOK;
}

uint8_t atk_idm751c_enter_transparent_mode(void)
{
    return atk_idm751c_send_cmd_to_dtu(ATK_CMD_ENTER_CONFIG, ATK_ACK_ENTER_CONFIG,
                                       ATK_IDM751C_DEFAULT_TIMEOUT_MS);
}

uint8_t atk_idm751c_init(uint32_t baudrate)
{
    atk_idm751c_uart_init(baudrate);

    if (atk_idm751c_dtu_enter_configmode() != ATK_IDM751C_EOK)
    {
        return ATK_IDM751C_ERROR;
    }

    /* AT 连通性测试 —— 这一步失败说明硬件链路本身有问题
     * （波特率不对、A/B 接反、模块没供电、SIM 卡未插好），
     * 不应该带着这个错误继续往下配置。 */
    if (atk_idm751c_at_test() != ATK_IDM751C_EOK)
    {
        return ATK_IDM751C_ERROR;
    }

    return ATK_IDM751C_EOK;
}

uint8_t atk_idm751c_is_cloud_registered(const uint8_t *frame)
{
    /* 原子云注册成功的固定返回帧（出处：论文 3.3.2 节） */
    static const uint8_t REG_PACKET[6] = {0xAFu, 0x20u, 0x7Eu, 0x00u, 0x01u, 0x5Au};

    if (frame == 0)
    {
        return 0u;
    }

    /* 注册包可能出现在一帧数据的任意位置，这里做子串匹配 */
    {
        uint16_t i;
        for (i = 0u; i < 64u; i++)      /* 只在合理长度内搜索，避免越界 */
        {
            if (memcmp(&frame[i], REG_PACKET, sizeof(REG_PACKET)) == 0)
            {
                return 1u;
            }
            /* 命中结束符就停 */
            if (frame[i] == 0u)
            {
                break;
            }
        }
    }

    return 0u;
}

void atk_idm751c_send_payload(const uint8_t *frame, uint16_t len)
{
    if ((frame == 0) || (len == 0u))
    {
        return;
    }

    /* 透传模式：串口送什么，DTU 就在网络上原样发什么。
     * 不需要自己实现 TCP/IP —— 这正是选 DTU 而不是裸模组的价值所在。 */
    atk_idm751c_uart_send_buf(frame, len);
}
