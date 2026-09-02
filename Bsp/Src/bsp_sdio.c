/**
 * @file    bsp_sdio.c
 * @brief   SDIO host interface and SD memory card block access (polled).
 *
 * Register-level sequencing follows RM0383 sec. 21; the card protocol follows
 * the SD Physical Layer Simplified Specification (v2.00 command set, which
 * every SDSC/SDHC/SDXC card speaks in SD mode).
 */

#include "bsp_sdio.h"

#if BSP_USE_SDIO

#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_systick.h"

/* ---- Configuration checks ----------------------------------------------- */

_Static_assert((BSP_SDIO_BUS_WIDTH == 1U) || (BSP_SDIO_BUS_WIDTH == 4U),
               "BSP_SDIO_BUS_WIDTH must be 1 or 4");
_Static_assert(BSP_SDIO_CLK_HZ <= 25000000UL,
               "BSP_SDIO_CLK_HZ above 25 MHz needs the high-speed switch (CMD6), not implemented");
_Static_assert(BSP_SDIO_CLK_HZ >= (BSP_PLL_Q_OUT_HZ / 257UL),
               "BSP_SDIO_CLK_HZ is below what the 8-bit divider can reach");
/* RM0383 21.3: SDIO_CK must not exceed HCLK / 2, and PCLK2 must be at least
 * 3/8 of SDIO_CK for the APB side to keep up with the FIFO. */
_Static_assert(BSP_SDIO_CLK_HZ <= (BSP_HCLK_HZ / 2UL), "SDIO_CK must be <= HCLK / 2");
_Static_assert((BSP_PCLK2_HZ * 8UL) >= (BSP_SDIO_CLK_HZ * 3UL), "PCLK2 must be >= 3/8 SDIO_CK");

/* ---- SD protocol constants ---------------------------------------------- */

#define SD_INIT_CLOCK_HZ 400000UL /**< identification mode: 100..400 kHz */

/* Command indexes (SD Physical Layer ch. 4.7.4). */
#define CMD0_GO_IDLE_STATE        0U
#define CMD2_ALL_SEND_CID         2U
#define CMD3_SEND_RELATIVE_ADDR   3U
#define CMD7_SELECT_CARD          7U
#define CMD8_SEND_IF_COND         8U
#define CMD9_SEND_CSD             9U
#define CMD12_STOP_TRANSMISSION   12U
#define CMD13_SEND_STATUS         13U
#define CMD16_SET_BLOCKLEN        16U
#define CMD17_READ_SINGLE_BLOCK   17U
#define CMD18_READ_MULTIPLE_BLOCK 18U
#define CMD24_WRITE_BLOCK         24U
#define CMD25_WRITE_MULTIPLE      25U
#define CMD55_APP_CMD             55U
#define ACMD6_SET_BUS_WIDTH       6U
#define ACMD41_SD_SEND_OP_COND    41U

/* CMD8 argument: 2.7-3.6 V (VHS = 1) and the 0xAA check pattern. */
#define SD_CMD8_ARG 0x000001AAUL

/* ACMD41 argument: 2.7-3.6 V window; HCS asks a v2 card whether it is SDHC. */
#define SD_OCR_VOLTAGE_WINDOW 0x00FF8000UL
#define SD_ACMD41_HCS         (1UL << 30)
#define SD_OCR_CCS            (1UL << 30) /**< card capacity status: 1 = SDHC/SDXC */
#define SD_OCR_BUSY           (1UL << 31) /**< 1 = power-up finished             */

/* ACMD6 argument. */
#define SD_BUS_WIDTH_1 0x0UL
#define SD_BUS_WIDTH_4 0x2UL

