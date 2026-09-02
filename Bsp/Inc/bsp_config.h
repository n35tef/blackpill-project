/**
 * @file    bsp_config.h
 * @brief   Single place to enable/disable BlackPill peripherals for a project.
 *
 * This is the only file you should need to edit to turn a peripheral on or
 * off. Every bsp_xxx.c module wraps its whole body in `#if BSP_USE_XXX`, so
 * setting a flag to 0 removes that peripheral's init code, IRQ handlers and
 * RAM/flash footprint from the build without touching CMakeLists.txt.
 *
 * To add a new peripheral to the BSP, follow the same pattern:
 *   1. Add a BSP_USE_xxx flag here (and any parameters it needs).
 *   2. Create Bsp/Inc/bsp_xxx.h + Bsp/Src/bsp_xxx.c, guarded by that flag.
 *   3. Call bsp_xxx_init() from bsp_init() in Bsp/Src/bsp.c.
 */
#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

/* ------------------------------------------------------------------------ */
/* Onboard GPIO                                                             */
/* ------------------------------------------------------------------------ */
#define BSP_USE_LED    1 /* Onboard LED, default PC13 (active low)         */
#define BSP_USE_BUTTON 1 /* Onboard/user button, default PA0 (active high) */

/* ------------------------------------------------------------------------ */
/* SPI2 (PB13 = SCK, PB14 = MISO, PB15 = MOSI)                              */
/* ------------------------------------------------------------------------ */
#define BSP_USE_SPI2     1 /* SPI2 peripheral, master mode                 */
#define BSP_USE_SPI2_DMA 1 /* DMA1 Stream3 (RX) / Stream4 (TX) for SPI2    */
                           /* only meaningful when BSP_USE_SPI2 is 1       */

#define BSP_SPI2_BAUDRATE_PRESCALER SPI_BAUDRATEPRESCALER_2
#define BSP_SPI2_CLK_POLARITY       SPI_POLARITY_LOW
#define BSP_SPI2_CLK_PHASE          SPI_PHASE_1EDGE

#endif /* BSP_CONFIG_H */
