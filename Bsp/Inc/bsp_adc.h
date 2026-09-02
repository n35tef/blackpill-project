/**
 * @file    bsp_adc.h
 * @brief   ADC1 single-conversion reads.
 *
 * Channels 0..7 are on PA0..PA7 and channels 8..9 on PB0..PB1. Channels 10..15
 * live on PC0..PC5, which the UFQFPN48 package does not bond out, so they are
 * unusable on the BlackPill. Channel 17 is the internal reference and channel
 * 18 is shared between the temperature sensor and VBAT.
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_ADC1

/** @brief Power up ADC1 with the resolution and prescaler from bsp_config.h. */
void bsp_adc_init(void);

/**
 * @brief Switch a pin to analog mode so it can be sampled.
 *
 * Only needed for external channels; the internal ones have no pin.
 */
void bsp_adc_config_pin(gpio_regs_t* port, uint32_t pin);

/**
 * @brief Run one conversion and return the raw result.
 * @param channel 0..18. Right aligned, so the range follows the resolution.
 */
uint16_t bsp_adc_read(uint32_t channel);

/** @brief Convert a raw reading to millivolts against a known supply. */
uint32_t bsp_adc_to_millivolts(uint16_t raw, uint32_t vref_mv);

/**
 * @brief Enable the internal temperature sensor and reference channels.
 *
 * They need a settling time after being switched on, and they are mutually
 * exclusive with VBAT because both sit on channel 18.
 */
void bsp_adc_enable_internal_channels(void);

/** @brief Enable the VBAT divider on channel 18, disabling the temp sensor. */
void bsp_adc_enable_vbat(void);

/**
 * @brief Read the die temperature in tenths of a degree Celsius.
 *
 * Uses the datasheet typicals (0.76 V at 25 C, 2.5 mV/C), which are not
 * trimmed per part - expect a couple of degrees of error.
 *
 * @param vref_mv Measured supply voltage in millivolts, usually 3300.
 */
int32_t bsp_adc_read_temperature_decidegrees(uint32_t vref_mv);

#endif /* BSP_USE_ADC1 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_H */
