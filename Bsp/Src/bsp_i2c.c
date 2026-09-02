/**
 * @file    bsp_i2c.c
 * @brief   I2C1..I2C3 as a blocking master.
 */

#include "bsp_i2c.h"

#include <stdbool.h>

#include "bsp_clock.h"
#include "bsp_gpio.h"

#if BSP_USE_I2C1 || BSP_USE_I2C2 || BSP_USE_I2C3

/* Loop budget for any single status flag. Generous enough for a slow slave
 * stretching the clock, short enough that a dead bus does not hang forever. */
#define I2C_TIMEOUT 100000UL

/** @brief Spin until @p mask appears in SR1, watching for NACK and bus errors. */
static int wait_sr1(i2c_regs_t* i2c, uint32_t mask)
{
    uint32_t timeout = I2C_TIMEOUT;

    for (;;)
    {
        const uint32_t sr1 = i2c->SR1;

        if ((sr1 & mask) != 0U)
        {
            return BSP_I2C_OK;
        }
        if ((sr1 & I2C_SR1_AF) != 0U)
        {
            i2c->SR1 = ~I2C_SR1_AF;
            i2c->CR1 |= I2C_CR1_STOP;
            return BSP_I2C_ENACK;
        }
        if ((sr1 & (I2C_SR1_BERR | I2C_SR1_ARLO)) != 0U)
        {
            i2c->SR1 = ~(I2C_SR1_BERR | I2C_SR1_ARLO);
            i2c->CR1 |= I2C_CR1_STOP;
            return BSP_I2C_EBUS;
        }
        if (timeout-- == 0U)
        {
            i2c->CR1 |= I2C_CR1_STOP;
            return BSP_I2C_ETIMEOUT;
        }
    }
}

/** @brief Wait for the bus to go idle before starting a new transfer. */
static int wait_not_busy(i2c_regs_t* i2c)
{
    uint32_t timeout = I2C_TIMEOUT;

    while ((i2c->SR2 & I2C_SR2_BUSY) != 0U)
    {
        if (timeout-- == 0U)
        {
            return BSP_I2C_EBUS;
        }
    }
    return BSP_I2C_OK;
}

/** @brief Generate START and wait for SB. */
static int send_start(i2c_regs_t* i2c)
{
    i2c->CR1 |= I2C_CR1_START;
    return wait_sr1(i2c, I2C_SR1_SB);
}

/**
 * @brief Send the address byte and clear ADDR.
 * @param read 1 for a read transfer, 0 for a write.
 */
static int send_address(i2c_regs_t* i2c, uint8_t address, uint32_t read)
{
    i2c->DR = (uint32_t)((address << 1U) | (read & 1U));

    const int status = wait_sr1(i2c, I2C_SR1_ADDR);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    /* ADDR is cleared by reading SR1 then SR2; SR1 was just read above. */
    (void)i2c->SR2;
    return BSP_I2C_OK;
}

static int write_bytes(i2c_regs_t* i2c, const uint8_t* data, size_t length)
{
    for (size_t i = 0U; i < length; i++)
    {
        const int status = wait_sr1(i2c, I2C_SR1_TXE);
        if (status != BSP_I2C_OK)
        {
            return status;
        }
        i2c->DR = data[i];
    }
    return wait_sr1(i2c, I2C_SR1_BTF);
}

/** @brief Set up the clock control and rise time registers for @p freq_hz. */
static void i2c_configure(i2c_regs_t* i2c, uint32_t freq_hz)
{
    const uint32_t pclk_mhz = BSP_PCLK1_HZ / 1000000UL;

    i2c->CR1 = I2C_CR1_SWRST; /* clear any latched bus error state */
    i2c->CR1 = 0U;

    i2c->CR2 = pclk_mhz & I2C_CR2_FREQ_MSK;

    if (freq_hz <= 100000UL)
    {
        /* Standard mode: SCL high and low are each CCR periods of PCLK1. */
        uint32_t ccr = BSP_PCLK1_HZ / (freq_hz * 2UL);
        if (ccr < 4U)
        {
            ccr = 4U;
        }
        i2c->CCR = ccr & I2C_CCR_CCR_MSK;
        i2c->TRISE = pclk_mhz + 1U; /* 1000 ns max rise time */
    }
    else
    {
        /* Fast mode with a 2:1 duty cycle: one period is 3 * CCR. */
        uint32_t ccr = BSP_PCLK1_HZ / (freq_hz * 3UL);
        if (ccr < 1U)
        {
            ccr = 1U;
        }
        i2c->CCR = I2C_CCR_FS | (ccr & I2C_CCR_CCR_MSK);
        i2c->TRISE = ((pclk_mhz * 300UL) / 1000UL) + 1U; /* 300 ns max rise time */
    }

    i2c->CR1 = I2C_CR1_PE;
}