/* R1 card status (SD Physical Layer 4.10.1). */
#define SD_R1_OUT_OF_RANGE       (1UL << 31)
#define SD_R1_ADDRESS_ERROR      (1UL << 30)
#define SD_R1_BLOCK_LEN_ERROR    (1UL << 29)
#define SD_R1_ERASE_SEQ_ERROR    (1UL << 28)
#define SD_R1_ERASE_PARAM        (1UL << 27)
#define SD_R1_WP_VIOLATION       (1UL << 26)
#define SD_R1_LOCK_UNLOCK_FAILED (1UL << 24)
#define SD_R1_COM_CRC_ERROR      (1UL << 23)
#define SD_R1_ILLEGAL_COMMAND    (1UL << 22)
#define SD_R1_CARD_ECC_FAILED    (1UL << 21)
#define SD_R1_CC_ERROR           (1UL << 20)
#define SD_R1_ERROR              (1UL << 19)
#define SD_R1_CSD_OVERWRITE      (1UL << 16)
#define SD_R1_WP_ERASE_SKIP      (1UL << 15)
#define SD_R1_CURRENT_STATE_POS  9U
#define SD_R1_CURRENT_STATE_MSK  (0xFUL << SD_R1_CURRENT_STATE_POS)
#define SD_R1_READY_FOR_DATA     (1UL << 8)
#define SD_R1_APP_CMD            (1UL << 5)
#define SD_R1_AKE_SEQ_ERROR      (1UL << 3)

#define SD_R1_ERRORS                                                                        \
    (SD_R1_OUT_OF_RANGE | SD_R1_ADDRESS_ERROR | SD_R1_BLOCK_LEN_ERROR | SD_R1_ERASE_SEQ_ERROR | \
     SD_R1_ERASE_PARAM | SD_R1_WP_VIOLATION | SD_R1_LOCK_UNLOCK_FAILED | SD_R1_COM_CRC_ERROR |  \
     SD_R1_ILLEGAL_COMMAND | SD_R1_CARD_ECC_FAILED | SD_R1_CC_ERROR | SD_R1_ERROR |             \
     SD_R1_CSD_OVERWRITE | SD_R1_WP_ERASE_SKIP | SD_R1_AKE_SEQ_ERROR)

#define SD_STATE_TRAN 4U

/* R6 status bits in the low half of the response. */
#define SD_R6_COM_CRC_ERROR   (1UL << 15)
#define SD_R6_ILLEGAL_COMMAND (1UL << 14)
#define SD_R6_ERROR           (1UL << 13)
#define SD_R6_ERRORS          (SD_R6_COM_CRC_ERROR | SD_R6_ILLEGAL_COMMAND | SD_R6_ERROR)

/* ---- Timeouts ----------------------------------------------------------- */

/* Polling loop bounds. Each iteration of a status loop is a handful of
 * cycles, so these are generous multiples of the card's specified limits
 * (Ncr 64 clocks for a response, 100 ms for a data block, 250 ms for a
 * write to finish, 1 s for ACMD41 to report power-up). */
#define SD_CMD_TIMEOUT_LOOPS   1000000UL
#define SD_DATA_TIMEOUT_LOOPS  10000000UL
#define SD_ACMD41_RETRIES      2000U
#define SD_BUSY_RETRIES        100000U

/* DTIMER is in SDIO_CK cycles and only bounds the wait for the start bit of
 * a data block. 0xFFFFFFFF is what every reference implementation uses; the
 * software loops above are the real guard. */
#define SD_DATA_TIMER 0xFFFFFFFFUL

/* Status flags that end a data transfer, successfully or not. */
#define SD_DATA_ERRORS (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_RXOVERR |             \
                        SDIO_STA_TXUNDERR | SDIO_STA_STBITERR)

#define SD_WORDS_PER_BLOCK (BSP_SD_BLOCK_SIZE / 4U)
#define SD_FIFO_HALF_WORDS 8U /**< RXFIFOHF / TXFIFOHE mean 8 words */

/* ---- State -------------------------------------------------------------- */

static bsp_sd_card_t s_card;
static bool s_ready;

/* ---- Small helpers ------------------------------------------------------ */

/* Busy-wait rather than bsp_delay_ms(): these are one-off bring-up pauses,
 * and a busy wait works before SysTick is up and with interrupts masked. */
static void delay_ms(uint32_t ms)
{
    bsp_delay_us(ms * 1000U);
}

static void sdio_pin(gpio_regs_t* port, uint32_t pin, uint32_t pull)
{
    bsp_gpio_config_alternate(port, pin, GPIO_AF12_SDIO, GPIO_OTYPER_PUSHPULL,
                              GPIO_OSPEED_VERY_HIGH, pull);
}

