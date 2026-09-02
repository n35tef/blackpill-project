/**
 * @file    bsp_usb.c
 * @brief   USB OTG_FS device core (RM0383 sec. 22).
 *
 * Programming model: slave mode (the CPU moves every word to and from the
 * packet FIFOs), interrupt driven. The sequences below follow RM0383 22.17,
 * "OTG_FS programming model", with the register order it prescribes:
 *
 *   core init  - 22.17.1  (reset, PHY power, mode)
 *   device init- 22.17.3  (speed, FIFO layout, masks)
 *   USB reset  - 22.17.5  (endpoint NAK, EP0 setup receive)
 *   enum done  - 22.17.5  (EP0 max packet from DSTS)
 *   OUT data   - 22.17.6  (GRXSTSP pop, FIFO read, XFRC)
 *   IN data    - 22.17.6  (DIEPTSIZ, EPENA, FIFO write, XFRC)
 *   control    - 22.17.5  (SETUP, data, status stages)
 */

#include "bsp_usb.h"

#if BSP_USE_USB_CDC

#include <string.h>

#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_systick.h"

/* ---- Configuration checks ----------------------------------------------- */

_Static_assert(BSP_PLL_Q_OUT_HZ == 48000000UL,
               "USB needs the PLL Q output at exactly 48 MHz: adjust BSP_PLL_N / BSP_PLL_Q");
_Static_assert((BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSE_PLL) ||
                   (BSP_CLOCK_SOURCE == BSP_CLOCK_SOURCE_HSI_PLL),
               "USB is clocked from the PLL, so BSP_CLOCK_SOURCE must be a PLL option");
_Static_assert(BSP_HCLK_HZ >= 14200000UL, "OTG_FS needs HCLK >= 14.2 MHz (RM0383 22.15.4)");

/* ---- FIFO layout, in 32-bit words (1.25 KB total) ----------------------- */

/* The receive FIFO is shared by every OUT endpoint and by SETUP packets. The
 * RM's minimum is (largest packet / 4 + 1) * 2 + 10 + 1 = 45 words for 64-byte
 * packets; 128 leaves room for the host to burst two packets while the ISR
 * is busy. Each IN endpoint has its own transmit FIFO; a transfer must fit
 * in it whole. */
#define USB_RX_FIFO_WORDS  128U
#define USB_TX0_FIFO_WORDS 32U  /**< EP0: one 64-byte packet plus slack */
#define USB_TX1_FIFO_WORDS 128U /**< EP1: 512 bytes, the bulk data pipe */
#define USB_TX2_FIFO_WORDS 16U  /**< EP2: 64 bytes, notifications        */
#define USB_TX3_FIFO_WORDS 16U  /**< EP3: unused by CDC, kept legal      */

_Static_assert(USB_RX_FIFO_WORDS + USB_TX0_FIFO_WORDS + USB_TX1_FIFO_WORDS +
                       USB_TX2_FIFO_WORDS + USB_TX3_FIFO_WORDS <= OTG_FS_FIFO_RAM_WORDS,
               "USB FIFO layout exceeds the 1.25 KB of packet RAM");
_Static_assert(USB_RX_FIFO_WORDS >= 16U && USB_TX0_FIFO_WORDS >= 16U &&
                   USB_TX1_FIFO_WORDS >= 16U && USB_TX2_FIFO_WORDS >= 16U && USB_TX3_FIFO_WORDS >= 16U,
               "every OTG_FS FIFO must be at least 16 words deep");

static const uint16_t k_tx_fifo_words[OTG_FS_EP_COUNT] = {
    USB_TX0_FIFO_WORDS, USB_TX1_FIFO_WORDS, USB_TX2_FIFO_WORDS, USB_TX3_FIFO_WORDS};

/* ---- Turnaround time (GUSBCFG.TRDT), from the AHB clock (RM0383 22.15.4) - */

#if BSP_HCLK_HZ >= 32000000UL
#define USB_TRDT 0x6UL
#elif BSP_HCLK_HZ >= 27500000UL
#define USB_TRDT 0x7UL
#elif BSP_HCLK_HZ >= 24000000UL
#define USB_TRDT 0x8UL
#elif BSP_HCLK_HZ >= 21800000UL
#define USB_TRDT 0x9UL
#elif BSP_HCLK_HZ >= 20000000UL
#define USB_TRDT 0xAUL
#elif BSP_HCLK_HZ >= 18500000UL
#define USB_TRDT 0xBUL
#elif BSP_HCLK_HZ >= 17200000UL
#define USB_TRDT 0xCUL
#elif BSP_HCLK_HZ >= 16000000UL
#define USB_TRDT 0xDUL
#elif BSP_HCLK_HZ >= 15000000UL
#define USB_TRDT 0xEUL
#else
#define USB_TRDT 0xFUL
#endif

/* ---- State -------------------------------------------------------------- */

typedef enum
{
    EP0_IDLE,      /**< waiting for SETUP                           */
    EP0_DATA_IN,   /**< sending the data stage to the host          */
    EP0_DATA_OUT,  /**< receiving the data stage from the host      */
    EP0_STATUS_IN, /**< sending the empty status packet             */
    EP0_STATUS_OUT /**< waiting for the host's empty status packet  */
} ep0_state_t;

typedef struct
{
    const uint8_t* data; /**< IN: what is left to send                   */
    uint16_t remaining;
    bool zlp;            /**< IN: a final empty packet is owed           */
    uint8_t* out_buffer; /**< OUT data stage destination                 */
    uint16_t out_length;
    ep0_state_t state;
    bsp_usb_setup_t setup;
} ep0_t;

