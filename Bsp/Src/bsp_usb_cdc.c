/**
 * @file    bsp_usb_cdc.c
 * @brief   CDC-ACM (virtual COM port) on top of the OTG_FS device core.
 *
 * Endpoints:
 *   EP0        control, standard and class requests
 *   EP1 OUT    bulk, host -> device data, 64-byte packets
 *   EP1 IN     bulk, device -> host data, 64-byte packets
 *   EP2 IN     interrupt, serial-state notifications (declared, never used)
 *
 * Refs: USB CDC 1.2 (Class Definitions for Communications Devices) and the
 * PSTN subclass spec for the ACM requests and functional descriptors.
 */

#include "bsp_usb_cdc.h"

#if BSP_USE_USB_CDC

#include <string.h>

#include "bsp_systick.h"
#include "bsp_usb.h"
#include "device/stm32f411.h"

/* ---- Configuration checks ----------------------------------------------- */

_Static_assert((BSP_USB_CDC_RX_BUFFER & (BSP_USB_CDC_RX_BUFFER - 1U)) == 0U &&
                   BSP_USB_CDC_RX_BUFFER >= 128U,
               "BSP_USB_CDC_RX_BUFFER must be a power of two, at least 128");
_Static_assert((BSP_USB_CDC_TX_BUFFER & (BSP_USB_CDC_TX_BUFFER - 1U)) == 0U &&
                   BSP_USB_CDC_TX_BUFFER >= 64U,
               "BSP_USB_CDC_TX_BUFFER must be a power of two, at least 64");
_Static_assert(BSP_USB_MAX_POWER_MA <= 500U, "BSP_USB_MAX_POWER_MA above 500 mA is not allowed by USB 2.0");
#if BSP_STDOUT_USB && (BSP_STDOUT_USART != 0)
#error "BSP_STDOUT_USB and BSP_STDOUT_USART both claim stdout; pick one"
#endif

/* ---- CDC constants ------------------------------------------------------ */

#define CDC_EP_DATA          1U
#define CDC_EP_NOTIFY        2U
#define CDC_DATA_PACKET      64U
#define CDC_NOTIFY_PACKET    8U
#define CDC_TX_CHUNK_MAX     512U /**< one transfer must fit the EP1 TX FIFO (128 words) */

#define CDC_REQ_SET_LINE_CODING        0x20U
#define CDC_REQ_GET_LINE_CODING        0x21U
#define CDC_REQ_SET_CONTROL_LINE_STATE 0x22U
#define CDC_REQ_SEND_BREAK             0x23U

/* ---- Descriptors -------------------------------------------------------- */

#define LO(x) ((uint8_t)((x) & 0xFFU))
#define HI(x) ((uint8_t)(((x) >> 8U) & 0xFFU))

static const uint8_t k_device_descriptor[18] = {
    18U,                     /* bLength                              */
    USB_DESC_DEVICE,         /* bDescriptorType                      */
    0x00U, 0x02U,            /* bcdUSB 2.00                          */
    0x02U,                   /* bDeviceClass: Communications         */
    0x00U,                   /* bDeviceSubClass                      */
    0x00U,                   /* bDeviceProtocol                      */
    USB_EP0_MAX_PACKET,      /* bMaxPacketSize0                      */
    LO(BSP_USB_VID), HI(BSP_USB_VID),
    LO(BSP_USB_PID), HI(BSP_USB_PID),
    LO(BSP_USB_DEVICE_VERSION), HI(BSP_USB_DEVICE_VERSION),
    1U,                      /* iManufacturer                        */
    2U,                      /* iProduct                             */
    3U,                      /* iSerialNumber                        */
    1U,                      /* bNumConfigurations                   */
};

#define CDC_CONFIG_TOTAL (9U + 9U + 5U + 5U + 4U + 5U + 7U + 9U + 7U + 7U)

