/**
 * @file    pwr.h
 * @brief   Power control registers (RM0383 sec. 5.4).
 */
#ifndef DEVICE_REGS_PWR_H
#define DEVICE_REGS_PWR_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR;  /**< 0x00 power control          */
    __IO uint32_t CSR; /**< 0x04 power control/status   */
} pwr_regs_t;

#define PWR_BASE (APB1PERIPH_BASE + 0x7000UL)
#define PWR      ((pwr_regs_t*)PWR_BASE)

/* ---- CR ----------------------------------------------------------------- */
#define PWR_CR_LPDS    (1UL << 0)  /**< low-power deepsleep          */
#define PWR_CR_PDDS    (1UL << 1)  /**< power down deepsleep         */
#define PWR_CR_CWUF    (1UL << 2)  /**< clear wakeup flag            */
#define PWR_CR_CSBF    (1UL << 3)  /**< clear standby flag           */
#define PWR_CR_PVDE    (1UL << 4)  /**< power voltage detector enable*/
#define PWR_CR_PLS_POS 5U
#define PWR_CR_PLS_MSK (0x7UL << PWR_CR_PLS_POS)
#define PWR_CR_DBP     (1UL << 8)  /**< disable backup domain write protection */
#define PWR_CR_FPDS    (1UL << 9)  /**< flash power down in stop     */
#define PWR_CR_LPLVDS  (1UL << 10)
#define PWR_CR_MRLVDS  (1UL << 11)
#define PWR_CR_ADCDC1  (1UL << 13)
#define PWR_CR_VOS_POS 14U
#define PWR_CR_VOS_MSK (0x3UL << PWR_CR_VOS_POS)

/** Regulator voltage scaling output selection (CR.VOS). */
#define PWR_VOS_SCALE3 0x1UL /**< up to 64 MHz  */
#define PWR_VOS_SCALE2 0x2UL /**< up to 84 MHz  */
#define PWR_VOS_SCALE1 0x3UL /**< up to 100 MHz */

/* ---- CSR ---------------------------------------------------------------- */
#define PWR_CSR_WUF    (1UL << 0)
#define PWR_CSR_SBF    (1UL << 1)
#define PWR_CSR_PVDO   (1UL << 2)
#define PWR_CSR_BRR    (1UL << 3)
#define PWR_CSR_EWUP   (1UL << 8)
#define PWR_CSR_BRE    (1UL << 9)
#define PWR_CSR_VOSRDY (1UL << 14)

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_PWR_H */