typedef struct
{
    uint8_t* buffer;   /**< where OUT data lands                        */
    uint16_t capacity; /**< armed length                                */
    uint16_t received; /**< bytes landed so far                         */
    uint16_t max_packet;
    bool in_busy;      /**< an IN transfer is in flight                 */
    bool halted_in;
    bool halted_out;
} endpoint_t;

static const bsp_usb_class_t* s_class;
static ep0_t s_ep0;
static endpoint_t s_ep[OTG_FS_EP_COUNT];
static uint8_t s_ep0_buffer[USB_EP0_MAX_PACKET];
static uint8_t s_setup_raw[8];
static volatile bool s_configured;
static volatile bool s_suspended;
static uint8_t s_pending_address;

/* UTF-16 string descriptors are built here on demand; 2 + 2 * 62 bytes. */
#define USB_STRING_MAX_CHARS 62U
static uint8_t s_string_buffer[2U + 2U * USB_STRING_MAX_CHARS];

/* ---- Small helpers ------------------------------------------------------ */

/* Busy-wait rather than bsp_delay_ms(): these are one-off bring-up pauses,
 * and a busy wait works before SysTick is up and with interrupts masked. */
static void delay_ms(uint32_t ms)
{
    bsp_delay_us(ms * 1000U);
}

static inline otg_fs_inep_regs_t* inep(uint8_t ep)
{
    return &OTG_FS_DEVICE->INEP[ep];
}

static inline otg_fs_outep_regs_t* outep(uint8_t ep)
{
    return &OTG_FS_DEVICE->OUTEP[ep];
}

/** @brief Copy @p length bytes into an endpoint's transmit FIFO, word by word. */
static void fifo_write(uint8_t ep, const uint8_t* data, uint16_t length)
{
    __IO uint32_t* const fifo = OTG_FS_FIFO(ep);
    const uint16_t words = (uint16_t)((length + 3U) / 4U);

    for (uint16_t i = 0U; i < words; i++)
    {
        /* The source is not necessarily aligned; assemble each word. A short
         * tail is padded with whatever follows in the buffer, which the core
         * ignores because XFRSIZ says how many bytes count. */
        uint32_t word;
        memcpy(&word, data + 4U * i, 4U);
        *fifo = word;
    }
}

/** @brief Pop @p length bytes of a received packet out of the shared receive FIFO. */
static void fifo_read(uint8_t* data, uint16_t length)
{
    __IO uint32_t* const fifo = OTG_FS_FIFO(0);
    uint16_t done = 0U;

    while (done < length)
    {
        const uint32_t word = *fifo;
        const uint16_t left = (uint16_t)(length - done);
        const uint16_t chunk = (left < 4U) ? left : 4U;
        if (data != 0)
        {
            memcpy(data + done, &word, chunk);
        }
        done = (uint16_t)(done + chunk);
    }
}

static void flush_tx_fifo(uint32_t fifo_number)
{
    OTG_FS_GLOBAL->GRSTCTL = (fifo_number << OTG_GRSTCTL_TXFNUM_POS) | OTG_GRSTCTL_TXFFLSH;
    while (OTG_FS_GLOBAL->GRSTCTL & OTG_GRSTCTL_TXFFLSH)
    {
    }
}

static void flush_rx_fifo(void)
{
    OTG_FS_GLOBAL->GRSTCTL = OTG_GRSTCTL_RXFFLSH;
    while (OTG_FS_GLOBAL->GRSTCTL & OTG_GRSTCTL_RXFFLSH)
    {
    }
}

/* ---- Endpoint transfers ------------------------------------------------- */

/**
 * @brief Start an IN transfer on any endpoint, EP0 included.
 *
 * Program the size, enable the endpoint, then push the whole payload. On
 * this core the payload has to be in the FIFO before the host's IN token
 * arrives or the endpoint answers NAK, which is why the FIFO must hold a
 * whole transfer.
 */
static void ep_in_start(uint8_t ep, const uint8_t* data, uint16_t length)
{
    const uint16_t mps = (ep == 0U) ? USB_EP0_MAX_PACKET : s_ep[ep].max_packet;
    uint32_t packets = (length + mps - 1U) / mps;
    if (packets == 0U)
    {
        packets = 1U; /* an empty transfer is still one (zero-length) packet */
    }

    inep(ep)->DIEPTSIZ = (packets << OTG_DIEPTSIZ_PKTCNT_POS) |
                         ((uint32_t)length << OTG_DIEPTSIZ_XFRSIZ_POS);
    inep(ep)->DIEPCTL |= OTG_DIEPCTL_EPENA | OTG_DIEPCTL_CNAK;
    s_ep[ep].in_busy = true;

    if (length != 0U)
    {
        fifo_write(ep, data, length);
    }
}

/** @brief Arm an OUT endpoint for one transfer of up to @p length bytes. */
static void ep_out_start(uint8_t ep, uint8_t* buffer, uint16_t length)
{
    const uint16_t mps = (ep == 0U) ? USB_EP0_MAX_PACKET : s_ep[ep].max_packet;
    uint32_t packets = (length + mps - 1U) / mps;
    if (packets == 0U)
    {
        packets = 1U;
    }

    s_ep[ep].buffer = buffer;
    s_ep[ep].capacity = length;
    s_ep[ep].received = 0U;

    uint32_t tsiz = (packets << OTG_DOEPTSIZ_PKTCNT_POS) |
                    ((uint32_t)length << OTG_DOEPTSIZ_XFRSIZ_POS);
    if (ep == 0U)
    {
        /* EP0 must always be ready to take SETUP packets as well. */
        tsiz |= 3UL << OTG_DOEPTSIZ_RXDPID_STUPCNT_POS;
    }
    outep(ep)->DOEPTSIZ = tsiz;
    outep(ep)->DOEPCTL |= OTG_DOEPCTL_EPENA | OTG_DOEPCTL_CNAK;
}

