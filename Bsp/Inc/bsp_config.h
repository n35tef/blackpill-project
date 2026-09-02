/**
 * @file    bsp_config.h
 * @brief   The one file you edit to configure the board.
 *
 * Everything in this BSP is driven from here: the clock tree, which
 * peripherals get compiled in, and their settings. Nothing else in Bsp/ needs
 * to be touched to enable or disable a peripheral - each module compiles to
 * nothing when its BSP_USE_* switch is 0.
 *
 * Target: WeAct BlackPill v3.x, STM32F411CEU6, 25 MHz HSE crystal.
 */
#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

/* ========================================================================= */
/* Clock tree                                                                */
/* ========================================================================= */

/** System clock source selectors for BSP_CLOCK_SOURCE. */
#define BSP_CLOCK_SOURCE_HSI     0 /**< 16 MHz internal RC, no PLL          */
#define BSP_CLOCK_SOURCE_HSE     1 /**< external crystal directly, no PLL   */
#define BSP_CLOCK_SOURCE_HSI_PLL 2 /**< PLL fed from HSI                    */
#define BSP_CLOCK_SOURCE_HSE_PLL 3 /**< PLL fed from the crystal (default)  */

#define BSP_CLOCK_SOURCE BSP_CLOCK_SOURCE_HSE_PLL

/** Crystal fitted on the board. WeAct BlackPill v3.x ships a 25 MHz part. */
#define BSP_HSE_FREQ_HZ 25000000UL

/** Set to 1 if an oscillator module drives OSC_IN instead of a crystal. */
#define BSP_HSE_BYPASS 0

/**
 * PLL dividers. VCO input = source / PLLM must land in 1..2 MHz (2 MHz is
 * recommended); VCO output = VCO input * PLLN must land in 100..432 MHz.
 *
 * Defaults give 25 MHz / 25 * 192 / 2 = 96 MHz SYSCLK and a 48 MHz PLLQ
 * output, which is exactly what USB full-speed needs.
 */
#define BSP_PLL_M 25U  /**< 2..63                           */
#define BSP_PLL_N 192U /**< 50..432                         */
#define BSP_PLL_P 2U   /**< 2, 4, 6 or 8                    */
#define BSP_PLL_Q 4U   /**< 2..15; USB/SDIO clock = VCO / Q */

/** Bus prescalers. AHB: 1..512, APB: 1, 2, 4, 8 or 16. */
#define BSP_AHB_PRESCALER  1U
#define BSP_APB1_PRESCALER 2U /**< max 50 MHz  */
#define BSP_APB2_PRESCALER 1U /**< max 100 MHz */

/** Low-speed oscillators, needed by the RTC and the independent watchdog. */
#define BSP_USE_LSI         0 /**< ~32 kHz internal RC */
#define BSP_USE_LSE         0 /**< 32.768 kHz crystal  */

/** Flash prefetch buffer and instruction/data caches. */
#define BSP_FLASH_PREFETCH 1
#define BSP_FLASH_ICACHE   1
#define BSP_FLASH_DCACHE   1

/* ========================================================================= */
/* SysTick                                                                   */
/* ========================================================================= */

/** 1 = run SysTick as a millisecond tick, providing bsp_tick()/bsp_delay_ms(). */
#define BSP_USE_SYSTICK     1
#define BSP_SYSTICK_FREQ_HZ  1000U
#define BSP_SYSTICK_PRIORITY 15U /**< 0 = highest, 15 = lowest */

/* ========================================================================= */
/* Board I/O                                                                 */
/* ========================================================================= */

/** On-board LED: PC13, active low. */
#define BSP_USE_LED         1
#define BSP_LED_PORT       GPIOC
#define BSP_LED_PIN        13U
#define BSP_LED_ACTIVE_LOW 1

/** On-board user key: PA0, pulled up, pressed = low. */
#define BSP_USE_BUTTON      1
#define BSP_BUTTON_PORT       GPIOA
#define BSP_BUTTON_PIN        0U
#define BSP_BUTTON_ACTIVE_LOW 1
#define BSP_BUTTON_PULL       GPIO_PUPD_UP

/* ========================================================================= */
/* Peripherals                                                               */
/* ========================================================================= */
/* Each switch pulls in exactly one Bsp/Src module. Turning one off removes  */
/* its code, its interrupt handlers and its RAM usage from the image.        */

