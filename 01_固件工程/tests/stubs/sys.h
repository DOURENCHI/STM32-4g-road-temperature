/**
 * @file    sys.h
 * @brief   ⚠️ 同样是一套用于 PC 端语法检查的桩，不是正点原子的真实 sys.h。
 *
 * 真实 sys.h 里 PGout()/PGin() 是位带操作（Bit-Band）宏，展开后
 * 是一个强制类型转换过的左值。这里用普通变量占位，
 * 目的是让「PGout(11) = 0;」这种写法在语法上成立。
 *
 * 真实使用时请使用正点原子例程里的 sys.h。
 */
#ifndef SYS_STUB_H
#define SYS_STUB_H

#include "stm32f10x.h"

/* 占位变量：为了能通过语法检查，不参与任何真实逻辑 */
extern volatile uint32_t g_stub_pin_out;
extern volatile uint32_t g_stub_pin_in;

#define PGout(n)    (g_stub_pin_out)
#define PGin(n)     (g_stub_pin_in)

#endif /* SYS_STUB_H */