/**
 * @brief Make EP0 OUT receptive to SETUP packets only.
 *
 * SETUP packets are accepted whether or not the endpoint is enabled, as long
 * as STUPCNT is non-zero (RM0383 22.17.5). Between control transfers this is
 * all EP0 OUT needs.
 */
static void ep0_expect_setup(void)
{
    outep(0)->DOEPTSIZ = (3UL << OTG_DOEPTSIZ_RXDPID_STUPCNT_POS) |
                         (1UL << OTG_DOEPTSIZ_PKTCNT_POS) | (24UL << OTG_DOEPTSIZ_XFRSIZ_POS);
    s_ep0.state = EP0_IDLE;
}

/* ---- Control transfer (EP0) state machine -------------------------------- */

static void ep0_stall(void)
{
    inep(0)->DIEPCTL |= OTG_DIEPCTL_STALL;
    outep(0)->DOEPCTL |= OTG_DOEPCTL_STALL;
    ep0_expect_setup(); /* the core clears STALL itself on the next SETUP */
}

/** @brief Send the next packet of the data stage, or the status OUT if done. */
static void ep0_continue_in(void)
{
    if ((s_ep0.remaining == 0U) && !s_ep0.zlp)
    {
        /* Data stage over: the host now sends an empty OUT packet. */
        s_ep0.state = EP0_STATUS_OUT;
        ep_out_start(0U, s_ep0_buffer, USB_EP0_MAX_PACKET);
        return;
    }

    uint16_t chunk = (s_ep0.remaining > USB_EP0_MAX_PACKET) ? USB_EP0_MAX_PACKET : s_ep0.remaining;
    if (chunk == 0U)
    {
        s_ep0.zlp = false; /* this is the owed empty packet */
    }
    ep_in_start(0U, s_ep0.data, chunk);
    s_ep0.data += chunk;
    s_ep0.remaining = (uint16_t)(s_ep0.remaining - chunk);
}

/** @brief Begin an IN data stage of at most wLength bytes. */
static void ep0_send(const uint8_t* data, uint16_t length)
{
    const uint16_t wanted = s_ep0.setup.wLength;
    if (length > wanted)
    {
        length = wanted;
    }
    s_ep0.data = data;
    s_ep0.remaining = length;
    /* If we send less than asked and it ends on a packet boundary, the host
     * only knows we are done when it sees a short packet - an empty one. */
    s_ep0.zlp = (length < wanted) && ((length % USB_EP0_MAX_PACKET) == 0U);
    s_ep0.state = EP0_DATA_IN;
    ep0_continue_in();
}

/** @brief No data stage: acknowledge with an empty IN packet. */
static void ep0_ack(void)
{
    s_ep0.state = EP0_STATUS_IN;
    ep_in_start(0U, 0, 0U);
}

/** @brief Begin an OUT data stage into @p buffer. */
static void ep0_receive(uint8_t* buffer, uint16_t length)
{
    s_ep0.out_buffer = buffer;
    s_ep0.out_length = length;
    s_ep0.state = EP0_DATA_OUT;
    ep_out_start(0U, buffer, length);
}

static uint16_t build_string_descriptor(uint8_t index)
{
    if (index == 0U)
    {
        /* LANGID list: US English. */
        s_string_buffer[0] = 4U;
        s_string_buffer[1] = USB_DESC_STRING;
        s_string_buffer[2] = 0x09U;
        s_string_buffer[3] = 0x04U;
        return 4U;
    }
    if ((index > s_class->string_count) || (s_class->strings == 0))
    {
        return 0U;
    }

    const char* text = s_class->strings[index - 1U];
    uint16_t chars = 0U;
    while ((text[chars] != '\0') && (chars < USB_STRING_MAX_CHARS))
    {
        s_string_buffer[2U + 2U * chars] = (uint8_t)text[chars];
        s_string_buffer[3U + 2U * chars] = 0U;
        chars++;
    }
    s_string_buffer[0] = (uint8_t)(2U + 2U * chars);
    s_string_buffer[1] = USB_DESC_STRING;
    return (uint16_t)(2U + 2U * chars);
}

static void handle_get_descriptor(void)
{
    const uint8_t type = (uint8_t)(s_ep0.setup.wValue >> 8U);
    const uint8_t index = (uint8_t)(s_ep0.setup.wValue & 0xFFU);

    switch (type)
    {
        case USB_DESC_DEVICE:
            ep0_send(s_class->device_descriptor, s_class->device_descriptor[0]);
            return;

        case USB_DESC_CONFIGURATION:
        {
            const uint8_t* cfg = s_class->config_descriptor;
            const uint16_t total = (uint16_t)(cfg[2] | ((uint16_t)cfg[3] << 8U));
            ep0_send(cfg, total);
            return;
        }

        case USB_DESC_STRING:
        {
            const uint16_t length = build_string_descriptor(index);
            if (length == 0U)
            {
                break;
            }
            ep0_send(s_string_buffer, length);
            return;
        }

        default:
            /* Device qualifier and anything else: a full-speed-only device
             * is required to STALL these. */
            break;
    }
    ep0_stall();
}

static endpoint_t* endpoint_from_index(uint16_t wIndex, bool* is_in)
{
    const uint8_t number = (uint8_t)(wIndex & 0x0FU);
    if (number >= OTG_FS_EP_COUNT)
    {
        return 0;
    }
    *is_in = (wIndex & USB_REQ_DIR_IN) != 0U;
    return &s_ep[number];
}

