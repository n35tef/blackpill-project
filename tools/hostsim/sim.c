/**
 * @file    sim.c
 * @brief   Run the real BSP drivers on the host against a fake peripheral space.
 *
 * The STM32 peripheral region is mapped into this process at its true
 * addresses, so `RCC->CFGR = x` inside the driver writes into ordinary memory
 * that the test can then inspect. A background thread plays the part of the
 * silicon by servicing the handshakes the drivers block on: oscillator ready
 * flags, the clock switch status, peripheral status bits.
 *
 * The point is that the code under test is exactly the code that ships. There
 * are no mocks and nothing is reimplemented, so a wrong shift or a wrong
 * encoding in a driver shows up here rather than on the bench.
 *
 * What this cannot check: anything analogue, anything timing dependent, and
 * any bus protocol detail the fake silicon does not model. See README.md.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bsp.h"

/* ------------------------------------------------------------------------ */
/* Test harness                                                              */
/* ------------------------------------------------------------------------ */
static unsigned tests_run, tests_failed;

static void group(const char* name)
{
    printf("\n[%s]\n", name);
}

static void check_eq(const char* what, uint64_t got, uint64_t expect)
{
    tests_run++;
    if (got == expect)
    {
        printf("  ok    %-44s 0x%llX\n", what, (unsigned long long)got);
    }
    else
    {
        tests_failed++;
        printf("  FAIL  %-44s got 0x%llX want 0x%llX\n", what,
               (unsigned long long)got, (unsigned long long)expect);
    }
}

/** @brief Write a register the firmware may only read. */
#define SIM_POKE(reg, value) (*(volatile uint32_t*)(uintptr_t)&(reg) = (uint32_t)(value))

#define FIELD(reg, msk, pos) (((uint32_t)(reg) & (msk)) >> (pos))

/* SMP encoding for the configured sample time, worked out here rather than
 * borrowed from the driver so the driver's own mapping is what gets tested. */
#if BSP_ADC1_SAMPLE_TIME == 3
#define EXPECTED_SMP 0U
#elif BSP_ADC1_SAMPLE_TIME == 15
#define EXPECTED_SMP 1U
#elif BSP_ADC1_SAMPLE_TIME == 28
#define EXPECTED_SMP 2U
#elif BSP_ADC1_SAMPLE_TIME == 56
#define EXPECTED_SMP 3U
#elif BSP_ADC1_SAMPLE_TIME == 84
#define EXPECTED_SMP 4U
#elif BSP_ADC1_SAMPLE_TIME == 112
#define EXPECTED_SMP 5U
#elif BSP_ADC1_SAMPLE_TIME == 144
#define EXPECTED_SMP 6U
#else
#define EXPECTED_SMP 7U
#endif

/* ------------------------------------------------------------------------ */
/* Fake silicon                                                              */
/* ------------------------------------------------------------------------ */
static atomic_int sim_stop;

/**
 * @brief Service the flags the drivers spin on.
 *
 * Only the handshakes that actually block a driver are modelled; anything the
 * drivers never poll is left as plain memory.
 */