/* ---- DMA ---------------------------------------------------------------- */
#define BSP_USE_DMA1        0
#define BSP_USE_DMA2        0

/* ---- SPI ---------------------------------------------------------------- */
#define BSP_USE_SPI1        0
#define BSP_USE_SPI2        0
#define BSP_USE_SPI3        0
#define BSP_USE_SPI4        0
#define BSP_USE_SPI5        0

/* ---- I2C ---------------------------------------------------------------- */
#define BSP_USE_I2C1        0
#define BSP_USE_I2C2        0
#define BSP_USE_I2C3        0

/* ---- USART -------------------------------------------------------------- */
#define BSP_USE_USART1      0
#define BSP_USE_USART2      0
#define BSP_USE_USART6      0

/* ---- Timers ------------------------------------------------------------- */
#define BSP_USE_TIM1        0
#define BSP_USE_TIM2        0
#define BSP_USE_TIM3        0
#define BSP_USE_TIM4        0
#define BSP_USE_TIM5        0
#define BSP_USE_TIM9        0
#define BSP_USE_TIM10       0
#define BSP_USE_TIM11       0

/* ---- Analog and misc ---------------------------------------------------- */
#define BSP_USE_ADC1        0
#define BSP_USE_RTC         0
#define BSP_USE_IWDG        0
#define BSP_USE_WWDG        0
#define BSP_USE_CRC         0
#define BSP_USE_EXTI        0

/* ---- USB and SD card ---------------------------------------------------- */
/* Both run from the 48 MHz PLL Q output, so they need a PLL clock source.   */
#define BSP_USE_USB_CDC     0 /**< virtual COM port on the USB-C connector   */
#define BSP_USE_SDIO        0 /**< SD card over the SDIO peripheral          */

/* ========================================================================= */
/* Per-peripheral settings                                                   */
/* ========================================================================= */
/* Only read when the matching BSP_USE_* switch above is 1.                  */

/* ---- SPI ----------------------------------------------------------------- */
/*                                                                           */
/* Pins are given as PORT / PIN / AF triplets so you can move a bus without   */
/* touching any driver code. The comment on each block lists every option the */
/* UFQFPN48 package actually bonds out - anything not listed is unavailable   */
/* on the BlackPill. BAUD_DIV is the APB divider and must be a power of two   */
/* between 2 and 256. USE_MISO 0 makes the bus transmit-only and frees the    */
/* MISO pin for something else.                                              */

/* SCK PA5 or PB3 | MISO PA6 or PB4 | MOSI PA7 or PB5 | NSS PA4 or PA15, all AF5. */
#define BSP_SPI1_BAUD_DIV   8U
#define BSP_SPI1_CPOL       0U
#define BSP_SPI1_CPHA       0U
#define BSP_SPI1_DATA_16BIT 0U
#define BSP_SPI1_LSB_FIRST  0U
#define BSP_SPI1_USE_MISO   1U
#define BSP_SPI1_SCK_PORT   GPIOA
#define BSP_SPI1_SCK_PIN    5U
#define BSP_SPI1_SCK_AF     5U
#define BSP_SPI1_MISO_PORT  GPIOA
#define BSP_SPI1_MISO_PIN   6U
#define BSP_SPI1_MISO_AF    5U
#define BSP_SPI1_MOSI_PORT  GPIOA
#define BSP_SPI1_MOSI_PIN   7U
#define BSP_SPI1_MOSI_AF    5U

/* SCK PB13 or PB10 | MISO PB14 only | MOSI PB15 only | NSS PB12 or PB9, all AF5. */
#define BSP_SPI2_BAUD_DIV   8U
#define BSP_SPI2_CPOL       0U
#define BSP_SPI2_CPHA       0U
#define BSP_SPI2_DATA_16BIT 0U
#define BSP_SPI2_LSB_FIRST  0U
#define BSP_SPI2_USE_MISO   1U
#define BSP_SPI2_SCK_PORT   GPIOB
#define BSP_SPI2_SCK_PIN    13U
#define BSP_SPI2_SCK_AF     5U
#define BSP_SPI2_MISO_PORT  GPIOB
#define BSP_SPI2_MISO_PIN   14U
#define BSP_SPI2_MISO_AF    5U
#define BSP_SPI2_MOSI_PORT  GPIOB
#define BSP_SPI2_MOSI_PIN   15U
#define BSP_SPI2_MOSI_AF    5U