static const uint8_t k_config_descriptor[CDC_CONFIG_TOTAL] = {
    /* Configuration */
    9U, USB_DESC_CONFIGURATION,
    LO(CDC_CONFIG_TOTAL), HI(CDC_CONFIG_TOTAL),
    2U,                                        /* bNumInterfaces                  */
    1U,                                        /* bConfigurationValue             */
    0U,                                        /* iConfiguration                  */
    (uint8_t)(0x80U | (BSP_USB_SELF_POWERED ? 0x40U : 0x00U)), /* bmAttributes     */
    (uint8_t)(BSP_USB_MAX_POWER_MA / 2U),      /* bMaxPower, 2 mA units           */

    /* Interface 0: communications class, ACM subclass */
    9U, USB_DESC_INTERFACE,
    0U,      /* bInterfaceNumber   */
    0U,      /* bAlternateSetting  */
    1U,      /* bNumEndpoints      */
    0x02U,   /* bInterfaceClass: CDC              */
    0x02U,   /* bInterfaceSubClass: ACM           */
    0x01U,   /* bInterfaceProtocol: AT commands (what every host expects) */
    0U,      /* iInterface         */

    /* Header functional descriptor */
    5U, USB_DESC_CS_INTERFACE, 0x00U, 0x10U, 0x01U, /* bcdCDC 1.10 */
    /* Call management functional descriptor */
    5U, USB_DESC_CS_INTERFACE, 0x01U,
    0x00U, /* bmCapabilities: no call management     */
    1U,    /* bDataInterface                         */
    /* ACM functional descriptor */
    4U, USB_DESC_CS_INTERFACE, 0x02U,
    0x02U, /* bmCapabilities: line coding + control line state + break */
    /* Union functional descriptor */
    5U, USB_DESC_CS_INTERFACE, 0x06U,
    0U, /* bControlInterface     */
    1U, /* bSubordinateInterface */

    /* Notification endpoint */
    7U, USB_DESC_ENDPOINT,
    USB_EP_IN(CDC_EP_NOTIFY),
    USB_EP_INTERRUPT,
    LO(CDC_NOTIFY_PACKET), HI(CDC_NOTIFY_PACKET),
    16U, /* bInterval, ms */

    /* Interface 1: CDC data class */
    9U, USB_DESC_INTERFACE,
    1U,    /* bInterfaceNumber   */
    0U,    /* bAlternateSetting  */
    2U,    /* bNumEndpoints      */
    0x0AU, /* bInterfaceClass: CDC data */
    0x00U, 0x00U, 0U,

    /* Data OUT */
    7U, USB_DESC_ENDPOINT,
    USB_EP_OUT(CDC_EP_DATA),
    USB_EP_BULK,
    LO(CDC_DATA_PACKET), HI(CDC_DATA_PACKET),
    0U,
    /* Data IN */
    7U, USB_DESC_ENDPOINT,
    USB_EP_IN(CDC_EP_DATA),
    USB_EP_BULK,
    LO(CDC_DATA_PACKET), HI(CDC_DATA_PACKET),
    0U,
};

#if BSP_USB_SERIAL_FROM_UID
static char s_serial[25]; /* 96 bits as 24 hex digits */
#endif

static const char* s_strings[3] = {
    BSP_USB_MANUFACTURER,
    BSP_USB_PRODUCT,
#if BSP_USB_SERIAL_FROM_UID
    s_serial,
#else
    BSP_USB_SERIAL,
#endif
};

/* ---- State -------------------------------------------------------------- */

static uint8_t s_rx_ring[BSP_USB_CDC_RX_BUFFER];
static uint8_t s_tx_ring[BSP_USB_CDC_TX_BUFFER];
static uint8_t s_rx_packet[CDC_DATA_PACKET];
static volatile size_t s_rx_head, s_rx_tail; /* head: ISR writes, tail: user reads   */
static volatile size_t s_tx_head, s_tx_tail; /* head: user writes, tail: ISR consumes */
static volatile bool s_rx_armed;             /* EP1 OUT is waiting for a packet       */
static volatile bool s_tx_in_flight;         /* EP1 IN has a transfer going           */
static volatile bool s_tx_zlp_owed;          /* last chunk was a multiple of 64       */
static volatile bool s_dtr, s_rts;
static uint8_t s_line_coding[7] = {0x00U, 0xC2U, 0x01U, 0x00U, 0U, 0U, 8U}; /* 115200 8N1 */

#define RX_MASK (BSP_USB_CDC_RX_BUFFER - 1U)
#define TX_MASK (BSP_USB_CDC_TX_BUFFER - 1U)

static inline size_t rx_used(void)
{
    return s_rx_head - s_rx_tail;
}

static inline size_t tx_used(void)
{
    return s_tx_head - s_tx_tail;
}

/* ---- Receive path (interrupt side) -------------------------------------- */

/** @brief Arm EP1 OUT if a whole packet fits in the ring. IRQs must be off or we are in the ISR. */
static void rx_arm_if_room(void)
{
    if (!s_rx_armed && ((BSP_USB_CDC_RX_BUFFER - rx_used()) >= CDC_DATA_PACKET))
    {
        s_rx_armed = true;
        bsp_usb_ep_read_start(CDC_EP_DATA, s_rx_packet, CDC_DATA_PACKET);
    }
}

static void on_out(uint8_t ep, const uint8_t* data, uint16_t length)
{
    if (ep != CDC_EP_DATA)
    {
        return;
    }
    s_rx_armed = false;
    for (uint16_t i = 0U; i < length; i++)
    {
        s_rx_ring[(s_rx_head + i) & RX_MASK] = data[i];
    }
    s_rx_head += length;
    rx_arm_if_room(); /* if the ring is nearly full the endpoint NAKs until read() drains it */
}