/* ---- Peripheral layer --------------------------------------------------- */

void bsp_sdio_set_clock(uint32_t hz)
{
    /* SDIO_CK = SDIOCLK / (CLKDIV + 2). Round the divider up so the result
     * never exceeds the request. */
    uint32_t div = (BSP_PLL_Q_OUT_HZ + hz - 1UL) / hz;
    div = (div < 2UL) ? 0UL : (div - 2UL);
    if (div > 0xFFUL)
    {
        div = 0xFFUL;
    }
    SDIO->CLKCR = (SDIO->CLKCR & ~SDIO_CLKCR_CLKDIV_MSK) | (div << SDIO_CLKCR_CLKDIV_POS);
}

static void sdio_set_bus_width(uint32_t widbus)
{
    SDIO->CLKCR = (SDIO->CLKCR & ~SDIO_CLKCR_WIDBUS_MSK) | (widbus << SDIO_CLKCR_WIDBUS_POS);
}

void bsp_sdio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;
    (void)RCC->APB2ENR;

    /* The card side has no pull-ups on the BlackPill, and the SD bus needs
     * them on CMD and every data line (the card holds D3 for card detect and
     * the host must not see a floating line). CK is driven, no pull. */
    sdio_pin(BSP_SDIO_CK_PORT, BSP_SDIO_CK_PIN, GPIO_PUPD_NONE);
    sdio_pin(BSP_SDIO_CMD_PORT, BSP_SDIO_CMD_PIN, GPIO_PUPD_UP);
    sdio_pin(BSP_SDIO_D0_PORT, BSP_SDIO_D0_PIN, GPIO_PUPD_UP);
#if BSP_SDIO_BUS_WIDTH == 4U
    sdio_pin(BSP_SDIO_D1_PORT, BSP_SDIO_D1_PIN, GPIO_PUPD_UP);
    sdio_pin(BSP_SDIO_D2_PORT, BSP_SDIO_D2_PIN, GPIO_PUPD_UP);
    sdio_pin(BSP_SDIO_D3_PORT, BSP_SDIO_D3_PIN, GPIO_PUPD_UP);
#endif

    /* Everything is polled. Interrupts stay masked; the clock stays off
     * until a card is asked for. */
    SDIO->MASK = 0U;
    SDIO->POWER = SDIO_PWRCTRL_OFF << SDIO_POWER_PWRCTRL_POS;
    SDIO->CLKCR = 0U;
    s_ready = false;
}

int bsp_sdio_command(uint8_t index, uint32_t argument, bsp_sd_resp_t type, uint32_t* resp)
{
    uint32_t waitresp;
    switch (type)
    {
        case SD_RESP_NONE: waitresp = SDIO_WAITRESP_NONE; break;
        case SD_RESP_R2:   waitresp = SDIO_WAITRESP_LONG; break;
        default:           waitresp = SDIO_WAITRESP_SHORT; break;
    }

    SDIO->ICR = SDIO_ICR_ALL_FLAGS;
    SDIO->ARG = argument;
    SDIO->CMD = ((uint32_t)index << SDIO_CMD_CMDINDEX_POS) |
                (waitresp << SDIO_CMD_WAITRESP_POS) | SDIO_CMD_CPSMEN;

    uint32_t done = (type == SD_RESP_NONE) ? SDIO_STA_CMDSENT
                                           : (SDIO_STA_CMDREND | SDIO_STA_CCRCFAIL | SDIO_STA_CTIMEOUT);
    uint32_t sta;
    uint32_t loops = SD_CMD_TIMEOUT_LOOPS;
    do
    {
        sta = SDIO->STA;
        if (loops-- == 0U)
        {
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return BSP_SD_ETIMEOUT;
        }
    } while ((sta & done) == 0U);

    SDIO->ICR = SDIO_ICR_ALL_FLAGS;

    if (type == SD_RESP_NONE)
    {
        return BSP_SD_OK;
    }
    if (sta & SDIO_STA_CTIMEOUT)
    {
        return BSP_SD_ETIMEOUT;
    }
    /* R3 carries a fixed 0x7F in place of the CRC, so the controller always
     * reports a CRC failure for it; that is the expected outcome, not an error. */
    if ((sta & SDIO_STA_CCRCFAIL) && (type != SD_RESP_R3))
    {
        return BSP_SD_ECRC;
    }

    if (type == SD_RESP_R2)
    {
        if (resp != 0)
        {
            resp[0] = SDIO->RESP1;
            resp[1] = SDIO->RESP2;
            resp[2] = SDIO->RESP3;
            resp[3] = SDIO->RESP4;
        }
        return BSP_SD_OK;
    }

    /* Short responses other than R3 echo the command index; a mismatch means
     * the answer belongs to something else. */
    if ((type != SD_RESP_R3) &&
        (((SDIO->RESPCMD & SDIO_RESPCMD_RESPCMD_MSK) >> SDIO_RESPCMD_RESPCMD_POS) != index))
    {
        return BSP_SD_ECRC;
    }

    const uint32_t r1 = SDIO->RESP1;
    if (resp != 0)
    {
        resp[0] = r1;
    }
    if (((type == SD_RESP_R1) || (type == SD_RESP_R1B)) && (r1 & SD_R1_ERRORS))
    {
        return BSP_SD_ECARD;
    }
    if ((type == SD_RESP_R6) && (r1 & SD_R6_ERRORS))
    {
        return BSP_SD_ECARD;
    }
    return BSP_SD_OK;
}

