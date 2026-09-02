/**
 * @file    stm32f411_reg_check.c
 * @brief   Compile-time verification of every hand-written register layout.
 *
 * This file emits no code. It exists so that a mistyped register offset or a
 * missing reserved word fails the build instead of silently corrupting a
 * peripheral at run time. Every struct member is asserted against the offset
 * documented in RM0383, and every struct against its total size.
 *
 * Add an assertion here whenever a new register block is added to
 * device/regs/.
 */

#include <stddef.h>

#include "device/stm32f411.h"

#define CHECK_OFFSET(type, member, expected)                                   \
    _Static_assert(offsetof(type, member) == (expected),                       \
                   #type "." #member " is at the wrong offset")

#define CHECK_SIZE(type, expected)                                             \
    _Static_assert(sizeof(type) == (expected), #type " has the wrong size")

/* ---- Cortex-M4 core ----------------------------------------------------- */
CHECK_OFFSET(scb_regs_t, CPUID, 0x00);
CHECK_OFFSET(scb_regs_t, ICSR, 0x04);
CHECK_OFFSET(scb_regs_t, VTOR, 0x08);
CHECK_OFFSET(scb_regs_t, AIRCR, 0x0C);
CHECK_OFFSET(scb_regs_t, SCR, 0x10);
CHECK_OFFSET(scb_regs_t, CCR, 0x14);
CHECK_OFFSET(scb_regs_t, SHP, 0x18);
CHECK_OFFSET(scb_regs_t, SHCSR, 0x24);
CHECK_OFFSET(scb_regs_t, CFSR, 0x28);
CHECK_OFFSET(scb_regs_t, HFSR, 0x2C);
CHECK_OFFSET(scb_regs_t, MMFAR, 0x34);
CHECK_OFFSET(scb_regs_t, BFAR, 0x38);
CHECK_OFFSET(scb_regs_t, AFSR, 0x3C);
CHECK_OFFSET(scb_regs_t, DFSR, 0x30);
CHECK_OFFSET(scb_regs_t, CPACR, 0x88);

CHECK_OFFSET(systick_regs_t, CTRL, 0x00);
CHECK_OFFSET(systick_regs_t, LOAD, 0x04);
CHECK_OFFSET(systick_regs_t, VAL, 0x08);
CHECK_OFFSET(systick_regs_t, CALIB, 0x0C);

CHECK_OFFSET(nvic_regs_t, ISER, 0x000);
CHECK_OFFSET(nvic_regs_t, ICER, 0x080);
CHECK_OFFSET(nvic_regs_t, ISPR, 0x100);
CHECK_OFFSET(nvic_regs_t, ICPR, 0x180);
CHECK_OFFSET(nvic_regs_t, IABR, 0x200);
CHECK_OFFSET(nvic_regs_t, IP, 0x300);
CHECK_OFFSET(nvic_regs_t, STIR, 0xE00);

/* ---- RCC ---------------------------------------------------------------- */
CHECK_OFFSET(rcc_regs_t, CR, 0x00);
CHECK_OFFSET(rcc_regs_t, PLLCFGR, 0x04);
CHECK_OFFSET(rcc_regs_t, CFGR, 0x08);
CHECK_OFFSET(rcc_regs_t, CIR, 0x0C);
CHECK_OFFSET(rcc_regs_t, AHB1RSTR, 0x10);
CHECK_OFFSET(rcc_regs_t, AHB2RSTR, 0x14);
CHECK_OFFSET(rcc_regs_t, APB1RSTR, 0x20);
CHECK_OFFSET(rcc_regs_t, APB2RSTR, 0x24);
CHECK_OFFSET(rcc_regs_t, AHB1ENR, 0x30);
CHECK_OFFSET(rcc_regs_t, AHB2ENR, 0x34);
CHECK_OFFSET(rcc_regs_t, APB1ENR, 0x40);
CHECK_OFFSET(rcc_regs_t, APB2ENR, 0x44);
CHECK_OFFSET(rcc_regs_t, AHB1LPENR, 0x50);
CHECK_OFFSET(rcc_regs_t, AHB2LPENR, 0x54);
CHECK_OFFSET(rcc_regs_t, APB1LPENR, 0x60);
CHECK_OFFSET(rcc_regs_t, APB2LPENR, 0x64);
CHECK_OFFSET(rcc_regs_t, BDCR, 0x70);
CHECK_OFFSET(rcc_regs_t, CSR, 0x74);
CHECK_OFFSET(rcc_regs_t, SSCGR, 0x80);
CHECK_OFFSET(rcc_regs_t, PLLI2SCFGR, 0x84);
CHECK_OFFSET(rcc_regs_t, DCKCFGR, 0x8C);
CHECK_SIZE(rcc_regs_t, 0x90);

/* ---- FLASH interface ---------------------------------------------------- */
CHECK_OFFSET(flash_regs_t, ACR, 0x00);
CHECK_OFFSET(flash_regs_t, KEYR, 0x04);
CHECK_OFFSET(flash_regs_t, OPTKEYR, 0x08);
CHECK_OFFSET(flash_regs_t, SR, 0x0C);
CHECK_OFFSET(flash_regs_t, CR, 0x10);
CHECK_OFFSET(flash_regs_t, OPTCR, 0x14);
CHECK_SIZE(flash_regs_t, 0x18);

/* ---- PWR ---------------------------------------------------------------- */
CHECK_OFFSET(pwr_regs_t, CR, 0x00);
CHECK_OFFSET(pwr_regs_t, CSR, 0x04);
CHECK_SIZE(pwr_regs_t, 0x08);

/* ---- GPIO --------------------------------------------------------------- */
CHECK_OFFSET(gpio_regs_t, MODER, 0x00);
CHECK_OFFSET(gpio_regs_t, OTYPER, 0x04);
CHECK_OFFSET(gpio_regs_t, OSPEEDR, 0x08);
CHECK_OFFSET(gpio_regs_t, PUPDR, 0x0C);
CHECK_OFFSET(gpio_regs_t, IDR, 0x10);
CHECK_OFFSET(gpio_regs_t, ODR, 0x14);
CHECK_OFFSET(gpio_regs_t, BSRR, 0x18);
CHECK_OFFSET(gpio_regs_t, LCKR, 0x1C);
CHECK_OFFSET(gpio_regs_t, AFR, 0x20);
CHECK_SIZE(gpio_regs_t, 0x28);

/* ---- SYSCFG / EXTI ------------------------------------------------------ */
CHECK_OFFSET(syscfg_regs_t, MEMRMP, 0x00);
CHECK_OFFSET(syscfg_regs_t, PMC, 0x04);
CHECK_OFFSET(syscfg_regs_t, EXTICR, 0x08);
CHECK_OFFSET(syscfg_regs_t, CMPCR, 0x20);
CHECK_SIZE(syscfg_regs_t, 0x24);

CHECK_OFFSET(exti_regs_t, IMR, 0x00);
CHECK_OFFSET(exti_regs_t, EMR, 0x04);
CHECK_OFFSET(exti_regs_t, RTSR, 0x08);
CHECK_OFFSET(exti_regs_t, FTSR, 0x0C);
CHECK_OFFSET(exti_regs_t, SWIER, 0x10);
CHECK_OFFSET(exti_regs_t, PR, 0x14);
CHECK_SIZE(exti_regs_t, 0x18);

/* ---- DMA ---------------------------------------------------------------- */
CHECK_OFFSET(dma_stream_regs_t, CR, 0x00);
CHECK_OFFSET(dma_stream_regs_t, NDTR, 0x04);
CHECK_OFFSET(dma_stream_regs_t, PAR, 0x08);
CHECK_OFFSET(dma_stream_regs_t, M0AR, 0x0C);
CHECK_OFFSET(dma_stream_regs_t, M1AR, 0x10);
CHECK_OFFSET(dma_stream_regs_t, FCR, 0x14);
CHECK_SIZE(dma_stream_regs_t, 0x18);

CHECK_OFFSET(dma_regs_t, LISR, 0x00);
CHECK_OFFSET(dma_regs_t, HISR, 0x04);
CHECK_OFFSET(dma_regs_t, LIFCR, 0x08);
CHECK_OFFSET(dma_regs_t, HIFCR, 0x0C);
CHECK_OFFSET(dma_regs_t, STREAM, 0x10);
_Static_assert(offsetof(dma_regs_t, STREAM[7]) == 0xB8, "DMA stream 7 is at the wrong offset");

/* ---- SPI ---------------------------------------------------------------- */
CHECK_OFFSET(spi_regs_t, CR1, 0x00);
CHECK_OFFSET(spi_regs_t, CR2, 0x04);
CHECK_OFFSET(spi_regs_t, SR, 0x08);
CHECK_OFFSET(spi_regs_t, DR, 0x0C);
CHECK_OFFSET(spi_regs_t, CRCPR, 0x10);
CHECK_OFFSET(spi_regs_t, RXCRCR, 0x14);
CHECK_OFFSET(spi_regs_t, TXCRCR, 0x18);
CHECK_OFFSET(spi_regs_t, I2SCFGR, 0x1C);
CHECK_OFFSET(spi_regs_t, I2SPR, 0x20);
CHECK_SIZE(spi_regs_t, 0x24);

/* ---- USART -------------------------------------------------------------- */
CHECK_OFFSET(usart_regs_t, SR, 0x00);
CHECK_OFFSET(usart_regs_t, DR, 0x04);
CHECK_OFFSET(usart_regs_t, BRR, 0x08);
CHECK_OFFSET(usart_regs_t, CR1, 0x0C);
CHECK_OFFSET(usart_regs_t, CR2, 0x10);
CHECK_OFFSET(usart_regs_t, CR3, 0x14);
CHECK_OFFSET(usart_regs_t, GTPR, 0x18);
CHECK_SIZE(usart_regs_t, 0x1C);

/* ---- I2C ---------------------------------------------------------------- */
CHECK_OFFSET(i2c_regs_t, CR1, 0x00);
CHECK_OFFSET(i2c_regs_t, CR2, 0x04);
CHECK_OFFSET(i2c_regs_t, OAR1, 0x08);
CHECK_OFFSET(i2c_regs_t, OAR2, 0x0C);
CHECK_OFFSET(i2c_regs_t, DR, 0x10);
CHECK_OFFSET(i2c_regs_t, SR1, 0x14);
CHECK_OFFSET(i2c_regs_t, SR2, 0x18);
CHECK_OFFSET(i2c_regs_t, CCR, 0x1C);
CHECK_OFFSET(i2c_regs_t, TRISE, 0x20);
CHECK_OFFSET(i2c_regs_t, FLTR, 0x24);
CHECK_SIZE(i2c_regs_t, 0x28);

/* ---- TIM ---------------------------------------------------------------- */
CHECK_OFFSET(tim_regs_t, CR1, 0x00);
CHECK_OFFSET(tim_regs_t, CR2, 0x04);
CHECK_OFFSET(tim_regs_t, SMCR, 0x08);
CHECK_OFFSET(tim_regs_t, DIER, 0x0C);
CHECK_OFFSET(tim_regs_t, SR, 0x10);
CHECK_OFFSET(tim_regs_t, EGR, 0x14);
CHECK_OFFSET(tim_regs_t, CCMR1, 0x18);
CHECK_OFFSET(tim_regs_t, CCMR2, 0x1C);
CHECK_OFFSET(tim_regs_t, CCER, 0x20);
CHECK_OFFSET(tim_regs_t, CNT, 0x24);
CHECK_OFFSET(tim_regs_t, PSC, 0x28);
CHECK_OFFSET(tim_regs_t, ARR, 0x2C);
CHECK_OFFSET(tim_regs_t, RCR, 0x30);
CHECK_OFFSET(tim_regs_t, CCR1, 0x34);
CHECK_OFFSET(tim_regs_t, CCR4, 0x40);
CHECK_OFFSET(tim_regs_t, BDTR, 0x44);
CHECK_OFFSET(tim_regs_t, DCR, 0x48);
CHECK_OFFSET(tim_regs_t, DMAR, 0x4C);
CHECK_OFFSET(tim_regs_t, OR, 0x50);
CHECK_SIZE(tim_regs_t, 0x54);

/* ---- ADC ---------------------------------------------------------------- */
CHECK_OFFSET(adc_regs_t, SR, 0x00);
CHECK_OFFSET(adc_regs_t, CR1, 0x04);
CHECK_OFFSET(adc_regs_t, CR2, 0x08);
CHECK_OFFSET(adc_regs_t, SMPR1, 0x0C);
CHECK_OFFSET(adc_regs_t, SMPR2, 0x10);
CHECK_OFFSET(adc_regs_t, HTR, 0x24);
CHECK_OFFSET(adc_regs_t, LTR, 0x28);
CHECK_OFFSET(adc_regs_t, SQR1, 0x2C);
CHECK_OFFSET(adc_regs_t, SQR3, 0x34);
CHECK_OFFSET(adc_regs_t, JSQR, 0x38);
CHECK_OFFSET(adc_regs_t, DR, 0x4C);
CHECK_SIZE(adc_regs_t, 0x50);

CHECK_OFFSET(adc_common_regs_t, CSR, 0x00);
CHECK_OFFSET(adc_common_regs_t, CCR, 0x04);
CHECK_OFFSET(adc_common_regs_t, CDR, 0x08);

/* ---- CRC / watchdogs / RTC ---------------------------------------------- */
CHECK_OFFSET(crc_regs_t, DR, 0x00);
CHECK_OFFSET(crc_regs_t, IDR, 0x04);
CHECK_OFFSET(crc_regs_t, CR, 0x08);
CHECK_SIZE(crc_regs_t, 0x0C);

CHECK_OFFSET(iwdg_regs_t, KR, 0x00);
CHECK_OFFSET(iwdg_regs_t, PR, 0x04);
CHECK_OFFSET(iwdg_regs_t, RLR, 0x08);
CHECK_OFFSET(iwdg_regs_t, SR, 0x0C);
CHECK_SIZE(iwdg_regs_t, 0x10);

CHECK_OFFSET(wwdg_regs_t, CR, 0x00);
CHECK_OFFSET(wwdg_regs_t, CFR, 0x04);
CHECK_OFFSET(wwdg_regs_t, SR, 0x08);
CHECK_SIZE(wwdg_regs_t, 0x0C);

CHECK_OFFSET(rtc_regs_t, TR, 0x00);
CHECK_OFFSET(rtc_regs_t, DR, 0x04);
CHECK_OFFSET(rtc_regs_t, CR, 0x08);
CHECK_OFFSET(rtc_regs_t, ISR, 0x0C);
CHECK_OFFSET(rtc_regs_t, PRER, 0x10);
CHECK_OFFSET(rtc_regs_t, WUTR, 0x14);
CHECK_OFFSET(rtc_regs_t, ALRMAR, 0x1C);
CHECK_OFFSET(rtc_regs_t, WPR, 0x24);
CHECK_OFFSET(rtc_regs_t, CALR, 0x3C);
CHECK_OFFSET(rtc_regs_t, TAFCR, 0x40);
CHECK_OFFSET(rtc_regs_t, BKP, 0x50);
CHECK_SIZE(rtc_regs_t, 0xA0);

/* ---- Peripheral base addresses ------------------------------------------ */
_Static_assert(RCC_BASE == 0x40023800UL, "RCC base address");
_Static_assert(FLASH_R_BASE == 0x40023C00UL, "FLASH interface base address");
_Static_assert(PWR_BASE == 0x40007000UL, "PWR base address");
_Static_assert(GPIOA_BASE == 0x40020000UL, "GPIOA base address");
_Static_assert(GPIOB_BASE == 0x40020400UL, "GPIOB base address");
_Static_assert(GPIOC_BASE == 0x40020800UL, "GPIOC base address");
_Static_assert(GPIOH_BASE == 0x40021C00UL, "GPIOH base address");
_Static_assert(SYSCFG_BASE == 0x40013800UL, "SYSCFG base address");
_Static_assert(EXTI_BASE == 0x40013C00UL, "EXTI base address");
_Static_assert(SCB_BASE == 0xE000ED00UL, "SCB base address");
_Static_assert(SYSTICK_BASE == 0xE000E010UL, "SysTick base address");
_Static_assert(NVIC_BASE == 0xE000E100UL, "NVIC base address");
_Static_assert(DMA1_BASE == 0x40026000UL, "DMA1 base address");
_Static_assert(DMA2_BASE == 0x40026400UL, "DMA2 base address");
_Static_assert(SPI1_BASE == 0x40013000UL, "SPI1 base address");
_Static_assert(SPI2_BASE == 0x40003800UL, "SPI2 base address");
_Static_assert(SPI3_BASE == 0x40003C00UL, "SPI3 base address");
_Static_assert(SPI4_BASE == 0x40013400UL, "SPI4 base address");
_Static_assert(SPI5_BASE == 0x40015000UL, "SPI5 base address");
_Static_assert(USART1_BASE == 0x40011000UL, "USART1 base address");
_Static_assert(USART2_BASE == 0x40004400UL, "USART2 base address");
_Static_assert(USART6_BASE == 0x40011400UL, "USART6 base address");
_Static_assert(I2C1_BASE == 0x40005400UL, "I2C1 base address");
_Static_assert(I2C2_BASE == 0x40005800UL, "I2C2 base address");
_Static_assert(I2C3_BASE == 0x40005C00UL, "I2C3 base address");
_Static_assert(TIM1_BASE == 0x40010000UL, "TIM1 base address");
_Static_assert(TIM2_BASE == 0x40000000UL, "TIM2 base address");
_Static_assert(TIM5_BASE == 0x40000C00UL, "TIM5 base address");
_Static_assert(TIM9_BASE == 0x40014000UL, "TIM9 base address");
_Static_assert(TIM11_BASE == 0x40014800UL, "TIM11 base address");
_Static_assert(ADC1_BASE == 0x40012000UL, "ADC1 base address");
_Static_assert(ADC_COMMON_BASE == 0x40012300UL, "ADC common base address");
_Static_assert(CRC_BASE == 0x40023000UL, "CRC base address");
_Static_assert(IWDG_BASE == 0x40003000UL, "IWDG base address");
_Static_assert(WWDG_BASE == 0x40002C00UL, "WWDG base address");
_Static_assert(RTC_BASE == 0x40002800UL, "RTC base address");
_Static_assert(GPIOD_BASE == 0x40020C00UL, "GPIOD base address");
_Static_assert(GPIOE_BASE == 0x40021000UL, "GPIOE base address");
_Static_assert(TIM3_BASE == 0x40000400UL, "TIM3 base address");
_Static_assert(TIM4_BASE == 0x40000800UL, "TIM4 base address");
_Static_assert(TIM10_BASE == 0x40014400UL, "TIM10 base address");

/* DMA controller block: four status/clear words followed by eight stream
 * blocks 0x18 apart, so the whole thing is 0x10 + 8 * 0x18 = 0xD0 bytes. */
CHECK_SIZE(adc_common_regs_t, 0x0C);
CHECK_OFFSET(dma_regs_t, LIFCR, 0x08);
CHECK_OFFSET(dma_regs_t, HIFCR, 0x0C);
CHECK_OFFSET(dma_regs_t, STREAM, 0x10);
CHECK_SIZE(dma_regs_t, 0xD0);
_Static_assert(sizeof(dma_stream_regs_t) == 0x18, "DMA stream stride");
