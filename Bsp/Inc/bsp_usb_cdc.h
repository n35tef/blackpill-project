/**
 * @file    bsp_usb_cdc.h
 * @brief   USB CDC-ACM: the board shows up as a virtual COM port.
 *
 * On Linux this is /dev/ttyACMx, on Windows a COMx port, on macOS
 * /dev/tty.usbmodem*. No driver is needed on any of them. Baud rate and the
 * other line-coding parameters are accepted from the host and stored, but
 * they mean nothing on a USB link: data moves at USB speed regardless.
 *
 * Received bytes land in a ring buffer from the USB interrupt and are pulled
 * with bsp_usb_cdc_read(). Transmitted bytes go into a second ring buffer and
 * are shipped in the background; bsp_usb_cdc_write() blocks only when that
 * buffer is full and the host is slow to drain it.
 */
#ifndef BSP_USB_CDC_H
#define BSP_USB_CDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_USB_CDC

/** Line coding as sent by the host with SET_LINE_CODING (CDC PSTN 6.3.10). */
typedef struct
{
    uint32_t baud;      /**< bits per second, informational                 */
    uint8_t stop_bits;  /**< 0 = 1 stop bit, 1 = 1.5, 2 = 2                  */
    uint8_t parity;     /**< 0 none, 1 odd, 2 even, 3 mark, 4 space          */
    uint8_t data_bits;  /**< 5, 6, 7, 8 or 16                                */
} bsp_usb_cdc_line_coding_t;

/** @brief Register the class with the USB core and connect to the bus. */
void bsp_usb_cdc_init(void);

/**
 * @brief True when the port is open on the host side.
 *
 * Configured by the host and DTR asserted, which every terminal program
 * does on open. Writes while not connected are discarded, so a firmware that
 * prints before anyone is listening does not stall.
 */
bool bsp_usb_cdc_connected(void);

/** @brief Bytes waiting in the receive buffer. */
size_t bsp_usb_cdc_available(void);

/** @brief Take up to @p length received bytes; returns how many were copied. Never blocks. */
size_t bsp_usb_cdc_read(uint8_t* data, size_t length);

/** @brief Wait for one byte and return it (0..255). */
uint8_t bsp_usb_cdc_read_byte(void);

/**
 * @brief Queue @p length bytes for transmission.
 *
 * Returns the number accepted. Everything is accepted while the port is
 * connected: the call waits for buffer space, giving up after
 * BSP_USB_CDC_TX_TIMEOUT_MS without progress (host stopped reading). While
 * not connected nothing is queued and 0 is returned.
 */
size_t bsp_usb_cdc_write(const uint8_t* data, size_t length);

/** @brief Free space in the transmit buffer, for callers that must not block. */
size_t bsp_usb_cdc_write_space(void);

/** @brief Wait until everything queued has been handed to the host, or the timeout hits. */
void bsp_usb_cdc_flush(void);

/** @brief Last line coding the host set. */
bsp_usb_cdc_line_coding_t bsp_usb_cdc_line_coding(void);

/** @brief Host's DTR and RTS signals from the last SET_CONTROL_LINE_STATE. */
bool bsp_usb_cdc_dtr(void);
bool bsp_usb_cdc_rts(void);

#endif /* BSP_USE_USB_CDC */

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_CDC_H */
