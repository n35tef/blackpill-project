/**
 * @file    bsp_systick.h
 * @brief   Millisecond time base built on the Cortex-M4 SysTick timer.
 *
 * Compiled away entirely when BSP_USE_SYSTICK is 0.
 */
#ifndef BSP_SYSTICK_H
#define BSP_SYSTICK_H

#include <stdint.h>

#include "bsp_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_SYSTICK

/** @brief Start the tick. Returns non-zero if the reload value does not fit. */
int bsp_systick_init(void);

/** @brief Ticks since bsp_systick_init(); wraps after ~49 days at 1 kHz. */
uint32_t bsp_tick(void);

/** @brief Busy-wait for at least @p ms milliseconds. */
void bsp_delay_ms(uint32_t ms);

/** @brief Busy-wait for approximately @p us microseconds (cycle counted). */
void bsp_delay_us(uint32_t us);

/**
 * @brief Called from the SysTick interrupt after the tick counter advances.
 *
 * Defined weak and empty; override it to hook a scheduler or a soft timer.
 */
void bsp_systick_callback(void);

#endif /* BSP_USE_SYSTICK */

#ifdef __cplusplus
}
#endif

#endif /* BSP_SYSTICK_H */
