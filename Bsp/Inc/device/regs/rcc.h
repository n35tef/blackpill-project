/**
 * @file    rcc.h
 * @brief   Reset and Clock Control register definitions (RM0383 sec. 6.3).
 */
#ifndef DEVICE_REGS_RCC_H
#define DEVICE_REGS_RCC_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR;         /**< 0x00 clock control                    */
    __IO uint32_t PLLCFGR;    /**< 0x04 PLL configuration                */
    __IO uint32_t CFGR;       /**< 0x08 clock configuration              */
    __IO uint32_t CIR;        /**< 0x0C clock interrupt                  */
    __IO uint32_t AHB1RSTR;   /**< 0x10 AHB1 peripheral reset            */
    __IO uint32_t AHB2RSTR;   /**< 0x14 AHB2 peripheral reset            */
    uint32_t RESERVED0[2];    /*   0x18..0x1C                            */
    __IO uint32_t APB1RSTR;   /**< 0x20 APB1 peripheral reset            */
    __IO uint32_t APB2RSTR;   /**< 0x24 APB2 peripheral reset            */
    uint32_t RESERVED1[2];    /*   0x28..0x2C                            */
    __IO uint32_t AHB1ENR;    /**< 0x30 AHB1 peripheral clock enable     */
    __IO uint32_t AHB2ENR;    /**< 0x34 AHB2 peripheral clock enable     */
    uint32_t RESERVED2[2];    /*   0x38..0x3C                            */
    __IO uint32_t APB1ENR;    /**< 0x40 APB1 peripheral clock enable     */
    __IO uint32_t APB2ENR;    /**< 0x44 APB2 peripheral clock enable     */
    uint32_t RESERVED3[2];    /*   0x48..0x4C                            */
    __IO uint32_t AHB1LPENR;  /**< 0x50 AHB1 clock enable in low power   */
    __IO uint32_t AHB2LPENR;  /**< 0x54 AHB2 clock enable in low power   */
    uint32_t RESERVED4[2];    /*   0x58..0x5C                            */
    __IO uint32_t APB1LPENR;  /**< 0x60 APB1 clock enable in low power   */
    __IO uint32_t APB2LPENR;  /**< 0x64 APB2 clock enable in low power   */
    uint32_t RESERVED5[2];    /*   0x68..0x6C                            */
    __IO uint32_t BDCR;       /**< 0x70 backup domain control            */
    __IO uint32_t CSR;        /**< 0x74 clock control and status         */
    uint32_t RESERVED6[2];    /*   0x78..0x7C                            */
    __IO uint32_t SSCGR;      /**< 0x80 spread spectrum clock generation */
    __IO uint32_t PLLI2SCFGR; /**< 0x84 PLLI2S configuration             */
    uint32_t RESERVED7;       /*   0x88                                  */
    __IO uint32_t DCKCFGR;    /**< 0x8C dedicated clocks configuration   */
} rcc_regs_t;

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define RCC      ((rcc_regs_t*)RCC_BASE)

/* ---- CR ----------------------------------------------------------------- */
#define RCC_CR_HSION       (1UL << 0)
#define RCC_CR_HSIRDY      (1UL << 1)
#define RCC_CR_HSITRIM_POS 3U
#define RCC_CR_HSITRIM_MSK (0x1FUL << RCC_CR_HSITRIM_POS)
#define RCC_CR_HSEON       (1UL << 16)
#define RCC_CR_HSERDY      (1UL << 17)
#define RCC_CR_HSEBYP      (1UL << 18)
#define RCC_CR_CSSON       (1UL << 19)
#define RCC_CR_PLLON       (1UL << 24)
#define RCC_CR_PLLRDY      (1UL << 25)
#define RCC_CR_PLLI2SON    (1UL << 26)
#define RCC_CR_PLLI2SRDY   (1UL << 27)

