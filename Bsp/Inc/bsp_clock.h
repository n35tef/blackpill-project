/**
 * @file    bsp_clock.h
 * @brief   Clock tree setup and derived frequencies.
 *
 * The whole tree is resolved at compile time from bsp_config.h, so other
 * drivers can use BSP_PCLK1_HZ and friends in constant expressions (baud rate
 * dividers, timer prescalers, and so on) without any run-time lookup.
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Fixed frequency of the internal high-speed RC oscillator. */
#define BSP_HSI_FREQ_HZ 16000000UL

/** Frequency feeding the PLL input divider. */
#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL)
#define BSP_PLL_SOURCE_HZ BSP_HSE_FREQ_HZ
#else
#define BSP_PLL_SOURCE_HZ BSP_HSI_FREQ_HZ
#endif

#define BSP_PLL_VCO_IN_HZ  (BSP_PLL_SOURCE_HZ / BSP_PLL_M)
#define BSP_PLL_VCO_OUT_HZ (BSP_PLL_VCO_IN_HZ * BSP_PLL_N)
#define BSP_PLL_P_OUT_HZ   (BSP_PLL_VCO_OUT_HZ / BSP_PLL_P)

/** 48 MHz domain clock (USB FS, SDIO, RNG) produced by the PLL Q divider. */
#define BSP_PLL_Q_OUT_HZ (BSP_PLL_VCO_OUT_HZ / BSP_PLL_Q)

/** Resulting system clock. */
#if (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSI)
#define BSP_SYSCLK_HZ BSP_HSI_FREQ_HZ
#elif (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE)
#define BSP_SYSCLK_HZ BSP_HSE_FREQ_HZ
#else
#define BSP_SYSCLK_HZ BSP_PLL_P_OUT_HZ
#endif

#define BSP_HCLK_HZ  (BSP_SYSCLK_HZ / BSP_AHB_PRESCALER)
#define BSP_PCLK1_HZ (BSP_HCLK_HZ / BSP_APB1_PRESCALER)
#define BSP_PCLK2_HZ (BSP_HCLK_HZ / BSP_APB2_PRESCALER)

/**
 * Timer clocks. When an APB prescaler is 1 the timers run at PCLK; otherwise
 * they run at twice PCLK (RM0383 sec. 6.2).
 */
#if (BSP_APB1_PRESCALER == 1U)
#define BSP_TIMER_PCLK1_HZ BSP_PCLK1_HZ
#else
#define BSP_TIMER_PCLK1_HZ (BSP_PCLK1_HZ * 2UL)
#endif

#if (BSP_APB2_PRESCALER == 1U)
#define BSP_TIMER_PCLK2_HZ BSP_PCLK2_HZ
#else
#define BSP_TIMER_PCLK2_HZ (BSP_PCLK2_HZ * 2UL)
#endif

/**
 * @brief Bring the clock tree up as described in bsp_config.h.
 *
 * Sets the voltage scale and flash wait states first, starts the requested
 * oscillator (and PLL), programs the bus prescalers, then switches SYSCLK
 * over. Called from bsp_init(); there is no reason to call it directly.
 *
 * @return 0 on success, non-zero if an oscillator or the PLL failed to lock.
 */
int bsp_clock_init(void);

/** @brief Current system core clock in Hz (equals BSP_HCLK_HZ after init). */
extern uint32_t g_system_core_clock;

#ifdef __cplusplus
}
#endif

#endif /* BSP_CLOCK_H */
