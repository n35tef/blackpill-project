/**
 * @file    stm32f411_irq.h
 * @brief   STM32F411 exception and interrupt numbers.
 *
 * Negative values are ARM core exceptions (handled through SCB); values >= 0
 * are device interrupts handled by the NVIC. Numbers follow the STM32F411
 * vector table in RM0383. Positions reserved on this part are simply absent
 * from the enum - the vector table in startup_stm32f411.c still reserves the
 * slots so every entry lands at the right offset.
 */
#ifndef DEVICE_STM32F411_IRQ_H
#define DEVICE_STM32F411_IRQ_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    /* ---- ARM Cortex-M4 core exceptions ---- */
    IRQ_NMI = -14,
    IRQ_HARD_FAULT = -13,
    IRQ_MEM_MANAGE = -12,
    IRQ_BUS_FAULT = -11,
    IRQ_USAGE_FAULT = -10,
    IRQ_SVCALL = -5,
    IRQ_DEBUG_MON = -4,
    IRQ_PENDSV = -2,
    IRQ_SYSTICK = -1,

    /* ---- STM32F411 device interrupts ---- */
    IRQ_WWDG = 0,
    IRQ_PVD = 1,
    IRQ_TAMP_STAMP = 2,
    IRQ_RTC_WKUP = 3,
    IRQ_FLASH = 4,
    IRQ_RCC = 5,
    IRQ_EXTI0 = 6,
    IRQ_EXTI1 = 7,
    IRQ_EXTI2 = 8,
    IRQ_EXTI3 = 9,
    IRQ_EXTI4 = 10,
    IRQ_DMA1_STREAM0 = 11,
    IRQ_DMA1_STREAM1 = 12,
    IRQ_DMA1_STREAM2 = 13,
    IRQ_DMA1_STREAM3 = 14,
    IRQ_DMA1_STREAM4 = 15,
    IRQ_DMA1_STREAM5 = 16,
    IRQ_DMA1_STREAM6 = 17,
    IRQ_ADC = 18,
    IRQ_EXTI9_5 = 23,
    IRQ_TIM1_BRK_TIM9 = 24,
    IRQ_TIM1_UP_TIM10 = 25,
    IRQ_TIM1_TRG_COM_TIM11 = 26,
    IRQ_TIM1_CC = 27,
    IRQ_TIM2 = 28,
    IRQ_TIM3 = 29,
    IRQ_TIM4 = 30,
    IRQ_I2C1_EV = 31,
    IRQ_I2C1_ER = 32,
    IRQ_I2C2_EV = 33,
    IRQ_I2C2_ER = 34,
    IRQ_SPI1 = 35,
    IRQ_SPI2 = 36,
    IRQ_USART1 = 37,
    IRQ_USART2 = 38,
    IRQ_EXTI15_10 = 40,
    IRQ_RTC_ALARM = 41,
    IRQ_OTG_FS_WKUP = 42,
    IRQ_DMA1_STREAM7 = 47,
    IRQ_SDIO = 49,
    IRQ_TIM5 = 50,
    IRQ_SPI3 = 51,
    IRQ_DMA2_STREAM0 = 56,
    IRQ_DMA2_STREAM1 = 57,
    IRQ_DMA2_STREAM2 = 58,
    IRQ_DMA2_STREAM3 = 59,
    IRQ_DMA2_STREAM4 = 60,
    IRQ_OTG_FS = 67,
    IRQ_DMA2_STREAM5 = 68,
    IRQ_DMA2_STREAM6 = 69,
    IRQ_DMA2_STREAM7 = 70,
    IRQ_USART6 = 71,
    IRQ_I2C3_EV = 72,
    IRQ_I2C3_ER = 73,
    IRQ_FPU = 81,
    IRQ_SPI4 = 84,
    IRQ_SPI5 = 85,
} irq_num_t;

/** Highest device interrupt number implemented on STM32F411. */
#define IRQ_MAX_DEVICE_IRQ 85

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_STM32F411_IRQ_H */
