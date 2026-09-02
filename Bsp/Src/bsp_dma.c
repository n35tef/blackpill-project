/**
 * @file    bsp_dma.c
 * @brief   DMA stream configuration helpers.
 */

#include "bsp_dma.h"

#if BSP_USE_DMA1 || BSP_USE_DMA2

/** @brief Interrupt number for a stream, needed to enable it in the NVIC. */
static int32_t stream_irq(const dma_regs_t* dma, uint32_t stream)
{
    if (dma == DMA1)
    {
        /* DMA1 streams 0-6 are contiguous; stream 7 sits on its own. */
        return (stream <= 6U) ? (int32_t)(IRQ_DMA1_STREAM0 + stream) : (int32_t)IRQ_DMA1_STREAM7;
    }

    /* DMA2 streams 0-4 are contiguous, then OTG_FS interrupts the run. */
    return (stream <= 4U) ? (int32_t)(IRQ_DMA2_STREAM0 + stream)
                          : (int32_t)(IRQ_DMA2_STREAM5 + (stream - 5U));
}

void bsp_dma_init(void)
{
#if BSP_USE_DMA1
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
#endif
#if BSP_USE_DMA2
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
#endif
    (void)RCC->AHB1ENR;
}

void bsp_dma_configure(const bsp_dma_config_t* config)
{
    dma_stream_regs_t* const stream = &config->controller->STREAM[config->stream];

    /* A stream must be fully disabled before CR can be rewritten. */
    stream->CR &= ~DMA_CR_EN;
    while ((stream->CR & DMA_CR_EN) != 0U)
    {
    }

    dma_clear_flags(config->controller, config->stream,
                    DMA_FLAG_FEIF | DMA_FLAG_DMEIF | DMA_FLAG_TEIF | DMA_FLAG_HTIF | DMA_FLAG_TCIF);

    uint32_t cr = ((uint32_t)config->channel << DMA_CR_CHSEL_POS) |
                  ((uint32_t)config->direction << DMA_CR_DIR_POS) |
                  ((uint32_t)config->periph_size << DMA_CR_PSIZE_POS) |
                  ((uint32_t)config->memory_size << DMA_CR_MSIZE_POS) |
                  ((uint32_t)config->priority << DMA_CR_PL_POS);

    if (config->periph_increment)
    {
        cr |= DMA_CR_PINC;
    }
    if (config->memory_increment)
    {
        cr |= DMA_CR_MINC;
    }
    if (config->circular)
    {
        cr |= DMA_CR_CIRC;
    }
    if (config->interrupt_on_complete)
    {
        cr |= DMA_CR_TCIE | DMA_CR_TEIE;
        nvic_enable_irq(stream_irq(config->controller, config->stream));
    }

    stream->CR = cr;
    stream->FCR = 0U; /* direct mode, no FIFO */
}

void bsp_dma_start(const bsp_dma_config_t* config, uint32_t periph_addr, uint32_t mem_addr,
                   uint16_t count)
{
    dma_stream_regs_t* const stream = &config->controller->STREAM[config->stream];

    stream->CR &= ~DMA_CR_EN;
    while ((stream->CR & DMA_CR_EN) != 0U)
    {
    }

    dma_clear_flags(config->controller, config->stream,
                    DMA_FLAG_FEIF | DMA_FLAG_DMEIF | DMA_FLAG_TEIF | DMA_FLAG_HTIF | DMA_FLAG_TCIF);

    stream->PAR = periph_addr;
    stream->M0AR = mem_addr;
    stream->NDTR = count;
    stream->CR |= DMA_CR_EN;
}

void bsp_dma_stop(const bsp_dma_config_t* config)
{
    dma_stream_regs_t* const stream = &config->controller->STREAM[config->stream];

    stream->CR &= ~DMA_CR_EN;
    while ((stream->CR & DMA_CR_EN) != 0U)
    {
    }
}

uint16_t bsp_dma_remaining(const bsp_dma_config_t* config)
{
    return (uint16_t)config->controller->STREAM[config->stream].NDTR;
}

bool bsp_dma_complete(const bsp_dma_config_t* config)
{
    return (dma_get_flags(config->controller, config->stream) & DMA_FLAG_TCIF) != 0U;
}

#endif /* BSP_USE_DMA1 || BSP_USE_DMA2 */