/* ---- Transmit path ------------------------------------------------------ */

/** @brief Ship the next contiguous chunk of the TX ring. IRQs must be off or we are in the ISR. */
static void tx_kick(void)
{
    if (s_tx_in_flight || !bsp_usb_configured())
    {
        return;
    }

    const size_t used = tx_used();
    if (used == 0U)
    {
        if (s_tx_zlp_owed)
        {
            /* The host may be waiting for a short packet to end its read. */
            s_tx_zlp_owed = false;
            if (bsp_usb_ep_write(CDC_EP_DATA, 0, 0U) == 0)
            {
                s_tx_in_flight = true;
            }
        }
        return;
    }

    const size_t tail = s_tx_tail & TX_MASK;
    size_t chunk = BSP_USB_CDC_TX_BUFFER - tail; /* up to the wrap point */
    if (chunk > used)
    {
        chunk = used;
    }
    if (chunk > CDC_TX_CHUNK_MAX)
    {
        chunk = CDC_TX_CHUNK_MAX;
    }

    /* The core copies into its FIFO before returning, so the ring bytes are
     * free again as soon as this succeeds. */
    if (bsp_usb_ep_write(CDC_EP_DATA, &s_tx_ring[tail], (uint16_t)chunk) == 0)
    {
        s_tx_tail += chunk;
        s_tx_in_flight = true;
        s_tx_zlp_owed = (chunk % CDC_DATA_PACKET) == 0U;
    }
}

static void on_in_done(uint8_t ep)
{
    if (ep == CDC_EP_DATA)
    {
        s_tx_in_flight = false;
        tx_kick();
    }
}

/* ---- Class requests ----------------------------------------------------- */

static bool on_class_request(const bsp_usb_setup_t* setup, const uint8_t** data, uint16_t* length)
{
    switch (setup->bRequest)
    {
        case CDC_REQ_SET_LINE_CODING:
            if (setup->wLength != sizeof(s_line_coding))
            {
                return false;
            }
            *data = s_line_coding; /* the core fills it, then calls on_class_data() */
            return true;

        case CDC_REQ_GET_LINE_CODING:
            *data = s_line_coding;
            *length = sizeof(s_line_coding);
            return true;

        case CDC_REQ_SET_CONTROL_LINE_STATE:
            s_dtr = (setup->wValue & 0x01U) != 0U;
            s_rts = (setup->wValue & 0x02U) != 0U;
            return true;

        case CDC_REQ_SEND_BREAK:
            return true; /* accepted, nothing to do on a USB link */

        default:
            return false;
    }
}

static void on_class_data(const bsp_usb_setup_t* setup, const uint8_t* data, uint16_t length)
{
    /* SET_LINE_CODING landed straight in s_line_coding; nothing to parse
     * until someone asks through bsp_usb_cdc_line_coding(). */
    (void)setup;
    (void)data;
    (void)length;
}

/* ---- Bus events --------------------------------------------------------- */

static void on_reset(void)
{
    s_rx_armed = false;
    s_tx_in_flight = false;
    s_tx_zlp_owed = false;
    s_dtr = false;
    s_rts = false;
    /* Whatever was queued was meant for a port that no longer exists. */
    s_tx_tail = s_tx_head;
}

static void on_configured(void)
{
    bsp_usb_ep_open(USB_EP_OUT(CDC_EP_DATA), USB_EP_BULK, CDC_DATA_PACKET);
    bsp_usb_ep_open(USB_EP_IN(CDC_EP_DATA), USB_EP_BULK, CDC_DATA_PACKET);
    bsp_usb_ep_open(USB_EP_IN(CDC_EP_NOTIFY), USB_EP_INTERRUPT, CDC_NOTIFY_PACKET);
    s_rx_armed = false;
    s_tx_in_flight = false;
    s_tx_zlp_owed = false;
    rx_arm_if_room();
    tx_kick();
}

static const bsp_usb_class_t k_cdc_class = {
    .device_descriptor = k_device_descriptor,
    .config_descriptor = k_config_descriptor,
    .strings = s_strings,
    .string_count = 3U,
    .on_reset = on_reset,
    .on_configured = on_configured,
    .on_class_request = on_class_request,
    .on_class_data = on_class_data,
    .on_out = on_out,
    .on_in_done = on_in_done,
    .on_sof = 0,
};

/* ---- Public API --------------------------------------------------------- */