static void* silicon(void* arg)
{
    (void)arg;
    while (!atomic_load(&sim_stop))
    {
        uint32_t cr = RCC->CR;
        uint32_t ready = 0;
        if (cr & RCC_CR_HSION)    { ready |= RCC_CR_HSIRDY; }
        if (cr & RCC_CR_HSEON)    { ready |= RCC_CR_HSERDY; }
        if (cr & RCC_CR_PLLON)    { ready |= RCC_CR_PLLRDY; }
        if (cr & RCC_CR_PLLI2SON) { ready |= RCC_CR_PLLI2SRDY; }
        RCC->CR = (cr & ~(RCC_CR_HSIRDY | RCC_CR_HSERDY | RCC_CR_PLLRDY |
                          RCC_CR_PLLI2SRDY)) | ready;

        /* SWS mirrors SW once the switch completes. */
        uint32_t cfgr = RCC->CFGR;
        uint32_t sw = FIELD(cfgr, RCC_CFGR_SW_MSK, RCC_CFGR_SW_POS);
        RCC->CFGR = (cfgr & ~RCC_CFGR_SWS_MSK) | (sw << RCC_CFGR_SWS_POS);

        uint32_t csr = RCC->CSR;
        RCC->CSR = (csr & RCC_CSR_LSION) ? (csr | RCC_CSR_LSIRDY)
                                         : (csr & ~RCC_CSR_LSIRDY);

        uint32_t bdcr = RCC->BDCR;
        RCC->BDCR = (bdcr & RCC_BDCR_LSEON) ? (bdcr | RCC_BDCR_LSERDY)
                                            : (bdcr & ~RCC_BDCR_LSERDY);

        PWR->CSR |= PWR_CSR_VOSRDY;

        /* Transmit buffers always empty and never busy: enough for init and
         * for the blocking write paths to make progress. */
        spi_regs_t* const spis[] = {SPI1, SPI2, SPI3, SPI4, SPI5};
        for (unsigned i = 0; i < 5; i++)
        {
            spis[i]->SR = (spis[i]->SR | SPI_SR_TXE | SPI_SR_RXNE) & ~SPI_SR_BSY;
        }
        usart_regs_t* const uarts[] = {USART1, USART2, USART6};
        for (unsigned i = 0; i < 3; i++)
        {
            uarts[i]->SR |= USART_SR_TXE | USART_SR_TC;
        }

        /* Registers the hardware drives are read-only to the firmware, so the
         * simulator has to cast the qualifier away to play their part. */
        SIM_POKE(IWDG->SR, 0);                             /* updates land at once */
        RTC->ISR |= RTC_ISR_INITF | RTC_ISR_RSF | RTC_ISR_INITS;
        SIM_POKE(ADC1->SR, ADC1->SR | ADC_SR_EOC);         /* conversion done      */

        usleep(20);
    }
    return NULL;
}

