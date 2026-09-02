#include "bsp_gpio.h"
#include "bsp_config.h"

#include "stm32f4xx_hal.h"

#if BSP_USE_LED
#ifndef BSP_LED_PORT
#define BSP_LED_PORT GPIOC
#define BSP_LED_PIN  GPIO_PIN_13
#endif
#endif

#if BSP_USE_BUTTON
#ifndef BSP_BUTTON_PORT
#define BSP_BUTTON_PORT GPIOA
#define BSP_BUTTON_PIN  GPIO_PIN_0
#endif
#endif

void bsp_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Cheap to always enable; HAL_GPIO_Init() elsewhere still needs the
     * matching port clock on regardless of which peripheral uses it. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

#if BSP_USE_LED
    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, GPIO_PIN_SET); /* off (active low) */
    GPIO_InitStruct.Pin = BSP_LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_LED_PORT, &GPIO_InitStruct);
#endif

#if BSP_USE_BUTTON
    GPIO_InitStruct.Pin = BSP_BUTTON_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BSP_BUTTON_PORT, &GPIO_InitStruct);
#endif
}

#if BSP_USE_LED
void bsp_led_write(int on)
{
    /* Onboard LED is active low. */
    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void bsp_led_toggle(void)
{
    HAL_GPIO_TogglePin(BSP_LED_PORT, BSP_LED_PIN);
}
#endif

#if BSP_USE_BUTTON
int bsp_button_read(void)
{
    return HAL_GPIO_ReadPin(BSP_BUTTON_PORT, BSP_BUTTON_PIN) == GPIO_PIN_SET;
}
#endif
