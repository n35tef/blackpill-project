/**
 * @file    bsp_spi.c
 * @brief   SPI1..SPI5 in master mode.
 */

#include "bsp_spi.h"

#include "bsp_gpio.h"

#if BSP_USE_SPI1 || BSP_USE_SPI2 || BSP_USE_SPI3 || BSP_USE_SPI4 || BSP_USE_SPI5

/** @brief Turn a plain divider into the three-bit CR1.BR encoding. */
static uint32_t baud_encode(uint32_t divider)
{
    uint32_t code = 0U;

    /* BR selects PCLK / 2^(BR+1), so the code is log2(divider) - 1. */
    while ((divider > 2U) && (code < SPI_BR_DIV256))
    {
        divider >>= 1U;
        code++;
    }
    return code;
}

/**
 * @brief Common master-mode bring-up.
 *
 * Software slave management is always on (SSM + SSI): without it a low NSS
 * input would silently drop the peripheral out of master mode.
 */
static void spi_configure(spi_regs_t* spi, uint32_t divider, uint32_t cpol, uint32_t cpha,
                          uint32_t data_16bit, uint32_t lsb_first, uint32_t use_miso)
{
    spi->CR1 = 0U; /* disable before touching the configuration */
    spi->I2SCFGR &= ~SPI_I2SCFGR_I2SMOD;

    uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                   (baud_encode(divider) << SPI_CR1_BR_POS);

    if (cpol != 0U)
    {
        cr1 |= SPI_CR1_CPOL;
    }
    if (cpha != 0U)
    {
        cr1 |= SPI_CR1_CPHA;
    }
    if (data_16bit != 0U)
    {
        cr1 |= SPI_CR1_DFF;
    }
    if (lsb_first != 0U)
    {
        cr1 |= SPI_CR1_LSBFIRST;
    }
    if (use_miso == 0U)
    {
        /* One-line output-only mode; MISO stays free for other uses. */
        cr1 |= SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE;
    }

    spi->CR1 = cr1;
    spi->CR1 |= SPI_CR1_SPE;
}

/** @brief True when the bus was configured without a MISO line. */
static bool is_transmit_only(const spi_regs_t* spi)
{
    return (spi->CR1 & SPI_CR1_BIDIMODE) != 0U;
}