/** @brief CMD55 prefix that turns the next command into an application command. */
static int sd_app_command(uint8_t index, uint32_t argument, bsp_sd_resp_t type, uint32_t* resp)
{
    uint32_t r1;
    int status = bsp_sdio_command(CMD55_APP_CMD, (uint32_t)s_card.rca << 16U, SD_RESP_R1, &r1);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    if ((r1 & SD_R1_APP_CMD) == 0U)
    {
        return BSP_SD_EUNSUPPORTED;
    }
    return bsp_sdio_command(index, argument, type, resp);
}

/** @brief Poll CMD13 until the card is back in transfer state and ready. */
static int sd_wait_ready(void)
{
    for (uint32_t i = 0U; i < SD_BUSY_RETRIES; i++)
    {
        uint32_t r1;
        const int status = bsp_sdio_command(CMD13_SEND_STATUS, (uint32_t)s_card.rca << 16U,
                                            SD_RESP_R1, &r1);
        if (status != BSP_SD_OK)
        {
            return status;
        }
        const uint32_t state = (r1 & SD_R1_CURRENT_STATE_MSK) >> SD_R1_CURRENT_STATE_POS;
        if ((r1 & SD_R1_READY_FOR_DATA) && (state == SD_STATE_TRAN))
        {
            return BSP_SD_OK;
        }
    }
    return BSP_SD_ETIMEOUT;
}

/** @brief Program the data path for a block transfer. */
static void sdio_start_data(uint32_t bytes, bool card_to_host)
{
    SDIO->DTIMER = SD_DATA_TIMER;
    SDIO->DLEN = bytes;
    SDIO->DCTRL = (9UL << SDIO_DCTRL_DBLOCKSIZE_POS) | /* 2^9 = 512-byte blocks */
                  (card_to_host ? SDIO_DCTRL_DTDIR : 0U) | SDIO_DCTRL_DTEN;
}

static int sd_data_error(uint32_t sta)
{
    if (sta & SDIO_STA_DTIMEOUT)
    {
        return BSP_SD_ETIMEOUT;
    }
    if (sta & (SDIO_STA_DCRCFAIL | SDIO_STA_STBITERR))
    {
        return BSP_SD_ECRC;
    }
    if (sta & (SDIO_STA_RXOVERR | SDIO_STA_TXUNDERR))
    {
        return BSP_SD_EFIFO;
    }
    return BSP_SD_OK;
}

/**
 * @brief Pull @p words from the FIFO as the card delivers them.
 *
 * The data path is already armed and the read command sent. Half-FIFO
 * chunks are taken while the transfer runs; whatever is left (fewer than 8
 * words at the tail) is drained after DATAEND.
 */