/* SCK PB3 (AF6) or PB12 (AF7!) | MISO PB4 AF6 | MOSI PB5 AF6 | NSS PA4/PA15 AF6. */
#define BSP_SPI3_BAUD_DIV   8U
#define BSP_SPI3_CPOL       0U
#define BSP_SPI3_CPHA       0U
#define BSP_SPI3_DATA_16BIT 0U
#define BSP_SPI3_LSB_FIRST  0U
#define BSP_SPI3_USE_MISO   1U
#define BSP_SPI3_SCK_PORT   GPIOB
#define BSP_SPI3_SCK_PIN    3U
#define BSP_SPI3_SCK_AF     6U
#define BSP_SPI3_MISO_PORT  GPIOB
#define BSP_SPI3_MISO_PIN   4U
#define BSP_SPI3_MISO_AF    6U
#define BSP_SPI3_MOSI_PORT  GPIOB
#define BSP_SPI3_MOSI_PIN   5U
#define BSP_SPI3_MOSI_AF    6U

/* Only one pin each, and the AFs differ: SCK PB13 AF6, MISO PA11 AF6,        */
/* MOSI PA1 AF5, NSS PB12 AF6. PA11 is also USB D-, so SPI4 and USB conflict. */
#define BSP_SPI4_BAUD_DIV   8U
#define BSP_SPI4_CPOL       0U
#define BSP_SPI4_CPHA       0U
#define BSP_SPI4_DATA_16BIT 0U
#define BSP_SPI4_LSB_FIRST  0U
#define BSP_SPI4_USE_MISO   1U
#define BSP_SPI4_SCK_PORT   GPIOB
#define BSP_SPI4_SCK_PIN    13U
#define BSP_SPI4_SCK_AF     6U
#define BSP_SPI4_MISO_PORT  GPIOA
#define BSP_SPI4_MISO_PIN   11U
#define BSP_SPI4_MISO_AF    6U
#define BSP_SPI4_MOSI_PORT  GPIOA
#define BSP_SPI4_MOSI_PIN   1U
#define BSP_SPI4_MOSI_AF    5U

/* SCK PB0 | MISO PA12 | MOSI PA10 or PB8 | NSS PB1, all AF6. PA12 is USB D+. */
#define BSP_SPI5_BAUD_DIV   8U
#define BSP_SPI5_CPOL       0U
#define BSP_SPI5_CPHA       0U
#define BSP_SPI5_DATA_16BIT 0U
#define BSP_SPI5_LSB_FIRST  0U
#define BSP_SPI5_USE_MISO   1U
#define BSP_SPI5_SCK_PORT   GPIOB
#define BSP_SPI5_SCK_PIN    0U
#define BSP_SPI5_SCK_AF     6U
#define BSP_SPI5_MISO_PORT  GPIOA
#define BSP_SPI5_MISO_PIN   12U
#define BSP_SPI5_MISO_AF    6U
#define BSP_SPI5_MOSI_PORT  GPIOA
#define BSP_SPI5_MOSI_PIN   10U
#define BSP_SPI5_MOSI_AF    6U

/* ---- USART --------------------------------------------------------------- */

/* TX PA9, PB6 or PA15 | RX PA10, PB7 or PB3, all AF7. */
#define BSP_USART1_BAUD    115200UL
#define BSP_USART1_TX_PORT GPIOA
#define BSP_USART1_TX_PIN  9U
#define BSP_USART1_TX_AF   7U
#define BSP_USART1_RX_PORT GPIOA
#define BSP_USART1_RX_PIN  10U
#define BSP_USART1_RX_AF   7U

/* TX PA2, RX PA3, AF7. The usual alternatives PD5/PD6 are not bonded here. */
#define BSP_USART2_BAUD    115200UL
#define BSP_USART2_TX_PORT GPIOA
#define BSP_USART2_TX_PIN  2U
#define BSP_USART2_TX_AF   7U
#define BSP_USART2_RX_PORT GPIOA
#define BSP_USART2_RX_PIN  3U
#define BSP_USART2_RX_AF   7U

/* TX PA11, RX PA12, AF8 - the only option, and it collides with USB. */
#define BSP_USART6_BAUD    115200UL
#define BSP_USART6_TX_PORT GPIOA
#define BSP_USART6_TX_PIN  11U
#define BSP_USART6_TX_AF   8U
#define BSP_USART6_RX_PORT GPIOA
#define BSP_USART6_RX_PIN  12U
#define BSP_USART6_RX_AF   8U

