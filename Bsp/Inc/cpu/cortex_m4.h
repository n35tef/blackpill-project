/**
 * @file    cortex_m4.h
 * @brief   ARM Cortex-M4 core peripheral definitions and intrinsics.
 *
 * Hand-written replacement for CMSIS-Core. Covers only what this BSP needs:
 * NVIC, SysTick, SCB (including the FPU coprocessor access register) and the
 * handful of compiler intrinsics the drivers rely on.
 *
 * Addresses and register layouts follow the ARMv7-M architecture reference
 * (System Control Space at 0xE000E000), which is identical on every Cortex-M4
 * part, so nothing in here is STM32-specific.
 */
#ifndef CPU_CORTEX_M4_H
#define CPU_CORTEX_M4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Register access qualifiers ---------------------------------------------- */
#define __IO volatile       /**< read/write register  */
#define __I  volatile const /**< read-only register   */
#define __O  volatile       /**< write-only register  */

/* Number of implemented interrupt priority bits on STM32F4 (16 levels). */
#define NVIC_PRIO_BITS 4U

/* ------------------------------------------------------------------------ */
/* System Control Space base addresses                                      */
/* ------------------------------------------------------------------------ */
#define SCS_BASE     0xE000E000UL
#define SYSTICK_BASE (SCS_BASE + 0x0010UL)
#define NVIC_BASE    (SCS_BASE + 0x0100UL)
#define SCB_BASE     (SCS_BASE + 0x0D00UL)

/* ------------------------------------------------------------------------ */
/* SysTick timer                                                            */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t CTRL;  /**< 0x00 control and status */
    __IO uint32_t LOAD;  /**< 0x04 reload value       */
    __IO uint32_t VAL;   /**< 0x08 current value      */
    __I uint32_t CALIB;  /**< 0x0C calibration value  */
} systick_regs_t;

#define SYSTICK ((systick_regs_t*)SYSTICK_BASE)

#define SYSTICK_CTRL_ENABLE    (1UL << 0)  /**< counter enable            */
#define SYSTICK_CTRL_TICKINT   (1UL << 1)  /**< exception request enable  */
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)  /**< 1 = processor clock       */
#define SYSTICK_CTRL_COUNTFLAG (1UL << 16) /**< set when counter wrapped  */

#define SYSTICK_LOAD_RELOAD_MAX 0x00FFFFFFUL /**< 24-bit down counter */

/* ------------------------------------------------------------------------ */
/* Nested Vectored Interrupt Controller                                     */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t ISER[8]; /**< 0x000 set-enable        */
    uint32_t RESERVED0[24];
    __IO uint32_t ICER[8]; /**< 0x080 clear-enable      */
    uint32_t RESERVED1[24];
    __IO uint32_t ISPR[8]; /**< 0x100 set-pending       */
    uint32_t RESERVED2[24];
    __IO uint32_t ICPR[8]; /**< 0x180 clear-pending     */
    uint32_t RESERVED3[24];
    __IO uint32_t IABR[8]; /**< 0x200 active bit        */
    uint32_t RESERVED4[56];
    __IO uint8_t IP[240];  /**< 0x300 priority, 8-bit   */
    uint32_t RESERVED5[644];
    __O uint32_t STIR;     /**< 0xE00 software trigger  */
} nvic_regs_t;

#define NVIC ((nvic_regs_t*)NVIC_BASE)

/* ------------------------------------------------------------------------ */
/* System Control Block                                                     */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __I uint32_t CPUID;   /**< 0x00 CPU ID base                        */
    __IO uint32_t ICSR;   /**< 0x04 interrupt control and state        */
    __IO uint32_t VTOR;   /**< 0x08 vector table offset                */
    __IO uint32_t AIRCR;  /**< 0x0C application interrupt/reset control*/
    __IO uint32_t SCR;    /**< 0x10 system control                     */
    __IO uint32_t CCR;    /**< 0x14 configuration and control          */
    __IO uint8_t SHP[12]; /**< 0x18 system handler priority (4-15)     */
    __IO uint32_t SHCSR;  /**< 0x24 system handler control and state   */
    __IO uint32_t CFSR;   /**< 0x28 configurable fault status          */
    __IO uint32_t HFSR;   /**< 0x2C hard fault status                  */
    __IO uint32_t DFSR;   /**< 0x30 debug fault status                 */
    __IO uint32_t MMFAR;  /**< 0x34 memmanage fault address            */
    __IO uint32_t BFAR;   /**< 0x38 bus fault address                  */
    __IO uint32_t AFSR;   /**< 0x3C auxiliary fault status             */
    __I uint32_t PFR[2];  /**< 0x40 processor feature                  */
    __I uint32_t DFR;     /**< 0x48 debug feature                      */
    __I uint32_t ADR;     /**< 0x4C auxiliary feature                  */
    __I uint32_t MMFR[4]; /**< 0x50 memory model feature               */
    __I uint32_t ISAR[5]; /**< 0x60 instruction set attributes         */
    uint32_t RESERVED0[5];
    __IO uint32_t CPACR;  /**< 0x88 coprocessor access control         */
} scb_regs_t;