static int sdio_receive(uint32_t* data, uint32_t words)
{
    uint32_t received = 0U;
    uint32_t loops = SD_DATA_TIMEOUT_LOOPS;
    uint32_t sta;

    for (;;)
    {
        sta = SDIO->STA;
        if (sta & (SD_DATA_ERRORS | SDIO_STA_DATAEND))
        {
            break;
        }
        if ((sta & SDIO_STA_RXFIFOHF) && ((words - received) >= SD_FIFO_HALF_WORDS))
        {
            for (uint32_t i = 0U; i < SD_FIFO_HALF_WORDS; i++)
            {
                data[received++] = SDIO->FIFO;
            }
        }
        if (loops-- == 0U)
        {
            SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return BSP_SD_ETIMEOUT;
        }
    }

    while ((SDIO->STA & SDIO_STA_RXDAVL) && (received < words))
    {
        data[received++] = SDIO->FIFO;
    }

    /* DTEN is not self-clearing: leave the data path idle so the next
     * transfer (or the write command that precedes one) starts clean. */
    SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
    SDIO->ICR = SDIO_ICR_ALL_FLAGS;
    const int status = sd_data_error(sta);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    return (received == words) ? BSP_SD_OK : BSP_SD_EFIFO;
}

/**
 * @brief Feed @p words into the FIFO as the card accepts them.
 *
 * The write command has been sent and the data path armed; the card starts
 * clocking once the FIFO has something in it.
 */
static int sdio_transmit(const uint32_t* data, uint32_t words)
{
    uint32_t sent = 0U;
    uint32_t loops = SD_DATA_TIMEOUT_LOOPS;
    uint32_t sta;

    for (;;)
    {
        sta = SDIO->STA;
        if (sta & (SD_DATA_ERRORS | SDIO_STA_DATAEND))
        {
            break;
        }
        if ((sta & SDIO_STA_TXFIFOHE) && (sent < words))
        {
            const uint32_t chunk = ((words - sent) < SD_FIFO_HALF_WORDS) ? (words - sent)
                                                                          : SD_FIFO_HALF_WORDS;
            for (uint32_t i = 0U; i < chunk; i++)
            {
                SDIO->FIFO = data[sent++];
            }
        }
        if (loops-- == 0U)
        {
            SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
            SDIO->ICR = SDIO_ICR_ALL_FLAGS;
            return BSP_SD_ETIMEOUT;
        }
    }

    SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
    SDIO->ICR = SDIO_ICR_ALL_FLAGS;
    const int status = sd_data_error(sta);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    return (sent == words) ? BSP_SD_OK : BSP_SD_EFIFO;
}

/* ---- Card layer --------------------------------------------------------- */

/** @brief Decode the capacity out of a CSD v1.0 (SDSC) or v2.0 (SDHC/SDXC). */
static uint32_t sd_csd_block_count(const uint32_t csd[4])
{
    const uint32_t structure = csd[0] >> 30U; /* CSD_STRUCTURE, bits 127:126 */

    if (structure == 0U)
    {
        /* v1.0: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN.
         * READ_BL_LEN [83:80], C_SIZE [73:62], C_SIZE_MULT [49:47]. */
        const uint32_t read_bl_len = (csd[1] >> 16U) & 0xFU;
        const uint32_t c_size = ((csd[1] & 0x3FFU) << 2U) | (csd[2] >> 30U);
        const uint32_t c_size_mult = (csd[2] >> 15U) & 0x7U;
        const uint32_t bytes_per_block = 1UL << read_bl_len;
        const uint32_t blocks = (c_size + 1U) << (c_size_mult + 2U);
        /* Express in 512-byte units whatever the card's native block size. */
        return (bytes_per_block >= BSP_SD_BLOCK_SIZE)
                   ? blocks * (bytes_per_block / BSP_SD_BLOCK_SIZE)
                   : blocks / (BSP_SD_BLOCK_SIZE / bytes_per_block);
    }

    /* v2.0: capacity = (C_SIZE + 1) * 512 KB. C_SIZE is [69:48]. */
    const uint32_t c_size = ((csd[1] & 0x3FU) << 16U) | (csd[2] >> 16U);
    return (c_size + 1U) * 1024U;
}