/** Send printf()/stdout to this USART. 0 = leave stdout unconnected. */
#define BSP_STDOUT_USART 0

/* ---- I2C ----------------------------------------------------------------- */
/* FREQ_HZ up to 100000 selects standard mode, above that fast mode.          */

/* SCL PB6 or PB8, SDA PB7 or PB9, all AF4. */
#define BSP_I2C1_FREQ_HZ  100000UL
#define BSP_I2C1_SCL_PORT GPIOB
#define BSP_I2C1_SCL_PIN  6U
#define BSP_I2C1_SCL_AF   4U
#define BSP_I2C1_SDA_PORT GPIOB
#define BSP_I2C1_SDA_PIN  7U
#define BSP_I2C1_SDA_AF   4U

/* SCL PB10 AF4 is the only choice. SDA must be PB3 or PB9 and uses AF9,     */
/* not AF4: the usual PB11 is not bonded on this package.                    */
#define BSP_I2C2_FREQ_HZ  100000UL
#define BSP_I2C2_SCL_PORT GPIOB
#define BSP_I2C2_SCL_PIN  10U
#define BSP_I2C2_SCL_AF   4U
#define BSP_I2C2_SDA_PORT GPIOB
#define BSP_I2C2_SDA_PIN  3U
#define BSP_I2C2_SDA_AF   9U

/* SCL PA8 AF4 only. SDA PB4 or PB8, AF9. */
#define BSP_I2C3_FREQ_HZ  100000UL
#define BSP_I2C3_SCL_PORT GPIOA
#define BSP_I2C3_SCL_PIN  8U
#define BSP_I2C3_SCL_AF   4U
#define BSP_I2C3_SDA_PORT GPIOB
#define BSP_I2C3_SDA_PIN  4U
#define BSP_I2C3_SDA_AF   9U

/* ---- Timers (period = (PSC+1)*(ARR+1) / timer clock) -------------------- */
/* TIM2 and TIM5 are 32-bit; the rest wrap at 0xFFFF.                        */
#define BSP_TIM1_PRESCALER  0U
#define BSP_TIM1_PERIOD     0xFFFFU
#define BSP_TIM2_PRESCALER  0U
#define BSP_TIM2_PERIOD     0xFFFFFFFFU
#define BSP_TIM3_PRESCALER  0U
#define BSP_TIM3_PERIOD     0xFFFFU
#define BSP_TIM4_PRESCALER  0U
#define BSP_TIM4_PERIOD     0xFFFFU
#define BSP_TIM5_PRESCALER  0U
#define BSP_TIM5_PERIOD     0xFFFFFFFFU
#define BSP_TIM9_PRESCALER  0U
#define BSP_TIM9_PERIOD     0xFFFFU
#define BSP_TIM10_PRESCALER 0U
#define BSP_TIM10_PERIOD    0xFFFFU
#define BSP_TIM11_PRESCALER 0U
#define BSP_TIM11_PERIOD    0xFFFFU

/** 1 = raise the update interrupt on every overflow, calling bsp_tim_callback(). */
#define BSP_TIM1_UPDATE_IRQ  0
#define BSP_TIM2_UPDATE_IRQ  0
#define BSP_TIM3_UPDATE_IRQ  0
#define BSP_TIM4_UPDATE_IRQ  0
#define BSP_TIM5_UPDATE_IRQ  0
#define BSP_TIM9_UPDATE_IRQ  0
#define BSP_TIM10_UPDATE_IRQ 0
#define BSP_TIM11_UPDATE_IRQ 0

/* ---- ADC1 --------------------------------------------------------------- */
/* Usable channels on this package: IN0..IN7 on PA0..PA7, IN8/IN9 on PB0/PB1, */
/* IN17 VREFINT and IN18 shared by the temperature sensor and VBAT.           */
#define BSP_ADC1_RESOLUTION  12U /**< 12, 10, 8 or 6 bits              */
#define BSP_ADC1_SAMPLE_TIME 3U  /**< 3,15,28,56,84,112,144,480 cycles */
#define BSP_ADC1_PRESCALER   4U  /**< PCLK2 divider: 2, 4, 6 or 8      */

