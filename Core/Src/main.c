/**
 * @file main.c
 * @brief Minimal application entry point on top of the BSP.
 *
 * This is the only file most projects built on this base need to grow:
 * bsp_init() brings up HAL + the clock + whatever peripherals are enabled
 * in Bsp/Inc/bsp_config.h, then this loop is where your application code
 * goes.
 */
#include "main.h"

#include "bsp.h"
#include "bsp_config.h"
#include "bsp_gpio.h"

int main(void)
{
    bsp_init();

    while (1)
    {
#if BSP_USE_LED
        bsp_led_toggle();
#endif
        HAL_Delay(500);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif /* USE_FULL_ASSERT */