/* ---- PLLCFGR ------------------------------------------------------------ */
#define RCC_PLLCFGR_PLLM_POS 0U
#define RCC_PLLCFGR_PLLM_MSK (0x3FUL << RCC_PLLCFGR_PLLM_POS)
#define RCC_PLLCFGR_PLLN_POS 6U
#define RCC_PLLCFGR_PLLN_MSK (0x1FFUL << RCC_PLLCFGR_PLLN_POS)
#define RCC_PLLCFGR_PLLP_POS 16U
#define RCC_PLLCFGR_PLLP_MSK (0x3UL << RCC_PLLCFGR_PLLP_POS)
#define RCC_PLLCFGR_PLLSRC   (1UL << 22) /**< 0 = HSI, 1 = HSE */
#define RCC_PLLCFGR_PLLQ_POS 24U
#define RCC_PLLCFGR_PLLQ_MSK (0xFUL << RCC_PLLCFGR_PLLQ_POS)

/* ---- CFGR --------------------------------------------------------------- */
#define RCC_CFGR_SW_POS      0U
#define RCC_CFGR_SW_MSK      (0x3UL << RCC_CFGR_SW_POS)
#define RCC_CFGR_SW_HSI      (0x0UL << RCC_CFGR_SW_POS)
#define RCC_CFGR_SW_HSE      (0x1UL << RCC_CFGR_SW_POS)
#define RCC_CFGR_SW_PLL      (0x2UL << RCC_CFGR_SW_POS)
#define RCC_CFGR_SWS_POS     2U
#define RCC_CFGR_SWS_MSK     (0x3UL << RCC_CFGR_SWS_POS)
#define RCC_CFGR_HPRE_POS    4U
#define RCC_CFGR_HPRE_MSK    (0xFUL << RCC_CFGR_HPRE_POS)
#define RCC_CFGR_PPRE1_POS   10U
#define RCC_CFGR_PPRE1_MSK   (0x7UL << RCC_CFGR_PPRE1_POS)
#define RCC_CFGR_PPRE2_POS   13U
#define RCC_CFGR_PPRE2_MSK   (0x7UL << RCC_CFGR_PPRE2_POS)
#define RCC_CFGR_RTCPRE_POS  16U
#define RCC_CFGR_RTCPRE_MSK  (0x1FUL << RCC_CFGR_RTCPRE_POS)
#define RCC_CFGR_MCO1_POS    21U
#define RCC_CFGR_MCO1_MSK    (0x3UL << RCC_CFGR_MCO1_POS)
#define RCC_CFGR_I2SSRC      (1UL << 23)
#define RCC_CFGR_MCO1PRE_POS 24U
#define RCC_CFGR_MCO1PRE_MSK (0x7UL << RCC_CFGR_MCO1PRE_POS)
#define RCC_CFGR_MCO2PRE_POS 27U
#define RCC_CFGR_MCO2PRE_MSK (0x7UL << RCC_CFGR_MCO2PRE_POS)
#define RCC_CFGR_MCO2_POS    30U
#define RCC_CFGR_MCO2_MSK    (0x3UL << RCC_CFGR_MCO2_POS)

/* AHB prescaler encodings (CFGR.HPRE) */
#define RCC_HPRE_DIV1   0x0UL
#define RCC_HPRE_DIV2   0x8UL
#define RCC_HPRE_DIV4   0x9UL
#define RCC_HPRE_DIV8   0xAUL
#define RCC_HPRE_DIV16  0xBUL
#define RCC_HPRE_DIV64  0xCUL
#define RCC_HPRE_DIV128 0xDUL
#define RCC_HPRE_DIV256 0xEUL
#define RCC_HPRE_DIV512 0xFUL

/* APB prescaler encodings (CFGR.PPRE1 / PPRE2) */
#define RCC_PPRE_DIV1  0x0UL
#define RCC_PPRE_DIV2  0x4UL
#define RCC_PPRE_DIV4  0x5UL
#define RCC_PPRE_DIV8  0x6UL
#define RCC_PPRE_DIV16 0x7UL

