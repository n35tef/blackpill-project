/**
 * @file    bsp_sdio.h
 * @brief   SDIO host interface and SD memory card block access.
 *
 * Two layers, both polled:
 *
 *  - The SDIO peripheral: clock, power, pins, command/response, block data.
 *    Configured from bsp_config.h and brought up by bsp_init().
 *  - The SD card: identification (SDSC and SDHC/SDXC, physical layer v2),
 *    selection, bus width, and 512-byte block read/write. Brought up by
 *    bsp_sd_init() when the application wants the card, because a card may be
 *    absent or hot-plugged and that should not fail bsp_init().
 *
 * bsp_sd_read_blocks()/bsp_sd_write_blocks() are exactly what a FatFs
 * disk_read()/disk_write() port needs.
 */
#ifndef BSP_SDIO_H
#define BSP_SDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_SDIO

/** Every SD block this driver moves is 512 bytes; SDHC/SDXC allow nothing else. */
#define BSP_SD_BLOCK_SIZE 512U

/** Result codes. Negative so they cannot be confused with a byte count. */
#define BSP_SD_OK            0
#define BSP_SD_ETIMEOUT     -1 /**< no response, or data never arrived      */
#define BSP_SD_ECRC         -2 /**< response or data CRC failed              */
#define BSP_SD_ENOCARD      -3 /**< nothing answered CMD0/CMD8/ACMD41        */
#define BSP_SD_EUNSUPPORTED -4 /**< card is not an SD memory card (MMC, ...) */
#define BSP_SD_ECARD        -5 /**< card reported an error in its status     */
#define BSP_SD_EPARAM       -6 /**< bad argument (count 0, block past the end)*/
#define BSP_SD_EFIFO        -7 /**< FIFO under/overrun: CPU could not keep up */
#define BSP_SD_ENOTREADY    -8 /**< bsp_sd_init() has not succeeded          */

typedef struct
{
    uint32_t block_count;   /**< capacity in 512-byte blocks             */
    bool high_capacity;     /**< SDHC/SDXC: commands take block addresses */
    bool version_2;         /**< answered CMD8 (physical layer v2.00+)    */
    uint16_t rca;           /**< relative card address from CMD3          */
    uint32_t cid[4];        /**< raw CID, bits 127..0 in [0]..[3]          */
    uint32_t csd[4];        /**< raw CSD, bits 127..0 in [0]..[3]          */
} bsp_sd_card_t;

/** @brief Power the SDIO peripheral and configure its pins. Called by bsp_init(). */
void bsp_sdio_init(void);

/**
 * @brief Identify and select the card, switch to the configured bus width and
 *        clock, and leave it in transfer state.
 * @return BSP_SD_OK or a BSP_SD_E* code. Safe to call again after a failure.
 */
int bsp_sd_init(void);

/** @brief True once bsp_sd_init() has succeeded. */
bool bsp_sd_ready(void);

/** @brief Card details filled in by bsp_sd_init(); zeros before that. */
const bsp_sd_card_t* bsp_sd_card(void);

/**
 * @brief Read @p count 512-byte blocks starting at block @p block.
 * @param data Destination, 4-byte aligned, at least count * 512 bytes.
 */
int bsp_sd_read_blocks(uint32_t block, uint8_t* data, uint32_t count);

/**
 * @brief Write @p count 512-byte blocks starting at block @p block.
 * @param data Source, 4-byte aligned. Returns once the card has finished
 *             programming, so a following read sees the new data.
 */
int bsp_sd_write_blocks(uint32_t block, const uint8_t* data, uint32_t count);

/* ---- Peripheral-level primitives, for other card types or debugging ------ */

/** Response formats a command can expect. */
typedef enum
{
    SD_RESP_NONE, /**< no response                                  */
    SD_RESP_R1,   /**< 48-bit card status                           */
    SD_RESP_R1B,  /**< R1 followed by busy on D0                    */
    SD_RESP_R2,   /**< 136-bit CID or CSD                           */
    SD_RESP_R3,   /**< 48-bit OCR, no CRC                           */
    SD_RESP_R6,   /**< 48-bit published RCA                         */
    SD_RESP_R7,   /**< 48-bit interface condition                   */
} bsp_sd_resp_t;

/**
 * @brief Send one command and wait for its response.
 * @param resp  Receives RESP1 (short) or RESP1..4 (long); may be NULL.
 * @return BSP_SD_OK, BSP_SD_ETIMEOUT, BSP_SD_ECRC or BSP_SD_ECARD (R1 error bits).
 */
int bsp_sdio_command(uint8_t index, uint32_t argument, bsp_sd_resp_t type, uint32_t* resp);

/** @brief Set the SDIO_CK frequency; rounds down to what the divider allows. */
void bsp_sdio_set_clock(uint32_t hz);

#endif /* BSP_USE_SDIO */

#ifdef __cplusplus
}
#endif

#endif /* BSP_SDIO_H */
