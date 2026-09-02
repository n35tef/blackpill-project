/**
 * @file    flash.h
 * @brief   Embedded flash interface registers (RM0383 sec. 3.6).
 *
 * Only the access-control register matters for normal operation (wait states,
 * caches, prefetch); the rest are needed for erase/program.
 */
#ifndef DEVICE_REGS_FLASH_H
#define DEVICE_REGS_FLASH_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t ACR;     /**< 0x00 access control  */
    __IO uint32_t KEYR;    /**< 0x04 key             */
    __IO uint32_t OPTKEYR; /**< 0x08 option key      */
    __IO uint32_t SR;      /**< 0x0C status          */
    __IO uint32_t CR;      /**< 0x10 control         */
    __IO uint32_t OPTCR;   /**< 0x14 option control  */
} flash_regs_t;

#define FLASH_R_BASE (AHB1PERIPH_BASE + 0x3C00UL)
#define FLASH_R      ((flash_regs_t*)FLASH_R_BASE)

/* ---- ACR ---------------------------------------------------------------- */
#define FLASH_ACR_LATENCY_POS 0U
/*
 * Three bits, not four. RM0383 prints the field as LATENCY[3:0], but ST's own
 * F411 header and ST's F411 SVD both define it as 0x7 and stop at 7 wait
 * states; the 16-state form belongs to the F42x/F43x/F446/F469 parts. It makes
 * no practical difference - the F411 tops out at 100 MHz, which is 3 wait
 * states - so the narrower mask is used to avoid writing a bit ST treats as
 * reserved here.
 */
#define FLASH_ACR_LATENCY_MSK (0x7UL << FLASH_ACR_LATENCY_POS)
#define FLASH_ACR_PRFTEN      (1UL << 8)  /**< prefetch enable      */
#define FLASH_ACR_ICEN        (1UL << 9)  /**< instruction cache    */
#define FLASH_ACR_DCEN        (1UL << 10) /**< data cache           */
#define FLASH_ACR_ICRST       (1UL << 11) /**< instruction cache rst*/
#define FLASH_ACR_DCRST       (1UL << 12) /**< data cache reset     */

/* ---- SR ----------------------------------------------------------------- */
#define FLASH_SR_EOP    (1UL << 0)
#define FLASH_SR_OPERR  (1UL << 1)
#define FLASH_SR_WRPERR (1UL << 4)
#define FLASH_SR_PGAERR (1UL << 5)
#define FLASH_SR_PGPERR (1UL << 6)
#define FLASH_SR_PGSERR (1UL << 7)
#define FLASH_SR_BSY    (1UL << 16)

/* ---- CR ----------------------------------------------------------------- */
#define FLASH_CR_PG      (1UL << 0)
#define FLASH_CR_SER     (1UL << 1)
#define FLASH_CR_MER     (1UL << 2)
#define FLASH_CR_SNB_POS 3U
#define FLASH_CR_SNB_MSK (0xFUL << FLASH_CR_SNB_POS)
#define FLASH_CR_PSIZE_POS 8U
#define FLASH_CR_PSIZE_MSK (0x3UL << FLASH_CR_PSIZE_POS)
#define FLASH_CR_STRT    (1UL << 16)
#define FLASH_CR_EOPIE   (1UL << 24)
#define FLASH_CR_ERRIE   (1UL << 25)
#define FLASH_CR_LOCK    (1UL << 31)

/** Unlock keys for FLASH->KEYR (RM0383 sec. 3.5.1). */
#define FLASH_KEY1 0x45670123UL
#define FLASH_KEY2 0xCDEF89ABUL

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_FLASH_H */