/* ---- AHB1ENR ------------------------------------------------------------ */
#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN (1UL << 1)
#define RCC_AHB1ENR_GPIOCEN (1UL << 2)
#define RCC_AHB1ENR_GPIODEN (1UL << 3)
#define RCC_AHB1ENR_GPIOEEN (1UL << 4)
#define RCC_AHB1ENR_GPIOHEN (1UL << 7)
#define RCC_AHB1ENR_CRCEN   (1UL << 12)
#define RCC_AHB1ENR_DMA1EN  (1UL << 21)
#define RCC_AHB1ENR_DMA2EN  (1UL << 22)

/* ---- AHB2ENR ------------------------------------------------------------ */
#define RCC_AHB2ENR_OTGFSEN (1UL << 7)

/* ---- APB1ENR ------------------------------------------------------------ */
#define RCC_APB1ENR_TIM2EN   (1UL << 0)
#define RCC_APB1ENR_TIM3EN   (1UL << 1)
#define RCC_APB1ENR_TIM4EN   (1UL << 2)
#define RCC_APB1ENR_TIM5EN   (1UL << 3)
#define RCC_APB1ENR_WWDGEN   (1UL << 11)
#define RCC_APB1ENR_SPI2EN   (1UL << 14)
#define RCC_APB1ENR_SPI3EN   (1UL << 15)
#define RCC_APB1ENR_USART2EN (1UL << 17)
#define RCC_APB1ENR_I2C1EN   (1UL << 21)
#define RCC_APB1ENR_I2C2EN   (1UL << 22)
#define RCC_APB1ENR_I2C3EN   (1UL << 23)
#define RCC_APB1ENR_PWREN    (1UL << 28)

/* ---- APB2ENR ------------------------------------------------------------ */
#define RCC_APB2ENR_TIM1EN   (1UL << 0)
#define RCC_APB2ENR_USART1EN (1UL << 4)
#define RCC_APB2ENR_USART6EN (1UL << 5)
#define RCC_APB2ENR_ADC1EN   (1UL << 8)
#define RCC_APB2ENR_SDIOEN   (1UL << 11)
#define RCC_APB2ENR_SPI1EN   (1UL << 12)
#define RCC_APB2ENR_SPI4EN   (1UL << 13)
#define RCC_APB2ENR_SYSCFGEN (1UL << 14)
#define RCC_APB2ENR_TIM9EN   (1UL << 16)
#define RCC_APB2ENR_TIM10EN  (1UL << 17)
#define RCC_APB2ENR_TIM11EN  (1UL << 18)
#define RCC_APB2ENR_SPI5EN   (1UL << 20)

/* ---- BDCR (backup domain) ----------------------------------------------- */
#define RCC_BDCR_LSEON      (1UL << 0)
#define RCC_BDCR_LSERDY     (1UL << 1)
#define RCC_BDCR_LSEBYP     (1UL << 2)
#define RCC_BDCR_LSEMOD     (1UL << 3)
#define RCC_BDCR_RTCSEL_POS 8U
#define RCC_BDCR_RTCSEL_MSK (0x3UL << RCC_BDCR_RTCSEL_POS)
#define RCC_BDCR_RTCEN      (1UL << 15)
#define RCC_BDCR_BDRST      (1UL << 16)

/* ---- CSR ---------------------------------------------------------------- */
#define RCC_CSR_LSION  (1UL << 0)
#define RCC_CSR_LSIRDY (1UL << 1)
#define RCC_CSR_RMVF   (1UL << 24)

/* ---- PLLI2SCFGR --------------------------------------------------------- */
#define RCC_PLLI2SCFGR_PLLI2SN_POS 6U
#define RCC_PLLI2SCFGR_PLLI2SN_MSK (0x1FFUL << RCC_PLLI2SCFGR_PLLI2SN_POS)
#define RCC_PLLI2SCFGR_PLLI2SR_POS 28U
#define RCC_PLLI2SCFGR_PLLI2SR_MSK (0x7UL << RCC_PLLI2SCFGR_PLLI2SR_POS)

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_RCC_H */