/* ---- Watchdogs ---------------------------------------------------------- */
/* The IWDG runs from the LSI, so BSP_USE_LSI must be on. The WWDG is not     */
/* started by bsp_init(); call bsp_wwdg_init() yourself with a window.        */
#define BSP_IWDG_TIMEOUT_MS 1000UL

/* ---- RTC ---------------------------------------------------------------- */
#define BSP_RTC_CLOCK_LSE 1 /**< 1 = LSE (needs BSP_USE_LSE), 0 = LSI */

/* ---- USB CDC-ACM (virtual COM port) ------------------------------------- */
/* D- PA11 and D+ PA12 are fixed (AF10) and shared with USART6 and with       */
/* SPI4 MISO / SPI5 MISO. The BlackPill has no VBUS divider, so VBUS sensing  */
/* is off and PA9 stays free. The core needs PLLQ = 48 MHz exactly.          */
#define BSP_USB_VID            0x1209U /**< pid.codes test VID: not for products */
#define BSP_USB_PID            0x0001U /**< pid.codes test PID                   */
#define BSP_USB_DEVICE_VERSION 0x0100U /**< bcdDevice 1.00                       */
#define BSP_USB_MANUFACTURER   "BlackPill BSP"
#define BSP_USB_PRODUCT        "STM32F411 Virtual COM Port"
#define BSP_USB_SERIAL_FROM_UID 1      /**< 1 = 24 hex digits from the chip's unique ID */
#define BSP_USB_SERIAL         "0001"  /**< used when SERIAL_FROM_UID is 0        */
#define BSP_USB_SELF_POWERED   0       /**< 0 = bus powered (BlackPill on USB)    */
#define BSP_USB_MAX_POWER_MA   100U    /**< reported to the host, 2..500          */
#define BSP_USB_CDC_RX_BUFFER  512U    /**< power of two, >= 128                  */
#define BSP_USB_CDC_TX_BUFFER  512U    /**< power of two, >= 64                   */
#define BSP_USB_CDC_TX_TIMEOUT_MS 50UL /**< give up on a host that stops reading  */
#define BSP_USB_IRQ_PRIORITY   8U      /**< OTG_FS interrupt, 0 = highest         */
#define BSP_USB_SOF_IRQ        0       /**< 1 = take an interrupt every 1 ms frame */

/** Send printf()/stdout to the virtual COM port (excludes BSP_STDOUT_USART). */
#define BSP_STDOUT_USB 0

/* ---- SDIO / SD card ----------------------------------------------------- */
/* AF12 options on this package: CK PB15 | CMD PA6 | D0 PB4 or PB7 | D1 PA8   */
/* D2 PA9 | D3 PB5 (D4 PB8, D5 PB9, D6 PB14, D7 PB10 exist but 8-bit SD is    */
/* not implemented). The defaults collide with SPI1 MISO (PA6), SPI2 MOSI    */
/* (PB15), SPI3 MISO/MOSI (PB4/PB5), USART1 TX (PA9) and I2C3 SDA (PB4).      */
/* 1-bit mode frees PA8, PA9 and PB5. CLK_HZ is the bus clock after the card  */
/* is identified: SDIO_CK = 48 MHz / (n + 2), n = 0..255, and it must not     */
/* exceed HCLK / 2 or 25 MHz (default-speed cards); 24 MHz is the practical   */
/* maximum. The card is always identified at 400 kHz first.                  */
#define BSP_SDIO_BUS_WIDTH 4U        /**< 1 or 4 data lines              */
#define BSP_SDIO_CLK_HZ    24000000UL /**< data transfer clock            */
#define BSP_SDIO_CK_PORT   GPIOB
#define BSP_SDIO_CK_PIN    15U
#define BSP_SDIO_CMD_PORT  GPIOA
#define BSP_SDIO_CMD_PIN   6U
#define BSP_SDIO_D0_PORT   GPIOB
#define BSP_SDIO_D0_PIN    4U
#define BSP_SDIO_D1_PORT   GPIOA
#define BSP_SDIO_D1_PIN    8U
#define BSP_SDIO_D2_PORT   GPIOA
#define BSP_SDIO_D2_PIN    9U
#define BSP_SDIO_D3_PORT   GPIOB
#define BSP_SDIO_D3_PIN    5U

#endif /* BSP_CONFIG_H */
