/**
 * @file    bsp_adc.c
 * @brief   ADC1 single-conversion reads.
 */

#include "bsp_adc.h"

#include "bsp_gpio.h"

#if BSP_USE_ADC1

/* Map the human-readable config values onto register encodings. */
#if BSP_ADC1_RESOLUTION == 12
#define ADC_RESOLUTION_BITS ADC_RES_12BIT
#define ADC_FULL_SCALE      4095U
#elif BSP_ADC1_RESOLUTION == 10
#define ADC_RESOLUTION_BITS ADC_RES_10BIT
#define ADC_FULL_SCALE      1023U
#elif BSP_ADC1_RESOLUTION == 8
#define ADC_RESOLUTION_BITS ADC_RES_8BIT
#define ADC_FULL_SCALE      255U
#elif BSP_ADC1_RESOLUTION == 6
#define ADC_RESOLUTION_BITS ADC_RES_6BIT
#define ADC_FULL_SCALE      63U
#else
#error "BSP_ADC1_RESOLUTION must be 12, 10, 8 or 6"
#endif

#if BSP_ADC1_PRESCALER == 2
#define ADC_PRESCALER_BITS ADC_PRESCALER_DIV2
#elif BSP_ADC1_PRESCALER == 4
#define ADC_PRESCALER_BITS ADC_PRESCALER_DIV4
#elif BSP_ADC1_PRESCALER == 6
#define ADC_PRESCALER_BITS ADC_PRESCALER_DIV6
#elif BSP_ADC1_PRESCALER == 8
#define ADC_PRESCALER_BITS ADC_PRESCALER_DIV8
#else
#error "BSP_ADC1_PRESCALER must be 2, 4, 6 or 8"
#endif

#if BSP_ADC1_SAMPLE_TIME == 3
#define ADC_SAMPLE_BITS ADC_SMP_3CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 15
#define ADC_SAMPLE_BITS ADC_SMP_15CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 28
#define ADC_SAMPLE_BITS ADC_SMP_28CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 56
#define ADC_SAMPLE_BITS ADC_SMP_56CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 84
#define ADC_SAMPLE_BITS ADC_SMP_84CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 112
#define ADC_SAMPLE_BITS ADC_SMP_112CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 144
#define ADC_SAMPLE_BITS ADC_SMP_144CYCLES
#elif BSP_ADC1_SAMPLE_TIME == 480
#define ADC_SAMPLE_BITS ADC_SMP_480CYCLES
#else
#error "BSP_ADC1_SAMPLE_TIME must be 3, 15, 28, 56, 84, 112, 144 or 480"
#endif

/**
 * @brief Write the sample time for a channel into SMPR1/SMPR2.
 *
 * The internal reference and temperature sensor are high impedance and need
 * at least 10 us of sampling, so they always get the longest setting rather
 * than the configured one - at the default 24 MHz ADC clock, 480 cycles is
 * 20 us while the default 3 cycles would be 0.125 us and read nonsense.
 */
static void set_sample_time(uint32_t channel)
{
    const uint32_t smp = (channel >= ADC_CHANNEL_VREFINT) ? ADC_SMP_480CYCLES : ADC_SAMPLE_BITS;

    if (channel <= 9U)
    {
        const uint32_t shift = channel * 3U;
        ADC1->SMPR2 = (ADC1->SMPR2 & ~(0x7UL << shift)) | (smp << shift);
    }
    else
    {
        const uint32_t shift = (channel - 10U) * 3U;
        ADC1->SMPR1 = (ADC1->SMPR1 & ~(0x7UL << shift)) | (smp << shift);
    }
}

void bsp_adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    ADC_COMMON->CCR = (ADC_COMMON->CCR & ~ADC_CCR_ADCPRE_MSK) |
                      (ADC_PRESCALER_BITS << ADC_CCR_ADCPRE_POS);

    ADC1->CR1 = ADC_RESOLUTION_BITS << ADC_CR1_RES_POS;
    ADC1->CR2 = 0U;      /* single conversion, right aligned, software trigger */
    ADC1->SQR1 = 0U;     /* sequence length of one                             */
    ADC1->CR2 = ADC_CR2_ADON;

    /* The analog block needs a few microseconds to stabilise after ADON;
     * converting before that returns garbage. */
    for (volatile uint32_t i = 0U; i < 1000U; i++)
    {
    }
}

void bsp_adc_config_pin(gpio_regs_t* port, uint32_t pin)
{
    bsp_gpio_config_analog(port, pin);
}

uint16_t bsp_adc_read(uint32_t channel)
{
    set_sample_time(channel);
    ADC1->SQR3 = channel & 0x1FU;

    ADC1->SR = 0U;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    while ((ADC1->SR & ADC_SR_EOC) == 0U)
    {
    }

    return (uint16_t)ADC1->DR; /* reading DR also clears EOC */
}

uint32_t bsp_adc_to_millivolts(uint16_t raw, uint32_t vref_mv)
{
    return ((uint32_t)raw * vref_mv) / ADC_FULL_SCALE;
}

void bsp_adc_enable_internal_channels(void)
{
    ADC_COMMON->CCR &= ~ADC_CCR_VBATE; /* VBAT would win the shared channel */
    ADC_COMMON->CCR |= ADC_CCR_TSVREFE;

    /* The sensor needs roughly 10 us to start up; this is comfortably over. */
    for (volatile uint32_t i = 0U; i < 10000U; i++)
    {
    }
}

void bsp_adc_enable_vbat(void)
{
    ADC_COMMON->CCR &= ~ADC_CCR_TSVREFE;
    ADC_COMMON->CCR |= ADC_CCR_VBATE;
}

int32_t bsp_adc_read_temperature_decidegrees(uint32_t vref_mv)
{
    const uint16_t raw = bsp_adc_read(ADC_CHANNEL_TEMPSENSOR);
    const int32_t mv = (int32_t)bsp_adc_to_millivolts(raw, vref_mv);

    /* T = (V - V25) / avg_slope + 25, with V25 = 760 mV and slope 2.5 mV/C. */
    return (((mv - 760) * 10) / 25) + 250;
}

#endif /* BSP_USE_ADC1 */
