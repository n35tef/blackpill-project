/**
 * @file    bsp_i2c.h
 * @brief   I2C1..I2C3 as a blocking master.
 *
 * Addresses are passed as 7-bit values (0x00..0x7F); the read/write bit is
 * added internally. Every call returns 0 on success or a negative error code,
 * and always leaves a STOP on the bus so a failed transfer cannot wedge it.
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_I2C1 || BSP_USE_I2C2 || BSP_USE_I2C3

#define BSP_I2C_OK       0  /**< transfer completed              */
#define BSP_I2C_ETIMEOUT -1 /**< a status flag never came up     */
#define BSP_I2C_ENACK    -2 /**< the slave did not acknowledge   */
#define BSP_I2C_EBUS     -3 /**< arbitration lost or bus error   */

/** @brief Bring up every I2C bus enabled in bsp_config.h. */
void bsp_i2c_init(void);

/** @brief Write @p length bytes to @p address. */
int bsp_i2c_write(i2c_regs_t* i2c, uint8_t address, const uint8_t* data, size_t length);

/** @brief Read @p length bytes from @p address. */
int bsp_i2c_read(i2c_regs_t* i2c, uint8_t address, uint8_t* data, size_t length);

/**
 * @brief Write a register pointer, then read back, with a repeated START.
 * @param reg_bytes Pointer/command bytes to send first.
 */
int bsp_i2c_write_read(i2c_regs_t* i2c, uint8_t address, const uint8_t* reg_bytes,
                       size_t reg_length, uint8_t* data, size_t length);

/** @brief Write one byte to an 8-bit register address. */
int bsp_i2c_write_reg(i2c_regs_t* i2c, uint8_t address, uint8_t reg, uint8_t value);

/** @brief Read one byte from an 8-bit register address. */
int bsp_i2c_read_reg(i2c_regs_t* i2c, uint8_t address, uint8_t reg, uint8_t* value);

/** @brief Probe for a device: BSP_I2C_OK if something acknowledges. */
int bsp_i2c_ping(i2c_regs_t* i2c, uint8_t address);

#endif /* any I2C */

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */
