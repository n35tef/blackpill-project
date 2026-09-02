/**
 * @file    bsp.h
 * @brief   Board support entry point.
 *
 * Include this from application code; it pulls in every module enabled in
 * bsp_config.h and nothing else.
 */
#ifndef BSP_H
#define BSP_H

#include "bsp_config.h"

#include "bsp_clock.h"
#include "bsp_gpio.h"
#include "bsp_systick.h"

#include "bsp_adc.h"
#include "bsp_dma.h"
#include "bsp_exti.h"
#include "bsp_i2c.h"
#include "bsp_misc.h"
#include "bsp_spi.h"
#include "bsp_tim.h"
#include "bsp_usart.h"

#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Result codes returned by bsp_init(). */
typedef enum
{
    BSP_OK = 0,
    BSP_ERR_CLOCK = 1,
    BSP_ERR_SYSTICK = 2,
    BSP_ERR_PERIPHERAL = 3,
} bsp_status_t;

/**
 * @brief Bring up the clock tree, the board I/O and every enabled peripheral.
 *
 * Call this as the first statement of main().
 */
bsp_status_t bsp_init(void);

/**
 * @brief Unrecoverable error trap.
 *
 * Declared weak: define your own to blink a code, log, or reset instead of
 * spinning forever with interrupts disabled.
 */
void bsp_error_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
