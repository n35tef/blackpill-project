/**
 * @file    bsp_exti.h
 * @brief   External interrupt lines on GPIO pins.
 *
 * Each EXTI line 0..15 can be driven by pin N of exactly one port, selected
 * through SYSCFG. Lines 0-4 have their own interrupt vectors; 5-9 and 10-15
 * share one each, so this module demultiplexes them for you and dispatches to
 * per-line callbacks.
 */
#ifndef BSP_EXTI_H
#define BSP_EXTI_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_EXTI

/** Signal edges an EXTI line can react to. */
typedef enum
{
    BSP_EXTI_EDGE_RISING = 1,
    BSP_EXTI_EDGE_FALLING = 2,
    BSP_EXTI_EDGE_BOTH = 3,
} bsp_exti_edge_t;

/** Callback invoked from interrupt context; @p line is 0..15. */
typedef void (*bsp_exti_callback_t)(uint32_t line);

/** @brief Enable the SYSCFG clock. Call before configuring any line. */
void bsp_exti_init(void);

/**
 * @brief Attach an interrupt to a GPIO pin.
 *
 * The pin itself must already be configured as an input. The EXTI line number
 * is the pin number, so PA3 and PB3 cannot both have an interrupt.
 *
 * @param port     Port owning the pin.
 * @param pin      0..15; also the EXTI line number.
 * @param edge     Which edges to trigger on.
 * @param callback Invoked from the interrupt, may be NULL.
 * @param priority NVIC priority, 0 (highest) to 15.
 */
void bsp_exti_attach(const gpio_regs_t* port, uint32_t pin, bsp_exti_edge_t edge,
                     bsp_exti_callback_t callback, uint32_t priority);

/** @brief Stop interrupts on a line and forget its callback. */
void bsp_exti_detach(uint32_t line);

#endif /* BSP_USE_EXTI */

#ifdef __cplusplus
}
#endif

#endif /* BSP_EXTI_H */
