/**
 * @file    bsp_gpio.h
 * @brief   Onboard LED / button helpers for the BlackPill board.
 *          GPIO port clocks are enabled unconditionally here since almost
 *          every peripheral needs at least one of GPIOA/B/C.
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdint.h>

#include "bsp_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Enable GPIOA/B/C clocks and configure the onboard LED/button
 *        (each individually toggled by BSP_USE_LED / BSP_USE_BUTTON).
 */
void bsp_gpio_init(void);

#if BSP_USE_LED
/** @brief Drive the onboard LED. @param on Non-zero turns the LED on. */
void bsp_led_write(int on);
/** @brief Toggle the onboard LED. */
void bsp_led_toggle(void);
#endif

#if BSP_USE_BUTTON
/** @brief Read the onboard/user button. @return Non-zero when pressed. */
int bsp_button_read(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
