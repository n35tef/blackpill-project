/**
 * @file    stm32f411.h
 * @brief   Single include for the STM32F411 device: CPU core, IRQ numbers,
 *          memory map and every peripheral register block.
 *
 * BSP sources should include only this header rather than the individual
 * device/regs headers.
 */
#ifndef DEVICE_STM32F411_H
#define DEVICE_STM32F411_H

#include "cpu/cortex_m4.h"
#include "device/stm32f411_irq.h"
#include "device/stm32f411_memmap.h"

#include "device/regs/adc.h"
#include "device/regs/dma.h"
#include "device/regs/flash.h"
#include "device/regs/gpio.h"
#include "device/regs/i2c.h"
#include "device/regs/misc.h"
#include "device/regs/pwr.h"
#include "device/regs/rcc.h"
#include "device/regs/spi.h"
#include "device/regs/syscfg_exti.h"
#include "device/regs/tim.h"
#include "device/regs/usart.h"

#endif /* DEVICE_STM32F411_H */
