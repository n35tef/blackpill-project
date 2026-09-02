/**
 * @file    stm32f411_memmap.h
 * @brief   STM32F411 memory and peripheral base addresses (RM0383 sec. 2.3).
 *
 * Peripheral register structs live in the device/regs headers; each of those includes
 * this header for the bus base addresses it needs.
 */
#ifndef DEVICE_STM32F411_MEMMAP_H
#define DEVICE_STM32F411_MEMMAP_H

#include <stdint.h>

#include "cpu/cortex_m4.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- Main memory regions (STM32F411CE: 512 KB flash, 128 KB SRAM) ------- */
#define STM32_FLASH_ORIGIN 0x08000000UL
#define STM32_FLASH_SIZE   (512UL * 1024UL)
#define STM32_SRAM_ORIGIN  0x20000000UL
#define STM32_SRAM_SIZE    (128UL * 1024UL)

/* ---- Peripheral bus base addresses -------------------------------------- */
#define PERIPH_BASE      0x40000000UL
#define APB1PERIPH_BASE  (PERIPH_BASE + 0x00000000UL)
#define APB2PERIPH_BASE  (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE  (PERIPH_BASE + 0x00020000UL)
#define AHB2PERIPH_BASE  0x50000000UL

/* ---- System memory: factory-programmed device data (RM0383 24.1, 24.2) -- */
#define UID_BASE       0x1FFF7A10UL /**< 96-bit unique device ID, 3 words    */
#define FLASHSIZE_BASE 0x1FFF7A22UL /**< flash size in KB, 16 bits           */

/* ---- Debug (private peripheral bus) ------------------------------------- */
#define DBGMCU_BASE 0xE0042000UL

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_STM32F411_MEMMAP_H */
