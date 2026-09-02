/**
 * @file    gpio.h
 * @brief   General-purpose I/O registers (RM0383 sec. 8.4).
 *
 * STM32F411CE (48-pin) bonds out GPIOA, GPIOB, GPIOC and GPIOH. GPIOD/GPIOE
 * exist in the register map on larger packages and are declared here for
 * completeness, but are not routed on the BlackPill.
 */
#ifndef DEVICE_REGS_GPIO_H
#define DEVICE_REGS_GPIO_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t MODER;   /**< 0x00 port mode                   */
    __IO uint32_t OTYPER;  /**< 0x04 output type                 */
    __IO uint32_t OSPEEDR; /**< 0x08 output speed                */
    __IO uint32_t PUPDR;   /**< 0x0C pull-up/pull-down           */
    __I uint32_t IDR;      /**< 0x10 input data                  */
    __IO uint32_t ODR;     /**< 0x14 output data                 */
    __O uint32_t BSRR;     /**< 0x18 bit set/reset               */
    __IO uint32_t LCKR;    /**< 0x1C configuration lock          */
    __IO uint32_t AFR[2];  /**< 0x20 alternate function low/high */
} gpio_regs_t;

#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define GPIOC_BASE (AHB1PERIPH_BASE + 0x0800UL)
#define GPIOD_BASE (AHB1PERIPH_BASE + 0x0C00UL)
#define GPIOE_BASE (AHB1PERIPH_BASE + 0x1000UL)
#define GPIOH_BASE (AHB1PERIPH_BASE + 0x1C00UL)

#define GPIOA ((gpio_regs_t*)GPIOA_BASE)
#define GPIOB ((gpio_regs_t*)GPIOB_BASE)
#define GPIOC ((gpio_regs_t*)GPIOC_BASE)
#define GPIOD ((gpio_regs_t*)GPIOD_BASE)
#define GPIOE ((gpio_regs_t*)GPIOE_BASE)
#define GPIOH ((gpio_regs_t*)GPIOH_BASE)

/* ---- MODER (2 bits per pin) --------------------------------------------- */
#define GPIO_MODER_INPUT  0x0UL
#define GPIO_MODER_OUTPUT 0x1UL
#define GPIO_MODER_AF     0x2UL
#define GPIO_MODER_ANALOG 0x3UL

/* ---- OTYPER (1 bit per pin) --------------------------------------------- */
#define GPIO_OTYPER_PUSHPULL  0x0UL
#define GPIO_OTYPER_OPENDRAIN 0x1UL

/* ---- OSPEEDR (2 bits per pin) ------------------------------------------- */
#define GPIO_OSPEED_LOW       0x0UL
#define GPIO_OSPEED_MEDIUM    0x1UL
#define GPIO_OSPEED_HIGH      0x2UL
#define GPIO_OSPEED_VERY_HIGH 0x3UL

/* ---- PUPDR (2 bits per pin) --------------------------------------------- */
#define GPIO_PUPD_NONE 0x0UL
#define GPIO_PUPD_UP   0x1UL
#define GPIO_PUPD_DOWN 0x2UL

/* ---- BSRR --------------------------------------------------------------- */
#define GPIO_BSRR_SET(pin)   (1UL << (pin))
#define GPIO_BSRR_RESET(pin) (1UL << ((pin) + 16U))

/* ---- Alternate function numbers (STM32F411 datasheet, AF mux table) ----- */
#define GPIO_AF0_SYS       0x0UL
#define GPIO_AF1_TIM1_2    0x1UL
#define GPIO_AF2_TIM3_4_5  0x2UL
#define GPIO_AF3_TIM9_10_11 0x3UL
#define GPIO_AF4_I2C1_2_3  0x4UL
#define GPIO_AF5_SPI1_2_3_4_5 0x5UL
#define GPIO_AF6_SPI2_3_4_5   0x6UL
#define GPIO_AF7_USART1_2  0x7UL
#define GPIO_AF8_USART6    0x8UL
#define GPIO_AF9_I2C2_3    0x9UL
#define GPIO_AF10_OTG_FS   0xAUL
#define GPIO_AF12_SDIO     0xCUL
#define GPIO_AF15_EVENTOUT 0xFUL

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_GPIO_H */
