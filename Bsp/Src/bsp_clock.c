/**
 * @file    bsp_clock.c
 * @brief   Register-level clock tree bring-up.
 */

#include "bsp_clock.h"

uint32_t g_system_core_clock = BSP_HSI_FREQ_HZ;

/* ------------------------------------------------------------------------ */
/* Compile-time sanity checks on the values in bsp_config.h                  */
/* ------------------------------------------------------------------------ */
_Static_assert(BSP_SYSCLK_HZ <= 100000000UL, "STM32F411 SYSCLK must not exceed 100 MHz");
_Static_assert(BSP_PCLK1_HZ <= 50000000UL, "APB1 must not exceed 50 MHz - raise BSP_APB1_PRESCALER");
_Static_assert(BSP_PCLK2_HZ <= 100000000UL, "APB2 must not exceed 100 MHz");

#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSI_PLL) || (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL)
_Static_assert(BSP_PLL_M >= 2U && BSP_PLL_M <= 63U, "BSP_PLL_M must be 2..63");
_Static_assert(BSP_PLL_N >= 50U && BSP_PLL_N <= 432U, "BSP_PLL_N must be 50..432");
_Static_assert(BSP_PLL_P == 2U || BSP_PLL_P == 4U || BSP_PLL_P == 6U || BSP_PLL_P == 8U,
               "BSP_PLL_P must be 2, 4, 6 or 8");
_Static_assert(BSP_PLL_Q >= 2U && BSP_PLL_Q <= 15U, "BSP_PLL_Q must be 2..15");
_Static_assert(BSP_PLL_VCO_IN_HZ >= 1000000UL && BSP_PLL_VCO_IN_HZ <= 2000000UL,
               "PLL input (source / BSP_PLL_M) must land in 1..2 MHz");
_Static_assert(BSP_PLL_VCO_OUT_HZ >= 100000000UL && BSP_PLL_VCO_OUT_HZ <= 432000000UL,
               "PLL VCO output must land in 100..432 MHz");
#endif

/** Encoded CFGR.HPRE value for BSP_AHB_PRESCALER. */
#if (BSP_AHB_PRESCALER == 1U)
#define HPRE_BITS RCC_HPRE_DIV1
#elif (BSP_AHB_PRESCALER == 2U)
#define HPRE_BITS RCC_HPRE_DIV2
#elif (BSP_AHB_PRESCALER == 4U)
#define HPRE_BITS RCC_HPRE_DIV4
#elif (BSP_AHB_PRESCALER == 8U)
#define HPRE_BITS RCC_HPRE_DIV8
#elif (BSP_AHB_PRESCALER == 16U)
#define HPRE_BITS RCC_HPRE_DIV16
#elif (BSP_AHB_PRESCALER == 64U)
#define HPRE_BITS RCC_HPRE_DIV64
#elif (BSP_AHB_PRESCALER == 128U)
#define HPRE_BITS RCC_HPRE_DIV128
#elif (BSP_AHB_PRESCALER == 256U)
#define HPRE_BITS RCC_HPRE_DIV256
#elif (BSP_AHB_PRESCALER == 512U)
#define HPRE_BITS RCC_HPRE_DIV512
#else
#error "BSP_AHB_PRESCALER must be 1, 2, 4, 8, 16, 64, 128, 256 or 512"
#endif

#define PPRE_ENCODE(div)                                                       \
    ((div) == 1U   ? RCC_PPRE_DIV1                                             \
     : (div) == 2U ? RCC_PPRE_DIV2                                             \
     : (div) == 4U ? RCC_PPRE_DIV4                                             \
     : (div) == 8U ? RCC_PPRE_DIV8                                             \
                   : RCC_PPRE_DIV16)

_Static_assert(BSP_APB1_PRESCALER == 1U || BSP_APB1_PRESCALER == 2U || BSP_APB1_PRESCALER == 4U ||
                   BSP_APB1_PRESCALER == 8U || BSP_APB1_PRESCALER == 16U,
               "BSP_APB1_PRESCALER must be 1, 2, 4, 8 or 16");
_Static_assert(BSP_APB2_PRESCALER == 1U || BSP_APB2_PRESCALER == 2U || BSP_APB2_PRESCALER == 4U ||
                   BSP_APB2_PRESCALER == 8U || BSP_APB2_PRESCALER == 16U,
               "BSP_APB2_PRESCALER must be 1, 2, 4, 8 or 16");

/**
 * Flash wait states at 3.3 V (RM0383 table 6): one extra state per 30 MHz.
 * Scale 1 voltage is required above 84 MHz, scale 2 above 64 MHz.
 */
#if (BSP_HCLK_HZ > 90000000UL)
#define FLASH_WAIT_STATES 3U
#elif (BSP_HCLK_HZ > 60000000UL)
#define FLASH_WAIT_STATES 2U
#elif (BSP_HCLK_HZ > 30000000UL)
#define FLASH_WAIT_STATES 1U
#else
#define FLASH_WAIT_STATES 0U
#endif