void bsp_i2c_init(void)
{
    /* Open drain with pull-ups; most breakout boards fit their own as well. */
#if BSP_USE_I2C1
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_I2C1_SCL_PORT, BSP_I2C1_SCL_PIN, BSP_I2C1_SCL_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_I2C1_SDA_PORT, BSP_I2C1_SDA_PIN, BSP_I2C1_SDA_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    i2c_configure(I2C1, BSP_I2C1_FREQ_HZ);
#endif

#if BSP_USE_I2C2
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_I2C2_SCL_PORT, BSP_I2C2_SCL_PIN, BSP_I2C2_SCL_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_I2C2_SDA_PORT, BSP_I2C2_SDA_PIN, BSP_I2C2_SDA_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    i2c_configure(I2C2, BSP_I2C2_FREQ_HZ);
#endif

#if BSP_USE_I2C3
    RCC->APB1ENR |= RCC_APB1ENR_I2C3EN;
    (void)RCC->APB1ENR;
    bsp_gpio_config_alternate(BSP_I2C3_SCL_PORT, BSP_I2C3_SCL_PIN, BSP_I2C3_SCL_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    bsp_gpio_config_alternate(BSP_I2C3_SDA_PORT, BSP_I2C3_SDA_PIN, BSP_I2C3_SDA_AF,
                              GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_HIGH, GPIO_PUPD_UP);
    i2c_configure(I2C3, BSP_I2C3_FREQ_HZ);
#endif
}

int bsp_i2c_write(i2c_regs_t* i2c, uint8_t address, const uint8_t* data, size_t length)
{
    int status = wait_not_busy(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_start(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_address(i2c, address, 0U);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = write_bytes(i2c, data, length);
    i2c->CR1 |= I2C_CR1_STOP;
    return status;
}

/**
 * @brief Address and receive phase of a master read.
 *
 * The tail of an I2C read is the fiddly part: the NACK for the last byte has
 * to be armed before that byte is clocked in, and the hardware only gives you
 * two bytes of buffering. RM0383 sec. 18.3.3 therefore specifies three
 * different sequences depending on the length, all of which are handled here.
 *
 * @param check_busy false when we already own the bus and are issuing a
 *                   repeated START, in which case BUSY is legitimately set.
 */
static int i2c_read_impl(i2c_regs_t* i2c, uint8_t address, uint8_t* data, size_t length,
                         bool check_busy)
{
    if (length == 0U)
    {
        return BSP_I2C_OK;
    }

    if (check_busy)
    {
        const int status = wait_not_busy(i2c);
        if (status != BSP_I2C_OK)
        {
            return status;
        }
    }

    i2c->CR1 &= ~I2C_CR1_POS;
    i2c->CR1 |= I2C_CR1_ACK;

    if (length == 2U)
    {
        /* POS makes the ACK bit apply to the byte after next, which is what
         * lets us NACK the second byte before it has been received. */
        i2c->CR1 |= I2C_CR1_POS;
    }

    int status = send_start(i2c);
    if (status != BSP_I2C_OK)
    {
        i2c->CR1 &= ~I2C_CR1_POS;
        return status;
    }

    i2c->DR = (uint32_t)((address << 1U) | 1U);

    status = wait_sr1(i2c, I2C_SR1_ADDR);
    if (status != BSP_I2C_OK)
    {
        i2c->CR1 &= ~I2C_CR1_POS;
        return status;
    }

    size_t index = 0U;

    if (length == 1U)
    {
        /* NACK has to be armed while the address phase still stretches SCL. */
        i2c->CR1 &= ~I2C_CR1_ACK;
        (void)i2c->SR2; /* clears ADDR and releases the clock */
        i2c->CR1 |= I2C_CR1_STOP;

        status = wait_sr1(i2c, I2C_SR1_RXNE);
        if (status != BSP_I2C_OK)
        {
            return status;
        }
        data[index] = (uint8_t)i2c->DR;
        return BSP_I2C_OK;
    }

    if (length == 2U)
    {
        i2c->CR1 &= ~I2C_CR1_ACK;
        (void)i2c->SR2;

        status = wait_sr1(i2c, I2C_SR1_BTF);
        if (status != BSP_I2C_OK)
        {
            i2c->CR1 &= ~I2C_CR1_POS;
            return status;
        }
        i2c->CR1 |= I2C_CR1_STOP;
        data[index++] = (uint8_t)i2c->DR;
        data[index] = (uint8_t)i2c->DR;
        i2c->CR1 &= ~I2C_CR1_POS;
        return BSP_I2C_OK;
    }

    (void)i2c->SR2;

    /* Stream everything except the last three bytes. */
    while ((length - index) > 3U)
    {
        status = wait_sr1(i2c, I2C_SR1_RXNE);
        if (status != BSP_I2C_OK)
        {
            return status;
        }
        data[index++] = (uint8_t)i2c->DR;
    }

    /* Three left: BTF means N-2 is in DR and N-1 is in the shift register, so
     * clearing ACK now sends the NACK on byte N. */
    status = wait_sr1(i2c, I2C_SR1_BTF);
    if (status != BSP_I2C_OK)
    {
        return status;
    }
    i2c->CR1 &= ~I2C_CR1_ACK;
    data[index++] = (uint8_t)i2c->DR;

    /* Wait for the second BTF before stopping. Setting STOP while N is still
     * being clocked in would cut the last byte short. */
    status = wait_sr1(i2c, I2C_SR1_BTF);
    if (status != BSP_I2C_OK)
    {
        return status;
    }
    i2c->CR1 |= I2C_CR1_STOP;
    data[index++] = (uint8_t)i2c->DR;
    data[index] = (uint8_t)i2c->DR;
    return BSP_I2C_OK;
}

int bsp_i2c_read(i2c_regs_t* i2c, uint8_t address, uint8_t* data, size_t length)
{
    return i2c_read_impl(i2c, address, data, length, true);
}

int bsp_i2c_write_read(i2c_regs_t* i2c, uint8_t address, const uint8_t* reg_bytes,
                       size_t reg_length, uint8_t* data, size_t length)
{
    int status = wait_not_busy(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_start(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_address(i2c, address, 0U);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = write_bytes(i2c, reg_bytes, reg_length);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    if (length == 0U)
    {
        /* Nothing to read, so release the bus instead of holding it. */
        i2c->CR1 |= I2C_CR1_STOP;
        return BSP_I2C_OK;
    }

    /* Repeated START keeps the bus, so no other master can interleave and the
     * BUSY flag is expected to still be set. */
    return i2c_read_impl(i2c, address, data, length, false);
}

int bsp_i2c_write_reg(i2c_regs_t* i2c, uint8_t address, uint8_t reg, uint8_t value)
{
    const uint8_t payload[2] = {reg, value};
    return bsp_i2c_write(i2c, address, payload, sizeof(payload));
}

int bsp_i2c_read_reg(i2c_regs_t* i2c, uint8_t address, uint8_t reg, uint8_t* value)
{
    return bsp_i2c_write_read(i2c, address, &reg, 1U, value, 1U);
}

int bsp_i2c_ping(i2c_regs_t* i2c, uint8_t address)
{
    int status = wait_not_busy(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_start(i2c);
    if (status != BSP_I2C_OK)
    {
        return status;
    }

    status = send_address(i2c, address, 0U);
    i2c->CR1 |= I2C_CR1_STOP;
    return status;
}

#endif /* any I2C */
