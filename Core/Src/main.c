/**
 * @file    main.c
 * @brief   Application entry point.
 *
 * This is normally the only file you need to touch. bsp_init() brings up the
 * clock tree and everything switched on in Bsp/Inc/bsp_config.h; the rest of
 * this file is yours.
 */

#include "bsp.h"

int main(void)
{
    if (bsp_init() != BSP_OK)
    {
        bsp_error_handler();
    }

    while (1)
    {
#if BSP_USE_LED
        bsp_led_toggle();
#endif
#if BSP_USE_SYSTICK
        bsp_delay_ms(500);
#endif
    }
}
