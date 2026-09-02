/**
 * @file    sdio.h
 * @brief   SDIO host interface registers (RM0383 sec. 21.9).
 */
#ifndef DEVICE_REGS_SDIO_H
#define DEVICE_REGS_SDIO_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t POWER;        /**< 0x00 power control            */
    __IO uint32_t CLKCR;        /**< 0x04 clock control            */
    __IO uint32_t ARG;          /**< 0x08 command argument         */
    __IO uint32_t CMD;          /**< 0x0C command                  */
    __I uint32_t RESPCMD;       /**< 0x10 command index of response*/
    __I uint32_t RESP1;         /**< 0x14 response word 1          */
    __I uint32_t RESP2;         /**< 0x18 response word 2          */
    __I uint32_t RESP3;         /**< 0x1C response word 3          */
    __I uint32_t RESP4;         /**< 0x20 response word 4          */
    __IO uint32_t DTIMER;       /**< 0x24 data timeout, in SDIO_CK */
    __IO uint32_t DLEN;         /**< 0x28 data length in bytes     */
    __IO uint32_t DCTRL;        /**< 0x2C data control             */
    __I uint32_t DCOUNT;        /**< 0x30 bytes remaining          */
    __I uint32_t STA;           /**< 0x34 status                   */
    __IO uint32_t ICR;          /**< 0x38 interrupt clear          */
    __IO uint32_t MASK;         /**< 0x3C interrupt mask           */
    uint32_t RESERVED0[2];      /*   0x40                          */
    __I uint32_t FIFOCNT;       /**< 0x48 words left to transfer   */
    uint32_t RESERVED1[13];     /*   0x4C                          */
    __IO uint32_t FIFO;         /**< 0x80 data FIFO (32 words deep)*/
} sdio_regs_t;

#define SDIO_BASE (APB2PERIPH_BASE + 0x2C00UL)

#define SDIO ((sdio_regs_t*)SDIO_BASE)

/* ---- POWER -------------------------------------------------------------- */
#define SDIO_POWER_PWRCTRL_POS 0U
#define SDIO_POWER_PWRCTRL_MSK (0x3UL << SDIO_POWER_PWRCTRL_POS)
#define SDIO_PWRCTRL_OFF 0x0UL /**< clock stopped, card unpowered  */
#define SDIO_PWRCTRL_ON  0x3UL /**< card clocked                   */

/* ---- CLKCR -------------------------------------------------------------- */
#define SDIO_CLKCR_CLKDIV_POS 0U
#define SDIO_CLKCR_CLKDIV_MSK (0xFFUL << SDIO_CLKCR_CLKDIV_POS) /**< SDIO_CK = SDIOCLK / (CLKDIV + 2) */
#define SDIO_CLKCR_CLKEN      (1UL << 8)
#define SDIO_CLKCR_PWRSAV     (1UL << 9)  /**< stop SDIO_CK when the bus is idle   */
#define SDIO_CLKCR_BYPASS     (1UL << 10) /**< SDIO_CK = SDIOCLK, divider bypassed */
#define SDIO_CLKCR_WIDBUS_POS 11U
#define SDIO_CLKCR_WIDBUS_MSK (0x3UL << SDIO_CLKCR_WIDBUS_POS)
#define SDIO_CLKCR_NEGEDGE    (1UL << 13)
#define SDIO_CLKCR_HWFC_EN    (1UL << 14) /**< hardware flow control */

#define SDIO_WIDBUS_1BIT 0x0UL
#define SDIO_WIDBUS_4BIT 0x1UL
#define SDIO_WIDBUS_8BIT 0x2UL

/* ---- CMD ---------------------------------------------------------------- */
#define SDIO_CMD_CMDINDEX_POS 0U
#define SDIO_CMD_CMDINDEX_MSK (0x3FUL << SDIO_CMD_CMDINDEX_POS)
#define SDIO_CMD_WAITRESP_POS 6U
#define SDIO_CMD_WAITRESP_MSK (0x3UL << SDIO_CMD_WAITRESP_POS)
#define SDIO_CMD_WAITINT      (1UL << 8)
#define SDIO_CMD_WAITPEND     (1UL << 9)  /**< wait for the end of data before sending */
#define SDIO_CMD_CPSMEN       (1UL << 10) /**< command path state machine enable       */
#define SDIO_CMD_SDIOSUSPEND  (1UL << 11)
#define SDIO_CMD_ENCMDCOMPL   (1UL << 12)
#define SDIO_CMD_NIEN         (1UL << 13)
#define SDIO_CMD_CE_ATACMD    (1UL << 14)

#define SDIO_WAITRESP_NONE  0x0UL /**< no response          */
#define SDIO_WAITRESP_SHORT 0x1UL /**< 48-bit response      */
#define SDIO_WAITRESP_LONG  0x3UL /**< 136-bit response     */

/* ---- RESPCMD ------------------------------------------------------------ */
#define SDIO_RESPCMD_RESPCMD_POS 0U
#define SDIO_RESPCMD_RESPCMD_MSK (0x3FUL << SDIO_RESPCMD_RESPCMD_POS)

/* ---- DLEN --------------------------------------------------------------- */
#define SDIO_DLEN_DATALENGTH_POS 0U
#define SDIO_DLEN_DATALENGTH_MSK (0x1FFFFFFUL << SDIO_DLEN_DATALENGTH_POS)