static void handle_standard_request(void)
{
    const bsp_usb_setup_t* const setup = &s_ep0.setup;
    const uint8_t recipient = setup->bmRequestType & USB_REQ_RECIP_MSK;
    static uint8_t reply[2];

    switch (setup->bRequest)
    {
        case USB_REQ_GET_STATUS:
        {
            reply[0] = 0U;
            reply[1] = 0U;
            if (recipient == USB_REQ_RECIP_ENDPOINT)
            {
                bool is_in = false;
                const endpoint_t* ep = endpoint_from_index(setup->wIndex, &is_in);
                if (ep == 0)
                {
                    break;
                }
                reply[0] = (is_in ? ep->halted_in : ep->halted_out) ? 1U : 0U;
            }
            else if (recipient != USB_REQ_RECIP_DEVICE && recipient != USB_REQ_RECIP_INTERFACE)
            {
                break;
            }
            ep0_send(reply, 2U);
            return;
        }

        case USB_REQ_CLEAR_FEATURE:
        case USB_REQ_SET_FEATURE:
            if ((recipient == USB_REQ_RECIP_ENDPOINT) &&
                (setup->wValue == USB_FEATURE_ENDPOINT_HALT))
            {
                const uint8_t address = (uint8_t)setup->wIndex;
                if ((address & 0x0FU) == 0U || (address & 0x0FU) >= OTG_FS_EP_COUNT)
                {
                    break;
                }
                if (setup->bRequest == USB_REQ_SET_FEATURE)
                {
                    bsp_usb_ep_stall(address);
                }
                else
                {
                    bsp_usb_ep_unstall(address);
                }
                ep0_ack();
                return;
            }
            if ((recipient == USB_REQ_RECIP_DEVICE) &&
                (setup->wValue == USB_FEATURE_REMOTE_WAKEUP))
            {
                ep0_ack(); /* accepted and ignored: we never wake the host */
                return;
            }
            break;

        case USB_REQ_SET_ADDRESS:
            if (recipient != USB_REQ_RECIP_DEVICE || setup->wValue > 127U)
            {
                break;
            }
            /* RM0383 22.17.5: the address is programmed before the status
             * stage, the core applies it once the status IN has been ACKed. */
            s_pending_address = (uint8_t)setup->wValue;
            OTG_FS_DEVICE->DCFG = (OTG_FS_DEVICE->DCFG & ~OTG_DCFG_DAD_MSK) |
                                  ((uint32_t)s_pending_address << OTG_DCFG_DAD_POS);
            ep0_ack();
            return;

        case USB_REQ_GET_DESCRIPTOR:
            if (recipient != USB_REQ_RECIP_DEVICE)
            {
                break;
            }
            handle_get_descriptor();
            return;

        case USB_REQ_GET_CONFIGURATION:
            reply[0] = s_configured ? 1U : 0U;
            ep0_send(reply, 1U);
            return;

        case USB_REQ_SET_CONFIGURATION:
            if (recipient != USB_REQ_RECIP_DEVICE)
            {
                break;
            }
            if (setup->wValue == 0U)
            {
                s_configured = false;
                for (uint8_t ep = 1U; ep < OTG_FS_EP_COUNT; ep++)
                {
                    bsp_usb_ep_close(USB_EP_IN(ep));
                    bsp_usb_ep_close(USB_EP_OUT(ep));
                }
                ep0_ack();
                return;
            }
            if (setup->wValue == 1U)
            {
                s_configured = true;
                if (s_class->on_configured != 0)
                {
                    s_class->on_configured();
                }
                ep0_ack();
                return;
            }
            break;

        case USB_REQ_GET_INTERFACE:
            reply[0] = 0U;
            ep0_send(reply, 1U);
            return;

        case USB_REQ_SET_INTERFACE:
            if (setup->wValue == 0U) /* only alternate setting 0 exists */
            {
                ep0_ack();
                return;
            }
            break;

        default:
            break;
    }
    ep0_stall();
}

static void handle_class_request(void)
{
    const bsp_usb_setup_t* const setup = &s_ep0.setup;
    const uint8_t* data = 0;
    uint16_t length = 0U;

    if ((s_class->on_class_request == 0) ||
        !s_class->on_class_request(setup, &data, &length))
    {
        ep0_stall();
        return;
    }

    if (setup->bmRequestType & USB_REQ_DIR_IN)
    {
        ep0_send(data, length);
    }
    else if (setup->wLength == 0U)
    {
        ep0_ack();
    }
    else if ((data != 0) && (setup->wLength <= USB_EP0_MAX_PACKET))
    {
        /* The class handed us its buffer; fill it, then report back. Control
         * OUT payloads larger than one packet are not needed by any class
         * here and are refused. */
        ep0_receive((uint8_t*)(uintptr_t)data, setup->wLength);
    }
    else
    {
        ep0_stall();
    }
}

static void handle_setup(void)
{
    memcpy(&s_ep0.setup.bmRequestType, &s_setup_raw[0], 1U);
    memcpy(&s_ep0.setup.bRequest, &s_setup_raw[1], 1U);
    s_ep0.setup.wValue = (uint16_t)(s_setup_raw[2] | ((uint16_t)s_setup_raw[3] << 8U));
    s_ep0.setup.wIndex = (uint16_t)(s_setup_raw[4] | ((uint16_t)s_setup_raw[5] << 8U));
    s_ep0.setup.wLength = (uint16_t)(s_setup_raw[6] | ((uint16_t)s_setup_raw[7] << 8U));

    /* A SETUP always starts over, whatever stage the previous transfer was in. */
    s_ep0.remaining = 0U;
    s_ep0.zlp = false;

    switch (s_ep0.setup.bmRequestType & USB_REQ_TYPE_MSK)
    {
        case USB_REQ_TYPE_STANDARD:
            handle_standard_request();
            break;
        case USB_REQ_TYPE_CLASS:
        case USB_REQ_TYPE_VENDOR:
            handle_class_request();
            break;
        default:
            ep0_stall();
            break;
    }
}

