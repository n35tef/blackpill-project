/**
 * @file    tim.h
 * @brief   Timer registers (RM0383 sec. 12-15).
 *
 * TIM1 (advanced), TIM2/3/4/5 (general purpose) and TIM9/10/11 (basic general
 * purpose) all share the same register block layout; the simpler timers just
 * leave the higher registers reserved. One struct therefore covers them all.
 */
#ifndef DEVICE_REGS_TIM_H
#define DEVICE_REGS_TIM_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR1;   /**< 0x00 control 1                 */
    __IO uint32_t CR2;   /**< 0x04 control 2                 */
    __IO uint32_t SMCR;  /**< 0x08 slave mode control        */
    __IO uint32_t DIER;  /**< 0x0C DMA/interrupt enable      */
    __IO uint32_t SR;    /**< 0x10 status                    */
    __O uint32_t EGR;    /**< 0x14 event generation          */
    __IO uint32_t CCMR1; /**< 0x18 capture/compare mode 1    */
    __IO uint32_t CCMR2; /**< 0x1C capture/compare mode 2    */
    __IO uint32_t CCER;  /**< 0x20 capture/compare enable    */
    __IO uint32_t CNT;   /**< 0x24 counter                   */
    __IO uint32_t PSC;   /**< 0x28 prescaler                 */
    __IO uint32_t ARR;   /**< 0x2C auto-reload               */
    __IO uint32_t RCR;   /**< 0x30 repetition counter (TIM1) */
    __IO uint32_t CCR1;  /**< 0x34 capture/compare 1         */
    __IO uint32_t CCR2;  /**< 0x38 capture/compare 2         */
    __IO uint32_t CCR3;  /**< 0x3C capture/compare 3         */
    __IO uint32_t CCR4;  /**< 0x40 capture/compare 4         */
    __IO uint32_t BDTR;  /**< 0x44 break and dead-time (TIM1)*/
    __IO uint32_t DCR;   /**< 0x48 DMA control               */
    __IO uint32_t DMAR;  /**< 0x4C DMA address for burst     */
    __IO uint32_t OR;    /**< 0x50 option                    */
} tim_regs_t;

#define TIM1_BASE  (APB2PERIPH_BASE + 0x0000UL)
#define TIM2_BASE  (APB1PERIPH_BASE + 0x0000UL)
#define TIM3_BASE  (APB1PERIPH_BASE + 0x0400UL)
#define TIM4_BASE  (APB1PERIPH_BASE + 0x0800UL)
#define TIM5_BASE  (APB1PERIPH_BASE + 0x0C00UL)
#define TIM9_BASE  (APB2PERIPH_BASE + 0x4000UL)
#define TIM10_BASE (APB2PERIPH_BASE + 0x4400UL)
#define TIM11_BASE (APB2PERIPH_BASE + 0x4800UL)

#define TIM1  ((tim_regs_t*)TIM1_BASE)
#define TIM2  ((tim_regs_t*)TIM2_BASE)
#define TIM3  ((tim_regs_t*)TIM3_BASE)
#define TIM4  ((tim_regs_t*)TIM4_BASE)
#define TIM5  ((tim_regs_t*)TIM5_BASE)
#define TIM9  ((tim_regs_t*)TIM9_BASE)
#define TIM10 ((tim_regs_t*)TIM10_BASE)
#define TIM11 ((tim_regs_t*)TIM11_BASE)

/* ---- CR1 ---------------------------------------------------------------- */
#define TIM_CR1_CEN      (1UL << 0)
#define TIM_CR1_UDIS     (1UL << 1)
#define TIM_CR1_URS      (1UL << 2)
#define TIM_CR1_OPM      (1UL << 3) /**< one pulse mode      */
#define TIM_CR1_DIR      (1UL << 4) /**< 1 = down counting   */
#define TIM_CR1_CMS_POS  5U
#define TIM_CR1_CMS_MSK  (0x3UL << TIM_CR1_CMS_POS)
#define TIM_CR1_ARPE     (1UL << 7) /**< auto-reload preload */
#define TIM_CR1_CKD_POS  8U
#define TIM_CR1_CKD_MSK  (0x3UL << TIM_CR1_CKD_POS)

/* ---- DIER --------------------------------------------------------------- */
#define TIM_DIER_UIE   (1UL << 0) /**< update interrupt       */
#define TIM_DIER_CC1IE (1UL << 1)
#define TIM_DIER_CC2IE (1UL << 2)
#define TIM_DIER_CC3IE (1UL << 3)
#define TIM_DIER_CC4IE (1UL << 4)
#define TIM_DIER_TIE   (1UL << 6)
#define TIM_DIER_BIE   (1UL << 7)
#define TIM_DIER_UDE   (1UL << 8) /**< update DMA request     */

/* ---- SR ----------------------------------------------------------------- */
#define TIM_SR_UIF   (1UL << 0)
#define TIM_SR_CC1IF (1UL << 1)
#define TIM_SR_CC2IF (1UL << 2)
#define TIM_SR_CC3IF (1UL << 3)
#define TIM_SR_CC4IF (1UL << 4)
#define TIM_SR_TIF   (1UL << 6)
#define TIM_SR_BIF   (1UL << 7)

/* ---- EGR ---------------------------------------------------------------- */
#define TIM_EGR_UG (1UL << 0) /**< force an update event to load PSC/ARR */

/* ---- CCMR (output compare mode, 3 bits at OCxM) ------------------------- */
#define TIM_CCMR1_OC1M_POS 4U
#define TIM_CCMR1_OC1M_MSK (0x7UL << TIM_CCMR1_OC1M_POS)
#define TIM_CCMR1_OC1PE    (1UL << 3)
#define TIM_CCMR1_OC2M_POS 12U
#define TIM_CCMR1_OC2M_MSK (0x7UL << TIM_CCMR1_OC2M_POS)
#define TIM_CCMR1_OC2PE    (1UL << 11)
#define TIM_CCMR2_OC3M_POS 4U
#define TIM_CCMR2_OC3M_MSK (0x7UL << TIM_CCMR2_OC3M_POS)
#define TIM_CCMR2_OC3PE    (1UL << 3)
#define TIM_CCMR2_OC4M_POS 12U
#define TIM_CCMR2_OC4M_MSK (0x7UL << TIM_CCMR2_OC4M_POS)
#define TIM_CCMR2_OC4PE    (1UL << 11)

#define TIM_OCMODE_FROZEN  0x0UL
#define TIM_OCMODE_PWM1    0x6UL
#define TIM_OCMODE_PWM2    0x7UL
#define TIM_OCMODE_TOGGLE  0x3UL

/* ---- CCER --------------------------------------------------------------- */
#define TIM_CCER_CC1E (1UL << 0)
#define TIM_CCER_CC1P (1UL << 1)
#define TIM_CCER_CC2E (1UL << 4)
#define TIM_CCER_CC2P (1UL << 5)
#define TIM_CCER_CC3E (1UL << 8)
#define TIM_CCER_CC3P (1UL << 9)
#define TIM_CCER_CC4E (1UL << 12)
#define TIM_CCER_CC4P (1UL << 13)

/* ---- BDTR (TIM1 only) --------------------------------------------------- */
#define TIM_BDTR_MOE (1UL << 15) /**< main output enable */

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_TIM_H */
