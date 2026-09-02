/**
 * @file    bsp.c
 * @brief   Board bring-up sequencing.
 */

#include "bsp.h"

bsp_status_t bsp_init(void)
{
    if (bsp_clock_init() != 0)
    {
        return BSP_ERR_CLOCK;
    }

    bsp_gpio_init();

#if BSP_USE_SYSTICK
    if (bsp_systick_init() != 0)
    {
        return BSP_ERR_SYSTICK;
    }
#endif

    /* From here on the order only matters in that DMA and SYSCFG must be up
     * before the peripherals that hang off them. */
#if BSP_USE_DMA1 || BSP_USE_DMA2
    bsp_dma_init();
#endif

#if BSP_USE_EXTI
    bsp_exti_init();
#endif

#if BSP_USE_SPI1 || BSP_USE_SPI2 || BSP_USE_SPI3 || BSP_USE_SPI4 || BSP_USE_SPI5
    bsp_spi_init();
#endif

#if BSP_USE_USART1 || BSP_USE_USART2 || BSP_USE_USART6
    bsp_usart_init();
#endif

#if BSP_USE_I2C1 || BSP_USE_I2C2 || BSP_USE_I2C3
    bsp_i2c_init();
#endif

#if BSP_USE_TIM1 || BSP_USE_TIM2 || BSP_USE_TIM3 || BSP_USE_TIM4 || BSP_USE_TIM5 ||                \
    BSP_USE_TIM9 || BSP_USE_TIM10 || BSP_USE_TIM11
    bsp_tim_init();
#endif

#if BSP_USE_ADC1
    bsp_adc_init();
#endif

#if BSP_USE_CRC
    bsp_crc_init();
#endif

#if BSP_USE_SDIO
    bsp_sdio_init(); /* peripheral only; call bsp_sd_init() when a card is inserted */
#endif

#if BSP_USE_USB_CDC
    bsp_usb_cdc_init();
#endif

#if BSP_USE_RTC
    if (bsp_rtc_init() != 0)
    {
        return BSP_ERR_PERIPHERAL;
    }
#endif

    /* The watchdogs come last so nothing above can trip them mid bring-up. */
#if BSP_USE_IWDG
    bsp_iwdg_init();
#endif

    return BSP_OK;
}

__attribute__((weak)) void bsp_error_handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