/** @brief EP0 OUT transfer completed: a data stage or a status stage landed. */
static void ep0_out_complete(void)
{
    switch (s_ep0.state)
    {
        case EP0_DATA_OUT:
            if (s_class->on_class_data != 0)
            {
                s_class->on_class_data(&s_ep0.setup, s_ep0.out_buffer, s_ep[0].received);
            }
            ep0_ack();
            break;

        case EP0_STATUS_OUT:
            ep0_expect_setup();
            break;

        default:
            ep0_expect_setup();
            break;
    }
}

/** @brief EP0 IN transfer completed: more data, or the status IN went out. */
static void ep0_in_complete(void)
{
    s_ep[0].in_busy = false;

    switch (s_ep0.state)
    {
        case EP0_DATA_IN:
            ep0_continue_in();
            break;

        case EP0_STATUS_IN:
            ep0_expect_setup();
            break;

        default:
            ep0_expect_setup();
            break;
    }
}

/* ---- Bus events --------------------------------------------------------- */

static void reset_endpoint_state(void)
{
    for (uint8_t ep = 0U; ep < OTG_FS_EP_COUNT; ep++)
    {
        s_ep[ep] = (endpoint_t){0};
    }
    s_ep[0].max_packet = USB_EP0_MAX_PACKET;
    s_ep0 = (ep0_t){0};
    s_configured = false;
    s_pending_address = 0U;
}

/** @brief USB reset from the host (RM0383 22.17.5, "USB reset"). */
static void handle_usb_reset(void)
{
    /* 1. NAK every OUT endpoint. */
    for (uint8_t ep = 0U; ep < OTG_FS_EP_COUNT; ep++)
    {
        outep(ep)->DOEPCTL |= OTG_DOEPCTL_SNAK;
    }

    /* 2. Unmask EP0 in both directions and the per-endpoint events. */
    OTG_FS_DEVICE->DAINTMSK = (1UL << OTG_DAINTMSK_IEPM_POS) | (1UL << OTG_DAINTMSK_OEPINT_POS);
    OTG_FS_DEVICE->DOEPMSK = OTG_DOEPMSK_STUPM | OTG_DOEPMSK_XFRCM | OTG_DOEPMSK_EPDM;
    OTG_FS_DEVICE->DIEPMSK = OTG_DIEPMSK_XFRCM | OTG_DIEPMSK_TOM | OTG_DIEPMSK_EPDM;

    /* 3. Back to address 0, nothing configured, FIFOs empty. */
    OTG_FS_DEVICE->DCFG &= ~OTG_DCFG_DAD_MSK;
    flush_tx_fifo(OTG_TXFNUM_ALL);
    flush_rx_fifo();
    reset_endpoint_state();

    /* 4. Ready for the first SETUP. */
    ep0_expect_setup();

    if (s_class->on_reset != 0)
    {
        s_class->on_reset();
    }
}

/** @brief Speed enumeration done: EP0 can be sized and NAKs lifted. */
static void handle_enum_done(void)
{
    /* Only full speed is possible on this PHY; ENUMSPD is read for form. */
    (void)(OTG_FS_DEVICE->DSTS & OTG_DSTS_ENUMSPD_MSK);

    inep(0)->DIEPCTL = (inep(0)->DIEPCTL & ~OTG_DIEPCTL0_MPSIZ_MSK) | OTG_EP0_MPSIZ_64;
    OTG_FS_DEVICE->DCTL |= OTG_DCTL_CGINAK;
}

/** @brief Something arrived in the receive FIFO (RM0383 22.17.6). */
static void handle_rx_level(void)
{
    const uint32_t status = OTG_FS_GLOBAL->GRXSTSP;
    const uint8_t ep = (uint8_t)((status & OTG_GRXSTSP_EPNUM_MSK) >> OTG_GRXSTSP_EPNUM_POS);
    const uint16_t count = (uint16_t)((status & OTG_GRXSTSP_BCNT_MSK) >> OTG_GRXSTSP_BCNT_POS);
    const uint32_t kind = (status & OTG_GRXSTSP_PKTSTS_MSK) >> OTG_GRXSTSP_PKTSTS_POS;

    switch (kind)
    {
        case OTG_PKTSTS_SETUP_DATA:
            fifo_read(s_setup_raw, 8U);
            break;

        case OTG_PKTSTS_OUT_DATA:
            if ((ep < OTG_FS_EP_COUNT) && (s_ep[ep].buffer != 0))
            {
                endpoint_t* const e = &s_ep[ep];
                const uint16_t room = (uint16_t)(e->capacity - e->received);
                const uint16_t keep = (count < room) ? count : room;
                fifo_read(e->buffer + e->received, keep);
                fifo_read(0, (uint16_t)(count - keep)); /* discard any excess */
                e->received = (uint16_t)(e->received + keep);
            }
            else
            {
                fifo_read(0, count);
            }
            break;

        case OTG_PKTSTS_OUT_COMPLETE:
        case OTG_PKTSTS_SETUP_COMPLETE:
        case OTG_PKTSTS_GLOBAL_OUT_NAK:
        default:
            /* Status-only entries; the endpoint interrupt follows. */
            break;
    }
}

