/**
 * @file    dma.h
 * @brief   DMA controller registers (RM0383 sec. 9.5).
 *
 * Each controller has a small global block (interrupt status and clear) plus
 * eight independent stream blocks at a fixed stride, so the streams are
 * modelled as an array rather than 48 individually named registers.
 */
#ifndef DEVICE_REGS_DMA_H
#define DEVICE_REGS_DMA_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    __IO uint32_t CR;   /**< 0x00 stream configuration        */
    __IO uint32_t NDTR; /**< 0x04 number of data items        */
    __IO uint32_t PAR;  /**< 0x08 peripheral address          */
    __IO uint32_t M0AR; /**< 0x0C memory 0 address            */
    __IO uint32_t M1AR; /**< 0x10 memory 1 address (dbl buf)  */
    __IO uint32_t FCR;  /**< 0x14 FIFO control                */
} dma_stream_regs_t;

typedef struct
{
    __I uint32_t LISR;  /**< 0x00 interrupt status, streams 0-3  */
    __I uint32_t HISR;  /**< 0x04 interrupt status, streams 4-7  */
    __O uint32_t LIFCR; /**< 0x08 interrupt clear, streams 0-3   */
    __O uint32_t HIFCR; /**< 0x0C interrupt clear, streams 4-7   */
    dma_stream_regs_t STREAM[8]; /**< 0x10 stream blocks, 0x18 apart */
} dma_regs_t;

#define DMA1_BASE (AHB1PERIPH_BASE + 0x6000UL)
#define DMA2_BASE (AHB1PERIPH_BASE + 0x6400UL)

#define DMA1 ((dma_regs_t*)DMA1_BASE)
#define DMA2 ((dma_regs_t*)DMA2_BASE)

/* ---- Stream CR ---------------------------------------------------------- */
#define DMA_CR_EN         (1UL << 0)
#define DMA_CR_DMEIE      (1UL << 1)  /**< direct mode error interrupt   */
#define DMA_CR_TEIE       (1UL << 2)  /**< transfer error interrupt      */
#define DMA_CR_HTIE       (1UL << 3)  /**< half transfer interrupt       */
#define DMA_CR_TCIE       (1UL << 4)  /**< transfer complete interrupt   */
#define DMA_CR_PFCTRL     (1UL << 5)  /**< peripheral is flow controller */
#define DMA_CR_DIR_POS    6U
#define DMA_CR_DIR_MSK    (0x3UL << DMA_CR_DIR_POS)
#define DMA_CR_CIRC       (1UL << 8)  /**< circular mode                 */
#define DMA_CR_PINC       (1UL << 9)  /**< peripheral address increment  */
#define DMA_CR_MINC       (1UL << 10) /**< memory address increment      */
#define DMA_CR_PSIZE_POS  11U
#define DMA_CR_PSIZE_MSK  (0x3UL << DMA_CR_PSIZE_POS)
#define DMA_CR_MSIZE_POS  13U
#define DMA_CR_MSIZE_MSK  (0x3UL << DMA_CR_MSIZE_POS)
#define DMA_CR_PINCOS     (1UL << 15)
#define DMA_CR_PL_POS     16U
#define DMA_CR_PL_MSK     (0x3UL << DMA_CR_PL_POS)
#define DMA_CR_DBM        (1UL << 18) /**< double buffer mode            */
#define DMA_CR_CT         (1UL << 19) /**< current target                */
#define DMA_CR_PBURST_POS 21U
#define DMA_CR_PBURST_MSK (0x3UL << DMA_CR_PBURST_POS)
#define DMA_CR_MBURST_POS 23U
#define DMA_CR_MBURST_MSK (0x3UL << DMA_CR_MBURST_POS)
#define DMA_CR_CHSEL_POS  25U
#define DMA_CR_CHSEL_MSK  (0x7UL << DMA_CR_CHSEL_POS)

/** Transfer direction (CR.DIR). */
#define DMA_DIR_PERIPH_TO_MEM 0x0UL
#define DMA_DIR_MEM_TO_PERIPH 0x1UL
#define DMA_DIR_MEM_TO_MEM    0x2UL

/** Transfer width (CR.PSIZE / CR.MSIZE). */
#define DMA_SIZE_BYTE 0x0UL
#define DMA_SIZE_HALF 0x1UL
#define DMA_SIZE_WORD 0x2UL

/** Stream priority (CR.PL). */
#define DMA_PRIORITY_LOW       0x0UL
#define DMA_PRIORITY_MEDIUM    0x1UL
#define DMA_PRIORITY_HIGH      0x2UL
#define DMA_PRIORITY_VERY_HIGH 0x3UL

/* ---- Stream FCR --------------------------------------------------------- */
#define DMA_FCR_FTH_POS 0U
#define DMA_FCR_FTH_MSK (0x3UL << DMA_FCR_FTH_POS)
#define DMA_FCR_DMDIS   (1UL << 2) /**< disable direct mode (use FIFO) */
#define DMA_FCR_FS_POS  3U
#define DMA_FCR_FS_MSK  (0x7UL << DMA_FCR_FS_POS)
#define DMA_FCR_FEIE    (1UL << 7)

/*
 * Interrupt flags. Streams 0-3 live in LISR/LIFCR and 4-7 in HISR/HIFCR, and
 * within each register the six flags per stream are laid out at bit offsets
 * 0, 6, 16 and 22 - hence the lookup table rather than an arithmetic formula.
 */
#define DMA_FLAG_FEIF  (1UL << 0)
#define DMA_FLAG_DMEIF (1UL << 2)
#define DMA_FLAG_TEIF  (1UL << 3)
#define DMA_FLAG_HTIF  (1UL << 4)
#define DMA_FLAG_TCIF  (1UL << 5)

/** @brief Bit position of stream @p s flags within its status register. */
static inline uint32_t dma_flag_shift(uint32_t stream)
{
    static const uint8_t shift[4] = {0U, 6U, 16U, 22U};
    return shift[stream & 0x3U];
}

/** @brief Read the interrupt flags for a stream, normalised to bit 0. */
static inline uint32_t dma_get_flags(const dma_regs_t* dma, uint32_t stream)
{
    const uint32_t status = (stream < 4U) ? dma->LISR : dma->HISR;
    return (status >> dma_flag_shift(stream)) & 0x3FUL;
}

/** @brief Clear the given interrupt flags for a stream. */
static inline void dma_clear_flags(dma_regs_t* dma, uint32_t stream, uint32_t flags)
{
    const uint32_t value = (flags & 0x3FUL) << dma_flag_shift(stream);

    if (stream < 4U)
    {
        dma->LIFCR = value;
    }
    else
    {
        dma->HIFCR = value;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_DMA_H */
