/**
 * @file    spi.h
 * @brief   SPI / I2S registers (RM0383 sec. 20.5).
 */
#ifndef DEVICE_REGS_SPI_H
#define DEVICE_REGS_SPI_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR1;     /**< 0x00 control 1            */
    __IO uint32_t CR2;     /**< 0x04 control 2            */
    __IO uint32_t SR;      /**< 0x08 status               */
    __IO uint32_t DR;      /**< 0x0C data                 */
    __IO uint32_t CRCPR;   /**< 0x10 CRC polynomial       */
    __I uint32_t RXCRCR;   /**< 0x14 RX CRC               */
    __I uint32_t TXCRCR;   /**< 0x18 TX CRC               */
    __IO uint32_t I2SCFGR; /**< 0x1C I2S configuration    */
    __IO uint32_t I2SPR;   /**< 0x20 I2S prescaler        */
} spi_regs_t;

#define SPI1_BASE (APB2PERIPH_BASE + 0x3000UL)
#define SPI2_BASE (APB1PERIPH_BASE + 0x3800UL)
#define SPI3_BASE (APB1PERIPH_BASE + 0x3C00UL)
#define SPI4_BASE (APB2PERIPH_BASE + 0x3400UL)
#define SPI5_BASE (APB2PERIPH_BASE + 0x5000UL)

#define SPI1 ((spi_regs_t*)SPI1_BASE)
#define SPI2 ((spi_regs_t*)SPI2_BASE)
#define SPI3 ((spi_regs_t*)SPI3_BASE)
#define SPI4 ((spi_regs_t*)SPI4_BASE)
#define SPI5 ((spi_regs_t*)SPI5_BASE)

/* ---- CR1 ---------------------------------------------------------------- */
#define SPI_CR1_CPHA      (1UL << 0)
#define SPI_CR1_CPOL      (1UL << 1)
#define SPI_CR1_MSTR      (1UL << 2)
#define SPI_CR1_BR_POS    3U
#define SPI_CR1_BR_MSK    (0x7UL << SPI_CR1_BR_POS)
#define SPI_CR1_SPE       (1UL << 6)
#define SPI_CR1_LSBFIRST  (1UL << 7)
#define SPI_CR1_SSI       (1UL << 8)
#define SPI_CR1_SSM       (1UL << 9)
#define SPI_CR1_RXONLY    (1UL << 10)
#define SPI_CR1_DFF       (1UL << 11) /**< 1 = 16-bit frames */
#define SPI_CR1_CRCNEXT   (1UL << 12)
#define SPI_CR1_CRCEN     (1UL << 13)
#define SPI_CR1_BIDIOE    (1UL << 14)
#define SPI_CR1_BIDIMODE  (1UL << 15)

/** Baud rate divider encodings (CR1.BR): PCLK / 2^(BR+1). */
#define SPI_BR_DIV2   0x0UL
#define SPI_BR_DIV4   0x1UL
#define SPI_BR_DIV8   0x2UL
#define SPI_BR_DIV16  0x3UL
#define SPI_BR_DIV32  0x4UL
#define SPI_BR_DIV64  0x5UL
#define SPI_BR_DIV128 0x6UL
#define SPI_BR_DIV256 0x7UL

/* ---- CR2 ---------------------------------------------------------------- */
#define SPI_CR2_RXDMAEN (1UL << 0)
#define SPI_CR2_TXDMAEN (1UL << 1)
#define SPI_CR2_SSOE    (1UL << 2)
#define SPI_CR2_FRF     (1UL << 4)
#define SPI_CR2_ERRIE   (1UL << 5)
#define SPI_CR2_RXNEIE  (1UL << 6)
#define SPI_CR2_TXEIE   (1UL << 7)

/* ---- SR ----------------------------------------------------------------- */
#define SPI_SR_RXNE   (1UL << 0)
#define SPI_SR_TXE    (1UL << 1)
#define SPI_SR_CHSIDE (1UL << 2)
#define SPI_SR_UDR    (1UL << 3)
#define SPI_SR_CRCERR (1UL << 4)
#define SPI_SR_MODF   (1UL << 5)
#define SPI_SR_OVR    (1UL << 6)
#define SPI_SR_BSY    (1UL << 7)
#define SPI_SR_FRE    (1UL << 8)

/* ---- I2SCFGR ------------------------------------------------------------ */
#define SPI_I2SCFGR_I2SMOD (1UL << 11) /**< 0 = SPI mode, 1 = I2S mode */

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_SPI_H */