static void handle_out_endpoints(void)
{
    const uint32_t pending = (OTG_FS_DEVICE->DAINT & OTG_DAINT_OEPINT_MSK) >> OTG_DAINT_OEPINT_POS;

    for (uint8_t ep = 0U; ep < OTG_FS_EP_COUNT; ep++)
    {
        if ((pending & (1UL << ep)) == 0U)
        {
            continue;
        }
        const uint32_t flags = outep(ep)->DOEPINT;
        outep(ep)->DOEPINT = flags; /* write 1 to clear */

        if (flags & OTG_DOEPINT_STUP)
        {
            handle_setup();
        }
        else if (flags & OTG_DOEPINT_XFRC)
        {
            if (ep == 0U)
            {
                ep0_out_complete();
            }
            else
            {
                endpoint_t* const e = &s_ep[ep];
                uint8_t* const buffer = e->buffer;
                const uint16_t received = e->received;
                e->buffer = 0; /* consumed; the class re-arms if it wants more */
                if (s_class->on_out != 0)
                {
                    s_class->on_out(ep, buffer, received);
                }
            }
        }
    }
}

static void handle_in_endpoints(void)
{
    const uint32_t pending = (OTG_FS_DEVICE->DAINT & OTG_DAINT_IEPINT_MSK) >> OTG_DAINT_IEPINT_POS;

    for (uint8_t ep = 0U; ep < OTG_FS_EP_COUNT; ep++)
    {
        if ((pending & (1UL << ep)) == 0U)
        {
            continue;
        }
        const uint32_t flags = inep(ep)->DIEPINT;
        inep(ep)->DIEPINT = flags;

        if (flags & OTG_DIEPINT_XFRC)
        {
            if (ep == 0U)
            {
                ep0_in_complete();
            }
            else
            {
                s_ep[ep].in_busy = false;
                if (s_class->on_in_done != 0)
                {
                    s_class->on_in_done(ep);
                }
            }
        }
        if (flags & OTG_DIEPINT_TOC)
        {
            /* Control IN timed out: the host gave up on this transfer. */
            if (ep == 0U)
            {
                s_ep[0].in_busy = false;
                ep0_expect_setup();
            }
        }
    }
}

/* ---- Interrupt entry ---------------------------------------------------- */

void OTG_FS_IRQHandler(void)
{
    const uint32_t status = OTG_FS_GLOBAL->GINTSTS & OTG_FS_GLOBAL->GINTMSK;

    if (status & OTG_GINTSTS_MMIS)
    {
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_MMIS;
    }
    if (status & OTG_GINTSTS_USBRST)
    {
        s_suspended = false;
        handle_usb_reset();
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_USBRST;
    }
    if (status & OTG_GINTSTS_ENUMDNE)
    {
        handle_enum_done();
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_ENUMDNE;
    }
    if (status & OTG_GINTSTS_RXFLVL)
    {
        /* Mask while popping so a second arrival cannot re-enter. */
        OTG_FS_GLOBAL->GINTMSK &= ~OTG_GINTMSK_RXFLVLM;
        handle_rx_level();
        OTG_FS_GLOBAL->GINTMSK |= OTG_GINTMSK_RXFLVLM;
    }
    if (status & OTG_GINTSTS_OEPINT)
    {
        handle_out_endpoints();
    }
    if (status & OTG_GINTSTS_IEPINT)
    {
        handle_in_endpoints();
    }
    if (status & OTG_GINTSTS_USBSUSP)
    {
        s_suspended = true;
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_USBSUSP;
    }
    if (status & OTG_GINTSTS_WKUPINT)
    {
        s_suspended = false;
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_WKUPINT;
    }
    if (status & OTG_GINTSTS_SOF)
    {
        if (s_class->on_sof != 0)
        {
            s_class->on_sof();
        }
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_SOF;
    }
    if (status & (OTG_GINTSTS_IISOIXFR | OTG_GINTSTS_IPXFR_INCOMPISOOUT))
    {
        OTG_FS_GLOBAL->GINTSTS = OTG_GINTSTS_IISOIXFR | OTG_GINTSTS_IPXFR_INCOMPISOOUT;
    }
}

/* ---- Public API --------------------------------------------------------- */

void bsp_usb_ep_open(uint8_t address, uint8_t type, uint16_t max_packet)
{
    const uint8_t ep = address & 0x0FU;
    if ((ep == 0U) || (ep >= OTG_FS_EP_COUNT) || (max_packet > 64U))
    {
        return;
    }
    s_ep[ep].max_packet = max_packet;

    if (address & USB_REQ_DIR_IN)
    {
        /* Each IN endpoint uses the transmit FIFO of the same number. Bulk and
         * interrupt endpoints start with DATA0, hence SD0PID. */
        inep(ep)->DIEPCTL = ((uint32_t)max_packet << OTG_DIEPCTL_MPSIZ_POS) |
                            ((uint32_t)type << OTG_DIEPCTL_EPTYP_POS) |
                            ((uint32_t)ep << OTG_DIEPCTL_TXFNUM_POS) |
                            OTG_DIEPCTL_SD0PID_SEVNFRM | OTG_DIEPCTL_USBAEP;
        OTG_FS_DEVICE->DAINTMSK |= 1UL << (OTG_DAINTMSK_IEPM_POS + ep);
        s_ep[ep].in_busy = false;
        s_ep[ep].halted_in = false;
    }
    else
    {
        outep(ep)->DOEPCTL = ((uint32_t)max_packet << OTG_DOEPCTL_MPSIZ_POS) |
                             ((uint32_t)type << OTG_DOEPCTL_EPTYP_POS) |
                             OTG_DOEPCTL_SD0PID_SEVNFRM | OTG_DOEPCTL_USBAEP;
        OTG_FS_DEVICE->DAINTMSK |= 1UL << (OTG_DAINTMSK_OEPINT_POS + ep);
        s_ep[ep].buffer = 0;
        s_ep[ep].halted_out = false;
    }
}