#define SCB ((scb_regs_t*)SCB_BASE)

#define SCB_AIRCR_VECTKEY   (0x5FAUL << 16) /**< write key for AIRCR       */
#define SCB_AIRCR_PRIGROUP  (7UL << 8)      /**< priority grouping field   */
#define SCB_AIRCR_SYSRESETREQ (1UL << 2)    /**< request a system reset    */

#define SCB_CCR_DIV_0_TRP   (1UL << 4)  /**< trap on divide by zero        */
#define SCB_CCR_UNALIGN_TRP (1UL << 3)  /**< trap on unaligned access      */

#define SCB_SHCSR_USGFAULTENA (1UL << 18)
#define SCB_SHCSR_BUSFAULTENA (1UL << 17)
#define SCB_SHCSR_MEMFAULTENA (1UL << 16)

/** Grant full access to coprocessors 10 and 11 (the FPU). */
#define SCB_CPACR_FPU_FULL_ACCESS ((3UL << 20) | (3UL << 22))

/* ------------------------------------------------------------------------ */
/* Compiler intrinsics                                                      */
/* ------------------------------------------------------------------------ */
__attribute__((always_inline)) static inline void __enable_irq(void)
{
    __asm volatile("cpsie i" ::: "memory");
}

__attribute__((always_inline)) static inline void __disable_irq(void)
{
    __asm volatile("cpsid i" ::: "memory");
}

/** @brief Read PRIMASK (non-zero when interrupts are masked). */
__attribute__((always_inline)) static inline uint32_t __get_PRIMASK(void)
{
    uint32_t result;
    __asm volatile("mrs %0, primask" : "=r"(result));
    return result;
}

__attribute__((always_inline)) static inline void __set_PRIMASK(uint32_t priMask)
{
    __asm volatile("msr primask, %0" : : "r"(priMask) : "memory");
}

__attribute__((always_inline)) static inline void __DSB(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

__attribute__((always_inline)) static inline void __ISB(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

__attribute__((always_inline)) static inline void __DMB(void)
{
    __asm volatile("dmb 0xF" ::: "memory");
}

__attribute__((always_inline)) static inline void __NOP(void)
{
    __asm volatile("nop");
}

__attribute__((always_inline)) static inline void __WFI(void)
{
    __asm volatile("wfi");
}

__attribute__((always_inline)) static inline void __WFE(void)
{
    __asm volatile("wfe");
}

/* ------------------------------------------------------------------------ */
/* NVIC helpers                                                             */
/* ------------------------------------------------------------------------ */

/**
 * @brief Enable an interrupt in the NVIC.
 * @param irqn Device interrupt number (must be >= 0; negative values are core
 *             exceptions, which are always enabled or handled via SCB).
 */
static inline void nvic_enable_irq(int32_t irqn)
{
    if (irqn >= 0)
    {
        NVIC->ISER[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
    }
}

/** @brief Disable an interrupt in the NVIC. */
static inline void nvic_disable_irq(int32_t irqn)
{
    if (irqn >= 0)
    {
        NVIC->ICER[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
        __DSB();
        __ISB();
    }
}

/** @brief Clear a pending interrupt. */
static inline void nvic_clear_pending_irq(int32_t irqn)
{
    if (irqn >= 0)
    {
        NVIC->ICPR[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
    }
}

/** @brief Set a pending interrupt. */
static inline void nvic_set_pending_irq(int32_t irqn)
{
    if (irqn >= 0)
    {
        NVIC->ISPR[((uint32_t)irqn) >> 5U] = 1UL << (((uint32_t)irqn) & 0x1FU);
    }
}

/**
 * @brief Set interrupt priority.
 * @param irqn     Interrupt number; negative values address core exceptions.
 * @param priority 0..15, lower value = higher priority. Only the top
 *                 NVIC_PRIO_BITS bits of the 8-bit field are implemented, so
 *                 the value is shifted into place here.
 */
static inline void nvic_set_priority(int32_t irqn, uint32_t priority)
{
    const uint8_t value = (uint8_t)((priority << (8U - NVIC_PRIO_BITS)) & 0xFFU);

    if (irqn >= 0)
    {
        NVIC->IP[(uint32_t)irqn] = value;
    }
    else
    {
        /* Core exceptions -4..-1 map to SHP[8..11]. */
        SCB->SHP[(((uint32_t)(irqn & 0xFU)) - 4U)] = value;
    }
}

/** @brief Trigger a system reset and never return. */
__attribute__((noreturn)) static inline void nvic_system_reset(void)
{
    __DSB();
    SCB->AIRCR = SCB_AIRCR_VECTKEY | (SCB->AIRCR & SCB_AIRCR_PRIGROUP) | SCB_AIRCR_SYSRESETREQ;
    __DSB();
    while (1)
    {
    }
}

#ifdef __cplusplus
}
#endif

#endif /* CPU_CORTEX_M4_H */
