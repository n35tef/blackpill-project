#include "bsp.h"
#include "bsp_clock.h"
#include "bsp_config.h"
#include "bsp_gpio.h"
#if BSP_USE_SPI2
#include "bsp_spi2.h"
#endif

#include "stm32f4xx_hal.h"

void bsp_init(void)
{
    HAL_Init();
    bsp_clock_init();
    bsp_gpio_init();

#if BSP_USE_SPI2
    bsp_spi2_init();
#endif
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

/**
 * @brief Global MSP init, called once by HAL_Init(). Peripheral-specific
 *        MSP callbacks (HAL_SPI_MspInit, etc.) live in their own bsp_xxx.c.
 */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}
