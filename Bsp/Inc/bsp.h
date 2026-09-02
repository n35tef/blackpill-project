/**
 * @file    bsp.h
 * @brief   Top-level BSP init entry point for the BlackPill (STM32F411CEU6).
 */
#ifndef BSP_H
#define BSP_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Initialize HAL, the system clock and every peripheral enabled in
 *        bsp_config.h. Call this once at the start of main().
 */
void bsp_init(void);

/**
 * @brief Called on unrecoverable errors (failed HAL_xxx_Init calls, etc).
 *        Default implementation disables interrupts and loops forever.
 */
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
