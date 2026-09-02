/**
 * @file    bsp_usb.h
 * @brief   USB OTG_FS in device mode: core bring-up, endpoints, control
 *          transfers and the standard requests.
 *
 * This is the part every USB device needs regardless of what it pretends to
 * be. A class (see bsp_usb_cdc.h) plugs in through a bsp_usb_class_t: it
 * supplies the descriptors, answers class-specific requests, and gets told
 * when data arrives or has been sent. Everything below runs from the OTG_FS
 * interrupt, so class callbacks execute in interrupt context.
 *
 * Only full speed, only device mode, no DMA, no isochronous helpers.
 */
#ifndef BSP_USB_H
#define BSP_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_USB_CDC

/* ---- USB 2.0 chapter 9 constants the classes need ----------------------- */

#define USB_EP0_MAX_PACKET 64U

/* bmRequestType. */
#define USB_REQ_DIR_IN         0x80U
#define USB_REQ_TYPE_MSK       0x60U
#define USB_REQ_TYPE_STANDARD  0x00U
#define USB_REQ_TYPE_CLASS     0x20U
#define USB_REQ_TYPE_VENDOR    0x40U
#define USB_REQ_RECIP_MSK      0x1FU
#define USB_REQ_RECIP_DEVICE   0x00U
#define USB_REQ_RECIP_INTERFACE 0x01U
#define USB_REQ_RECIP_ENDPOINT 0x02U

/* Standard bRequest codes. */
#define USB_REQ_GET_STATUS        0U
#define USB_REQ_CLEAR_FEATURE     1U
#define USB_REQ_SET_FEATURE       3U
#define USB_REQ_SET_ADDRESS       5U
#define USB_REQ_GET_DESCRIPTOR    6U
#define USB_REQ_SET_DESCRIPTOR    7U
#define USB_REQ_GET_CONFIGURATION 8U
#define USB_REQ_SET_CONFIGURATION 9U
#define USB_REQ_GET_INTERFACE     10U
#define USB_REQ_SET_INTERFACE     11U

/* Descriptor types. */
#define USB_DESC_DEVICE           1U
#define USB_DESC_CONFIGURATION    2U
#define USB_DESC_STRING           3U
#define USB_DESC_INTERFACE        4U
#define USB_DESC_ENDPOINT         5U
#define USB_DESC_DEVICE_QUALIFIER 6U
#define USB_DESC_CS_INTERFACE     0x24U

/* Feature selectors. */
#define USB_FEATURE_ENDPOINT_HALT  0U
#define USB_FEATURE_REMOTE_WAKEUP  1U

/* Endpoint transfer types, as in bmAttributes and bsp_usb_ep_open(). */
#define USB_EP_CONTROL     0U
#define USB_EP_ISOCHRONOUS 1U
#define USB_EP_BULK        2U
#define USB_EP_INTERRUPT   3U

/** Endpoint addresses carry the direction in bit 7. */
#define USB_EP_IN(n)  ((uint8_t)(0x80U | (n)))
#define USB_EP_OUT(n) ((uint8_t)(n))

/** The 8-byte SETUP packet. */
typedef struct
{
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} bsp_usb_setup_t;

/**
 * Hooks a device class provides. Any callback may be NULL except the two
 * descriptors. All callbacks run in interrupt context.
 */
typedef struct
{
    const uint8_t* device_descriptor; /**< 18 bytes                                      */
    const uint8_t* config_descriptor; /**< wTotalLength bytes, one configuration         */
    const char* const* strings;       /**< ASCII strings for indexes 1..string_count     */
    uint8_t string_count;

    /** Bus reset seen: forget every endpoint, the host will re-enumerate. */
    void (*on_reset)(void);
    /** SET_CONFIGURATION(1) accepted: open the class endpoints here. */
    void (*on_configured)(void);
    /**
     * Class or vendor SETUP. For an IN request point data and length at what to
     * send (length may exceed wLength, it is clipped). For an OUT request
     * with wLength > 0 point data at a buffer of at least wLength bytes; the
     * core fills it and then calls on_class_data(). Return false to STALL.
     */
    bool (*on_class_request)(const bsp_usb_setup_t* setup, const uint8_t** data, uint16_t* length);
    /** Data stage of a class OUT request has arrived in the buffer given above. */
    void (*on_class_data)(const bsp_usb_setup_t* setup, const uint8_t* data, uint16_t length);
    /** An OUT transfer armed with bsp_usb_ep_read_start() has completed. */
    void (*on_out)(uint8_t ep, const uint8_t* data, uint16_t length);
    /** An IN transfer started with bsp_usb_ep_write() has been sent. */
    void (*on_in_done)(uint8_t ep);
    /** Start of frame, every 1 ms while the bus is active. */
    void (*on_sof)(void);
} bsp_usb_class_t;

/** @brief Bring the core up in device mode and connect to the bus. */
void bsp_usb_init(const bsp_usb_class_t* cls);

/** @brief True after SET_CONFIGURATION(1), until reset or disconnect. */
bool bsp_usb_configured(void);

/** @brief True while the host has the bus suspended. */
bool bsp_usb_suspended(void);

/**
 * @brief Activate an endpoint. Call from on_configured().
 * @param address    USB_EP_IN(n) or USB_EP_OUT(n), n = 1..3.
 * @param type       USB_EP_BULK or USB_EP_INTERRUPT.
 * @param max_packet Up to 64 bytes.
 */
void bsp_usb_ep_open(uint8_t address, uint8_t type, uint16_t max_packet);

/** @brief Deactivate an endpoint; in-flight transfers are dropped. */
void bsp_usb_ep_close(uint8_t address);

/**
 * @brief Start an IN transfer of @p length bytes (0 sends an empty packet).
 *
 * The data is copied into the endpoint's transmit FIFO before this returns,
 * so the buffer may be reused immediately. Completion is reported through
 * on_in_done(). Length is limited to the endpoint's FIFO size (see
 * BSP_USB_TX_FIFO_WORDS in bsp_usb.c).
 *
 * @return 0 if started, -1 if the endpoint is busy or the transfer too large.
 */
int bsp_usb_ep_write(uint8_t ep, const uint8_t* data, uint16_t length);

/** @brief True while an IN transfer is in progress on endpoint @p ep. */
bool bsp_usb_ep_in_busy(uint8_t ep);

/**
 * @brief Arm an OUT endpoint to receive up to @p length bytes into @p buffer.
 *
 * @p length should be a multiple of the endpoint's max packet size. The
 * transfer completes on a short packet or when @p length bytes are in, and
 * on_out() is then called with what arrived. The endpoint NAKs until armed
 * again.
 */
void bsp_usb_ep_read_start(uint8_t ep, uint8_t* buffer, uint16_t length);

/** @brief Halt an endpoint (STALL every token) until the host clears it. */
void bsp_usb_ep_stall(uint8_t address);

/** @brief Undo bsp_usb_ep_stall() and reset the data toggle. */
void bsp_usb_ep_unstall(uint8_t address);

#endif /* BSP_USE_USB_CDC */

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_H */
