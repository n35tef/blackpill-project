/**
 * @file    bsp_gpio.c
 * @brief   Register-level GPIO configuration.
 */

#include "bsp_gpio.h"

/** @brief Write a 2-bit field for @p pin into a MODER/OSPEEDR/PUPDR register. */
static inline void write_2bit_field(volatile uint32_t* reg, uint32_t pin, uint32_t value)
{
    const uint32_t shift = pin * 2U;
    *reg = (*reg & ~(0x3UL << shift)) | ((value & 0x3UL) << shift);
}

void bsp_gpio_enable_port(const gpio_regs_t* port)
{
    uint32_t bit = 0U;

    if (port == GPIOA)
    {
        bit = RCC_AHB1ENR_GPIOAEN;
    }
    else if (port == GPIOB)
    {
        bit = RCC_AHB1ENR_GPIOBEN;
    }
    else if (port == GPIOC)
    {
        bit = RCC_AHB1ENR_GPIOCEN;
    }
    else if (port == GPIOD)
    {
        bit = RCC_AHB1ENR_GPIODEN;
    }
    else if (port == GPIOE)
    {
        bit = RCC_AHB1ENR_GPIOEEN;
    }
    else if (port == GPIOH)
    {
        bit = RCC_AHB1ENR_GPIOHEN;
    }
    else
    {
        return;
    }

    RCC->AHB1ENR |= bit;
    /* Read back: the clock needs a cycle to reach the peripheral. */
    (void)RCC->AHB1ENR;
}

void bsp_gpio_config_output(gpio_regs_t* port, uint32_t pin, uint32_t otype, uint32_t ospeed,
                            uint32_t pupd)
{
    bsp_gpio_enable_port(port);

    write_2bit_field(&port->OSPEEDR, pin, ospeed);
    write_2bit_field(&port->PUPDR, pin, pupd);
    port->OTYPER = (port->OTYPER & ~(1UL << pin)) | ((otype & 1UL) << pin);
    write_2bit_field(&port->MODER, pin, GPIO_MODER_OUTPUT);
}

void bsp_gpio_config_input(gpio_regs_t* port, uint32_t pin, uint32_t pupd)
{
    bsp_gpio_enable_port(port);

    write_2bit_field(&port->PUPDR, pin, pupd);
    write_2bit_field(&port->MODER, pin, GPIO_MODER_INPUT);
}

void bsp_gpio_config_analog(gpio_regs_t* port, uint32_t pin)
{
    bsp_gpio_enable_port(port);

    write_2bit_field(&port->PUPDR, pin, GPIO_PUPD_NONE);
    write_2bit_field(&port->MODER, pin, GPIO_MODER_ANALOG);
}

void bsp_gpio_config_alternate(gpio_regs_t* port, uint32_t pin, uint32_t af, uint32_t otype,
                               uint32_t ospeed, uint32_t pupd)
{
    bsp_gpio_enable_port(port);

    /* AFR[0] covers pins 0-7, AFR[1] covers pins 8-15. */
    const uint32_t index = pin >> 3U;
    const uint32_t shift = (pin & 0x7U) * 4U;
    port->AFR[index] = (port->AFR[index] & ~(0xFUL << shift)) | ((af & 0xFUL) << shift);

    write_2bit_field(&port->OSPEEDR, pin, ospeed);
    write_2bit_field(&port->PUPDR, pin, pupd);
    port->OTYPER = (port->OTYPER & ~(1UL << pin)) | ((otype & 1UL) << pin);
    write_2bit_field(&port->MODER, pin, GPIO_MODER_AF);
}

void bsp_gpio_init(void)
{
#if BSP_USE_LED
    bsp_gpio_config_output(BSP_LED_PORT, BSP_LED_PIN, GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_LOW,
                           GPIO_PUPD_NONE);
    bsp_led_off();
#endif

#if BSP_USE_BUTTON
    bsp_gpio_config_input(BSP_BUTTON_PORT, BSP_BUTTON_PIN, BSP_BUTTON_PULL);
#endif
}
