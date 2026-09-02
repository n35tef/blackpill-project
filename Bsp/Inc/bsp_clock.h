/**
 * @file    bsp_clock.h
 * @brief   System clock configuration (always enabled - the core needs it).
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Configure the system clock to 100 MHz using the internal HSI
 *        oscillator through the main PLL (no external crystal required).
 */
void bsp_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CLOCK_H */
