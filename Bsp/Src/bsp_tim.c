/**
 * @file    bsp_tim.c
 * @brief   TIM1..TIM5 and TIM9..TIM11 as time bases and PWM generators.
 */

#include "bsp_tim.h"

#if BSP_USE_TIM1 || BSP_USE_TIM2 || BSP_USE_TIM3 || BSP_USE_TIM4 || BSP_USE_TIM5 ||                \
    BSP_USE_TIM9 || BSP_USE_TIM10 || BSP_USE_TIM11

void bsp_tim_config(tim_regs_t* tim, uint32_t prescaler, uint32_t period)
{
    tim->CR1 = TIM_CR1_ARPE;
    tim->PSC = prescaler;
    tim->ARR = period;
    tim->EGR = TIM_EGR_UG; /* load the shadow registers immediately */
    tim->SR = 0U;          /* the forced update also set UIF        */
    tim->CR1 |= TIM_CR1_CEN;
}

/** @brief Interrupt number carrying a timer's update event. */
static int32_t update_irq(const tim_regs_t* tim)
{
    if (tim == TIM1)
    {
        return IRQ_TIM1_UP_TIM10;
    }
    if (tim == TIM2)
    {
        return IRQ_TIM2;
    }
    if (tim == TIM3)
    {
        return IRQ_TIM3;
    }
    if (tim == TIM4)
    {
        return IRQ_TIM4;
    }
    if (tim == TIM5)
    {
        return IRQ_TIM5;
    }
    if (tim == TIM9)
    {
        return IRQ_TIM1_BRK_TIM9;
    }
    if (tim == TIM10)
    {
        return IRQ_TIM1_UP_TIM10;
    }
    return IRQ_TIM1_TRG_COM_TIM11; /* TIM11 */
}

void bsp_tim_enable_update_irq(tim_regs_t* tim, uint32_t priority)
{
    tim->SR = ~TIM_SR_UIF;
    tim->DIER |= TIM_DIER_UIE;
    nvic_set_priority(update_irq(tim), priority);
    nvic_enable_irq(update_irq(tim));
}

void bsp_tim_pwm_config(tim_regs_t* tim, uint32_t channel, uint32_t duty, bool inverted)
{
    const uint32_t mode = inverted ? TIM_OCMODE_PWM2 : TIM_OCMODE_PWM1;
    uint32_t enable_bit;

    /* Program the channel with its output still disconnected, so nothing
     * driven by the pin sees a partially configured compare unit. */
    switch (channel)
    {
        case 1U:
            tim->CCER &= ~TIM_CCER_CC1E;
            tim->CCMR1 = (tim->CCMR1 & ~TIM_CCMR1_OC1M_MSK) |
                         (mode << TIM_CCMR1_OC1M_POS) | TIM_CCMR1_OC1PE;
            tim->CCR1 = duty;
            enable_bit = TIM_CCER_CC1E;
            break;
        case 2U:
            tim->CCER &= ~TIM_CCER_CC2E;
            tim->CCMR1 = (tim->CCMR1 & ~TIM_CCMR1_OC2M_MSK) |
                         (mode << TIM_CCMR1_OC2M_POS) | TIM_CCMR1_OC2PE;
            tim->CCR2 = duty;
            enable_bit = TIM_CCER_CC2E;
            break;
        case 3U:
            tim->CCER &= ~TIM_CCER_CC3E;
            tim->CCMR2 = (tim->CCMR2 & ~TIM_CCMR2_OC3M_MSK) |
                         (mode << TIM_CCMR2_OC3M_POS) | TIM_CCMR2_OC3PE;
            tim->CCR3 = duty;
            enable_bit = TIM_CCER_CC3E;
            break;
        case 4U:
            tim->CCER &= ~TIM_CCER_CC4E;
            tim->CCMR2 = (tim->CCMR2 & ~TIM_CCMR2_OC4M_MSK) |
                         (mode << TIM_CCMR2_OC4M_POS) | TIM_CCMR2_OC4PE;
            tim->CCR4 = duty;
            enable_bit = TIM_CCER_CC4E;
            break;
        default:
            return;
    }

    /* OCxPE means the compare value only takes effect on an update event, so
     * one has to be forced. URS makes that forced event skip the interrupt
     * path, which would otherwise fire a callback the moment PWM is set up. */
    const uint32_t cr1 = tim->CR1;
    tim->CR1 = cr1 | TIM_CR1_URS;
    tim->EGR = TIM_EGR_UG;
    tim->SR = ~TIM_SR_UIF;
    tim->CR1 = cr1;

    tim->CCER |= enable_bit;

    if (tim == TIM1)
    {
        /* Advanced timers keep their outputs disconnected until MOE is set. */
        tim->BDTR |= TIM_BDTR_MOE;
    }
}

