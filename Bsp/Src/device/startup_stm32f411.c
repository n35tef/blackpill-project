/**
 * @file    startup_stm32f411.c
 * @brief   Reset handler and vector table for STM32F411, written in C.
 *
 * Replaces the toolchain/vendor assembly startup file. The reset handler
 * enables the FPU, initialises .data/.bss, runs static constructors and calls
 * main(). The vector table is a const array placed in the .isr_vector section,
 * which the linker script puts at the very start of flash.
 *
 * Every interrupt handler is a weak alias of default_handler(), so a project
 * only has to define the handlers it actually uses.
 */

#include <stdint.h>

#include "device/stm32f411.h"

/* Symbols provided by the linker script (stm32f411ce.ld). */
extern uint32_t _sidata; /**< .data initialiser image in flash */
extern uint32_t _sdata;  /**< .data start in RAM               */
extern uint32_t _edata;  /**< .data end in RAM                 */
extern uint32_t _sbss;   /**< .bss start                       */
extern uint32_t _ebss;   /**< .bss end                         */
extern uint32_t _estack; /**< initial stack pointer            */

extern int main(void);
extern void __libc_init_array(void);

void reset_handler(void);
void default_handler(void);

/**
 * @brief Entry point taken out of reset.
 *
 * Runs before any C runtime state exists, so it must not rely on initialised
 * globals until the copy/zero loops below have completed.
 */
void reset_handler(void)
{
    /*
     * Enable the FPU first: the compiler is free to emit floating point
     * instructions in any code that follows, including the loops below.
     */
    SCB->CPACR |= SCB_CPACR_FPU_FULL_ACCESS;
    __DSB();
    __ISB();

    const uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;

    while (dst < &_edata)
    {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss)
    {
        *dst++ = 0U;
    }

    __libc_init_array();

    (void)main();

    /* main() is not expected to return; trap here if it does. */
    while (1)
    {
    }
}

/** @brief Fallback for any interrupt without its own handler. */
void default_handler(void)
{
    while (1)
    {
    }
}

#define WEAK_ALIAS(name) void name(void) __attribute__((weak, alias("default_handler")))