void bsp_usb_ep_close(uint8_t address)
{
    const uint8_t ep = address & 0x0FU;
    if ((ep == 0U) || (ep >= OTG_FS_EP_COUNT))
    {
        return;
    }

    if (address & USB_REQ_DIR_IN)
    {
        if (inep(ep)->DIEPCTL & OTG_DIEPCTL_EPENA)
        {
            /* Disabling an armed IN endpoint: NAK it, wait for the NAK to
             * take effect, then disable (RM0383 22.17.6 "disabling"). */
            inep(ep)->DIEPCTL |= OTG_DIEPCTL_SNAK;
            while ((inep(ep)->DIEPINT & OTG_DIEPINT_INEPNE) == 0U)
            {
            }
            inep(ep)->DIEPCTL |= OTG_DIEPCTL_EPDIS | OTG_DIEPCTL_SNAK;
            while ((inep(ep)->DIEPINT & OTG_DIEPINT_EPDISD) == 0U)
            {
            }
            inep(ep)->DIEPINT = OTG_DIEPINT_EPDISD | OTG_DIEPINT_INEPNE;
        }
        inep(ep)->DIEPCTL = 0U;
        OTG_FS_DEVICE->DAINTMSK &= ~(1UL << (OTG_DAINTMSK_IEPM_POS + ep));
        flush_tx_fifo(ep);
        s_ep[ep].in_busy = false;
    }
    else
    {
        if (outep(ep)->DOEPCTL & OTG_DOEPCTL_EPENA)
        {
            /* An OUT endpoint can only be disabled under global OUT NAK. */
            OTG_FS_DEVICE->DCTL |= OTG_DCTL_SGONAK;
            while ((OTG_FS_GLOBAL->GINTSTS & OTG_GINTSTS_GOUTNAKEFF) == 0U)
            {
            }
            outep(ep)->DOEPCTL |= OTG_DOEPCTL_EPDIS | OTG_DOEPCTL_SNAK;
            while ((outep(ep)->DOEPINT & OTG_DOEPINT_EPDISD) == 0U)
            {
            }
            outep(ep)->DOEPINT = OTG_DOEPINT_EPDISD;
            OTG_FS_DEVICE->DCTL |= OTG_DCTL_CGONAK;
        }
        outep(ep)->DOEPCTL = 0U;
        OTG_FS_DEVICE->DAINTMSK &= ~(1UL << (OTG_DAINTMSK_OEPINT_POS + ep));
        s_ep[ep].buffer = 0;
    }
}

int bsp_usb_ep_write(uint8_t ep, const uint8_t* data, uint16_t length)
{
    ep &= 0x0FU;
    if ((ep == 0U) || (ep >= OTG_FS_EP_COUNT) || !s_configured)
    {
        return -1;
    }
    if (s_ep[ep].in_busy || (length > (k_tx_fifo_words[ep] * 4U)))
    {
        return -1;
    }
    if (((length + 3U) / 4U) > (inep(ep)->DTXFSTS & OTG_DTXFSTS_INEPTFSAV_MSK))
    {
        return -1; /* FIFO still draining the previous transfer */
    }

    ep_in_start(ep, data, length);
    return 0;
}

bool bsp_usb_ep_in_busy(uint8_t ep)
{
    ep &= 0x0FU;
    return (ep < OTG_FS_EP_COUNT) ? s_ep[ep].in_busy : false;
}

void bsp_usb_ep_read_start(uint8_t ep, uint8_t* buffer, uint16_t length)
{
    ep &= 0x0FU;
    if ((ep == 0U) || (ep >= OTG_FS_EP_COUNT) || (buffer == 0))
    {
        return;
    }
    ep_out_start(ep, buffer, length);
}

void bsp_usb_ep_stall(uint8_t address)
{
    const uint8_t ep = address & 0x0FU;
    if (ep >= OTG_FS_EP_COUNT)
    {
        return;
    }
    if (address & USB_REQ_DIR_IN)
    {
        inep(ep)->DIEPCTL |= OTG_DIEPCTL_STALL;
        s_ep[ep].halted_in = true;
    }
    else
    {
        outep(ep)->DOEPCTL |= OTG_DOEPCTL_STALL;
        s_ep[ep].halted_out = true;
    }
}

void bsp_usb_ep_unstall(uint8_t address)
{
    const uint8_t ep = address & 0x0FU;
    if ((ep == 0U) || (ep >= OTG_FS_EP_COUNT))
    {
        return;
    }
    if (address & USB_REQ_DIR_IN)
    {
        inep(ep)->DIEPCTL = (inep(ep)->DIEPCTL & ~OTG_DIEPCTL_STALL) | OTG_DIEPCTL_SD0PID_SEVNFRM;
        s_ep[ep].halted_in = false;
    }
    else
    {
        outep(ep)->DOEPCTL = (outep(ep)->DOEPCTL & ~OTG_DOEPCTL_STALL) | OTG_DOEPCTL_SD0PID_SEVNFRM;
        s_ep[ep].halted_out = false;
    }
}

bool bsp_usb_configured(void)
{
    return s_configured;
}

bool bsp_usb_suspended(void)
{
    return s_suspended;
}

/* ---- Bring-up ----------------------------------------------------------- */

