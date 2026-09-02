/**
 * @file    i2c.h
 * @brief   I2C registers (RM0383 sec. 18.6).
 */
#ifndef DEVICE_REGS_I2C_H
#define DEVICE_REGS_I2C_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR1;   /**< 0x00 control 1            */
    __IO uint32_t CR2;   /**< 0x04 control 2            */
    __IO uint32_t OAR1;  /**< 0x08 own address 1        */
    __IO uint32_t OAR2;  /**< 0x0C own address 2        */
    __IO uint32_t DR;    /**< 0x10 data                 */
    __IO uint32_t SR1;   /**< 0x14 status 1             */
    __IO uint32_t SR2;   /**< 0x18 status 2             */
    __IO uint32_t CCR;   /**< 0x1C clock control        */
    __IO uint32_t TRISE; /**< 0x20 rise time            */
    __IO uint32_t FLTR;  /**< 0x24 noise filter         */
} i2c_regs_t;

#define I2C1_BASE (APB1PERIPH_BASE + 0x5400UL)
#define I2C2_BASE (APB1PERIPH_BASE + 0x5800UL)
#define I2C3_BASE (APB1PERIPH_BASE + 0x5C00UL)

#define I2C1 ((i2c_regs_t*)I2C1_BASE)
#define I2C2 ((i2c_regs_t*)I2C2_BASE)
#define I2C3 ((i2c_regs_t*)I2C3_BASE)

/* ---- CR1 ---------------------------------------------------------------- */
#define I2C_CR1_PE         (1UL << 0)
#define I2C_CR1_SMBUS      (1UL << 1)
#define I2C_CR1_SMBTYPE    (1UL << 3)
#define I2C_CR1_ENARP      (1UL << 4)
#define I2C_CR1_ENPEC      (1UL << 5)
#define I2C_CR1_ENGC       (1UL << 6)
#define I2C_CR1_NOSTRETCH  (1UL << 7)
#define I2C_CR1_START      (1UL << 8)
#define I2C_CR1_STOP       (1UL << 9)
#define I2C_CR1_ACK        (1UL << 10)
#define I2C_CR1_POS        (1UL << 11)
#define I2C_CR1_PEC        (1UL << 12)
#define I2C_CR1_ALERT      (1UL << 13)
#define I2C_CR1_SWRST      (1UL << 15)

/* ---- CR2 ---------------------------------------------------------------- */
#define I2C_CR2_FREQ_MSK (0x3FUL << 0) /**< APB1 clock in MHz */
#define I2C_CR2_ITERREN  (1UL << 8)
#define I2C_CR2_ITEVTEN  (1UL << 9)
#define I2C_CR2_ITBUFEN  (1UL << 10)
#define I2C_CR2_DMAEN    (1UL << 11)
#define I2C_CR2_LAST     (1UL << 12)

/* ---- SR1 ---------------------------------------------------------------- */
#define I2C_SR1_SB       (1UL << 0)
#define I2C_SR1_ADDR     (1UL << 1)
#define I2C_SR1_BTF      (1UL << 2)
#define I2C_SR1_ADD10    (1UL << 3)
#define I2C_SR1_STOPF    (1UL << 4)
#define I2C_SR1_RXNE     (1UL << 6)
#define I2C_SR1_TXE      (1UL << 7)
#define I2C_SR1_BERR     (1UL << 8)
#define I2C_SR1_ARLO     (1UL << 9)
#define I2C_SR1_AF       (1UL << 10)
#define I2C_SR1_OVR      (1UL << 11)
#define I2C_SR1_PECERR   (1UL << 12)
#define I2C_SR1_TIMEOUT  (1UL << 14)
#define I2C_SR1_SMBALERT (1UL << 15)

/* ---- SR2 ---------------------------------------------------------------- */
#define I2C_SR2_MSL     (1UL << 0)
#define I2C_SR2_BUSY    (1UL << 1)
#define I2C_SR2_TRA     (1UL << 2)
#define I2C_SR2_GENCALL (1UL << 4)
#define I2C_SR2_DUALF   (1UL << 7)

/* ---- CCR ---------------------------------------------------------------- */
#define I2C_CCR_CCR_MSK (0xFFFUL << 0)
#define I2C_CCR_DUTY    (1UL << 14) /**< fast mode 16/9 duty cycle */
#define I2C_CCR_FS      (1UL << 15) /**< 1 = fast mode             */

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_I2C_H */
