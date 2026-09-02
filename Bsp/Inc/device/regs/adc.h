/**
 * @file    adc.h
 * @brief   ADC registers (RM0383 sec. 11.12).
 */
#ifndef DEVICE_REGS_ADC_H
#define DEVICE_REGS_ADC_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t SR;    /**< 0x00 status                       */
    __IO uint32_t CR1;   /**< 0x04 control 1                    */
    __IO uint32_t CR2;   /**< 0x08 control 2                    */
    __IO uint32_t SMPR1; /**< 0x0C sample time, channels 10-18  */
    __IO uint32_t SMPR2; /**< 0x10 sample time, channels 0-9    */
    __IO uint32_t JOFR1; /**< 0x14 injected offset 1            */
    __IO uint32_t JOFR2; /**< 0x18 injected offset 2            */
    __IO uint32_t JOFR3; /**< 0x1C injected offset 3            */
    __IO uint32_t JOFR4; /**< 0x20 injected offset 4            */
    __IO uint32_t HTR;   /**< 0x24 watchdog high threshold      */
    __IO uint32_t LTR;   /**< 0x28 watchdog low threshold       */
    __IO uint32_t SQR1;  /**< 0x2C regular sequence 1 (length)  */
    __IO uint32_t SQR2;  /**< 0x30 regular sequence 2           */
    __IO uint32_t SQR3;  /**< 0x34 regular sequence 3           */
    __IO uint32_t JSQR;  /**< 0x38 injected sequence            */
    __I uint32_t JDR1;   /**< 0x3C injected data 1              */
    __I uint32_t JDR2;   /**< 0x40 injected data 2              */
    __I uint32_t JDR3;   /**< 0x44 injected data 3              */
    __I uint32_t JDR4;   /**< 0x48 injected data 4              */
    __I uint32_t DR;     /**< 0x4C regular data                 */
} adc_regs_t;

/** Registers shared by all ADCs (only one is present on STM32F411). */
typedef struct
{
    __I uint32_t CSR;  /**< 0x00 common status         */
    __IO uint32_t CCR; /**< 0x04 common control        */
    __I uint32_t CDR;  /**< 0x08 dual/triple mode data - unused on this part */
} adc_common_regs_t;

#define ADC1_BASE       (APB2PERIPH_BASE + 0x2000UL)
#define ADC_COMMON_BASE (APB2PERIPH_BASE + 0x2300UL)

#define ADC1       ((adc_regs_t*)ADC1_BASE)
#define ADC_COMMON ((adc_common_regs_t*)ADC_COMMON_BASE)

/* ---- SR ----------------------------------------------------------------- */
#define ADC_SR_AWD   (1UL << 0)
#define ADC_SR_EOC   (1UL << 1)
#define ADC_SR_JEOC  (1UL << 2)
#define ADC_SR_JSTRT (1UL << 3)
#define ADC_SR_STRT  (1UL << 4)
#define ADC_SR_OVR   (1UL << 5)

/* ---- CR1 ---------------------------------------------------------------- */
#define ADC_CR1_EOCIE   (1UL << 5)
#define ADC_CR1_SCAN    (1UL << 8)
#define ADC_CR1_DISCEN  (1UL << 11)
#define ADC_CR1_RES_POS 24U
#define ADC_CR1_RES_MSK (0x3UL << ADC_CR1_RES_POS)
#define ADC_CR1_OVRIE   (1UL << 26)

/** Resolution encodings (CR1.RES). */
#define ADC_RES_12BIT 0x0UL
#define ADC_RES_10BIT 0x1UL
#define ADC_RES_8BIT  0x2UL
#define ADC_RES_6BIT  0x3UL

/* ---- CR2 ---------------------------------------------------------------- */
#define ADC_CR2_ADON     (1UL << 0)
#define ADC_CR2_CONT     (1UL << 1)
#define ADC_CR2_DMA      (1UL << 8)
#define ADC_CR2_DDS      (1UL << 9)
#define ADC_CR2_EOCS     (1UL << 10)
#define ADC_CR2_ALIGN    (1UL << 11) /**< 1 = left aligned */
#define ADC_CR2_SWSTART  (1UL << 30)

/* ---- Common CCR --------------------------------------------------------- */
#define ADC_CCR_ADCPRE_POS 16U
#define ADC_CCR_ADCPRE_MSK (0x3UL << ADC_CCR_ADCPRE_POS)
#define ADC_CCR_VBATE      (1UL << 22)
#define ADC_CCR_TSVREFE    (1UL << 23)

/** PCLK2 prescaler encodings (CCR.ADCPRE). */
#define ADC_PRESCALER_DIV2 0x0UL
#define ADC_PRESCALER_DIV4 0x1UL
#define ADC_PRESCALER_DIV6 0x2UL
#define ADC_PRESCALER_DIV8 0x3UL

/** Sample time encodings written into SMPR1/SMPR2 (3 bits per channel). */
#define ADC_SMP_3CYCLES   0x0UL
#define ADC_SMP_15CYCLES  0x1UL
#define ADC_SMP_28CYCLES  0x2UL
#define ADC_SMP_56CYCLES  0x3UL
#define ADC_SMP_84CYCLES  0x4UL
#define ADC_SMP_112CYCLES 0x5UL
#define ADC_SMP_144CYCLES 0x6UL
#define ADC_SMP_480CYCLES 0x7UL

/**
 * Internal channels multiplexed into the ADC.
 *
 * On STM32F411 the temperature sensor sits on channel 18 and shares it with
 * VBAT - not on channel 16 as on the F405/F407. CCR.TSVREFE selects the
 * sensor, CCR.VBATE selects the divider, and enabling VBATE wins, so only one
 * of the two can be read at a time.
 */
#define ADC_CHANNEL_VREFINT    17U
#define ADC_CHANNEL_TEMPSENSOR 18U
#define ADC_CHANNEL_VBAT       18U

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_ADC_H */
