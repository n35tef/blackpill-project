/**
 * @file    bsp_usart.h
 * @brief   USART1, USART2 and USART6 in asynchronous 8N1 mode.
 *
 * Set BSP_STDOUT_USART in bsp_config.h to 1, 2 or 6 and printf() goes to that
 * port; the newlib stubs in bsp_syscalls.c pick it up automatically.
 */
#ifndef BSP_USART_H
#define BSP_USART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_USART1 || BSP_USE_USART2 || BSP_USE_USART6

/** @brief Bring up every USART enabled in bsp_config.h. */
void bsp_usart_init(void);

/** @brief Send one byte, blocking until the transmit register is free. */
void bsp_usart_write_byte(usart_regs_t* usart, uint8_t byte);

/** @brief Send a block of bytes. */
void bsp_usart_write(usart_regs_t* usart, const uint8_t* data, size_t length);

/** @brief Send a NUL-terminated string. */
void bsp_usart_write_string(usart_regs_t* usart, const char* text);

/** @brief Block until a byte arrives, then return it. */
uint8_t bsp_usart_read_byte(usart_regs_t* usart);

/** @brief True if a received byte is waiting to be read. */
static inline bool bsp_usart_readable(const usart_regs_t* usart)
{
    return (usart->SR & USART_SR_RXNE) != 0U;
}

/**
 * @brief Read a byte if one is already waiting.
 * @return true if @p byte was written, false if nothing was pending.
 */
bool bsp_usart_read_byte_nonblocking(usart_regs_t* usart, uint8_t* byte);

/** @brief Block until the last bit has left the shift register. */
void bsp_usart_flush(usart_regs_t* usart);

/** @brief Change the baud rate of a live port. */
void bsp_usart_set_baud(usart_regs_t* usart, uint32_t baud);

#endif /* any USART */

#ifdef __cplusplus
}
#endif

#endif /* BSP_USART_H */
