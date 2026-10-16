/**
 * @file    atk_idm751c.h
 * @brief   ATK-IDM751C（4G Cat.1 DTU）AT 指令交互模块
 *
 * @tag     [2023 实做]  本模块的函数骨架与论文 3.6 节完全对应：
 *          atk_idm751c_init() / atk_idm751c_dtu_enter_configmode() /
 *          atk_idm751c_at_test() / atk_idm751c_send_cmd_to_dtu()
 *          错误码 ATK_IDM751C_EOK / _ERROR / _ETIMEOUT 也沿用论文写法。
 *          论文里那个「发送指令 -> 在超时窗口内等待期望应答 -> 匹配成功才算成功」
 *          的通用函数，就是本文件的 atk_idm751c_send_cmd_to_dtu()。
 *
 * @tag     [未经硬件回归验证]
 *
 * ------------------------------------------------------------------
 * ⚠️ 关于 AT 指令文本的重要说明（请务必先读）
 *
 * ATK-IDM751C 的《AT 指令手册》随模块资料包发放在正点原子的百度网盘里，
 * 本次重建时无法取得原始文档。因此：
 *   - 下面 ATK_CMD_xxx 这些指令**文本形态参照同系列 4G Cat.1 DTU 的通用做法**
 *     （指令模式 / 网络透传模式的切换、注册包、心跳包等），
 *   - **不代表 ATK-IDM751C 逐字一致**。
 *
 * 使用前请用你手上模块的手册核对每一个字符串。
 * 这样做而不是"随便编几个看起来对的指令"，是因为：
 * 编出来的指令在面试现场一旦被追问"这句 AT 具体返回什么"就会当场露馅。
 * 把不确定性明确标出来，反而是更稳的做法。
 *
 * 论文里有据、可以放心使用的常量只有一个：
 *   原子云注册成功的固定返回帧  0xAF 0x20 0x7E 0x00 0x01 0x5A
 *   （出处：论文 3.3.2 节）
 * ------------------------------------------------------------------
 *
 * 速率等级选择依据（论文 3.3.1 / 面试稿 Q11）：
 *   Cat.1 上下行约 5 / 10 Mbps，成本、功耗、速率居中；
 *   Cat.4 约 50 / 150 Mbps，对温湿度这种小数据量是浪费；
 *   NB-IoT 只有百 kbps 级，实时性与交互性偏弱。
 *   温湿度数据量很小但要能实时回传、还要能远程配置，Cat.1 是平衡点。
 */
#ifndef ATK_IDM751C_H
#define ATK_IDM751C_H

#include <stdint.h>

/* 返回值（与论文一致） */
#define ATK_IDM751C_EOK         0u   /* 成功 */
#define ATK_IDM751C_ERROR       1u   /* 初始化/执行失败 */
#define ATK_IDM751C_ETIMEOUT    2u   /* 等待期望应答超时 */

/* 默认超时（毫秒）。AT 指令的应答通常在几十毫秒内到达；
 * 给 1000ms 是为了覆盖模块刚上电、还没稳定时的情况。 */
#define ATK_IDM751C_DEFAULT_TIMEOUT_MS  1000u

/* 设备编号与密码的长度上限（连原子云用） */
#define ATK_CLOUD_ID_MAX_LEN     24u
#define ATK_CLOUD_PWD_MAX_LEN    24u

/**
 * @brief 模块初始化
 * @param baudrate 串口波特率（IDM751C 常用 115200）
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ERROR
 *
 * 流程（每一步失败就返回错误码，不带着错误继续往下跑）：
 *   1. 串口初始化
 *   2. 进入配置模式
 *   3. AT 连通性测试
 *   4. 配置设备编号与密码（连原子云）
 *   5. 切换到网络透传模式
 */
uint8_t atk_idm751c_init(uint32_t baudrate);

/**
 * @brief 让模块进入配置模式
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ERROR
 */
uint8_t atk_idm751c_dtu_enter_configmode(void);

/**
 * @brief AT 连通性测试（发 "AT" 等 "OK"）
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ETIMEOUT
 */
uint8_t atk_idm751c_at_test(void);

/**
 * @brief 通过 DTU 把设备编号与密码写入模块（原子云注册用）
 * @param dev_id  设备编号
 * @param dev_pwd 设备密码
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ETIMEOUT
 */
uint8_t atk_idm751c_set_cloud_account(const char *dev_id, const char *dev_pwd);

/**
 * @brief 切换到网络透传模式
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ETIMEOUT
 */
uint8_t atk_idm751c_enter_transparent_mode(void);

/**
 * @brief 发送一条 AT 指令并等待期望应答 ——【本模块的核心函数】
 * @param cmd     待发送的 AT 指令（不含结尾的 \r\n，函数内部会补）
 * @param ack     期望在应答里出现的关键字；传 NULL 或 timeout=0 表示不等应答
 * @param timeout 等待超时（毫秒）
 * @return ATK_IDM751C_EOK / ATK_IDM751C_ETIMEOUT
 *
 * 设计要点（面试稿第 5.4 节重点讲的就是这个函数）：
 *   把「命令已经发出」变成「命令已经确认生效」。
 *   模块、网络、SIM 卡任何一环出问题，都会在这里的超时里暴露出来，
 *   而不是等到最后发现云端没数据再回头找。
 *
 * 三层故障定位合起来看：
 *   第 1 层 本函数      —— 指令有没有被模块接受
 *   第 2 层 app_retrans —— 这一帧数据云端有没有收到（ACK）
 *   第 3 层 app_ringbuf —— 彻底发不出去时，数据先存本地
 */
uint8_t atk_idm751c_send_cmd_to_dtu(const char *cmd, const char *ack, uint32_t timeout);

/**
 * @brief 检查是否收到了「已注册到原子云」的固定注册包
 * @param frame 一帧接收数据
 * @return 1 已连上原子云；0 未连接
 *
 * 判据来源（论文 3.3.2 节）：
 *   DTU 注册上原子云后，服务器固定返回
 *   0xAF 0x20 0x7E 0x00 0x01 0x5A
 */
uint8_t atk_idm751c_is_cloud_registered(const uint8_t *frame);

/**
 * @brief 通过透传通道发送一帧应用层数据
 * @param frame 已组好的数据帧
 * @param len   帧长度
 * @note  透传模式下，串口送进去什么，DTU 就在网络上原样发出去，
 *        所以这里只需要把字节写出去，不需要自己实现 TCP/IP。
 */
void atk_idm751c_send_payload(const uint8_t *frame, uint16_t len);

#endif /* ATK_IDM751C_H */