/* ---- DCTRL -------------------------------------------------------------- */
#define SDIO_DCTRL_DTEN           (1UL << 0)
#define SDIO_DCTRL_DTDIR          (1UL << 1) /**< 1 = card to controller       */
#define SDIO_DCTRL_DTMODE         (1UL << 2) /**< 1 = stream, 0 = block        */
#define SDIO_DCTRL_DMAEN          (1UL << 3)
#define SDIO_DCTRL_DBLOCKSIZE_POS 4U
#define SDIO_DCTRL_DBLOCKSIZE_MSK (0xFUL << SDIO_DCTRL_DBLOCKSIZE_POS) /**< block = 2^n bytes */
#define SDIO_DCTRL_RWSTART        (1UL << 8)
#define SDIO_DCTRL_RWSTOP         (1UL << 9)
#define SDIO_DCTRL_RWMOD          (1UL << 10)
#define SDIO_DCTRL_SDIOEN         (1UL << 11)

/* ---- DCOUNT / FIFOCNT --------------------------------------------------- */
#define SDIO_DCOUNT_DATACOUNT_POS  0U
#define SDIO_DCOUNT_DATACOUNT_MSK  (0x1FFFFFFUL << SDIO_DCOUNT_DATACOUNT_POS)
#define SDIO_FIFOCNT_FIFOCOUNT_POS 0U
#define SDIO_FIFOCNT_FIFOCOUNT_MSK (0xFFFFFFUL << SDIO_FIFOCNT_FIFOCOUNT_POS)

/* ---- STA (ICR clears the same positions; MASK enables them) ------------- */
#define SDIO_STA_CCRCFAIL (1UL << 0)  /**< command response CRC failed        */
#define SDIO_STA_DCRCFAIL (1UL << 1)  /**< data block CRC failed              */
#define SDIO_STA_CTIMEOUT (1UL << 2)  /**< command response timeout           */
#define SDIO_STA_DTIMEOUT (1UL << 3)  /**< data timeout                       */
#define SDIO_STA_TXUNDERR (1UL << 4)  /**< transmit FIFO underrun             */
#define SDIO_STA_RXOVERR  (1UL << 5)  /**< receive FIFO overrun               */
#define SDIO_STA_CMDREND  (1UL << 6)  /**< command response received, CRC ok  */
#define SDIO_STA_CMDSENT  (1UL << 7)  /**< command sent (no response expected)*/
#define SDIO_STA_DATAEND  (1UL << 8)  /**< data end, DCOUNT reached zero      */
#define SDIO_STA_STBITERR (1UL << 9)  /**< start bit missing on a data line   */
#define SDIO_STA_DBCKEND  (1UL << 10) /**< data block sent/received, CRC ok   */
#define SDIO_STA_CMDACT   (1UL << 11)
#define SDIO_STA_TXACT    (1UL << 12)
#define SDIO_STA_RXACT    (1UL << 13)
#define SDIO_STA_TXFIFOHE (1UL << 14) /**< tx FIFO half empty: 8 words free   */
#define SDIO_STA_RXFIFOHF (1UL << 15) /**< rx FIFO half full: 8 words ready   */
#define SDIO_STA_TXFIFOF  (1UL << 16)
#define SDIO_STA_RXFIFOF  (1UL << 17)
#define SDIO_STA_TXFIFOE  (1UL << 18)
#define SDIO_STA_RXFIFOE  (1UL << 19)
#define SDIO_STA_TXDAVL   (1UL << 20)
#define SDIO_STA_RXDAVL   (1UL << 21) /**< at least one word in the rx FIFO   */
#define SDIO_STA_SDIOIT   (1UL << 22)
#define SDIO_STA_CEATAEND (1UL << 23)

#define SDIO_ICR_CCRCFAILC (1UL << 0)
#define SDIO_ICR_DCRCFAILC (1UL << 1)
#define SDIO_ICR_CTIMEOUTC (1UL << 2)
#define SDIO_ICR_DTIMEOUTC (1UL << 3)
#define SDIO_ICR_TXUNDERRC (1UL << 4)
#define SDIO_ICR_RXOVERRC  (1UL << 5)
#define SDIO_ICR_CMDRENDC  (1UL << 6)
#define SDIO_ICR_CMDSENTC  (1UL << 7)
#define SDIO_ICR_DATAENDC  (1UL << 8)
#define SDIO_ICR_STBITERRC (1UL << 9)
#define SDIO_ICR_DBCKENDC  (1UL << 10)
#define SDIO_ICR_SDIOITC   (1UL << 22)
#define SDIO_ICR_CEATAENDC (1UL << 23)

/** Every flag ICR can clear. */
#define SDIO_ICR_ALL_FLAGS                                                                      \
    (SDIO_ICR_CCRCFAILC | SDIO_ICR_DCRCFAILC | SDIO_ICR_CTIMEOUTC | SDIO_ICR_DTIMEOUTC |         \
     SDIO_ICR_TXUNDERRC | SDIO_ICR_RXOVERRC | SDIO_ICR_CMDRENDC | SDIO_ICR_CMDSENTC |            \
     SDIO_ICR_DATAENDC | SDIO_ICR_STBITERRC | SDIO_ICR_DBCKENDC | SDIO_ICR_SDIOITC |             \
     SDIO_ICR_CEATAENDC)

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_SDIO_H */
