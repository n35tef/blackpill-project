/**
 * @file    sim.c
 * @brief   Run the real BSP drivers on the host against a fake peripheral space.
 *
 * The STM32 peripheral region is mapped into this process at its true
 * addresses, so `RCC->CFGR = x` inside the driver writes into ordinary memory
 * that the test can then inspect. A background thread plays the part of the
 * silicon for the flags the drivers spin on: oscillator ready bits, the clock
 * switch status, transmit-empty flags.
 *
 * The point is that the code under test is exactly the code that ships. There
 * are no mocks and nothing is reimplemented, so a wrong shift or a wrong
 * encoding in a driver shows up here rather than on the bench.
 *
 * Two kinds of check run on every driver call:
 *
 *  1. State checks - after the call, are the registers what RM0383 says they
 *     should be? Expected values are worked out here with plain arithmetic,
 *     never by reusing the driver's own macros.
 *
 *  2. Sequence checks - while the call runs, every single register access is
 *     trapped (the pages are write-protected; each access faults, is recorded,
 *     and the one instruction is single-stepped). A set of rules taken from
 *     RM0383 is applied to the stream: no touching a peripheral before its
 *     clock is on, no switching SYSCLK before the flash wait states are raised,
 *     no reconfiguring SPI/I2C/DMA while enabled, and so on. This is what turns
 *     "the final register values look right" into "and they were written in an
 *     order the hardware accepts".
 *
 * The I2C peripheral is modelled event by event on top of the trace rather
 * than by the polling thread, because its handshakes depend on the order in
 * which the driver reads SR1, SR2 and DR - which is exactly what we want to
 * check.
 *
 * What this still cannot check: anything analogue, real timing, and any bus
 * protocol detail the fake silicon does not model. See README.md.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include "bsp.h"

#if !defined(__x86_64__) || !defined(__linux__)
#error "the access monitor relies on x86-64 Linux page faults and the trap flag"
#endif

/* ------------------------------------------------------------------------ */
/* Test harness                                                              */
/* ------------------------------------------------------------------------ */
static unsigned tests_run, tests_failed;

