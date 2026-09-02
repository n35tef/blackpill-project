/**
 * @file    bsp_systick.c
 * @brief   SysTick-based millisecond time base.
 */

#include "bsp_systick.h"

#include "bsp_clock.h"
#include "device/stm32f411.h"

void bsp_delay_us(uint32_t us)
{
#ifdef BSP_HOST_SIM
    /* The simulator counts cycles instead of spinning, so drivers that cut a
     * settling time short are caught by its timing rules. */
    __busy_wait((BSP_HCLK_HZ / 1000000UL) * us);
#else
    /*
     * Rough loop calibrated on cycles rather than the tick counter, so it
     * still works for delays shorter than one tick. The loop body is three
     * instructions on Cortex-M4, hence the divide by three.
     */
    volatile uint32_t cycles = (BSP_HCLK_HZ / 3000000UL) * us;

    while (cycles-- != 0U)
    {
    }
#endif
}

#if BSP_USE_SYSTICK

#define SYSTICK_RELOAD (BSP_HCLK_HZ / BSP_SYSTICK_FREQ_HZ)

_Static_assert(SYSTICK_RELOAD >= 2UL, "BSP_SYSTICK_FREQ_HZ is too high for this core clock");
_Static_assert(SYSTICK_RELOAD <= 0x01000000UL,
               "BSP_SYSTICK_FREQ_HZ is too low - SysTick is only 24 bits wide");

static volatile uint32_t s_ticks;

int bsp_systick_init(void)
{
    s_ticks = 0U;

    SYSTICK->LOAD = SYSTICK_RELOAD - 1UL;
    SYSTICK->VAL = 0U;
    nvic_set_priority(IRQ_SYSTICK, BSP_SYSTICK_PRIORITY);
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;

    return 0;
}

uint32_t bsp_tick(void)
{
    return s_ticks;
}

void bsp_delay_ms(uint32_t ms)
{
    const uint32_t start = s_ticks;

    /*
     * One extra tick: the current period is already partly elapsed, so
     * without it a request for 1 ms could return almost immediately.
     */
    const uint32_t target = (ms * BSP_SYSTICK_FREQ_HZ) / 1000UL + 1UL;

    while ((s_ticks - start) < target)
    {
        __WFI();
    }
}

__attribute__((weak)) void bsp_systick_callback(void)
{
}

/** SysTick exception; the vector table entry is declared in the startup file. */
void SysTick_Handler(void)
{
    s_ticks++;
    bsp_systick_callback();
}

#endif /* BSP_USE_SYSTICK */
