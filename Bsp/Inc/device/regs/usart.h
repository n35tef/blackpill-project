/**
 * @file    usart.h
 * @brief   USART registers (RM0383 sec. 19.6).
 */
#ifndef DEVICE_REGS_USART_H
#define DEVICE_REGS_USART_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t SR;   /**< 0x00 status               */
    __IO uint32_t DR;   /**< 0x04 data                 */
    __IO uint32_t BRR;  /**< 0x08 baud rate            */
    __IO uint32_t CR1;  /**< 0x0C control 1            */
    __IO uint32_t CR2;  /**< 0x10 control 2            */
    __IO uint32_t CR3;  /**< 0x14 control 3            */
    __IO uint32_t GTPR; /**< 0x18 guard time/prescaler */
} usart_regs_t;

#define USART1_BASE (APB2PERIPH_BASE + 0x1000UL)
#define USART2_BASE (APB1PERIPH_BASE + 0x4400UL)
#define USART6_BASE (APB2PERIPH_BASE + 0x1400UL)

#define USART1 ((usart_regs_t*)USART1_BASE)
#define USART2 ((usart_regs_t*)USART2_BASE)
#define USART6 ((usart_regs_t*)USART6_BASE)

/* ---- SR ----------------------------------------------------------------- */
#define USART_SR_PE   (1UL << 0)
#define USART_SR_FE   (1UL << 1)
#define USART_SR_NF   (1UL << 2)
#define USART_SR_ORE  (1UL << 3)
#define USART_SR_IDLE (1UL << 4)
#define USART_SR_RXNE (1UL << 5)
#define USART_SR_TC   (1UL << 6)
#define USART_SR_TXE  (1UL << 7)
#define USART_SR_LBD  (1UL << 8)
#define USART_SR_CTS  (1UL << 9)

/* ---- CR1 ---------------------------------------------------------------- */
#define USART_CR1_SBK    (1UL << 0)
#define USART_CR1_RWU    (1UL << 1)
#define USART_CR1_RE     (1UL << 2)
#define USART_CR1_TE     (1UL << 3)
#define USART_CR1_IDLEIE (1UL << 4)
#define USART_CR1_RXNEIE (1UL << 5)
#define USART_CR1_TCIE   (1UL << 6)
#define USART_CR1_TXEIE  (1UL << 7)
#define USART_CR1_PEIE   (1UL << 8)
#define USART_CR1_PS     (1UL << 9)  /**< 0 = even, 1 = odd  */
#define USART_CR1_PCE    (1UL << 10) /**< parity enable      */
#define USART_CR1_WAKE   (1UL << 11)
#define USART_CR1_M      (1UL << 12) /**< 1 = 9 data bits    */
#define USART_CR1_UE     (1UL << 13)
#define USART_CR1_OVER8  (1UL << 15) /**< 8x oversampling    */

/* ---- CR2 ---------------------------------------------------------------- */
#define USART_CR2_ADD_MSK  (0xFUL << 0)
#define USART_CR2_LBDL     (1UL << 5)
#define USART_CR2_LBDIE    (1UL << 6)
#define USART_CR2_LBCL     (1UL << 8)
#define USART_CR2_CPHA     (1UL << 9)
#define USART_CR2_CPOL     (1UL << 10)
#define USART_CR2_CLKEN    (1UL << 11)
#define USART_CR2_STOP_POS 12U
#define USART_CR2_STOP_MSK (0x3UL << USART_CR2_STOP_POS)
#define USART_CR2_LINEN    (1UL << 14)

/** Stop bit encodings (CR2.STOP). */
#define USART_STOP_1   0x0UL
#define USART_STOP_0P5 0x1UL
#define USART_STOP_2   0x2UL
#define USART_STOP_1P5 0x3UL

/* ---- CR3 ---------------------------------------------------------------- */
#define USART_CR3_EIE    (1UL << 0)
#define USART_CR3_IREN   (1UL << 1)
#define USART_CR3_IRLP   (1UL << 2)
#define USART_CR3_HDSEL  (1UL << 3)
#define USART_CR3_NACK   (1UL << 4)
#define USART_CR3_SCEN   (1UL << 5)
#define USART_CR3_DMAR   (1UL << 6)
#define USART_CR3_DMAT   (1UL << 7)
#define USART_CR3_RTSE   (1UL << 8)
#define USART_CR3_CTSE   (1UL << 9)
#define USART_CR3_CTSIE  (1UL << 10)
#define USART_CR3_ONEBIT (1UL << 11)

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_USART_H */
