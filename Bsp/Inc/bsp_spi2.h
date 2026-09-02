/**
 * @file    bsp_spi2.h
 * @brief   SPI2 master (PB13=SCK, PB14=MISO, PB15=MOSI), optionally with
 *          DMA1 Stream3 (RX) / Stream4 (TX).
 *
 * Whole file compiles to nothing when BSP_USE_SPI2 is 0 in bsp_config.h.
 */
#ifndef BSP_SPI2_H
#define BSP_SPI2_H

#include "bsp_config.h"

#if BSP_USE_SPI2

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif

extern SPI_HandleTypeDef hspi2;

/**
 * @brief Configure SPI2 (and its MSP: GPIO alternate function + optional
 *        DMA linkage) as master, 8-bit, mode set by bsp_config.h.
 */
void bsp_spi2_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USE_SPI2 */

#endif /* BSP_SPI2_H */