/** @brief Map zeroed RAM over a hardware region at its real address. */
static int map_region(uintptr_t base, size_t len)
{
    void* p = mmap((void*)base, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p != base)
    {
        fprintf(stderr, "cannot map 0x%lX..0x%lX\n", (unsigned long)base,
                (unsigned long)(base + len));
        return -1;
    }
    memset(p, 0, len);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

/*
 * Expected values are recomputed here from RM0383 using plain arithmetic,
 * deliberately not by reusing the driver's own helper macros - otherwise a
 * wrong formula would agree with itself and the test would pass.
 */
static void test_clock(void)
{
    group("RCC / clock tree");

    check_eq("bsp_clock_init() returns 0", (uint64_t)(uint32_t)bsp_clock_init(), 0);

    const uint32_t pllcfgr = RCC->PLLCFGR;
    check_eq("PLLCFGR.PLLM", FIELD(pllcfgr, RCC_PLLCFGR_PLLM_MSK, RCC_PLLCFGR_PLLM_POS),
             BSP_PLL_M);
    check_eq("PLLCFGR.PLLN", FIELD(pllcfgr, RCC_PLLCFGR_PLLN_MSK, RCC_PLLCFGR_PLLN_POS),
             BSP_PLL_N);
    check_eq("PLLCFGR.PLLP encodes P as P/2-1",
             FIELD(pllcfgr, RCC_PLLCFGR_PLLP_MSK, RCC_PLLCFGR_PLLP_POS),
             (BSP_PLL_P / 2U) - 1U);
    check_eq("PLLCFGR.PLLQ", FIELD(pllcfgr, RCC_PLLCFGR_PLLQ_MSK, RCC_PLLCFGR_PLLQ_POS),
             BSP_PLL_Q);

    check_eq("CFGR.SWS follows CFGR.SW",
             FIELD(RCC->CFGR, RCC_CFGR_SWS_MSK, RCC_CFGR_SWS_POS),
             FIELD(RCC->CFGR, RCC_CFGR_SW_MSK, RCC_CFGR_SW_POS));

    /* Derived independently: f_VCO = f_in / M * N, SYSCLK = f_VCO / P. */
    const uint64_t f_in = BSP_PLL_SOURCE_HZ;
    const uint64_t vco = f_in / BSP_PLL_M * BSP_PLL_N;
    const uint64_t sysclk = vco / BSP_PLL_P;

    check_eq("BSP_SYSCLK_HZ matches the PLL maths", BSP_SYSCLK_HZ, sysclk);
    check_eq("g_system_core_clock set to HCLK", g_system_core_clock, BSP_HCLK_HZ);
    check_eq("PLL Q output is exactly 48 MHz", vco / BSP_PLL_Q, 48000000ULL);

    /* Datasheet limits for the STM32F411 at 2.7-3.6 V. */
    check_eq("SYSCLK within the 100 MHz limit", sysclk <= 100000000ULL, 1);
    check_eq("PCLK1 within the 50 MHz limit", BSP_PCLK1_HZ <= 50000000UL, 1);
    check_eq("PCLK2 within the 100 MHz limit", BSP_PCLK2_HZ <= 100000000UL, 1);
    check_eq("PLL VCO input in the 1-2 MHz sweet spot",
             (f_in / BSP_PLL_M) >= 1000000ULL && (f_in / BSP_PLL_M) <= 2000000ULL, 1);
    check_eq("PLL VCO output within 100-432 MHz",
             vco >= 100000000ULL && vco <= 432000000ULL, 1);

    check_eq("FLASH prefetch and both caches enabled",
             (FLASH_R->ACR & (FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN)) ==
                 (FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN), 1);
    /* RM0383 Table 6: one extra wait state per 30 MHz of HCLK at 2.7-3.6 V. */
    const uint32_t required_ws = (uint32_t)((BSP_HCLK_HZ - 1U) / 30000000U);
    check_eq("FLASH latency matches RM0383 table 6",
             FIELD(FLASH_R->ACR, FLASH_ACR_LATENCY_MSK, FLASH_ACR_LATENCY_POS),
             required_ws);
}

static void test_gpio(void)
{
    group("GPIO");

    bsp_gpio_enable_port(GPIOC);
    check_eq("AHB1ENR.GPIOCEN", (RCC->AHB1ENR & RCC_AHB1ENR_GPIOCEN) ? 1 : 0, 1);

    bsp_gpio_config_output(GPIOC, 13, GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_LOW, GPIO_PUPD_NONE);
    check_eq("MODER pin 13 = output", (GPIOC->MODER >> 26) & 0x3, GPIO_MODER_OUTPUT);
    check_eq("OTYPER pin 13 = push-pull", (GPIOC->OTYPER >> 13) & 0x1, GPIO_OTYPER_PUSHPULL);
    check_eq("MODER left neighbouring pins alone", GPIOC->MODER & ~(0x3U << 26), 0);

    bsp_gpio_write(GPIOC, 13, false);
    check_eq("BSRR reset half for a low write", (GPIOC->BSRR >> 16) & 0xFFFF, 1U << 13);
    bsp_gpio_write(GPIOC, 13, true);
    check_eq("BSRR set half for a high write", GPIOC->BSRR & 0xFFFF, 1U << 13);

    /* Pin 9 is in the high AF register, nibble 9-8 = 1. */
    bsp_gpio_config_alternate(GPIOA, 9, 7, GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH,
                              GPIO_PUPD_NONE);
    check_eq("AFR[1] nibble 1 holds AF7", (GPIOA->AFR[1] >> 4) & 0xF, 7);
    check_eq("MODER pin 9 = alternate", (GPIOA->MODER >> 18) & 0x3, GPIO_MODER_AF);

    /* Pin 3 is in the low AF register, nibble 3. */
    bsp_gpio_config_alternate(GPIOB, 3, 9, GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_VERY_HIGH,
                              GPIO_PUPD_UP);
    check_eq("AFR[0] nibble 3 holds AF9", (GPIOB->AFR[0] >> 12) & 0xF, 9);
    check_eq("OTYPER pin 3 = open drain", (GPIOB->OTYPER >> 3) & 0x1, GPIO_OTYPER_OPENDRAIN);
    check_eq("PUPDR pin 3 = pull-up", (GPIOB->PUPDR >> 6) & 0x3, GPIO_PUPD_UP);

    bsp_gpio_config_analog(GPIOA, 0);
    check_eq("MODER pin 0 = analog", GPIOA->MODER & 0x3, GPIO_MODER_ANALOG);
}

static void test_spi(void)
{
    group("SPI");

    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    /* RM0383: BR is log2(divider) - 1. */
    const struct { uint32_t div, br; } cases[] = {
        {2, 0}, {4, 1}, {8, 2}, {16, 3}, {32, 4}, {64, 5}, {128, 6}, {256, 7},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        char label[64];
        snprintf(label, sizeof(label), "CR1.BR for divider /%u", cases[i].div);
        bsp_spi_set_baud_divider(SPI2, cases[i].div);
        check_eq(label, FIELD(SPI2->CR1, SPI_CR1_BR_MSK, SPI_CR1_BR_POS), cases[i].br);
    }
}

static void test_usart(void)
{
    group("USART");

    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* OVER8 = 0, so BRR is simply the 12.4 fixed-point value of fCK / baud. */
    const uint32_t pclk = BSP_PCLK1_HZ;
    const uint32_t bauds[] = {9600, 38400, 115200, 921600};
    for (unsigned i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++)
    {
        char label[64];
        snprintf(label, sizeof(label), "BRR at %u baud", bauds[i]);
        bsp_usart_set_baud(USART2, bauds[i]);
        check_eq(label, USART2->BRR, (pclk + bauds[i] / 2U) / bauds[i]);
    }

    /* Round trip: the divider must reproduce the baud rate within 2%. */
    bsp_usart_set_baud(USART2, 115200);
    const uint32_t actual = pclk * 16U / (USART2->BRR * 16U);
    const uint32_t err_permille = (actual > 115200U ? actual - 115200U : 115200U - actual)
                                  * 1000U / 115200U;
    check_eq("115200 baud error under 2%", err_permille < 20U, 1);
}

static void test_adc(void)
{
    group("ADC");

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->SMPR1 = 0;
    ADC1->SMPR2 = 0;
    SIM_POKE(ADC1->DR, 0x0ABC);

    check_eq("read returns the DR contents", bsp_adc_read(3), 0x0ABC);
    check_eq("SQR3 holds the requested channel", ADC1->SQR3 & 0x1F, 3);
    check_eq("SMPR2 field for channel 3", (ADC1->SMPR2 >> 9) & 0x7, EXPECTED_SMP);

    /* Internal channels are high impedance and need >= 10 us regardless of
     * what the user configured for the external ones. */
    (void)bsp_adc_read(ADC_CHANNEL_TEMPSENSOR);
    check_eq("SQR3 holds channel 18", ADC1->SQR3 & 0x1F, 18);
    check_eq("SMPR1 forces 480 cycles on ch 18", (ADC1->SMPR1 >> 24) & 0x7, ADC_SMP_480CYCLES);

    (void)bsp_adc_read(ADC_CHANNEL_VREFINT);
    check_eq("SMPR1 forces 480 cycles on ch 17", (ADC1->SMPR1 >> 21) & 0x7, ADC_SMP_480CYCLES);

    check_eq("full scale converts to Vref", bsp_adc_to_millivolts(4095, 3300), 3300);
    check_eq("mid scale converts to Vref/2", bsp_adc_to_millivolts(2048, 3300), 1650);
    check_eq("zero converts to 0 mV", bsp_adc_to_millivolts(0, 3300), 0);
}

static void test_tim(void)
{
    group("TIM");

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    bsp_tim_config(TIM3, 95, 999);
    check_eq("PSC", TIM3->PSC, 95);
    check_eq("ARR", TIM3->ARR, 999);

    TIM3->CCER = 0;
    TIM3->SR = 0xFFFF;
    bsp_tim_pwm_config(TIM3, 1, 250, false);
    check_eq("CCR1 holds the duty", TIM3->CCR1, 250);
    check_eq("CCMR1.OC1M = PWM mode 1",
             FIELD(TIM3->CCMR1, TIM_CCMR1_OC1M_MSK, TIM_CCMR1_OC1M_POS), TIM_OCMODE_PWM1);
    check_eq("CCMR1.OC1PE preload enabled", (TIM3->CCMR1 & TIM_CCMR1_OC1PE) ? 1 : 0, 1);
    check_eq("CCER.CC1E enabled", (TIM3->CCER & TIM_CCER_CC1E) ? 1 : 0, 1);
    /* The forced update must not leave a pending interrupt behind. */
    check_eq("UIF cleared after the forced update", (TIM3->SR & TIM_SR_UIF) ? 1 : 0, 0);
    check_eq("CR1.URS restored afterwards", (TIM3->CR1 & TIM_CR1_URS) ? 1 : 0, 0);

    bsp_tim_pwm_config(TIM3, 2, 100, true);
    check_eq("CCMR1.OC2M = PWM mode 2 when inverted",
             FIELD(TIM3->CCMR1, TIM_CCMR1_OC2M_MSK, TIM_CCMR1_OC2M_POS), TIM_OCMODE_PWM2);
    check_eq("channel 1 settings untouched", TIM3->CCR1, 250);

    /* TIM1 is the only advanced timer here and needs its main output enable. */
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    TIM1->BDTR = 0;
    bsp_tim_config(TIM1, 0, 100);
    bsp_tim_pwm_config(TIM1, 1, 50, false);
    check_eq("TIM1 BDTR.MOE set for the advanced timer",
             (TIM1->BDTR & TIM_BDTR_MOE) ? 1 : 0, 1);
}

static void test_dma(void)
{
    group("DMA");

    /* RM0383: stream n control block is at 0x10 + n * 0x18 from the DMA base. */
    check_eq("stream control block is 6 words", sizeof(dma_stream_regs_t), 24);
    for (unsigned n = 0; n < 8; n++)
    {
        char label[64];
        snprintf(label, sizeof(label), "DMA1 stream %u offset", n);
        check_eq(label, (uintptr_t)&DMA1->STREAM[n] - (uintptr_t)DMA1, 0x10UL + n * 0x18UL);
    }
    check_eq("DMA2 base is 0x400 above DMA1", (uintptr_t)DMA2 - (uintptr_t)DMA1, 0x400);
}

static void check_i2c_instance(const char* name, i2c_regs_t* i2c, uint32_t hz)
{
    char label[80];
    const uint32_t pclk_mhz = BSP_PCLK1_HZ / 1000000U;

    snprintf(label, sizeof(label), "%s CR2.FREQ in MHz", name);
    check_eq(label, i2c->CR2 & I2C_CR2_FREQ_MSK, pclk_mhz);

    if (hz <= 100000U)
    {
        /* RM0383 standard mode: SCL high and low are each CCR periods of
         * PCLK1, and the rise time limit is 1000 ns. */
        snprintf(label, sizeof(label), "%s standard mode selected", name);
        check_eq(label, (i2c->CCR & I2C_CCR_FS) ? 1 : 0, 0);
        snprintf(label, sizeof(label), "%s CCR = pclk / (2 * speed)", name);
        check_eq(label, i2c->CCR & I2C_CCR_CCR_MSK, BSP_PCLK1_HZ / (2U * hz));
        snprintf(label, sizeof(label), "%s TRISE = pclk_MHz + 1", name);
        check_eq(label, i2c->TRISE, pclk_mhz + 1U);
    }
    else
    {
        /* Fast mode with a 2:1 duty cycle: one period is 3 * CCR, and the
         * rise time limit is 300 ns. */
        snprintf(label, sizeof(label), "%s fast mode selected", name);
        check_eq(label, (i2c->CCR & I2C_CCR_FS) ? 1 : 0, 1);
        snprintf(label, sizeof(label), "%s CCR = pclk / (3 * speed)", name);
        check_eq(label, i2c->CCR & I2C_CCR_CCR_MSK, BSP_PCLK1_HZ / (3U * hz));
        snprintf(label, sizeof(label), "%s TRISE = pclk_MHz * 300 / 1000 + 1", name);
        check_eq(label, i2c->TRISE, (pclk_mhz * 300U) / 1000U + 1U);
    }

    snprintf(label, sizeof(label), "%s peripheral enabled", name);
    check_eq(label, (i2c->CR1 & I2C_CR1_PE) ? 1 : 0, 1);
}

static void test_i2c(void)
{
    group("I2C");

    bsp_i2c_init();
    check_i2c_instance("I2C1", I2C1, BSP_I2C1_FREQ_HZ);
    check_i2c_instance("I2C2", I2C2, BSP_I2C2_FREQ_HZ);
    check_i2c_instance("I2C3", I2C3, BSP_I2C3_FREQ_HZ);
}

int main(void)
{
    if (map_region(0x40000000UL, 0x00030000UL) != 0 ||   /* APB1 / APB2 / AHB1 */
        map_region(0x50000000UL, 0x00010000UL) != 0 ||   /* AHB2               */
        map_region(0xE0000000UL, 0x00100000UL) != 0)     /* Cortex-M private   */
    {
        fprintf(stderr, "the peripheral region is not available in this process\n");
        return 2;
    }

    /* Reset values the drivers rely on (RM0383 register reset states). */
    RCC->CR = 0x00000083UL;
    RCC->CFGR = 0;
    RCC->PLLCFGR = 0x24003010UL;

    pthread_t th;
    if (pthread_create(&th, NULL, silicon, NULL) != 0)
    {
        fprintf(stderr, "cannot start the fake silicon thread\n");
        return 2;
    }

    test_clock();
    test_gpio();
    test_spi();
    test_usart();
    test_adc();
    test_tim();
    test_dma();
    test_i2c();

    atomic_store(&sim_stop, 1);
    pthread_join(th, NULL);

    printf("\n%u checks, %u failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
