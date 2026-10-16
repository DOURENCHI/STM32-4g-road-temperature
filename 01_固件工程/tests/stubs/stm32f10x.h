/**
 * @file    stm32f10x.h
 * @brief   ⚠️ 这不是真实的 STM32 头文件，只是一套用于「PC 端语法检查」的桩。
 *
 * 为什么需要它：
 *   硬件驱动层（GPIO 操作、串口收发、寄存器访问）没法在 PC 上真跑，
 *   但如果连语法都不检查一遍就交付，错别字、少个分号、函数名拼错
 *   这类低级问题只能等烧到板子上才发现 —— 而板子已经不在手上了。
 *
 *   所以这里提供一份最小的类型/宏/函数声明，让 gcc 能对驱动层做
 *   -fsyntax-only 检查。这**不能**证明逻辑正确，只能证明「它是一份
 *   语法正确、能通过编译的 C 代码」。
 *
 * 使用时通过 -I 把它放在包含路径最前面：
 *   gcc -fsyntax-only -I tests/stubs -I App ... Core/main.c Drivers 下的全部 .c
 *
 * 真正编译固件时，请用 ST 官方的标准外设库（STM32F10x_StdPeriph_Driver）。
 */
#ifndef STM32F10X_STUB_H
#define STM32F10X_STUB_H

#include <stdint.h>

/* ---------------- 正点原子常用的简写类型 ---------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* ---------------- 状态枚举 ---------------- */
typedef enum { RESET = 0, SET = 1 } FlagStatus, ITStatus;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;

/* ---------------- GPIO ---------------- */
typedef struct
{
    volatile uint32_t CRL;
    volatile uint32_t CRH;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t BRR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef enum
{
    GPIO_Speed_10MHz = 1,
    GPIO_Speed_2MHz,
    GPIO_Speed_50MHz
} GPIOSpeed_TypeDef;

typedef enum
{
    GPIO_Mode_AIN          = 0x0,
    GPIO_Mode_IN_FLOATING  = 0x04,
    GPIO_Mode_IPD          = 0x28,
    GPIO_Mode_IPU          = 0x48,
    GPIO_Mode_Out_OD       = 0x14,
    GPIO_Mode_Out_PP       = 0x10,
    GPIO_Mode_AF_OD        = 0x1C,
    GPIO_Mode_AF_PP        = 0x18
} GPIOMode_TypeDef;

typedef struct
{
    uint16_t         GPIO_Pin;
    GPIOSpeed_TypeDef GPIO_Speed;
    GPIOMode_TypeDef  GPIO_Mode;
} GPIO_InitTypeDef;

#define GPIO_Pin_0    ((uint16_t)0x0001)
#define GPIO_Pin_2    ((uint16_t)0x0004)
#define GPIO_Pin_3    ((uint16_t)0x0008)
#define GPIO_Pin_7    ((uint16_t)0x0080)
#define GPIO_Pin_11   ((uint16_t)0x0800)

/* 基地址取真实值的低位差异无关紧要 —— 这里只做语法检查 */
#define GPIOA   ((GPIO_TypeDef *)0x40010800u)
#define GPIOB   ((GPIO_TypeDef *)0x40010C00u)
#define GPIOD   ((GPIO_TypeDef *)0x40011400u)
#define GPIOG   ((GPIO_TypeDef *)0x40012000u)

void GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_InitStruct);
void GPIO_SetBits(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
void GPIO_ResetBits(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);

/* ---------------- USART ---------------- */
typedef struct
{
    volatile uint16_t SR;   uint16_t r0;
    volatile uint16_t DR;   uint16_t r1;
    volatile uint16_t BRR;  uint16_t r2;
    volatile uint16_t CR1;  uint16_t r3;
    volatile uint16_t CR2;  uint16_t r4;
    volatile uint16_t CR3;  uint16_t r5;
    volatile uint16_t GTPR; uint16_t r6;
} USART_TypeDef;

#define USART1  ((USART_TypeDef *)0x40013800u)
#define USART2  ((USART_TypeDef *)0x40004400u)

typedef struct
{
    uint32_t USART_BaudRate;
    uint16_t USART_WordLength;
    uint16_t USART_StopBits;
    uint16_t USART_Parity;
    uint16_t USART_Mode;
    uint16_t USART_HardwareFlowControl;
} USART_InitTypeDef;

#define USART_WordLength_8b             ((uint16_t)0x0000)
#define USART_StopBits_1                ((uint16_t)0x0000)
#define USART_Parity_No                 ((uint16_t)0x0000)
#define USART_Mode_Rx                   ((uint16_t)0x0004)
#define USART_Mode_Tx                   ((uint16_t)0x0008)
#define USART_HardwareFlowControl_None  ((uint16_t)0x0000)

void USART_Init(USART_TypeDef *USARTx, USART_InitTypeDef *init);
void USART_Cmd(USART_TypeDef *USARTx, FunctionalState state);
void USART_ITConfig(USART_TypeDef *USARTx, uint16_t it, FunctionalState state);
FlagStatus USART_GetFlagStatus(USART_TypeDef *USARTx, uint16_t flag);
ITStatus   USART_GetITStatus(USART_TypeDef *USARTx, uint16_t it);
void       USART_ClearITPendingBit(USART_TypeDef *USARTx, uint16_t it);
uint16_t   USART_ReceiveData(USART_TypeDef *USARTx);
void       USART_SendData(USART_TypeDef *USARTx, uint16_t data);

#define USART_IT_RXNE   ((uint16_t)0x0525)
#define USART_IT_IDLE   ((uint16_t)0x0424)
#define USART_FLAG_TXE  ((uint16_t)0x0080)
#define USART_FLAG_TC   ((uint16_t)0x0040)

/* ---------------- RCC ---------------- */
void RCC_APB2PeriphClockCmd(uint32_t periph, FunctionalState state);
void RCC_APB1PeriphClockCmd(uint32_t periph, FunctionalState state);

#define RCC_APB2Periph_GPIOA   ((uint32_t)0x00000004)
#define RCC_APB2Periph_GPIOB   ((uint32_t)0x00000008)
#define RCC_APB2Periph_GPIOD   ((uint32_t)0x00000020)
#define RCC_APB2Periph_GPIOG   ((uint32_t)0x00000100)
#define RCC_APB2Periph_AFIO    ((uint32_t)0x00000001)
#define RCC_APB1Periph_USART2  ((uint32_t)0x00020000)

/* ---------------- NVIC ---------------- */
typedef enum
{
    USART1_IRQn = 37,
    USART2_IRQn = 38,
    USART3_IRQn = 39
} IRQn_Type;

typedef struct
{
    uint8_t  NVIC_IRQChannel;
    uint8_t  NVIC_IRQChannelPreemptionPriority;
    uint8_t  NVIC_IRQChannelSubPriority;
    FunctionalState NVIC_IRQChannelCmd;
} NVIC_InitTypeDef;

void NVIC_Init(NVIC_InitTypeDef *init);
void NVIC_PriorityGroupConfig(uint32_t group);

#define NVIC_PriorityGroup_2   ((uint32_t)0x00000500)

/* ---------------- 系统时钟 ---------------- */
extern uint32_t SystemCoreClock;

#endif /* STM32F10X_STUB_H */