static void check_eq(const char* what, uint64_t got, uint64_t expect)
{
    tests_run++;
    if (got == expect)
    {
        printf("  ok    %-50s 0x%llX\n", what, (unsigned long long)got);
    }
    else
    {
        tests_failed++;
        printf("  FAIL  %-50s got 0x%llX want 0x%llX\n", what,
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
/* Memory regions                                                            */
/* ------------------------------------------------------------------------ */

/*
 * Each hardware region is backed by a memfd mapped twice: once at its real
 * address, where the drivers see it and where the monitor can write-protect
 * it, and once at an arbitrary address as an alias. The alias never faults,
 * so the fake-silicon thread and the monitor's own rules use it to read and
 * drive registers without tripping the trap machinery.
 */
typedef struct
{
    uintptr_t base;
    size_t len;
    uint8_t* alias;
} region_t;

static region_t regions[] = {
    {0x40000000UL, 0x00030000UL, NULL}, /* APB1 / APB2 / AHB1 */
    {0x50000000UL, 0x00010000UL, NULL}, /* AHB2               */
    {0xE0000000UL, 0x00100000UL, NULL}, /* Cortex-M private   */
};
#define REGION_COUNT (sizeof(regions) / sizeof(regions[0]))

static region_t* region_of(uintptr_t addr)
{
    for (size_t i = 0; i < REGION_COUNT; i++)
    {
        if (addr >= regions[i].base && addr < regions[i].base + regions[i].len)
        {
            return &regions[i];
        }
    }
    return NULL;
}

static int map_region(region_t* r)
{
    int fd = memfd_create("stm32-periph", 0);
    if (fd < 0 || ftruncate(fd, (off_t)r->len) != 0)
    {
        return -1;
    }
    void* fixed = mmap((void*)r->base, r->len, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    void* alias = mmap(NULL, r->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (fixed == MAP_FAILED || (uintptr_t)fixed != r->base || alias == MAP_FAILED)
    {
        fprintf(stderr, "cannot map 0x%lX..0x%lX\n", (unsigned long)r->base,
                (unsigned long)(r->base + r->len));
        return -1;
    }
    r->alias = alias;
    return 0;
}

/** @brief Host pointer to the alias of a register - never faults. */
static volatile uint32_t* alias_of(const volatile void* reg)
{
    uintptr_t a = (uintptr_t)reg;
    region_t* r = region_of(a);
    if (r == NULL)
    {
        fprintf(stderr, "alias_of: 0x%lX is not a peripheral address\n", (unsigned long)a);
        abort();
    }
    return (volatile uint32_t*)(r->alias + (a - r->base));
}

/** @brief Register access through the alias; usable inside the monitor. */
#define ALIAS(reg) (*alias_of(&(reg)))

static uint32_t rd_at(uintptr_t addr)
{
    return *alias_of((const volatile void*)addr);
}

/* ------------------------------------------------------------------------ */
/* Access monitor                                                            */
/* ------------------------------------------------------------------------ */

/*
 * How the trap works: while armed, the real-address mappings are PROT_NONE.
 * The first touch of a register raises SIGSEGV; the handler notes the address
 * and direction (REG_ERR bit 1 is set for writes), snapshots the value, lifts
 * the protection and sets the x86 trap flag. The kernel then runs exactly one
 * instruction - the access - and raises SIGTRAP. That handler restores the
 * protection, snapshots the value again, and hands the completed access to the
 * rule engine. The driver code is untouched and does not know it happened.
 *
 * Only the main thread runs traced code; the silicon thread uses the alias.
 */

#define MAX_VIOLATIONS 64

static volatile sig_atomic_t mon_armed;
static const char* mon_phase = "setup";
static unsigned long mon_accesses;
static unsigned long mon_accesses_at_group_start;
static uint64_t sim_cycles; /* advanced only by __busy_wait() in the drivers */

static char violations[MAX_VIOLATIONS][200];
static unsigned violation_count, violations_reported;

static struct
{
    uintptr_t addr;
    region_t* region;
    int write;
    uint32_t before;
} pending;

static void on_access(uintptr_t addr, int write, uint32_t before, uint32_t after);

static void on_segv(int sig, siginfo_t* si, void* ctx)
{
    (void)sig;
    ucontext_t* uc = ctx;
    uintptr_t addr = (uintptr_t)si->si_addr;
    region_t* r = region_of(addr);

    if (r == NULL || !mon_armed)
    {
        /* A genuine crash: let the default action produce a core dump. */
        signal(SIGSEGV, SIG_DFL);
        return;
    }

    pending.addr = addr & ~(uintptr_t)3U;
    pending.region = r;
    pending.write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    pending.before = rd_at(pending.addr);

    mprotect((void*)r->base, r->len, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100; /* TF: one instruction, then SIGTRAP */
}

static void on_trap(int sig, siginfo_t* si, void* ctx)
{
    (void)sig;
    (void)si;
    ucontext_t* uc = ctx;
    uc->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)0x100;

    region_t* r = pending.region;
    const uint32_t after = rd_at(pending.addr);
    mprotect((void*)r->base, r->len, PROT_NONE);

    mon_accesses++;
    on_access(pending.addr, pending.write, pending.before, after);
}

static void monitor_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = on_segv;
    sigaction(SIGSEGV, &sa, NULL);
    sa.sa_sigaction = on_trap;
    sigaction(SIGTRAP, &sa, NULL);
}

static void monitor_arm(void)
{
    for (size_t i = 0; i < REGION_COUNT; i++)
    {
        mprotect((void*)regions[i].base, regions[i].len, PROT_NONE);
    }
    mon_armed = 1;
}

static void monitor_disarm(void)
{
    mon_armed = 0;
    for (size_t i = 0; i < REGION_COUNT; i++)
    {
        mprotect((void*)regions[i].base, regions[i].len, PROT_READ | PROT_WRITE);
    }
}

/** @brief The drivers' settling delays land here instead of spinning. */
void bsp_sim_busy_wait(uint32_t cycles)
{
    sim_cycles += cycles;
}

/** @brief Name a driver call so violations can say where they happened. */
#define DRIVE(call) (mon_phase = #call, call)

/* -- peripheral naming and clock gating ---------------------------------- */

typedef struct
{
    uintptr_t base;
    size_t span;
    size_t enr_offset; /* within rcc_regs_t; 0 = no gate */
    uint32_t enr_bit;
    const char* name;
} periph_t;

#define GATED(b, span, reg, bit, n) {b, span, offsetof(rcc_regs_t, reg), bit, n}
#define UNGATED(b, span, n)         {b, span, 0, 0, n}

static const periph_t periphs[] = {
    GATED(GPIOA_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIOAEN, "GPIOA"),
    GATED(GPIOB_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIOBEN, "GPIOB"),
    GATED(GPIOC_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIOCEN, "GPIOC"),
    GATED(GPIOD_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIODEN, "GPIOD"),
    GATED(GPIOE_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIOEEN, "GPIOE"),
    GATED(GPIOH_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_GPIOHEN, "GPIOH"),
    GATED(CRC_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_CRCEN, "CRC"),
    GATED(DMA1_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_DMA1EN, "DMA1"),
    GATED(DMA2_BASE, 0x400, AHB1ENR, RCC_AHB1ENR_DMA2EN, "DMA2"),
    GATED(TIM2_BASE, 0x400, APB1ENR, RCC_APB1ENR_TIM2EN, "TIM2"),
    GATED(TIM3_BASE, 0x400, APB1ENR, RCC_APB1ENR_TIM3EN, "TIM3"),
    GATED(TIM4_BASE, 0x400, APB1ENR, RCC_APB1ENR_TIM4EN, "TIM4"),
    GATED(TIM5_BASE, 0x400, APB1ENR, RCC_APB1ENR_TIM5EN, "TIM5"),
    GATED(WWDG_BASE, 0x400, APB1ENR, RCC_APB1ENR_WWDGEN, "WWDG"),
    GATED(SPI2_BASE, 0x400, APB1ENR, RCC_APB1ENR_SPI2EN, "SPI2"),
    GATED(SPI3_BASE, 0x400, APB1ENR, RCC_APB1ENR_SPI3EN, "SPI3"),
    GATED(USART2_BASE, 0x400, APB1ENR, RCC_APB1ENR_USART2EN, "USART2"),
    GATED(I2C1_BASE, 0x400, APB1ENR, RCC_APB1ENR_I2C1EN, "I2C1"),
    GATED(I2C2_BASE, 0x400, APB1ENR, RCC_APB1ENR_I2C2EN, "I2C2"),
    GATED(I2C3_BASE, 0x400, APB1ENR, RCC_APB1ENR_I2C3EN, "I2C3"),
    GATED(PWR_BASE, 0x400, APB1ENR, RCC_APB1ENR_PWREN, "PWR"),
    GATED(TIM1_BASE, 0x400, APB2ENR, RCC_APB2ENR_TIM1EN, "TIM1"),
    GATED(USART1_BASE, 0x400, APB2ENR, RCC_APB2ENR_USART1EN, "USART1"),
    GATED(USART6_BASE, 0x400, APB2ENR, RCC_APB2ENR_USART6EN, "USART6"),
    GATED(ADC1_BASE, 0x400, APB2ENR, RCC_APB2ENR_ADC1EN, "ADC1"), /* includes common */
    GATED(SPI1_BASE, 0x400, APB2ENR, RCC_APB2ENR_SPI1EN, "SPI1"),
    GATED(SPI4_BASE, 0x400, APB2ENR, RCC_APB2ENR_SPI4EN, "SPI4"),
    GATED(SYSCFG_BASE, 0x400, APB2ENR, RCC_APB2ENR_SYSCFGEN, "SYSCFG"),
    GATED(TIM9_BASE, 0x400, APB2ENR, RCC_APB2ENR_TIM9EN, "TIM9"),
    GATED(TIM10_BASE, 0x400, APB2ENR, RCC_APB2ENR_TIM10EN, "TIM10"),
    GATED(TIM11_BASE, 0x400, APB2ENR, RCC_APB2ENR_TIM11EN, "TIM11"),
    GATED(SPI5_BASE, 0x400, APB2ENR, RCC_APB2ENR_SPI5EN, "SPI5"),
    UNGATED(RCC_BASE, 0x400, "RCC"),
    UNGATED(FLASH_R_BASE, 0x400, "FLASH"),
    UNGATED(EXTI_BASE, 0x400, "EXTI"),
    UNGATED(IWDG_BASE, 0x400, "IWDG"),
    UNGATED(RTC_BASE, 0x400, "RTC"),
    UNGATED(0xE000E000UL, 0x1000, "SCS"),
};

static const periph_t* periph_of(uintptr_t addr)
{
    for (size_t i = 0; i < sizeof(periphs) / sizeof(periphs[0]); i++)
    {
        if (addr >= periphs[i].base && addr < periphs[i].base + periphs[i].span)
        {
            return &periphs[i];
        }
    }
    return NULL;
}

static void violation(uintptr_t addr, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void violation(uintptr_t addr, const char* fmt, ...)
{
    if (violation_count >= MAX_VIOLATIONS)
    {
        return;
    }
    char* out = violations[violation_count++];
    const periph_t* p = periph_of(addr);
    int n;
    if (p != NULL)
    {
        n = snprintf(out, sizeof(violations[0]), "[%s+0x%02lX during %s] ", p->name,
                     (unsigned long)(addr - p->base), mon_phase);
    }
    else
    {
        n = snprintf(out, sizeof(violations[0]), "[0x%08lX during %s] ", (unsigned long)addr,
                     mon_phase);
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + n, sizeof(violations[0]) - (size_t)n, fmt, ap);
    va_end(ap);
}

/* -- RM0383 sequencing rules --------------------------------------------- */

/* RCC_CFGR.HPRE encodes the AHB divider as 0xxx = 1, 1000 = 2 ... 1111 = 512
 * (with 32 skipped). */
static uint32_t hpre_divider(uint32_t hpre)
{
    static const uint32_t table[8] = {2, 4, 8, 16, 64, 128, 256, 512};
    return (hpre & 0x8U) ? table[hpre & 0x7U] : 1U;
}

/** @brief SYSCLK the RCC registers currently describe for a given SW choice. */
static uint64_t sysclk_for(uint32_t sw)
{
    if (sw == RCC_CFGR_SW_HSI)
    {
        return BSP_HSI_FREQ_HZ;
    }
    if (sw == RCC_CFGR_SW_HSE)
    {
        return BSP_HSE_FREQ_HZ;
    }
    const uint32_t pllcfgr = ALIAS(RCC->PLLCFGR);
    const uint64_t src = (pllcfgr & RCC_PLLCFGR_PLLSRC) ? BSP_HSE_FREQ_HZ : BSP_HSI_FREQ_HZ;
    const uint32_t m = FIELD(pllcfgr, RCC_PLLCFGR_PLLM_MSK, RCC_PLLCFGR_PLLM_POS);
    const uint32_t n = FIELD(pllcfgr, RCC_PLLCFGR_PLLN_MSK, RCC_PLLCFGR_PLLN_POS);
    const uint32_t p = (FIELD(pllcfgr, RCC_PLLCFGR_PLLP_MSK, RCC_PLLCFGR_PLLP_POS) + 1U) * 2U;
    if (m == 0U)
    {
        return 0;
    }
    return src / m * n / p;
}

static void rule_rcc(uintptr_t addr, uint32_t before, uint32_t after)
{
    if (addr == (uintptr_t)&RCC->PLLCFGR && (ALIAS(RCC->CR) & RCC_CR_PLLON))
    {
        violation(addr, "PLLCFGR written while PLLON=1 (RM0383 6.3.2: configure with the PLL off)");
    }

    if (addr == (uintptr_t)&RCC->CFGR)
    {
        const uint32_t old_sw = before & RCC_CFGR_SW_MSK;
        const uint32_t new_sw = after & RCC_CFGR_SW_MSK;
        if (old_sw == new_sw)
        {
            return;
        }

        const uint32_t cr = ALIAS(RCC->CR);
        if (new_sw == RCC_CFGR_SW_PLL && !(cr & RCC_CR_PLLRDY))
        {
            violation(addr, "SYSCLK switched to the PLL before PLLRDY");
        }
        if (new_sw == RCC_CFGR_SW_HSE && !(cr & RCC_CR_HSERDY))
        {
            violation(addr, "SYSCLK switched to HSE before HSERDY");
        }

        const uint64_t sysclk = sysclk_for(new_sw);
        const uint64_t hclk = sysclk / hpre_divider(FIELD(after, RCC_CFGR_HPRE_MSK, RCC_CFGR_HPRE_POS));

        /* RM0383 table 6 at 2.7-3.6 V: one wait state per 30 MHz of HCLK. */
        const uint32_t needed_ws = hclk == 0 ? 0U : (uint32_t)((hclk - 1U) / 30000000U);
        const uint32_t have_ws = FIELD(ALIAS(FLASH_R->ACR), FLASH_ACR_LATENCY_MSK, FLASH_ACR_LATENCY_POS);
        if (have_ws < needed_ws)
        {
            violation(addr, "SYSCLK switched to %llu Hz with FLASH latency %u; RM0383 table 6 needs %u",
                      (unsigned long long)hclk, have_ws, needed_ws);
        }

        /* RM0383 5.1.4: scale 1 above 84 MHz, scale 2 above 64 MHz. */
        const uint32_t vos = FIELD(ALIAS(PWR->CR), PWR_CR_VOS_MSK, PWR_CR_VOS_POS);
        if (hclk > 84000000ULL && vos != 3U)
        {
            violation(addr, "HCLK %llu Hz needs PWR VOS scale 1 (0b11), have 0b%u%u",
                      (unsigned long long)hclk, (vos >> 1) & 1U, vos & 1U);
        }
        else if (hclk > 64000000ULL && vos < 2U)
        {
            violation(addr, "HCLK %llu Hz needs PWR VOS scale 2 or better, have 0b%u%u",
                      (unsigned long long)hclk, (vos >> 1) & 1U, vos & 1U);
        }
    }
}

static void rule_spi(uintptr_t addr, spi_regs_t* spi, int write, uint32_t before, uint32_t after)
{
    if (!write)
    {
        return;
    }
    if (addr == (uintptr_t)&spi->CR1)
    {
        /* RM0383 20.5.1: the mode bits must be set before SPE, and not touched
         * while the peripheral is enabled. */
        if ((before & SPI_CR1_SPE) && (after & SPI_CR1_SPE) &&
            ((before ^ after) & ~SPI_CR1_SPE))
        {
            violation(addr, "SPI CR1 reconfigured while SPE=1 (0x%04X -> 0x%04X)", before, after);
        }
    }
    else if (addr == (uintptr_t)&spi->DR)
    {
        const uint32_t cr1 = ALIAS(spi->CR1);
        if (!(cr1 & SPI_CR1_SPE))
        {
            violation(addr, "SPI DR written while SPE=0: nothing would be transmitted");
        }
        /* Fake slave: the received frame is the complement of what was sent.
         * DR is one word here where the hardware has separate Tx and Rx
         * buffers, so the reply has to be placed by the model. */
        const uint32_t frame_mask = (cr1 & SPI_CR1_DFF) ? 0xFFFFU : 0xFFU;
        ALIAS(spi->DR) = ~after & frame_mask;
    }
}

static void rule_usart(uintptr_t addr, usart_regs_t* usart, int write)
{
    if (write && addr == (uintptr_t)&usart->DR)
    {
        const uint32_t cr1 = ALIAS(usart->CR1);
        if (!(cr1 & USART_CR1_UE) || !(cr1 & USART_CR1_TE))
        {
            violation(addr, "USART DR written with UE=%u TE=%u: nothing would be sent",
                      (cr1 & USART_CR1_UE) ? 1U : 0U, (cr1 & USART_CR1_TE) ? 1U : 0U);
        }
    }
}

static void rule_dma(uintptr_t addr, dma_regs_t* dma, int write, uint32_t before, uint32_t after)
{
    if (!write || addr < (uintptr_t)&dma->STREAM[0])
    {
        return;
    }
    const size_t off = addr - (uintptr_t)&dma->STREAM[0];
    const unsigned stream = (unsigned)(off / sizeof(dma_stream_regs_t));
    const size_t reg = off % sizeof(dma_stream_regs_t);
    dma_stream_regs_t* s = &dma->STREAM[stream];

    /* RM0383 9.3.17: everything about a stream is configured with EN=0. */
    if (reg == offsetof(dma_stream_regs_t, CR))
    {
        if ((before & DMA_CR_EN) && (after & DMA_CR_EN) && ((before ^ after) & ~DMA_CR_EN))
        {
            violation(addr, "DMA stream %u CR reconfigured while EN=1", stream);
        }
    }
    else if (ALIAS(s->CR) & DMA_CR_EN)
    {
        violation(addr, "DMA stream %u register at +0x%02zX written while EN=1", stream, reg);
    }
}

static struct
{
    uint64_t adon_at;
    bool adon_seen;
} adc_state;

/* Datasheet tSTAB: 3 us maximum; at the 100 MHz ceiling that is 300 cycles. */
#define ADC_TSTAB_CYCLES 300U

static void rule_adc(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    if (!write || addr != (uintptr_t)&ADC1->CR2)
    {
        return;
    }
    if (!(before & ADC_CR2_ADON) && (after & ADC_CR2_ADON))
    {
        adc_state.adon_at = sim_cycles;
        adc_state.adon_seen = true;
    }
    if ((after & ADC_CR2_SWSTART) && !(before & ADC_CR2_SWSTART))
    {
        if (!(after & ADC_CR2_ADON) || !adc_state.adon_seen)
        {
            violation(addr, "SWSTART with ADON=0: the ADC is powered down");
        }
        else if (sim_cycles - adc_state.adon_at < ADC_TSTAB_CYCLES)
        {
            violation(addr, "conversion started %llu cycles after ADON; tSTAB needs >= %u",
                      (unsigned long long)(sim_cycles - adc_state.adon_at), ADC_TSTAB_CYCLES);
        }
    }
}

/* -- I2C: event-driven fake silicon plus rules --------------------------- */

/* A fake slave that answers everything except this address. */
#define I2C_ABSENT_ADDRESS 0x3FU

typedef struct
{
    bool addr_pending; /* ADDR seen in SR1, SR2 not yet read */
    bool receiving;
    uint8_t next_byte;
} i2c_state_t;

static i2c_state_t i2c_state[3];

static i2c_state_t* i2c_state_of(i2c_regs_t* i2c)
{
    return &i2c_state[i2c == I2C1 ? 0 : i2c == I2C2 ? 1 : 2];
}

/** @brief Byte @p n of the pattern the fake slave sends back. */
static uint8_t i2c_pattern(unsigned n)
{
    return (uint8_t)(0x10U + n);
}

static void i2c_deliver(i2c_regs_t* i2c, i2c_state_t* st)
{
    ALIAS(i2c->DR) = i2c_pattern(st->next_byte++);
    ALIAS(i2c->SR1) |= I2C_SR1_RXNE | I2C_SR1_BTF;
}

static void sim_i2c(uintptr_t addr, i2c_regs_t* i2c, int write, uint32_t before, uint32_t after)
{
    i2c_state_t* st = i2c_state_of(i2c);

    if (write)
    {
        if (addr == (uintptr_t)&i2c->CR1)
        {
            if ((after & I2C_CR1_START) && !(after & I2C_CR1_PE))
            {
                violation(addr, "START requested with PE=0");
            }
            if ((after & I2C_CR1_START) && !(before & I2C_CR1_START))
            {
                ALIAS(i2c->SR1) = I2C_SR1_SB;
                ALIAS(i2c->SR2) |= I2C_SR2_BUSY | I2C_SR2_MSL;
                ALIAS(i2c->CR1) &= ~I2C_CR1_START;
            }
            if ((after & I2C_CR1_STOP) && !(before & I2C_CR1_STOP))
            {
                if (st->addr_pending)
                {
                    violation(addr, "STOP requested while ADDR is still set (SR2 never read)");
                }
                /* The byte in flight still completes after STOP is requested
                 * (RM0383 18.3.3, "closing the communication"), so a receiver's
                 * RXNE/BTF survive; only the bus-level state is dropped. */
                ALIAS(i2c->SR1) &= ~(I2C_SR1_SB | I2C_SR1_ADDR | I2C_SR1_TXE | I2C_SR1_AF);
                ALIAS(i2c->SR2) &= ~(I2C_SR2_BUSY | I2C_SR2_MSL | I2C_SR2_TRA);
                ALIAS(i2c->CR1) &= ~I2C_CR1_STOP;
            }
        }
        else if (addr == (uintptr_t)&i2c->CCR || addr == (uintptr_t)&i2c->TRISE ||
                 addr == (uintptr_t)&i2c->CR2)
        {
            /* RM0383 18.6.2 / 18.6.8 / 18.6.9: all three demand PE=0. */
            if (ALIAS(i2c->CR1) & I2C_CR1_PE)
            {
                violation(addr, "I2C timing register written while PE=1");
            }
        }
        else if (addr == (uintptr_t)&i2c->DR)
        {
            if (ALIAS(i2c->SR1) & I2C_SR1_SB)
            {
                /* Address phase. */
                const uint32_t address = (after >> 1U) & 0x7FU;
                st->receiving = (after & 1U) != 0U;
                st->next_byte = 0;
                if (address == I2C_ABSENT_ADDRESS)
                {
                    ALIAS(i2c->SR1) = I2C_SR1_AF;
                }
                else
                {
                    ALIAS(i2c->SR1) = I2C_SR1_ADDR;
                    if (!st->receiving)
                    {
                        ALIAS(i2c->SR2) |= I2C_SR2_TRA;
                    }
                }
            }
            else
            {
                if (st->addr_pending)
                {
                    violation(addr, "DR written before ADDR was cleared by reading SR2");
                }
                ALIAS(i2c->SR1) |= I2C_SR1_TXE | I2C_SR1_BTF;
            }
        }
        return;
    }

    /* Reads. */
    if (addr == (uintptr_t)&i2c->SR1)
    {
        if (before & I2C_SR1_ADDR)
        {
            st->addr_pending = true;
        }
    }
    else if (addr == (uintptr_t)&i2c->SR2)
    {
        if (st->addr_pending)
        {
            st->addr_pending = false;
            ALIAS(i2c->SR1) &= ~I2C_SR1_ADDR;
            if (st->receiving)
            {
                i2c_deliver(i2c, st);
            }
            else
            {
                ALIAS(i2c->SR1) |= I2C_SR1_TXE;
            }
        }
    }
    else if (addr == (uintptr_t)&i2c->DR)
    {
        if (st->addr_pending)
        {
            violation(addr, "DR read before ADDR was cleared by reading SR2: SCL stays stretched");
        }
        if (st->receiving)
        {
            i2c_deliver(i2c, st);
        }
    }
}

/* -- dispatcher ---------------------------------------------------------- */

static void on_access(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    const periph_t* p = periph_of(addr);

    if (p != NULL && p->enr_offset != 0U)
    {
        const uint32_t enr = rd_at(RCC_BASE + p->enr_offset);
        if (!(enr & p->enr_bit))
        {
            violation(addr, "%s accessed before its RCC clock enable bit was set", p->name);
        }
    }

    if (p == NULL)
    {
        return;
    }

    switch (p->base)
    {
        case RCC_BASE:
            if (write)
            {
                rule_rcc(addr, before, after);
            }
            break;
        case SPI1_BASE: case SPI2_BASE: case SPI3_BASE: case SPI4_BASE: case SPI5_BASE:
            rule_spi(addr, (spi_regs_t*)p->base, write, before, after);
            break;
        case USART1_BASE: case USART2_BASE: case USART6_BASE:
            rule_usart(addr, (usart_regs_t*)p->base, write);
            break;
        case DMA1_BASE: case DMA2_BASE:
            rule_dma(addr, (dma_regs_t*)p->base, write, before, after);
            break;
        case ADC1_BASE:
            rule_adc(addr, write, before, after);
            break;
        case I2C1_BASE: case I2C2_BASE: case I2C3_BASE:
            sim_i2c(addr, (i2c_regs_t*)p->base, write, before, after);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------------ */
/* Fake silicon: polled flags                                                */
/* ------------------------------------------------------------------------ */
static atomic_int sim_stop;

/**
 * @brief Service the flags the drivers spin on.
 *
 * Only the handshakes that actually block a driver are modelled; anything the
 * drivers never poll is left as plain memory. Everything goes through the
 * alias so this thread is never trapped.
 */
static void* silicon(void* arg)
{
    (void)arg;
    while (!atomic_load(&sim_stop))
    {
        uint32_t cr = ALIAS(RCC->CR);
        uint32_t ready = 0;
        if (cr & RCC_CR_HSION)    { ready |= RCC_CR_HSIRDY; }
        if (cr & RCC_CR_HSEON)    { ready |= RCC_CR_HSERDY; }
        if (cr & RCC_CR_PLLON)    { ready |= RCC_CR_PLLRDY; }
        if (cr & RCC_CR_PLLI2SON) { ready |= RCC_CR_PLLI2SRDY; }
        ALIAS(RCC->CR) = (cr & ~(RCC_CR_HSIRDY | RCC_CR_HSERDY | RCC_CR_PLLRDY |
                                 RCC_CR_PLLI2SRDY)) | ready;

        /* SWS mirrors SW once the switch completes. */
        uint32_t cfgr = ALIAS(RCC->CFGR);
        uint32_t sw = FIELD(cfgr, RCC_CFGR_SW_MSK, RCC_CFGR_SW_POS);
        ALIAS(RCC->CFGR) = (cfgr & ~RCC_CFGR_SWS_MSK) | (sw << RCC_CFGR_SWS_POS);

        uint32_t csr = ALIAS(RCC->CSR);
        ALIAS(RCC->CSR) = (csr & RCC_CSR_LSION) ? (csr | RCC_CSR_LSIRDY)
                                                : (csr & ~RCC_CSR_LSIRDY);

        uint32_t bdcr = ALIAS(RCC->BDCR);
        ALIAS(RCC->BDCR) = (bdcr & RCC_BDCR_LSEON) ? (bdcr | RCC_BDCR_LSERDY)
                                                   : (bdcr & ~RCC_BDCR_LSERDY);

        ALIAS(PWR->CSR) |= PWR_CSR_VOSRDY;

        /* Transmit buffers always empty and never busy: enough for init and
         * for the blocking write paths to make progress. */
        spi_regs_t* const spis[] = {SPI1, SPI2, SPI3, SPI4, SPI5};
        for (unsigned i = 0; i < 5; i++)
        {
            ALIAS(spis[i]->SR) = (ALIAS(spis[i]->SR) | SPI_SR_TXE | SPI_SR_RXNE) & ~SPI_SR_BSY;
        }
        usart_regs_t* const uarts[] = {USART1, USART2, USART6};
        for (unsigned i = 0; i < 3; i++)
        {
            ALIAS(uarts[i]->SR) |= USART_SR_TXE | USART_SR_TC;
        }

        ALIAS(IWDG->SR) = 0;                                  /* updates land at once */
        ALIAS(RTC->ISR) |= RTC_ISR_INITF | RTC_ISR_RSF | RTC_ISR_INITS;
        ALIAS(ADC1->SR) |= ADC_SR_EOC;                        /* conversion done      */

        usleep(20);
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

static void group(const char* name)
{
    printf("\n[%s]\n", name);
    mon_accesses_at_group_start = mon_accesses;
}

/** @brief Close a group: report any sequencing violations it produced. */
static void group_end(void)
{
    char label[96];
    snprintf(label, sizeof(label), "RM0383 sequencing rules over %lu register accesses",
             mon_accesses - mon_accesses_at_group_start);
    tests_run++;
    if (violation_count == violations_reported)
    {
        printf("  ok    %-50s\n", label);
        return;
    }
    tests_failed++;
    printf("  FAIL  %s\n", label);
    for (; violations_reported < violation_count; violations_reported++)
    {
        printf("        %s\n", violations[violations_reported]);
    }
}

/*
 * Expected values are recomputed here from RM0383 using plain arithmetic,
 * deliberately not by reusing the driver's own helper macros - otherwise a
 * wrong formula would agree with itself and the test would pass.
 */
static void test_clock(void)
{
    group("RCC / clock tree");

    check_eq("bsp_clock_init() returns 0", (uint64_t)(uint32_t)DRIVE(bsp_clock_init()), 0);

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

    group_end();
}

static void test_gpio(void)
{
    group("GPIO");

    DRIVE(bsp_gpio_enable_port(GPIOC));
    check_eq("AHB1ENR.GPIOCEN", (RCC->AHB1ENR & RCC_AHB1ENR_GPIOCEN) ? 1 : 0, 1);

    DRIVE(bsp_gpio_config_output(GPIOC, 13, GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_LOW, GPIO_PUPD_NONE));
    check_eq("MODER pin 13 = output", (GPIOC->MODER >> 26) & 0x3, GPIO_MODER_OUTPUT);
    check_eq("OTYPER pin 13 = push-pull", (GPIOC->OTYPER >> 13) & 0x1, GPIO_OTYPER_PUSHPULL);
    check_eq("MODER left neighbouring pins alone", GPIOC->MODER & ~(0x3U << 26), 0);

    DRIVE(bsp_gpio_write(GPIOC, 13, false));
    check_eq("BSRR reset half for a low write", (GPIOC->BSRR >> 16) & 0xFFFF, 1U << 13);
    DRIVE(bsp_gpio_write(GPIOC, 13, true));
    check_eq("BSRR set half for a high write", GPIOC->BSRR & 0xFFFF, 1U << 13);

    /* Pin 9 is in the high AF register, nibble 9-8 = 1. */
    DRIVE(bsp_gpio_config_alternate(GPIOA, 9, 7, GPIO_OTYPER_PUSHPULL, GPIO_OSPEED_VERY_HIGH,
                                    GPIO_PUPD_NONE));
    check_eq("AFR[1] nibble 1 holds AF7", (GPIOA->AFR[1] >> 4) & 0xF, 7);
    check_eq("MODER pin 9 = alternate", (GPIOA->MODER >> 18) & 0x3, GPIO_MODER_AF);

    /* Pin 3 is in the low AF register, nibble 3. */
    DRIVE(bsp_gpio_config_alternate(GPIOB, 3, 9, GPIO_OTYPER_OPENDRAIN, GPIO_OSPEED_VERY_HIGH,
                                    GPIO_PUPD_UP));
    check_eq("AFR[0] nibble 3 holds AF9", (GPIOB->AFR[0] >> 12) & 0xF, 9);
    check_eq("OTYPER pin 3 = open drain", (GPIOB->OTYPER >> 3) & 0x1, GPIO_OTYPER_OPENDRAIN);
    check_eq("PUPDR pin 3 = pull-up", (GPIOB->PUPDR >> 6) & 0x3, GPIO_PUPD_UP);

    DRIVE(bsp_gpio_config_analog(GPIOA, 0));
    check_eq("MODER pin 0 = analog", GPIOA->MODER & 0x3, GPIO_MODER_ANALOG);

    group_end();
}

static void test_spi(void)
{
    group("SPI");

    DRIVE(bsp_spi_init());
    check_eq("SPI2 enabled after init", (SPI2->CR1 & SPI_CR1_SPE) ? 1 : 0, 1);
    check_eq("SPI2 is master", (SPI2->CR1 & SPI_CR1_MSTR) ? 1 : 0, 1);

    /* RM0383: BR is log2(divider) - 1. */
    const struct { uint32_t div, br; } cases[] = {
        {2, 0}, {4, 1}, {8, 2}, {16, 3}, {32, 4}, {64, 5}, {128, 6}, {256, 7},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        char label[64];
        snprintf(label, sizeof(label), "CR1.BR for divider /%u", cases[i].div);
        DRIVE(bsp_spi_set_baud_divider(SPI2, cases[i].div));
        check_eq(label, FIELD(SPI2->CR1, SPI_CR1_BR_MSK, SPI_CR1_BR_POS), cases[i].br);
    }
    check_eq("SPE restored after the baud change", (SPI2->CR1 & SPI_CR1_SPE) ? 1 : 0, 1);

    /* The fake slave answers with the complement of each frame. */
    check_eq("transfer returns the received frame", DRIVE(bsp_spi_transfer(SPI2, 0xA5)), 0x5A);

    group_end();
}

static void test_usart(void)
{
    group("USART");

    DRIVE(bsp_usart_init());
    check_eq("USART2 UE|TE|RE after init",
             USART2->CR1 & (USART_CR1_UE | USART_CR1_TE | USART_CR1_RE),
             USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);

    /* OVER8 = 0, so BRR is simply the 12.4 fixed-point value of fCK / baud. */
    const uint32_t pclk = BSP_PCLK1_HZ;
    const uint32_t bauds[] = {9600, 38400, 115200, 921600};
    for (unsigned i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++)
    {
        char label[64];
        snprintf(label, sizeof(label), "BRR at %u baud", bauds[i]);
        DRIVE(bsp_usart_set_baud(USART2, bauds[i]));
        check_eq(label, USART2->BRR, (pclk + bauds[i] / 2U) / bauds[i]);
    }

    /* Round trip: the divider must reproduce the baud rate within 2%. */
    DRIVE(bsp_usart_set_baud(USART2, 115200));
    const uint32_t actual = pclk * 16U / (USART2->BRR * 16U);
    const uint32_t err_permille = (actual > 115200U ? actual - 115200U : 115200U - actual)
                                  * 1000U / 115200U;
    check_eq("115200 baud error under 2%", err_permille < 20U, 1);

    DRIVE(bsp_usart_write_byte(USART2, 'Z'));
    check_eq("write_byte lands in DR", USART2->DR, 'Z');

    group_end();
}

static void test_adc(void)
{
    group("ADC");

    DRIVE(bsp_adc_init());
    check_eq("ADON set after init", (ADC1->CR2 & ADC_CR2_ADON) ? 1 : 0, 1);

    ADC1->SMPR1 = 0;
    ADC1->SMPR2 = 0;
    SIM_POKE(ADC1->DR, 0x0ABC);

    check_eq("read returns the DR contents", DRIVE(bsp_adc_read(3)), 0x0ABC);
    check_eq("SQR3 holds the requested channel", ADC1->SQR3 & 0x1F, 3);
    check_eq("SMPR2 field for channel 3", (ADC1->SMPR2 >> 9) & 0x7, EXPECTED_SMP);

    /* Internal channels are high impedance and need >= 10 us regardless of
     * what the user configured for the external ones. */
    DRIVE(bsp_adc_enable_internal_channels());
    (void)DRIVE(bsp_adc_read(ADC_CHANNEL_TEMPSENSOR));
    check_eq("SQR3 holds channel 18", ADC1->SQR3 & 0x1F, 18);
    check_eq("SMPR1 forces 480 cycles on ch 18", (ADC1->SMPR1 >> 24) & 0x7, ADC_SMP_480CYCLES);

    (void)DRIVE(bsp_adc_read(ADC_CHANNEL_VREFINT));
    check_eq("SMPR1 forces 480 cycles on ch 17", (ADC1->SMPR1 >> 21) & 0x7, ADC_SMP_480CYCLES);

    check_eq("full scale converts to Vref", bsp_adc_to_millivolts(4095, 3300), 3300);
    check_eq("mid scale converts to Vref/2", bsp_adc_to_millivolts(2048, 3300), 1650);
    check_eq("zero converts to 0 mV", bsp_adc_to_millivolts(0, 3300), 0);

    group_end();
}

static void test_tim(void)
{
    group("TIM");

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    DRIVE(bsp_tim_config(TIM3, 95, 999));
    check_eq("PSC", TIM3->PSC, 95);
    check_eq("ARR", TIM3->ARR, 999);

    TIM3->CCER = 0;
    TIM3->SR = 0xFFFF;
    DRIVE(bsp_tim_pwm_config(TIM3, 1, 250, false));
    check_eq("CCR1 holds the duty", TIM3->CCR1, 250);
    check_eq("CCMR1.OC1M = PWM mode 1",
             FIELD(TIM3->CCMR1, TIM_CCMR1_OC1M_MSK, TIM_CCMR1_OC1M_POS), TIM_OCMODE_PWM1);
    check_eq("CCMR1.OC1PE preload enabled", (TIM3->CCMR1 & TIM_CCMR1_OC1PE) ? 1 : 0, 1);
    check_eq("CCER.CC1E enabled", (TIM3->CCER & TIM_CCER_CC1E) ? 1 : 0, 1);
    /* The forced update must not leave a pending interrupt behind. */
    check_eq("UIF cleared after the forced update", (TIM3->SR & TIM_SR_UIF) ? 1 : 0, 0);
    check_eq("CR1.URS restored afterwards", (TIM3->CR1 & TIM_CR1_URS) ? 1 : 0, 0);

    DRIVE(bsp_tim_pwm_config(TIM3, 2, 100, true));
    check_eq("CCMR1.OC2M = PWM mode 2 when inverted",
             FIELD(TIM3->CCMR1, TIM_CCMR1_OC2M_MSK, TIM_CCMR1_OC2M_POS), TIM_OCMODE_PWM2);
    check_eq("channel 1 settings untouched", TIM3->CCR1, 250);

    /* TIM1 is the only advanced timer here and needs its main output enable. */
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    TIM1->BDTR = 0;
    DRIVE(bsp_tim_config(TIM1, 0, 100));
    DRIVE(bsp_tim_pwm_config(TIM1, 1, 50, false));
    check_eq("TIM1 BDTR.MOE set for the advanced timer",
             (TIM1->BDTR & TIM_BDTR_MOE) ? 1 : 0, 1);

    group_end();
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

    DRIVE(bsp_dma_init());

    /* SPI2_TX is DMA1 stream 4 channel 0 (RM0383 table 27). */
    const bsp_dma_config_t cfg = {
        .controller = DMA1,
        .stream = 4,
        .channel = 0,
        .direction = DMA_DIR_MEM_TO_PERIPH,
        .periph_size = DMA_SIZE_BYTE,
        .memory_size = DMA_SIZE_BYTE,
        .priority = DMA_PRIORITY_HIGH,
        .periph_increment = false,
        .memory_increment = true,
        .circular = false,
        .interrupt_on_complete = false,
    };
    DRIVE(bsp_dma_configure(&cfg));
    const uint32_t cr = DMA1->STREAM[4].CR;
    check_eq("CR.CHSEL", FIELD(cr, DMA_CR_CHSEL_MSK, DMA_CR_CHSEL_POS), 0);
    check_eq("CR.DIR = memory to peripheral", FIELD(cr, DMA_CR_DIR_MSK, DMA_CR_DIR_POS), 1);
    check_eq("CR.PSIZE = byte", FIELD(cr, DMA_CR_PSIZE_MSK, DMA_CR_PSIZE_POS), 0);
    check_eq("CR.MINC set, PINC clear", cr & (DMA_CR_MINC | DMA_CR_PINC), DMA_CR_MINC);
    check_eq("CR.PL = high", FIELD(cr, DMA_CR_PL_MSK, DMA_CR_PL_POS), 2);
    check_eq("stream left disabled after configure", cr & DMA_CR_EN, 0);

    static uint8_t buffer[16];
    DRIVE(bsp_dma_start(&cfg, (uint32_t)(uintptr_t)&SPI2->DR, (uint32_t)(uintptr_t)buffer, 16));
    check_eq("PAR = SPI2 DR", DMA1->STREAM[4].PAR, (uint32_t)(uintptr_t)&SPI2->DR);
    check_eq("NDTR = 16", DMA1->STREAM[4].NDTR, 16);
    check_eq("EN set by start", (DMA1->STREAM[4].CR & DMA_CR_EN) ? 1 : 0, 1);

    DRIVE(bsp_dma_stop(&cfg));
    check_eq("EN clear after stop", DMA1->STREAM[4].CR & DMA_CR_EN, 0);

    group_end();
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

    DRIVE(bsp_i2c_init());
    check_i2c_instance("I2C1", I2C1, BSP_I2C1_FREQ_HZ);
    check_i2c_instance("I2C2", I2C2, BSP_I2C2_FREQ_HZ);
    check_i2c_instance("I2C3", I2C3, BSP_I2C3_FREQ_HZ);

    /* Transfers against the fake slave, which echoes 0x10, 0x11, 0x12 ... The
     * one-, two- and many-byte receive paths are separate code in the driver
     * (RM0383 18.3.3 gives each its own NACK/STOP choreography), so all three
     * get exercised. */
    uint8_t rx[5];
    const uint8_t tx[3] = {0xDE, 0xAD, 0xBE};

    check_eq("write of 3 bytes completes", (uint64_t)(uint32_t)DRIVE(bsp_i2c_write(I2C1, 0x50, tx, 3)), 0);
    check_eq("I2C1 bus released after write", I2C1->SR2 & I2C_SR2_BUSY, 0);

    memset(rx, 0, sizeof(rx));
    check_eq("read of 1 byte completes", (uint64_t)(uint32_t)DRIVE(bsp_i2c_read(I2C1, 0x50, rx, 1)), 0);
    check_eq("1-byte read data", rx[0], i2c_pattern(0));

    memset(rx, 0, sizeof(rx));
    check_eq("read of 2 bytes completes", (uint64_t)(uint32_t)DRIVE(bsp_i2c_read(I2C1, 0x50, rx, 2)), 0);
    check_eq("2-byte read data", ((uint32_t)rx[0] << 8) | rx[1],
             ((uint32_t)i2c_pattern(0) << 8) | i2c_pattern(1));
    check_eq("POS cleared after the 2-byte read", I2C1->CR1 & I2C_CR1_POS, 0);

    memset(rx, 0, sizeof(rx));
    check_eq("read of 5 bytes completes", (uint64_t)(uint32_t)DRIVE(bsp_i2c_read(I2C1, 0x50, rx, 5)), 0);
    uint32_t got = 0, want = 0;
    for (unsigned i = 0; i < 5; i++)
    {
        got = (got << 6) ^ rx[i];
        want = (want << 6) ^ i2c_pattern(i);
    }
    check_eq("5-byte read data", got, want);
    check_eq("NACK armed on the last byte (ACK=0)", I2C1->CR1 & I2C_CR1_ACK, 0);

    uint8_t value = 0;
    check_eq("read_reg completes", (uint64_t)(uint32_t)DRIVE(bsp_i2c_read_reg(I2C2, 0x68, 0x75, &value)), 0);
    check_eq("read_reg data", value, i2c_pattern(0));

    check_eq("ping of a present slave", (uint64_t)(uint32_t)DRIVE(bsp_i2c_ping(I2C3, 0x50)), 0);
    check_eq("ping of an absent slave reports NACK",
             (uint64_t)(uint32_t)DRIVE(bsp_i2c_ping(I2C3, I2C_ABSENT_ADDRESS)), (uint32_t)BSP_I2C_ENACK);
    check_eq("AF cleared after the NACK", I2C3->SR1 & I2C_SR1_AF, 0);
    check_eq("I2C3 bus released after the NACK", I2C3->SR2 & I2C_SR2_BUSY, 0);

    group_end();
}

int main(void)
{
    for (size_t i = 0; i < REGION_COUNT; i++)
    {
        if (map_region(&regions[i]) != 0)
        {
            fprintf(stderr, "the peripheral region is not available in this process\n");
            return 2;
        }
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

    monitor_install();
    monitor_arm();

    test_clock();
    test_gpio();
    test_spi();
    test_usart();
    test_adc();
    test_tim();
    test_dma();
    test_i2c();

    monitor_disarm();
    atomic_store(&sim_stop, 1);
    pthread_join(th, NULL);

    printf("\n%u checks, %u failed; %lu register accesses traced\n", tests_run, tests_failed,
           mon_accesses);
    return tests_failed == 0 ? 0 : 1;
}