#if (BSP_SYSCLK_HZ > 84000000UL)
#define VOLTAGE_SCALE PWR_VOS_SCALE1
#elif (BSP_SYSCLK_HZ > 64000000UL)
#define VOLTAGE_SCALE PWR_VOS_SCALE2
#else
#define VOLTAGE_SCALE PWR_VOS_SCALE3
#endif

/** Iterations to wait for an oscillator or the PLL to report ready. */
#define CLOCK_TIMEOUT 0x00100000UL

/** @brief Spin until @p expr becomes true; return 1 on timeout. */
static int wait_until(volatile const uint32_t* reg, uint32_t mask, int set)
{
    uint32_t timeout = CLOCK_TIMEOUT;

    while (timeout-- != 0U)
    {
        const int is_set = ((*reg & mask) != 0U);
        if (is_set == set)
        {
            return 0;
        }
    }
    return 1;
}

int bsp_clock_init(void)
{
    /* PWR must be clocked before the voltage scale can be programmed. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;

    PWR->CR = (PWR->CR & ~PWR_CR_VOS_MSK) | (VOLTAGE_SCALE << PWR_CR_VOS_POS);

    /*
     * Raise the flash wait states before speeding the core up. Doing it in
     * this order is safe in both directions: extra wait states never break a
     * slow clock, but too few always break a fast one.
     */
    uint32_t acr = FLASH_WAIT_STATES << FLASH_ACR_LATENCY_POS;
#if BSP_FLASH_PREFETCH
    acr |= FLASH_ACR_PRFTEN;
#endif
#if BSP_FLASH_ICACHE
    acr |= FLASH_ACR_ICEN;
#endif
#if BSP_FLASH_DCACHE
    acr |= FLASH_ACR_DCEN;
#endif
    FLASH_R->ACR = acr;

    /* HSI is running out of reset and is the fallback while we reconfigure. */
    RCC->CR |= RCC_CR_HSION;
    if (wait_until(&RCC->CR, RCC_CR_HSIRDY, 1) != 0)
    {
        return -1;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MSK) | RCC_CFGR_SW_HSI;
    RCC->CR &= ~RCC_CR_PLLON;

#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE) || (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL)
#if BSP_HSE_BYPASS
    RCC->CR |= RCC_CR_HSEBYP;
#else
    RCC->CR &= ~RCC_CR_HSEBYP;
#endif
    RCC->CR |= RCC_CR_HSEON;
    if (wait_until(&RCC->CR, RCC_CR_HSERDY, 1) != 0)
    {
        return -2;
    }
#endif

#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSI_PLL) || (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL)
    /* PLLP is encoded as (P / 2) - 1. */
    RCC->PLLCFGR = ((uint32_t)BSP_PLL_M << RCC_PLLCFGR_PLLM_POS) |
                   ((uint32_t)BSP_PLL_N << RCC_PLLCFGR_PLLN_POS) |
                   ((((uint32_t)BSP_PLL_P / 2U) - 1U) << RCC_PLLCFGR_PLLP_POS) |
                   ((uint32_t)BSP_PLL_Q << RCC_PLLCFGR_PLLQ_POS)
#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL)
                   | RCC_PLLCFGR_PLLSRC
#endif
        ;

    RCC->CR |= RCC_CR_PLLON;
    if (wait_until(&RCC->CR, RCC_CR_PLLRDY, 1) != 0)
    {
        return -3;
    }
#endif

    /*
     * Program the bus prescalers before switching SYSCLK so the APB buses are
     * never briefly overclocked.
     */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE_MSK | RCC_CFGR_PPRE1_MSK | RCC_CFGR_PPRE2_MSK)) |
                (HPRE_BITS << RCC_CFGR_HPRE_POS) |
                (PPRE_ENCODE(BSP_APB1_PRESCALER) << RCC_CFGR_PPRE1_POS) |
                (PPRE_ENCODE(BSP_APB2_PRESCALER) << RCC_CFGR_PPRE2_POS);

#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSI)
    const uint32_t sw = RCC_CFGR_SW_HSI;
#elif (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE)
    const uint32_t sw = RCC_CFGR_SW_HSE;
#else
    const uint32_t sw = RCC_CFGR_SW_PLL;
#endif

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MSK) | sw;

    uint32_t timeout = CLOCK_TIMEOUT;
    while (((RCC->CFGR & RCC_CFGR_SWS_MSK) >> RCC_CFGR_SWS_POS) != (sw >> RCC_CFGR_SW_POS))
    {
        if (timeout-- == 0U)
        {
            return -4;
        }
    }

#if BSP_USE_LSI
    RCC->CSR |= RCC_CSR_LSION;
    if (wait_until(&RCC->CSR, RCC_CSR_LSIRDY, 1) != 0)
    {
        return -5;
    }
#endif

#if BSP_USE_LSE
    /* The LSE lives in the backup domain, which is write protected on reset. */
    PWR->CR |= PWR_CR_DBP;
    RCC->BDCR |= RCC_BDCR_LSEON;
    if (wait_until(&RCC->BDCR, RCC_BDCR_LSERDY, 1) != 0)
    {
        return -6;
    }
#endif

    g_system_core_clock = BSP_HCLK_HZ;
    return 0;
}
