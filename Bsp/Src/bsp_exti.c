/**
 * @file    bsp_exti.c
 * @brief   External interrupt lines on GPIO pins.
 */

#include "bsp_exti.h"

#if BSP_USE_EXTI

static bsp_exti_callback_t s_callbacks[16];

/** @brief SYSCFG_EXTICR port code for a GPIO port. */
static uint32_t port_code(const gpio_regs_t* port)
{
    if (port == GPIOA)
    {
        return SYSCFG_EXTI_PORTA;
    }
    if (port == GPIOB)
    {
        return SYSCFG_EXTI_PORTB;
    }
    if (port == GPIOC)
    {
        return SYSCFG_EXTI_PORTC;
    }
    if (port == GPIOD)
    {
        return SYSCFG_EXTI_PORTD;
    }
    if (port == GPIOE)
    {
        return SYSCFG_EXTI_PORTE;
    }
    return SYSCFG_EXTI_PORTH;
}

/** @brief Interrupt number serving an EXTI line. */
static int32_t line_irq(uint32_t line)
{
    if (line <= 4U)
    {
        return (int32_t)(IRQ_EXTI0 + line);
    }
    if (line <= 9U)
    {
        return IRQ_EXTI9_5;
    }
    return IRQ_EXTI15_10;
}

void bsp_exti_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
}

void bsp_exti_attach(const gpio_regs_t* port, uint32_t pin, bsp_exti_edge_t edge,
                     bsp_exti_callback_t callback, uint32_t priority)
{
    const uint32_t line = pin & 0xFU;

    s_callbacks[line] = callback;

    /* Four lines per EXTICR word, four bits each. */
    const uint32_t index = line >> 2U;
    const uint32_t shift = (line & 0x3U) * 4U;
    SYSCFG->EXTICR[index] =
        (SYSCFG->EXTICR[index] & ~(0xFUL << shift)) | (port_code(port) << shift);

    if ((edge & BSP_EXTI_EDGE_RISING) != 0)
    {
        EXTI->RTSR |= (1UL << line);
    }
    else
    {
        EXTI->RTSR &= ~(1UL << line);
    }

    if ((edge & BSP_EXTI_EDGE_FALLING) != 0)
    {
        EXTI->FTSR |= (1UL << line);
    }
    else
    {
        EXTI->FTSR &= ~(1UL << line);
    }

    EXTI->PR = (1UL << line); /* discard anything latched while configuring */
    EXTI->IMR |= (1UL << line);

    nvic_set_priority(line_irq(line), priority);
    nvic_enable_irq(line_irq(line));
}

void bsp_exti_detach(uint32_t line)
{
    line &= 0xFU;

    EXTI->IMR &= ~(1UL << line);
    EXTI->PR = (1UL << line);
    s_callbacks[line] = 0;
}

/** @brief Clear and dispatch every pending line in @p mask. */
static void dispatch(uint32_t mask)
{
    uint32_t pending = EXTI->PR & mask;

    while (pending != 0U)
    {
        const uint32_t line = (uint32_t)__builtin_ctz(pending);

        EXTI->PR = (1UL << line);
        if (s_callbacks[line] != 0)
        {
            s_callbacks[line](line);
        }
        pending &= ~(1UL << line);
    }
}

void EXTI0_IRQHandler(void)
{
    dispatch(1UL << 0);
}

void EXTI1_IRQHandler(void)
{
    dispatch(1UL << 1);
}

void EXTI2_IRQHandler(void)
{
    dispatch(1UL << 2);
}

void EXTI3_IRQHandler(void)
{
    dispatch(1UL << 3);
}

void EXTI4_IRQHandler(void)
{
    dispatch(1UL << 4);
}

void EXTI9_5_IRQHandler(void)
{
    dispatch(0x03E0UL); /* lines 5-9 */
}

void EXTI15_10_IRQHandler(void)
{
    dispatch(0xFC00UL); /* lines 10-15 */
}

#endif /* BSP_USE_EXTI */