#if BSP_USB_SERIAL_FROM_UID
static void build_serial_from_uid(void)
{
    static const char hex[] = "0123456789ABCDEF";
    const volatile uint32_t* const uid = (const volatile uint32_t*)UID_BASE;
    size_t pos = 0U;

    for (int word = 2; word >= 0; word--)
    {
        const uint32_t value = uid[word];
        for (int nibble = 7; nibble >= 0; nibble--)
        {
            s_serial[pos++] = hex[(value >> (4U * (uint32_t)nibble)) & 0xFU];
        }
    }
    s_serial[pos] = '\0';
}
#endif

void bsp_usb_cdc_init(void)
{
#if BSP_USB_SERIAL_FROM_UID
    build_serial_from_uid();
#endif
    s_rx_head = s_rx_tail = 0U;
    s_tx_head = s_tx_tail = 0U;
    on_reset();
    bsp_usb_init(&k_cdc_class);
}

bool bsp_usb_cdc_connected(void)
{
    return bsp_usb_configured() && s_dtr;
}

bool bsp_usb_cdc_dtr(void)
{
    return s_dtr;
}

bool bsp_usb_cdc_rts(void)
{
    return s_rts;
}

size_t bsp_usb_cdc_available(void)
{
    return rx_used();
}

size_t bsp_usb_cdc_read(uint8_t* data, size_t length)
{
    size_t count = rx_used();
    if (count > length)
    {
        count = length;
    }
    for (size_t i = 0U; i < count; i++)
    {
        data[i] = s_rx_ring[(s_rx_tail + i) & RX_MASK];
    }
    s_rx_tail += count;

    if (count != 0U)
    {
        /* Draining may have made room for the packet the host is holding. */
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if (bsp_usb_configured())
        {
            rx_arm_if_room();
        }
        __set_PRIMASK(primask);
    }
    return count;
}

uint8_t bsp_usb_cdc_read_byte(void)
{
    uint8_t byte = 0U;
    while (bsp_usb_cdc_read(&byte, 1U) == 0U)
    {
        __WFI();
    }
    return byte;
}

size_t bsp_usb_cdc_write_space(void)
{
    return BSP_USB_CDC_TX_BUFFER - tx_used();
}

/**
 * Wait for the host with a bounded patience. Returns false when the timeout
 * expires or the port closes, so a stalled host cannot hang the firmware.
 */
static bool wait_slice(uint32_t* idle_us)
{
    if (!bsp_usb_cdc_connected() || (*idle_us >= (BSP_USB_CDC_TX_TIMEOUT_MS * 1000UL)))
    {
        return false;
    }
    bsp_delay_us(100U);
    *idle_us += 100U;
    return true;
}

size_t bsp_usb_cdc_write(const uint8_t* data, size_t length)
{
    size_t written = 0U;
    uint32_t idle_us = 0U;

    if (!bsp_usb_cdc_connected())
    {
        return 0U;
    }

    while (written < length)
    {
        size_t room = bsp_usb_cdc_write_space();
        if (room == 0U)
        {
            if (!wait_slice(&idle_us))
            {
                break;
            }
            continue;
        }
        idle_us = 0U;

        size_t chunk = length - written;
        if (chunk > room)
        {
            chunk = room;
        }
        for (size_t i = 0U; i < chunk; i++)
        {
            s_tx_ring[(s_tx_head + i) & TX_MASK] = data[written + i];
        }
        s_tx_head += chunk;
        written += chunk;

        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        tx_kick();
        __set_PRIMASK(primask);
    }
    return written;
}

void bsp_usb_cdc_flush(void)
{
    uint32_t idle_us = 0U;
    while ((tx_used() != 0U) || s_tx_in_flight)
    {
        if (!wait_slice(&idle_us))
        {
            break;
        }
    }
}

bsp_usb_cdc_line_coding_t bsp_usb_cdc_line_coding(void)
{
    bsp_usb_cdc_line_coding_t lc;
    lc.baud = (uint32_t)s_line_coding[0] | ((uint32_t)s_line_coding[1] << 8U) |
              ((uint32_t)s_line_coding[2] << 16U) | ((uint32_t)s_line_coding[3] << 24U);
    lc.stop_bits = s_line_coding[4];
    lc.parity = s_line_coding[5];
    lc.data_bits = s_line_coding[6];
    return lc;
}

/* ---- printf() retargeting ---------------------------------------------- */
#if BSP_STDOUT_USB

/* Overrides the weak stubs in bsp_syscalls.c. */
int bsp_putchar(int ch)
{
    const uint8_t byte = (uint8_t)ch;
    if (ch == '\n')
    {
        const uint8_t cr = (uint8_t)'\r';
        (void)bsp_usb_cdc_write(&cr, 1U);
    }
    (void)bsp_usb_cdc_write(&byte, 1U);
    return ch;
}

int bsp_getchar(void)
{
    return (int)bsp_usb_cdc_read_byte();
}

#endif /* BSP_STDOUT_USB */

#endif /* BSP_USE_USB_CDC */