void bsp_spi_init(void)
{
#if BSP_USE_SPI1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    bsp_gpio_config_alternate(BSP_SPI1_SCK_PORT, BSP_SPI1_SCK_PIN, BSP_SPI1_SCK_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(BSP_SPI1_MOSI_PORT, BSP_SPI1_MOSI_PIN, BSP_SPI1_MOSI_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#if BSP_SPI1_USE_MISO
    bsp_gpio_config_alternate(BSP_SPI1_MISO_PORT, BSP_SPI1_MISO_PIN, BSP_SPI1_MISO_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#endif
    spi_configure(SPI1, BSP_SPI1_BAUD_DIV, BSP_SPI1_CPOL, BSP_SPI1_CPHA, BSP_SPI1_DATA_16BIT,
                  BSP_SPI1_LSB_FIRST, BSP_SPI1_USE_MISO);
#endif

#if BSP_USE_SPI2
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_SPI2_SCK_PORT, BSP_SPI2_SCK_PIN, BSP_SPI2_SCK_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(BSP_SPI2_MOSI_PORT, BSP_SPI2_MOSI_PIN, BSP_SPI2_MOSI_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#if BSP_SPI2_USE_MISO
    bsp_gpio_config_alternate(BSP_SPI2_MISO_PORT, BSP_SPI2_MISO_PIN, BSP_SPI2_MISO_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#endif
    spi_configure(SPI2, BSP_SPI2_BAUD_DIV, BSP_SPI2_CPOL, BSP_SPI2_CPHA, BSP_SPI2_DATA_16BIT,
                  BSP_SPI2_LSB_FIRST, BSP_SPI2_USE_MISO);
#endif

#if BSP_USE_SPI3
    RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_SPI3_SCK_PORT, BSP_SPI3_SCK_PIN, BSP_SPI3_SCK_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(BSP_SPI3_MOSI_PORT, BSP_SPI3_MOSI_PIN, BSP_SPI3_MOSI_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#if BSP_SPI3_USE_MISO
    bsp_gpio_config_alternate(BSP_SPI3_MISO_PORT, BSP_SPI3_MISO_PIN, BSP_SPI3_MISO_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#endif
    spi_configure(SPI3, BSP_SPI3_BAUD_DIV, BSP_SPI3_CPOL, BSP_SPI3_CPHA, BSP_SPI3_DATA_16BIT,
                  BSP_SPI3_LSB_FIRST, BSP_SPI3_USE_MISO);
#endif

#if BSP_USE_SPI4
    RCC->APB2ENR |= RCC_APB2ENR_SPI4EN;
    (void)RCC->APB2ENR;
    bsp_gpio_config_alternate(BSP_SPI4_SCK_PORT, BSP_SPI4_SCK_PIN, BSP_SPI4_SCK_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(BSP_SPI4_MOSI_PORT, BSP_SPI4_MOSI_PIN, BSP_SPI4_MOSI_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#if BSP_SPI4_USE_MISO
    bsp_gpio_config_alternate(BSP_SPI4_MISO_PORT, BSP_SPI4_MISO_PIN, BSP_SPI4_MISO_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#endif
    spi_configure(SPI4, BSP_SPI4_BAUD_DIV, BSP_SPI4_CPOL, BSP_SPI4_CPHA, BSP_SPI4_DATA_16BIT,
                  BSP_SPI4_LSB_FIRST, BSP_SPI4_USE_MISO);
#endif

#if BSP_USE_SPI5
    RCC->APB2ENR |= RCC_APB2ENR_SPI5EN;
    (void)RCC->APB2ENR;
    bsp_gpio_config_alternate(BSP_SPI5_SCK_PORT, BSP_SPI5_SCK_PIN, BSP_SPI5_SCK_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
    bsp_gpio_config_alternate(BSP_SPI5_MOSI_PORT, BSP_SPI5_MOSI_PIN, BSP_SPI5_MOSI_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#if BSP_SPI5_USE_MISO
    bsp_gpio_config_alternate(BSP_SPI5_MISO_PORT, BSP_SPI5_MISO_PIN, BSP_SPI5_MISO_AF,
                              GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH, GPIO_PUPD_NONE);
#endif
    spi_configure(SPI5, BSP_SPI5_BAUD_DIV, BSP_SPI5_CPOL, BSP_SPI5_CPHA, BSP_SPI5_DATA_16BIT,
                  BSP_SPI5_LSB_FIRST, BSP_SPI5_USE_MISO);
#endif
}

uint16_t bsp_spi_transfer(spi_regs_t* spi, uint16_t value)
{
    while ((spi->SR & SPI_SR_TXE) == 0U)
    {
    }
    spi->DR = value;

    if (is_transmit_only(spi))
    {
        /* No receive side to wait on, so block until the frame has actually
         * been shifted out; otherwise the caller could drop chip select mid
         * transfer. */
        bsp_spi_wait_idle(spi);
        return 0U;
    }

    while ((spi->SR & SPI_SR_RXNE) == 0U)
    {
    }
    return (uint16_t)spi->DR;
}

void bsp_spi_transfer_bytes(spi_regs_t* spi, const uint8_t* tx, uint8_t* rx, size_t length)
{
    for (size_t i = 0U; i < length; i++)
    {
        const uint16_t out = (tx != 0) ? tx[i] : 0xFFU;
        const uint16_t in = bsp_spi_transfer(spi, out);

        if (rx != 0)
        {
            rx[i] = (uint8_t)in;
        }
    }
}

void bsp_spi_write_bytes(spi_regs_t* spi, const uint8_t* data, size_t length)
{
    if (is_transmit_only(spi))
    {
        /* No receive side to drain, so just keep the TX FIFO fed. */
        for (size_t i = 0U; i < length; i++)
        {
            while ((spi->SR & SPI_SR_TXE) == 0U)
            {
            }
            spi->DR = data[i];
        }
        bsp_spi_wait_idle(spi);
        return;
    }

    bsp_spi_transfer_bytes(spi, data, 0, length);
}

void bsp_spi_wait_idle(spi_regs_t* spi)
{
    /* BSY only rises a couple of APB cycles after DR is written, so checking
     * it alone can pass before the frame has even started. Waiting for TXE
     * first guarantees the write has been taken up by the shift register. */
    while ((spi->SR & SPI_SR_TXE) == 0U)
    {
    }
    while ((spi->SR & SPI_SR_BSY) != 0U)
    {
    }
}

void bsp_spi_set_baud_divider(spi_regs_t* spi, uint32_t divider)
{
    const uint32_t enabled = spi->CR1 & SPI_CR1_SPE;

    bsp_spi_wait_idle(spi);
    spi->CR1 &= ~SPI_CR1_SPE;
    spi->CR1 = (spi->CR1 & ~SPI_CR1_BR_MSK) | (baud_encode(divider) << SPI_CR1_BR_POS);
    spi->CR1 |= enabled;
}

#endif /* any SPI */