int bsp_sd_init(void)
{
    int status;
    uint32_t resp[4];

    s_ready = false;
    s_card = (bsp_sd_card_t){0};

    /* Power on, then clock at identification speed on one data line. The
     * card needs 74 SDIO_CK cycles (about 0.2 ms at 400 kHz) before CMD0. */
    SDIO->POWER = SDIO_PWRCTRL_ON << SDIO_POWER_PWRCTRL_POS;
    delay_ms(2U);
    SDIO->CLKCR = (SDIO_WIDBUS_1BIT << SDIO_CLKCR_WIDBUS_POS);
    bsp_sdio_set_clock(SD_INIT_CLOCK_HZ);
    SDIO->CLKCR |= SDIO_CLKCR_CLKEN;
    delay_ms(2U);

    /* CMD0: everything to idle. No response. */
    status = bsp_sdio_command(CMD0_GO_IDLE_STATE, 0U, SD_RESP_NONE, 0);
    if (status != BSP_SD_OK)
    {
        return status;
    }

    /* CMD8: a v2 card echoes the argument; a v1 card does not answer at all. */
    status = bsp_sdio_command(CMD8_SEND_IF_COND, SD_CMD8_ARG, SD_RESP_R7, resp);
    if (status == BSP_SD_OK)
    {
        if ((resp[0] & 0x1FFU) != (SD_CMD8_ARG & 0x1FFU))
        {
            return BSP_SD_EUNSUPPORTED; /* wrong voltage range or pattern */
        }
        s_card.version_2 = true;
    }
    else if (status != BSP_SD_ETIMEOUT)
    {
        return status;
    }

    /* ACMD41 until the card reports power-up done. HCS is only meaningful to
     * a v2 card; a v1 card that sees it set ignores it. An MMC card fails
     * CMD55 here with ILLEGAL_COMMAND, which is how it is told apart. */
    const uint32_t acmd41_arg = SD_OCR_VOLTAGE_WINDOW | (s_card.version_2 ? SD_ACMD41_HCS : 0U);
    uint32_t retries = SD_ACMD41_RETRIES;
    for (;;)
    {
        status = sd_app_command(ACMD41_SD_SEND_OP_COND, acmd41_arg, SD_RESP_R3, resp);
        if (status == BSP_SD_ECARD)
        {
            return BSP_SD_EUNSUPPORTED;
        }
        if (status == BSP_SD_ETIMEOUT)
        {
            return BSP_SD_ENOCARD;
        }
        if (status != BSP_SD_OK)
        {
            return status;
        }
        if (resp[0] & SD_OCR_BUSY)
        {
            break;
        }
        if (retries-- == 0U)
        {
            return BSP_SD_ETIMEOUT;
        }
        delay_ms(1U);
    }
    s_card.high_capacity = (resp[0] & SD_OCR_CCS) != 0U;

    /* CMD2: CID. CMD3: the card picks its address. */
    status = bsp_sdio_command(CMD2_ALL_SEND_CID, 0U, SD_RESP_R2, s_card.cid);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    status = bsp_sdio_command(CMD3_SEND_RELATIVE_ADDR, 0U, SD_RESP_R6, resp);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    s_card.rca = (uint16_t)(resp[0] >> 16U);

    /* CMD9: CSD, for the capacity. Only valid while the card is in standby. */
    status = bsp_sdio_command(CMD9_SEND_CSD, (uint32_t)s_card.rca << 16U, SD_RESP_R2, s_card.csd);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    s_card.block_count = sd_csd_block_count(s_card.csd);

    /* CMD7: select it, moving it to transfer state. */
    status = bsp_sdio_command(CMD7_SELECT_CARD, (uint32_t)s_card.rca << 16U, SD_RESP_R1B, 0);
    if (status != BSP_SD_OK)
    {
        return status;
    }

    /* SDSC cards default to their CSD block length; pin it to 512 so both
     * card classes look the same from here on. SDHC ignores CMD16 politely. */
    status = bsp_sdio_command(CMD16_SET_BLOCKLEN, BSP_SD_BLOCK_SIZE, SD_RESP_R1, 0);
    if (status != BSP_SD_OK)
    {
        return status;
    }

#if BSP_SDIO_BUS_WIDTH == 4U
    /* ACMD6 on the card first, then the controller; the other order would
     * clock the response in on four lines the card is still not driving. */
    status = sd_app_command(ACMD6_SET_BUS_WIDTH, SD_BUS_WIDTH_4, SD_RESP_R1, 0);
    if (status != BSP_SD_OK)
    {
        return status;
    }
    sdio_set_bus_width(SDIO_WIDBUS_4BIT);
#endif

    /* Identification is over; run at the configured transfer clock. */
    bsp_sdio_set_clock(BSP_SDIO_CLK_HZ);

    status = sd_wait_ready();
    if (status != BSP_SD_OK)
    {
        return status;
    }

    s_ready = true;
    return BSP_SD_OK;
}

