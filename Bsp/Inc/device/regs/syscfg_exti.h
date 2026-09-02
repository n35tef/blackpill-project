/**
 * @file    syscfg_exti.h
 * @brief   System configuration controller and external interrupt/event
 *          controller registers (RM0383 sec. 7.2 and 10.3).
 */
#ifndef DEVICE_REGS_SYSCFG_EXTI_H
#define DEVICE_REGS_SYSCFG_EXTI_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------------ */
/* SYSCFG                                                                    */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t MEMRMP;   /**< 0x00 memory remap                */
    __IO uint32_t PMC;      /**< 0x04 peripheral mode config      */
    __IO uint32_t EXTICR[4];/**< 0x08 external interrupt config   */
    uint32_t RESERVED0[2];  /*   0x18..0x1C                       */
    __IO uint32_t CMPCR;    /**< 0x20 compensation cell control   */
} syscfg_regs_t;

#define SYSCFG_BASE (APB2PERIPH_BASE + 0x3800UL)
#define SYSCFG      ((syscfg_regs_t*)SYSCFG_BASE)

/** Port selection values for SYSCFG->EXTICR (4 bits per EXTI line). */
#define SYSCFG_EXTI_PORTA 0x0UL
#define SYSCFG_EXTI_PORTB 0x1UL
#define SYSCFG_EXTI_PORTC 0x2UL
#define SYSCFG_EXTI_PORTD 0x3UL
#define SYSCFG_EXTI_PORTE 0x4UL
#define SYSCFG_EXTI_PORTH 0x7UL

/* ------------------------------------------------------------------------ */
/* EXTI                                                                      */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t IMR;   /**< 0x00 interrupt mask               */
    __IO uint32_t EMR;   /**< 0x04 event mask                   */
    __IO uint32_t RTSR;  /**< 0x08 rising trigger selection     */
    __IO uint32_t FTSR;  /**< 0x0C falling trigger selection    */
    __IO uint32_t SWIER; /**< 0x10 software interrupt event     */
    __IO uint32_t PR;    /**< 0x14 pending (write 1 to clear)   */
} exti_regs_t;

#define EXTI_BASE (APB2PERIPH_BASE + 0x3C00UL)
#define EXTI      ((exti_regs_t*)EXTI_BASE)

/** EXTI lines connected to internal sources rather than GPIO pins. */
#define EXTI_LINE_PVD        16U
#define EXTI_LINE_RTC_ALARM  17U
#define EXTI_LINE_OTG_FS_WKUP 18U
#define EXTI_LINE_RTC_TAMPER 21U
#define EXTI_LINE_RTC_WKUP   22U

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_SYSCFG_EXTI_H */
