/**
 * @file    bsp_dma.h
 * @brief   DMA stream configuration helpers.
 *
 * The STM32F4 DMA is stream- and channel-based: a given peripheral request is
 * hard-wired to specific (controller, stream, channel) combinations, listed in
 * RM0383 tables 27 and 28. This module only handles the mechanics of setting a
 * stream up; the peripheral drivers supply the request mapping.
 */
#ifndef BSP_DMA_H
#define BSP_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_DMA1 || BSP_USE_DMA2

/** Everything needed to program one DMA stream. */
typedef struct
{
    dma_regs_t* controller; /**< DMA1 or DMA2                           */
    uint8_t stream;         /**< 0..7                                   */
    uint8_t channel;        /**< 0..7, from the request mapping table   */
    uint8_t direction;      /**< DMA_DIR_*                              */
    uint8_t periph_size;    /**< DMA_SIZE_*                             */
    uint8_t memory_size;    /**< DMA_SIZE_*                             */
    uint8_t priority;       /**< DMA_PRIORITY_*                         */
    bool periph_increment;
    bool memory_increment;
    bool circular;
    bool interrupt_on_complete;
} bsp_dma_config_t;

/** @brief Enable the clocks for the DMA controllers selected in bsp_config.h. */
void bsp_dma_init(void);

/**
 * @brief Program a stream, leaving it disabled.
 *
 * Call bsp_dma_start() afterwards to arm the transfer. Any pending interrupt
 * flags for the stream are cleared here, which the reference manual requires
 * before a stream can be re-enabled.
 */
void bsp_dma_configure(const bsp_dma_config_t* config);

/**
 * @brief Arm a configured stream.
 * @param periph_addr Peripheral data register address.
 * @param mem_addr    Memory buffer address.
 * @param count       Number of items (not bytes) to transfer.
 */
void bsp_dma_start(const bsp_dma_config_t* config, uint32_t periph_addr, uint32_t mem_addr,
                   uint16_t count);

/** @brief Disable a stream and wait for it to actually stop. */
void bsp_dma_stop(const bsp_dma_config_t* config);

/** @brief Items still outstanding on a stream. */
uint16_t bsp_dma_remaining(const bsp_dma_config_t* config);

/** @brief True once the stream's transfer complete flag is set. */
bool bsp_dma_complete(const bsp_dma_config_t* config);

#endif /* BSP_USE_DMA1 || BSP_USE_DMA2 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_DMA_H */