bool bsp_sd_ready(void)
{
    return s_ready;
}

const bsp_sd_card_t* bsp_sd_card(void)
{
    return &s_card;
}

/** @brief Command argument for a block: byte address on SDSC, block number on SDHC. */
static uint32_t sd_address(uint32_t block)
{
    return s_card.high_capacity ? block : (block * BSP_SD_BLOCK_SIZE);
}

static int sd_check_range(uint32_t block, uint32_t count)
{
    if (!s_ready)
    {
        return BSP_SD_ENOTREADY;
    }
    if ((count == 0U) || (block >= s_card.block_count) ||
        (count > (s_card.block_count - block)))
    {
        return BSP_SD_EPARAM;
    }
    return BSP_SD_OK;
}

int bsp_sd_read_blocks(uint32_t block, uint8_t* data, uint32_t count)
{
    int status = sd_check_range(block, count);
    if (status != BSP_SD_OK)
    {
        return status;
    }

    /* Reads: arm the data path before the command, so the first block is
     * caught the moment the card starts sending (RM0383 21.4, "read"). */
    sdio_start_data(count * BSP_SD_BLOCK_SIZE, true);

    const uint8_t cmd = (count == 1U) ? CMD17_READ_SINGLE_BLOCK : CMD18_READ_MULTIPLE_BLOCK;
    status = bsp_sdio_command(cmd, sd_address(block), SD_RESP_R1, 0);
    if (status != BSP_SD_OK)
    {
        SDIO->DCTRL &= ~SDIO_DCTRL_DTEN;
        return status;
    }

    status = sdio_receive((uint32_t*)data, count * SD_WORDS_PER_BLOCK);

    if (count > 1U)
    {
        /* Open-ended multiple-block read; the card keeps going until told to
         * stop. Report the first error if the stop itself also fails. */
        const int stop = bsp_sdio_command(CMD12_STOP_TRANSMISSION, 0U, SD_RESP_R1B, 0);
        if ((status == BSP_SD_OK) && (stop != BSP_SD_OK))
        {
            status = stop;
        }
    }
    return status;
}

int bsp_sd_write_blocks(uint32_t block, const uint8_t* data, uint32_t count)
{
    int status = sd_check_range(block, count);
    if (status != BSP_SD_OK)
    {
        return status;
    }

    status = sd_wait_ready();
    if (status != BSP_SD_OK)
    {
        return status;
    }

    /* Writes: command first, then the data path. The card starts clocking
     * data in as soon as the FIFO is fed (RM0383 21.4, "write"). */
    const uint8_t cmd = (count == 1U) ? CMD24_WRITE_BLOCK : CMD25_WRITE_MULTIPLE;
    status = bsp_sdio_command(cmd, sd_address(block), SD_RESP_R1, 0);
    if (status != BSP_SD_OK)
    {
        return status;
    }

    sdio_start_data(count * BSP_SD_BLOCK_SIZE, false);
    status = sdio_transmit((const uint32_t*)data, count * SD_WORDS_PER_BLOCK);

    if (count > 1U)
    {
        const int stop = bsp_sdio_command(CMD12_STOP_TRANSMISSION, 0U, SD_RESP_R1B, 0);
        if ((status == BSP_SD_OK) && (stop != BSP_SD_OK))
        {
            status = stop;
        }
    }
    if (status != BSP_SD_OK)
    {
        return status;
    }

    /* The card is busy programming after the last block; wait it out so the
     * caller can read the data straight back. */
    return sd_wait_ready();
}

#endif /* BSP_USE_SDIO */
