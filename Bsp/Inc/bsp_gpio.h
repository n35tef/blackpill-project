/**
 * @file    bsp_gpio.h
 * @brief   Register-level GPIO helpers plus the board LED and button.
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief Enable the peripheral clock for a GPIO port. Safe to call twice. */
void bsp_gpio_enable_port(const gpio_regs_t* port);

/**
 * @brief Configure a pin as a digital output.
 * @param port   GPIOA..GPIOE or GPIOH.
 * @param pin    0..15.
 * @param otype  GPIO_OTYPER_PUSHPULL or GPIO_OTYPER_OPENDRAIN.
 * @param ospeed One of the GPIO_OSPEED_* values.
 * @param pupd   One of the GPIO_PUPD_* values.
 */
void bsp_gpio_config_output(gpio_regs_t* port, uint32_t pin, uint32_t otype, uint32_t ospeed,
                            uint32_t pupd);

/** @brief Configure a pin as a digital input with the given pull resistor. */
void bsp_gpio_config_input(gpio_regs_t* port, uint32_t pin, uint32_t pupd);

/** @brief Configure a pin as analog (also the lowest-power idle state). */
void bsp_gpio_config_analog(gpio_regs_t* port, uint32_t pin);

/**
 * @brief Route a pin to a peripheral.
 * @param af One of the GPIO_AFx_* values from device/regs/gpio.h.
 */
void bsp_gpio_config_alternate(gpio_regs_t* port, uint32_t pin, uint32_t af, uint32_t otype,
                               uint32_t ospeed, uint32_t pupd);

/** @brief Drive a pin high or low (atomic, via BSRR). */
static inline void bsp_gpio_write(gpio_regs_t* port, uint32_t pin, bool high)
{
    port->BSRR = high ? GPIO_BSRR_SET(pin) : GPIO_BSRR_RESET(pin);
}

/** @brief Invert a pin's output. */
static inline void bsp_gpio_toggle(gpio_regs_t* port, uint32_t pin)
{
    port->ODR ^= (1UL << pin);
}

/** @brief Read a pin's input level. */
static inline bool bsp_gpio_read(const gpio_regs_t* port, uint32_t pin)
{
    return (port->IDR & (1UL << pin)) != 0U;
}

/** @brief Configure the board LED and button selected in bsp_config.h. */
void bsp_gpio_init(void);

#if BSP_USE_LED
/** @brief Turn the on-board LED on, accounting for its active level. */
static inline void bsp_led_on(void)
{
    bsp_gpio_write(BSP_LED_PORT, BSP_LED_PIN, BSP_LED_ACTIVE_LOW ? false : true);
}

/** @brief Turn the on-board LED off. */
static inline void bsp_led_off(void)
{
    bsp_gpio_write(BSP_LED_PORT, BSP_LED_PIN, BSP_LED_ACTIVE_LOW ? true : false);
}

/** @brief Invert the on-board LED. */
static inline void bsp_led_toggle(void)
{
    bsp_gpio_toggle(BSP_LED_PORT, BSP_LED_PIN);
}
#endif /* BSP_USE_LED */

#if BSP_USE_BUTTON
/** @brief True while the on-board user key is held down. */
static inline bool bsp_button_pressed(void)
{
    const bool level = bsp_gpio_read(BSP_BUTTON_PORT, BSP_BUTTON_PIN);
    return BSP_BUTTON_ACTIVE_LOW ? !level : level;
}
#endif /* BSP_USE_BUTTON */

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