void bsp_tim_pwm_set(tim_regs_t* tim, uint32_t channel, uint32_t duty)
{
    switch (channel)
    {
        case 1U:
            tim->CCR1 = duty;
            break;
        case 2U:
            tim->CCR2 = duty;
            break;
        case 3U:
            tim->CCR3 = duty;
            break;
        case 4U:
            tim->CCR4 = duty;
            break;
        default:
            break;
    }
}

__attribute__((weak)) void bsp_tim_callback(tim_regs_t* tim)
{
    (void)tim;
}

/**
 * @brief Acknowledge an update event and run the callback.
 *
 * UIF latches whether or not the interrupt is enabled, so UIE has to be
 * checked too: TIM1 shares its vector with TIM10, and without this a running
 * but non-interrupting timer would have its callback run from the other
 * timer's interrupt.
 */
__attribute__((unused)) static void handle_update(tim_regs_t* tim)
{
    if (((tim->SR & TIM_SR_UIF) != 0U) && ((tim->DIER & TIM_DIER_UIE) != 0U))
    {
        tim->SR = ~TIM_SR_UIF;
        bsp_tim_callback(tim);
    }
}

void bsp_tim_init(void)
{
#if BSP_USE_TIM1
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;
    bsp_tim_config(TIM1, BSP_TIM1_PRESCALER, BSP_TIM1_PERIOD);
#if BSP_TIM1_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM1, 8U);
#endif
#endif

#if BSP_USE_TIM2
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    bsp_tim_config(TIM2, BSP_TIM2_PRESCALER, BSP_TIM2_PERIOD);
#if BSP_TIM2_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM2, 8U);
#endif
#endif

#if BSP_USE_TIM3
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    bsp_tim_config(TIM3, BSP_TIM3_PRESCALER, BSP_TIM3_PERIOD);
#if BSP_TIM3_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM3, 8U);
#endif
#endif

#if BSP_USE_TIM4
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    (void)RCC->APB1ENR;
    bsp_tim_config(TIM4, BSP_TIM4_PRESCALER, BSP_TIM4_PERIOD);
#if BSP_TIM4_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM4, 8U);
#endif
#endif

#if BSP_USE_TIM5
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    bsp_tim_config(TIM5, BSP_TIM5_PRESCALER, BSP_TIM5_PERIOD);
#if BSP_TIM5_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM5, 8U);
#endif
#endif

#if BSP_USE_TIM9
    RCC->APB2ENR |= RCC_APB2ENR_TIM9EN;
    (void)RCC->APB2ENR;
    bsp_tim_config(TIM9, BSP_TIM9_PRESCALER, BSP_TIM9_PERIOD);
#if BSP_TIM9_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM9, 8U);
#endif
#endif

#if BSP_USE_TIM10
    RCC->APB2ENR |= RCC_APB2ENR_TIM10EN;
    (void)RCC->APB2ENR;
    bsp_tim_config(TIM10, BSP_TIM10_PRESCALER, BSP_TIM10_PERIOD);
#if BSP_TIM10_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM10, 8U);
#endif
#endif

#if BSP_USE_TIM11
    RCC->APB2ENR |= RCC_APB2ENR_TIM11EN;
    (void)RCC->APB2ENR;
    bsp_tim_config(TIM11, BSP_TIM11_PRESCALER, BSP_TIM11_PERIOD);
#if BSP_TIM11_UPDATE_IRQ
    bsp_tim_enable_update_irq(TIM11, 8U);
#endif
#endif
}

/* ------------------------------------------------------------------------ */
/* Interrupt handlers                                                        */
/* ------------------------------------------------------------------------ */
/* TIM1 shares its vectors with TIM9, TIM10 and TIM11, so the handlers below */
/* have to be defined once and check both timers that can reach them. They   */
/* are compiled for any enabled timer, not just those with BSP_TIMx_UPDATE_  */
/* IRQ set, so that calling bsp_tim_enable_update_irq() at runtime cannot     */
/* vector into the default handler.                                          */

#if BSP_USE_TIM1 || BSP_USE_TIM10
void TIM1_UP_TIM10_IRQHandler(void)
{
#if BSP_USE_TIM1
    handle_update(TIM1);
#endif
#if BSP_USE_TIM10
    handle_update(TIM10);
#endif
}
#endif

#if BSP_USE_TIM9
void TIM1_BRK_TIM9_IRQHandler(void)
{
    handle_update(TIM9);
}
#endif

#if BSP_USE_TIM11
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    handle_update(TIM11);
}
#endif

#if BSP_USE_TIM2
void TIM2_IRQHandler(void)
{
    handle_update(TIM2);
}
#endif

#if BSP_USE_TIM3
void TIM3_IRQHandler(void)
{
    handle_update(TIM3);
}
#endif

#if BSP_USE_TIM4
void TIM4_IRQHandler(void)
{
    handle_update(TIM4);
}
#endif

#if BSP_USE_TIM5
void TIM5_IRQHandler(void)
{
    handle_update(TIM5);
}
#endif

#endif /* any timer */