/* ---- ARM core exceptions ------------------------------------------------ */
WEAK_ALIAS(NMI_Handler);
WEAK_ALIAS(HardFault_Handler);
WEAK_ALIAS(MemManage_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(SVC_Handler);
WEAK_ALIAS(DebugMon_Handler);
WEAK_ALIAS(PendSV_Handler);
WEAK_ALIAS(SysTick_Handler);

/* ---- STM32F411 device interrupts ---------------------------------------- */
WEAK_ALIAS(WWDG_IRQHandler);
WEAK_ALIAS(PVD_IRQHandler);
WEAK_ALIAS(TAMP_STAMP_IRQHandler);
WEAK_ALIAS(RTC_WKUP_IRQHandler);
WEAK_ALIAS(FLASH_IRQHandler);
WEAK_ALIAS(RCC_IRQHandler);
WEAK_ALIAS(EXTI0_IRQHandler);
WEAK_ALIAS(EXTI1_IRQHandler);
WEAK_ALIAS(EXTI2_IRQHandler);
WEAK_ALIAS(EXTI3_IRQHandler);
WEAK_ALIAS(EXTI4_IRQHandler);
WEAK_ALIAS(DMA1_Stream0_IRQHandler);
WEAK_ALIAS(DMA1_Stream1_IRQHandler);
WEAK_ALIAS(DMA1_Stream2_IRQHandler);
WEAK_ALIAS(DMA1_Stream3_IRQHandler);
WEAK_ALIAS(DMA1_Stream4_IRQHandler);
WEAK_ALIAS(DMA1_Stream5_IRQHandler);
WEAK_ALIAS(DMA1_Stream6_IRQHandler);
WEAK_ALIAS(ADC_IRQHandler);
WEAK_ALIAS(EXTI9_5_IRQHandler);
WEAK_ALIAS(TIM1_BRK_TIM9_IRQHandler);
WEAK_ALIAS(TIM1_UP_TIM10_IRQHandler);
WEAK_ALIAS(TIM1_TRG_COM_TIM11_IRQHandler);
WEAK_ALIAS(TIM1_CC_IRQHandler);
WEAK_ALIAS(TIM2_IRQHandler);
WEAK_ALIAS(TIM3_IRQHandler);
WEAK_ALIAS(TIM4_IRQHandler);
WEAK_ALIAS(I2C1_EV_IRQHandler);
WEAK_ALIAS(I2C1_ER_IRQHandler);
WEAK_ALIAS(I2C2_EV_IRQHandler);
WEAK_ALIAS(I2C2_ER_IRQHandler);
WEAK_ALIAS(SPI1_IRQHandler);
WEAK_ALIAS(SPI2_IRQHandler);
WEAK_ALIAS(USART1_IRQHandler);
WEAK_ALIAS(USART2_IRQHandler);
WEAK_ALIAS(EXTI15_10_IRQHandler);
WEAK_ALIAS(RTC_Alarm_IRQHandler);
WEAK_ALIAS(OTG_FS_WKUP_IRQHandler);
WEAK_ALIAS(DMA1_Stream7_IRQHandler);
WEAK_ALIAS(SDIO_IRQHandler);
WEAK_ALIAS(TIM5_IRQHandler);
WEAK_ALIAS(SPI3_IRQHandler);
WEAK_ALIAS(DMA2_Stream0_IRQHandler);
WEAK_ALIAS(DMA2_Stream1_IRQHandler);
WEAK_ALIAS(DMA2_Stream2_IRQHandler);
WEAK_ALIAS(DMA2_Stream3_IRQHandler);
WEAK_ALIAS(DMA2_Stream4_IRQHandler);
WEAK_ALIAS(OTG_FS_IRQHandler);
WEAK_ALIAS(DMA2_Stream5_IRQHandler);
WEAK_ALIAS(DMA2_Stream6_IRQHandler);
WEAK_ALIAS(DMA2_Stream7_IRQHandler);
WEAK_ALIAS(USART6_IRQHandler);
WEAK_ALIAS(I2C3_EV_IRQHandler);
WEAK_ALIAS(I2C3_ER_IRQHandler);
WEAK_ALIAS(FPU_IRQHandler);
WEAK_ALIAS(SPI4_IRQHandler);
WEAK_ALIAS(SPI5_IRQHandler);

typedef void (*vector_entry_t)(void);

/**
 * Vector table. Entry 0 is the initial stack pointer rather than a function
 * pointer, hence the cast. Slots that are reserved on STM32F411 are left NULL
 * so that every following entry keeps its architectural offset.
 */
__attribute__((section(".isr_vector"), used))
const vector_entry_t g_vector_table[] = {
    (vector_entry_t)(&_estack), /* -16 initial stack pointer  */
    reset_handler,              /* -15 reset                  */
    NMI_Handler,                /* -14                        */
    HardFault_Handler,          /* -13                        */
    MemManage_Handler,          /* -12                        */
    BusFault_Handler,           /* -11                        */
    UsageFault_Handler,         /* -10                        */
    0,                          /*  -9 reserved               */
    0,                          /*  -8 reserved               */
    0,                          /*  -7 reserved               */
    0,                          /*  -6 reserved               */
    SVC_Handler,                /*  -5                        */
    DebugMon_Handler,           /*  -4                        */
    0,                          /*  -3 reserved               */
    PendSV_Handler,             /*  -2                        */
    SysTick_Handler,            /*  -1                        */

    WWDG_IRQHandler,             /*  0 */
    PVD_IRQHandler,              /*  1 */
    TAMP_STAMP_IRQHandler,       /*  2 */
    RTC_WKUP_IRQHandler,         /*  3 */
    FLASH_IRQHandler,            /*  4 */
    RCC_IRQHandler,              /*  5 */
    EXTI0_IRQHandler,            /*  6 */
    EXTI1_IRQHandler,            /*  7 */
    EXTI2_IRQHandler,            /*  8 */
    EXTI3_IRQHandler,            /*  9 */
    EXTI4_IRQHandler,            /* 10 */
    DMA1_Stream0_IRQHandler,     /* 11 */
    DMA1_Stream1_IRQHandler,     /* 12 */
    DMA1_Stream2_IRQHandler,     /* 13 */
    DMA1_Stream3_IRQHandler,     /* 14 */
    DMA1_Stream4_IRQHandler,     /* 15 */
    DMA1_Stream5_IRQHandler,     /* 16 */
    DMA1_Stream6_IRQHandler,     /* 17 */
    ADC_IRQHandler,              /* 18 */
    0,                           /* 19 reserved (CAN1_TX)  */
    0,                           /* 20 reserved (CAN1_RX0) */
    0,                           /* 21 reserved (CAN1_RX1) */
    0,                           /* 22 reserved (CAN1_SCE) */
    EXTI9_5_IRQHandler,          /* 23 */
    TIM1_BRK_TIM9_IRQHandler,    /* 24 */
    TIM1_UP_TIM10_IRQHandler,    /* 25 */
    TIM1_TRG_COM_TIM11_IRQHandler, /* 26 */
    TIM1_CC_IRQHandler,          /* 27 */
    TIM2_IRQHandler,             /* 28 */
    TIM3_IRQHandler,             /* 29 */
    TIM4_IRQHandler,             /* 30 */
    I2C1_EV_IRQHandler,          /* 31 */
    I2C1_ER_IRQHandler,          /* 32 */
    I2C2_EV_IRQHandler,          /* 33 */
    I2C2_ER_IRQHandler,          /* 34 */
    SPI1_IRQHandler,             /* 35 */
    SPI2_IRQHandler,             /* 36 */
    USART1_IRQHandler,           /* 37 */
    USART2_IRQHandler,           /* 38 */
    0,                           /* 39 reserved (USART3)   */
    EXTI15_10_IRQHandler,        /* 40 */
    RTC_Alarm_IRQHandler,        /* 41 */
    OTG_FS_WKUP_IRQHandler,      /* 42 */
    0,                           /* 43 reserved */
    0,                           /* 44 reserved */
    0,                           /* 45 reserved */
    0,                           /* 46 reserved */
    DMA1_Stream7_IRQHandler,     /* 47 */
    0,                           /* 48 reserved (FSMC) */
    SDIO_IRQHandler,             /* 49 */
    TIM5_IRQHandler,             /* 50 */
    SPI3_IRQHandler,             /* 51 */
    0,                           /* 52 reserved (UART4)      */
    0,                           /* 53 reserved (UART5)      */
    0,                           /* 54 reserved (TIM6_DAC)   */
    0,                           /* 55 reserved (TIM7)       */
    DMA2_Stream0_IRQHandler,     /* 56 */
    DMA2_Stream1_IRQHandler,     /* 57 */
    DMA2_Stream2_IRQHandler,     /* 58 */
    DMA2_Stream3_IRQHandler,     /* 59 */
    DMA2_Stream4_IRQHandler,     /* 60 */
    0,                           /* 61 reserved (ETH)        */
    0,                           /* 62 reserved (ETH_WKUP)   */
    0,                           /* 63 reserved (CAN2_TX)    */
    0,                           /* 64 reserved (CAN2_RX0)   */
    0,                           /* 65 reserved (CAN2_RX1)   */
    0,                           /* 66 reserved (CAN2_SCE)   */
    OTG_FS_IRQHandler,           /* 67 */
    DMA2_Stream5_IRQHandler,     /* 68 */
    DMA2_Stream6_IRQHandler,     /* 69 */
    DMA2_Stream7_IRQHandler,     /* 70 */
    USART6_IRQHandler,           /* 71 */
    I2C3_EV_IRQHandler,          /* 72 */
    I2C3_ER_IRQHandler,          /* 73 */
    0,                           /* 74 reserved (OTG_HS_EP1_OUT) */
    0,                           /* 75 reserved (OTG_HS_EP1_IN)  */
    0,                           /* 76 reserved (OTG_HS_WKUP)    */
    0,                           /* 77 reserved (OTG_HS)         */
    0,                           /* 78 reserved (DCMI)           */
    0,                           /* 79 reserved (CRYP)           */
    0,                           /* 80 reserved (HASH_RNG)       */
    FPU_IRQHandler,              /* 81 */
    0,                           /* 82 reserved (UART7)      */
    0,                           /* 83 reserved (UART8)      */
    SPI4_IRQHandler,             /* 84 */
    SPI5_IRQHandler,             /* 85 */
};

_Static_assert(sizeof(g_vector_table) / sizeof(g_vector_table[0]) == 16 + IRQ_MAX_DEVICE_IRQ + 1,
               "vector table length does not match the STM32F411 interrupt map");