/** @brief Core initialisation (RM0383 22.17.1). */
static void core_init(void)
{
    /* Full-speed serial transceiver, then a soft reset of the whole core.
     * The reset may only be issued once the AHB master is idle. */
    OTG_FS_GLOBAL->GUSBCFG |= OTG_GUSBCFG_PHYSEL;
    while ((OTG_FS_GLOBAL->GRSTCTL & OTG_GRSTCTL_AHBIDL) == 0U)
    {
    }
    OTG_FS_GLOBAL->GRSTCTL |= OTG_GRSTCTL_CSRST;
    while (OTG_FS_GLOBAL->GRSTCTL & OTG_GRSTCTL_CSRST)
    {
    }

    /* Power the transceiver up. VBUS sensing is off: the BlackPill has no
     * VBUS divider on PA9, and a device that is powered from the same USB
     * port cannot see VBUS go away anyway. */
    OTG_FS_GLOBAL->GCCFG = OTG_GCCFG_PWRDWN | OTG_GCCFG_NOVBUSSENS;

    /* Device mode only. The mode change takes up to 25 ms to settle. */
    OTG_FS_GLOBAL->GUSBCFG = (OTG_FS_GLOBAL->GUSBCFG & ~(OTG_GUSBCFG_FHMOD | OTG_GUSBCFG_TRDT_MSK)) |
                             OTG_GUSBCFG_FDMOD | (USB_TRDT << OTG_GUSBCFG_TRDT_POS);
    delay_ms(25U);
}

/** @brief Device initialisation (RM0383 22.17.3). */
static void device_init(void)
{
    /* No clock gating; full speed. */
    OTG_FS_PWRCLK->PCGCCTL = 0U;
    OTG_FS_DEVICE->DCFG = OTG_DSPD_FULL_SPEED << OTG_DCFG_DSPD_POS;

    /* FIFO layout: receive FIFO first, then one transmit FIFO per IN
     * endpoint, back to back. Sizes and start addresses are in words. */
    uint32_t start = USB_RX_FIFO_WORDS;
    OTG_FS_GLOBAL->GRXFSIZ = USB_RX_FIFO_WORDS;
    OTG_FS_GLOBAL->DIEPTXF0 = ((uint32_t)USB_TX0_FIFO_WORDS << OTG_DIEPTXF0_TX0FD_POS) | start;
    start += USB_TX0_FIFO_WORDS;
    OTG_FS_GLOBAL->DIEPTXF1 = ((uint32_t)USB_TX1_FIFO_WORDS << OTG_DIEPTXF_INEPTXFD_POS) | start;
    start += USB_TX1_FIFO_WORDS;
    OTG_FS_GLOBAL->DIEPTXF2 = ((uint32_t)USB_TX2_FIFO_WORDS << OTG_DIEPTXF_INEPTXFD_POS) | start;
    start += USB_TX2_FIFO_WORDS;
    OTG_FS_GLOBAL->DIEPTXF3 = ((uint32_t)USB_TX3_FIFO_WORDS << OTG_DIEPTXF_INEPTXFD_POS) | start;

    flush_tx_fifo(OTG_TXFNUM_ALL);
    flush_rx_fifo();

    /* Every endpoint quiet, every endpoint interrupt cleared and masked. */
    OTG_FS_DEVICE->DIEPMSK = 0U;
    OTG_FS_DEVICE->DOEPMSK = 0U;
    OTG_FS_DEVICE->DAINTMSK = 0U;
    for (uint8_t ep = 0U; ep < OTG_FS_EP_COUNT; ep++)
    {
        inep(ep)->DIEPCTL = (inep(ep)->DIEPCTL & OTG_DIEPCTL_EPENA)
                                ? (OTG_DIEPCTL_EPDIS | OTG_DIEPCTL_SNAK)
                                : 0U;
        inep(ep)->DIEPTSIZ = 0U;
        inep(ep)->DIEPINT = 0xFFU;
        outep(ep)->DOEPCTL = (outep(ep)->DOEPCTL & OTG_DOEPCTL_EPENA)
                                 ? (OTG_DOEPCTL_EPDIS | OTG_DOEPCTL_SNAK)
                                 : 0U;
        outep(ep)->DOEPTSIZ = 0U;
        outep(ep)->DOEPINT = 0xFFU;
    }

    /* Interrupt sources that matter to a device; clear stale ones first. */
    OTG_FS_GLOBAL->GINTMSK = 0U;
    OTG_FS_GLOBAL->GINTSTS = 0xBFFFFFFFUL;
    OTG_FS_GLOBAL->GINTMSK = OTG_GINTMSK_USBRST | OTG_GINTMSK_ENUMDNEM | OTG_GINTMSK_RXFLVLM |
                             OTG_GINTMSK_IEPINT | OTG_GINTMSK_OEPINT | OTG_GINTMSK_USBSUSPM |
                             OTG_GINTMSK_WUIM | OTG_GINTMSK_IISOIXFRM | OTG_GINTMSK_IPXFRM_IISOOXFRM |
                             (BSP_USB_SOF_IRQ ? OTG_GINTMSK_SOFM : 0UL);
}

void bsp_usb_init(const bsp_usb_class_t* cls)
{
    s_class = cls;
    reset_endpoint_state();
    s_suspended = false;

    /* D- on PA11, D+ on PA12, AF10. No pull: the core has its own D+ pull-up
     * (released by clearing SDIS). */
    bsp_gpio_config_alternate(GPIOA, 11U, GPIO_AF10_OTG_FS, GPIO_OTYPER_PUSHPULL,
                              GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(GPIOA, 12U, GPIO_AF10_OTG_FS, GPIO_OTYPER_PUSHPULL,
                              GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;

    /* Stay disconnected until everything is programmed. */
    OTG_FS_GLOBAL->GAHBCFG &= ~OTG_GAHBCFG_GINT;
    OTG_FS_DEVICE->DCTL |= OTG_DCTL_SDIS;

    core_init();
    device_init();

    nvic_set_priority(IRQ_OTG_FS, BSP_USB_IRQ_PRIORITY);
    nvic_enable_irq(IRQ_OTG_FS);
    OTG_FS_GLOBAL->GAHBCFG |= OTG_GAHBCFG_GINT;

    /* Connect: release the D+ pull-up and let the host reset us. */
    OTG_FS_DEVICE->DCTL &= ~OTG_DCTL_SDIS;
}

#endif /* BSP_USE_USB_CDC */
