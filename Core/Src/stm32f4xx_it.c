/**
 * @file    stm32f4xx_it.c
 * @brief   Core Cortex-M4 fault/system interrupt handlers.
 *
 * Peripheral IRQ handlers (e.g. DMA1_Stream3/4_IRQHandler) live next to the
 * peripheral they belong to in their own Bsp/Src/bsp_xxx.c module, so that
 * disabling a peripheral in bsp_config.h also removes its handler - nothing
 * to keep in sync here.
 */
#include "stm32f4xx_it.h"
#include "main.h"

void NMI_Handler(void)
{
    while (1)
    {
    }
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void) {}

void DebugMon_Handler(void) {}

void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
