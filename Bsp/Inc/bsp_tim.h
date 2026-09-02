/**
 * @file    bsp_tim.h
 * @brief   TIM1..TIM5 and TIM9..TIM11 as time bases and PWM generators.
 *
 * Prescaler and period come from bsp_config.h, so a timer enabled there is
 * already counting after bsp_init(). PWM output pins are not routed
 * automatically because most channels have more than one possible pin - call
 * bsp_gpio_config_alternate() yourself, then bsp_tim_pwm_config().
 *
 * Verified channel pins on the UFQFPN48 package:
 *   TIM1  AF1: CH1 PA8,  CH2 PA9,  CH3 PA10, CH4 PA11
 *   TIM2  AF1: CH1 PA0/PA5/PA15, CH2 PA1/PB3, CH3 PA2/PB10, CH4 PA3
 *   TIM3  AF2: CH1 PA6/PB4, CH2 PA7/PB5, CH3 PB0, CH4 PB1
 *   TIM4  AF2: CH1 PB6,  CH2 PB7,  CH3 PB8,  CH4 PB9
 *   TIM5  AF2: CH1 PA0,  CH2 PA1,  CH3 PA2,  CH4 PA3
 *   TIM9  AF3: CH1 PA2,  CH2 PA3
 *   TIM10 AF3: CH1 PB8
 *   TIM11 AF3: CH1 PB9
 */
#ifndef BSP_TIM_H
#define BSP_TIM_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if BSP_USE_TIM1 || BSP_USE_TIM2 || BSP_USE_TIM3 || BSP_USE_TIM4 || BSP_USE_TIM5 ||                \
    BSP_USE_TIM9 || BSP_USE_TIM10 || BSP_USE_TIM11

/** @brief Bring up and start every timer enabled in bsp_config.h. */
void bsp_tim_init(void);

/**
 * @brief Configure a timer's time base and start it.
 * @param tim       Any TIMx.
 * @param prescaler Written to PSC; the counter clock is timer clock/(PSC+1).
 * @param period    Written to ARR; the counter wraps after period+1 ticks.
 */
void bsp_tim_config(tim_regs_t* tim, uint32_t prescaler, uint32_t period);

/** @brief Start the counter. */
static inline void bsp_tim_start(tim_regs_t* tim)
{
    tim->CR1 |= TIM_CR1_CEN;
}

/** @brief Stop the counter, leaving its value intact. */
static inline void bsp_tim_stop(tim_regs_t* tim)
{
    tim->CR1 &= ~TIM_CR1_CEN;
}

/** @brief Current counter value. */
static inline uint32_t bsp_tim_counter(const tim_regs_t* tim)
{
    return tim->CNT;
}

/** @brief Change the wrap point. Takes effect at the next update event. */
static inline void bsp_tim_set_period(tim_regs_t* tim, uint32_t period)
{
    tim->ARR = period;
}

/**
 * @brief Enable the update interrupt so bsp_tim_callback() fires on overflow.
 * @param priority NVIC priority, 0 (highest) to 15.
 */
void bsp_tim_enable_update_irq(tim_regs_t* tim, uint32_t priority);

/**
 * @brief Set a channel up for PWM and enable its output.
 * @param tim      Any TIMx.
 * @param channel  1..4 (TIM9 has 1..2, TIM10/TIM11 only 1).
 * @param duty     Initial compare value, in the same units as the period.
 * @param inverted false for PWM mode 1 (high while CNT < CCR), true for mode 2.
 */
void bsp_tim_pwm_config(tim_regs_t* tim, uint32_t channel, uint32_t duty, bool inverted);

/** @brief Change a PWM channel's compare value. Cheap enough to call often. */
void bsp_tim_pwm_set(tim_regs_t* tim, uint32_t channel, uint32_t duty);

/**
 * @brief Update callback, invoked from every enabled timer's interrupt.
 *
 * Weak and empty by default; define it in your application to override.
 * @param tim The timer that overflowed.
 */
void bsp_tim_callback(tim_regs_t* tim);

#endif /* any timer */

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIM_H */
