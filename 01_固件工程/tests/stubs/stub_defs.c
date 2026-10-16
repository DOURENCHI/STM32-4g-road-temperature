/**
 * @file    stub_defs.c
 * @brief   ⚠️ 桩实现：只为让 PC 端语法检查能链接通过，不含任何真实逻辑。
 *          固件编译时**不要**包含这个文件。
 */
#include "stm32f10x.h"

uint32_t SystemCoreClock = 72000000u;

volatile uint32_t g_stub_pin_out = 0u;
volatile uint32_t g_stub_pin_in  = 0u;

/* 下面这些空壳只为满足链接，绝不参与真实硬件行为 */
void GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *s) { (void)GPIOx; (void)s; }
void GPIO_SetBits(GPIO_TypeDef *GPIOx, uint16_t p) { (void)GPIOx; (void)p; }
void GPIO_ResetBits(GPIO_TypeDef *GPIOx, uint16_t p) { (void)GPIOx; (void)p; }

void USART_Init(USART_TypeDef *u, USART_InitTypeDef *s) { (void)u; (void)s; }
void USART_Cmd(USART_TypeDef *u, FunctionalState s) { (void)u; (void)s; }
void USART_ITConfig(USART_TypeDef *u, uint16_t i, FunctionalState s) { (void)u; (void)i; (void)s; }
FlagStatus USART_GetFlagStatus(USART_TypeDef *u, uint16_t f) { (void)u; (void)f; return SET; }
ITStatus   USART_GetITStatus(USART_TypeDef *u, uint16_t i) { (void)u; (void)i; return RESET; }
void       USART_ClearITPendingBit(USART_TypeDef *u, uint16_t i) { (void)u; (void)i; }
uint16_t   USART_ReceiveData(USART_TypeDef *u) { (void)u; return 0u; }
void       USART_SendData(USART_TypeDef *u, uint16_t d) { (void)u; (void)d; }

void RCC_APB2PeriphClockCmd(uint32_t p, FunctionalState s) { (void)p; (void)s; }
void RCC_APB1PeriphClockCmd(uint32_t p, FunctionalState s) { (void)p; (void)s; }

void NVIC_Init(NVIC_InitTypeDef *i) { (void)i; }
void NVIC_PriorityGroupConfig(uint32_t g) { (void)g; }
