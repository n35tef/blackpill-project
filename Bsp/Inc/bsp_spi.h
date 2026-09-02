/**
 * @file    bsp_spi.h
 * @brief   SPI1..SPI5 in master mode.
 *
 * Each bus is configured entirely from bsp_config.h: pins, clock divider,
 * clock polarity/phase, frame size and bit order. Chip select is left to the
 * application because most boards need more than one, and because hardware
 * NSS is rarely what you want.
 */
#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_SPI1 || BSP_USE_SPI2 || BSP_USE_SPI3 || BSP_USE_SPI4 || BSP_USE_SPI5

/** @brief Bring up every SPI bus enabled in bsp_config.h. */
void bsp_spi_init(void);

/**
 * @brief Exchange one frame, blocking until the shift register has drained.
 * @param spi   SPI1..SPI5.
 * @param value Frame to send; the upper byte is ignored in 8-bit mode.
 * @return The frame shifted in, or 0 on a bus configured without MISO.
 */
uint16_t bsp_spi_transfer(spi_regs_t* spi, uint16_t value);

/**
 * @brief Exchange a block of 8-bit frames.
 *
 * @p tx and @p rx may alias, and either may be NULL: a NULL @p tx sends 0xFF
 * padding, a NULL @p rx discards what comes back.
 */
void bsp_spi_transfer_bytes(spi_regs_t* spi, const uint8_t* tx, uint8_t* rx, size_t length);

/** @brief Send a block of 8-bit frames, discarding the received data. */
void bsp_spi_write_bytes(spi_regs_t* spi, const uint8_t* data, size_t length);

/** @brief Block until the bus is idle. Call before deasserting chip select. */
void bsp_spi_wait_idle(spi_regs_t* spi);

/**
 * @brief Change the clock divider on a live bus.
 * @param divider One of 2, 4, 8, 16, 32, 64, 128 or 256.
 */
void bsp_spi_set_baud_divider(spi_regs_t* spi, uint32_t divider);

#endif /* any SPI */

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */
