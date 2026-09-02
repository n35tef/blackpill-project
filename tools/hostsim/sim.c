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
    {0x50000000UL, 0x00040000UL, NULL}, /* AHB2 (OTG_FS FIFOs)*/
    {0xE0000000UL, 0x00100000UL, NULL}, /* Cortex-M private   */
    {0x1FFF7000UL, 0x00001000UL, NULL}, /* system memory: UID */
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
    GATED(SDIO_BASE, 0x400, APB2ENR, RCC_APB2ENR_SDIOEN, "SDIO"),
    GATED(OTG_FS_BASE, 0x40000, AHB2ENR, RCC_AHB2ENR_OTGFSEN, "OTG_FS"),
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

/* -- SD card constants (SD Physical Layer spec; private to bsp_sdio.c) ---- */

#define SD_OCR_VOLTAGE_WINDOW 0x00FF8000UL
#define SD_ACMD41_HCS         (1UL << 30)
#define SD_OCR_CCS            (1UL << 30)
#define SD_OCR_BUSY           (1UL << 31)
#define SD_BUS_WIDTH_4        0x2UL
#define SD_R1_OUT_OF_RANGE    (1UL << 31)
#define SD_R1_ADDRESS_ERROR   (1UL << 30)
#define SD_R1_BLOCK_LEN_ERROR (1UL << 29)
#define SD_R1_ILLEGAL_COMMAND (1UL << 22)
#define SD_R1_CURRENT_STATE_POS 9U
#define SD_R1_READY_FOR_DATA  (1UL << 8)
#define SD_R1_APP_CMD         (1UL << 5)

/* CDC-ACM class requests (USB CDC PSTN 1.2, table 13; private to bsp_usb_cdc.c) */
#define CDC_REQ_SET_LINE_CODING        0x20U
#define CDC_REQ_GET_LINE_CODING        0x21U
#define CDC_REQ_SET_CONTROL_LINE_STATE 0x22U

/* -- SDIO: fake SD card plus RM0383 / SD-spec rules ------------------------ */

/*
 * The card model answers the command set bsp_sdio.c uses (CMD0/2/3/7/8/9/12/
 * 13/16/17/18/24/25/55, ACMD6/41) and moves data through a 32-word FIFO the
 * way the controller does: the card pushes or pulls up to 8 words each time
 * the driver looks at STA, so a driver that services RXFIFOHF/TXFIFOHE too
 * slowly sees RXOVERR rather than a conveniently infinite FIFO. It can be a
 * v2 SDHC card (block addressing, CSD v2) or a v1 SDSC card (byte addressing,
 * CSD v1, no answer to CMD8), so both identification paths run.
 */

#define SD_FAKE_BLOCKS 8U
#define SD_FIFO_DEPTH  32U
#define SD_MODEL_HC_CSIZE 15231U /* CSD v2 C_SIZE: (15231 + 1) * 1024 blocks, an 8 GB card */
#define SD_MODEL_SC_READ_BL_LEN 10U /* 1024-byte native blocks                             */
#define SD_MODEL_SC_CSIZE       1999U
#define SD_MODEL_SC_CSIZE_MULT  7U   /* (1999 + 1) * 2^9 native blocks = 1 GB               */
#define SD_MODEL_RCA 0x1234U

typedef enum
{
    SD_S_IDLE = 0, SD_S_READY, SD_S_IDENT, SD_S_STBY, SD_S_TRAN, SD_S_DATA, SD_S_RCV, SD_S_PRG
} sd_card_state_t;

typedef enum
{
    SD_X_NONE, SD_X_READ, SD_X_WRITE_WAIT_DCTRL, SD_X_WRITE
} sd_xfer_t;

static struct
{
    bool high_capacity;   /* which card is inserted                          */
    bool present;
    sd_card_state_t state;
    bool app_cmd;         /* CMD55 seen, next command is an ACMD             */
    bool saw_cmd8;
    unsigned acmd41_polls;
    bool wide_bus;        /* ACMD6 accepted                                  */
    unsigned busy_polls;  /* CMD13 answers "programming" this many more times */

    uint64_t power_on_at, clock_on_at;
    bool clock_was_on;

    sd_xfer_t xfer;
    bool multi_open;      /* CMD18/25 without CMD12 yet                      */
    uint32_t block, byte_offset, total_words, moved_words;
    uint32_t fifo[SD_FIFO_DEPTH];
    unsigned fifo_head, fifo_level;

    uint8_t storage[SD_FAKE_BLOCKS][512];
    unsigned commands;    /* for the test to see the fake was exercised      */
} sd;

/** @brief Insert a card: SDHC v2 or SDSC v1. */
static void sd_insert(bool high_capacity)
{
    memset(&sd, 0, sizeof(sd));
    sd.present = true;
    sd.high_capacity = high_capacity;
    for (unsigned b = 0; b < SD_FAKE_BLOCKS; b++)
    {
        for (unsigned i = 0; i < 512; i++)
        {
            sd.storage[b][i] = (uint8_t)(0xA0U + b + i);
        }
    }
}

static uint32_t sd_clock_hz(void)
{
    const uint32_t clkcr = ALIAS(SDIO->CLKCR);
    if (clkcr & SDIO_CLKCR_BYPASS)
    {
        return BSP_PLL_Q_OUT_HZ;
    }
    return BSP_PLL_Q_OUT_HZ / (FIELD(clkcr, SDIO_CLKCR_CLKDIV_MSK, SDIO_CLKCR_CLKDIV_POS) + 2U);
}

static uint32_t sd_r1(uint32_t extra)
{
    return ((uint32_t)sd.state << SD_R1_CURRENT_STATE_POS) |
           ((sd.state == SD_S_TRAN) ? SD_R1_READY_FOR_DATA : 0U) | extra;
}

/** @brief Deliver a short or long response with the controller-side flags. */
static void sd_respond(uint8_t index, bsp_sd_resp_t kind, const uint32_t* words)
{
    uint32_t sta = ALIAS(SDIO->STA) & ~(SDIO_STA_CMDACT);
    switch (kind)
    {
        case SD_RESP_NONE:
            sta |= SDIO_STA_CMDSENT;
            break;
        case SD_RESP_R3:
            /* R3 has 0x7F where the CRC goes; the controller reports it as a
             * CRC failure, and a driver that treats that as an error can
             * never bring a card up. */
            sta |= SDIO_STA_CCRCFAIL;
            ALIAS(SDIO->RESPCMD) = 0x3FU;
            ALIAS(SDIO->RESP1) = words[0];
            break;
        case SD_RESP_R2:
            sta |= SDIO_STA_CMDREND;
            ALIAS(SDIO->RESPCMD) = 0x3FU;
            ALIAS(SDIO->RESP1) = words[0];
            ALIAS(SDIO->RESP2) = words[1];
            ALIAS(SDIO->RESP3) = words[2];
            ALIAS(SDIO->RESP4) = words[3];
            break;
        default:
            sta |= SDIO_STA_CMDREND;
            ALIAS(SDIO->RESPCMD) = index;
            ALIAS(SDIO->RESP1) = words[0];
            break;
    }
    ALIAS(SDIO->STA) = sta;
}

/** @brief No card answers: CTIMEOUT, unless no response was expected (CMDSENT). */
static void sd_timeout(void)
{
    const bool no_response =
        FIELD(ALIAS(SDIO->CMD), SDIO_CMD_WAITRESP_MSK, SDIO_CMD_WAITRESP_POS) == SDIO_WAITRESP_NONE;
    ALIAS(SDIO->STA) = (ALIAS(SDIO->STA) & ~SDIO_STA_CMDACT) |
                       (no_response ? SDIO_STA_CMDSENT : SDIO_STA_CTIMEOUT);
}

static void sd_fifo_reset(void)
{
    sd.fifo_head = 0;
    sd.fifo_level = 0;
}

static void sd_fifo_push(uint32_t word)
{
    sd.fifo[(sd.fifo_head + sd.fifo_level) % SD_FIFO_DEPTH] = word;
    sd.fifo_level++;
    if (sd.fifo_level == 1U)
    {
        ALIAS(SDIO->FIFO) = word; /* the next read sees the head word */
    }
}

static uint32_t sd_fifo_pop(void)
{
    const uint32_t word = sd.fifo[sd.fifo_head];
    sd.fifo_head = (sd.fifo_head + 1U) % SD_FIFO_DEPTH;
    sd.fifo_level--;
    ALIAS(SDIO->FIFO) = sd.fifo_level ? sd.fifo[sd.fifo_head] : 0xDEADBEEFU;
    return word;
}

/** @brief Word @p n of the current transfer, from the card's storage. */
static uint32_t sd_storage_word(uint32_t n)
{
    const uint32_t byte = sd.byte_offset + 4U * n;
    const uint32_t block = sd.block + byte / 512U;
    if (block >= SD_FAKE_BLOCKS)
    {
        return 0U;
    }
    uint32_t w;
    memcpy(&w, &sd.storage[block][byte % 512U], 4U);
    return w;
}

static void sd_storage_store(uint32_t n, uint32_t w)
{
    const uint32_t byte = sd.byte_offset + 4U * n;
    const uint32_t block = sd.block + byte / 512U;
    if (block < SD_FAKE_BLOCKS)
    {
        memcpy(&sd.storage[block][byte % 512U], &w, 4U);
    }
}

/** @brief Refresh the FIFO level flags in STA from the model. */
static void sd_update_fifo_flags(void)
{
    uint32_t sta = ALIAS(SDIO->STA) &
                   ~(SDIO_STA_RXDAVL | SDIO_STA_RXFIFOHF | SDIO_STA_RXFIFOF | SDIO_STA_RXFIFOE |
                     SDIO_STA_TXDAVL | SDIO_STA_TXFIFOHE | SDIO_STA_TXFIFOF | SDIO_STA_TXFIFOE |
                     SDIO_STA_RXACT | SDIO_STA_TXACT);
    if (sd.xfer == SD_X_READ || (sd.xfer == SD_X_NONE && sd.fifo_level))
    {
        sta |= sd.fifo_level ? SDIO_STA_RXDAVL : SDIO_STA_RXFIFOE;
        sta |= (sd.fifo_level >= 8U) ? SDIO_STA_RXFIFOHF : 0U;
        sta |= (sd.fifo_level >= SD_FIFO_DEPTH) ? SDIO_STA_RXFIFOF : 0U;
        sta |= (sd.xfer == SD_X_READ) ? SDIO_STA_RXACT : 0U;
    }
    else if (sd.xfer == SD_X_WRITE)
    {
        sta |= sd.fifo_level ? SDIO_STA_TXDAVL : SDIO_STA_TXFIFOE;
        sta |= (sd.fifo_level <= SD_FIFO_DEPTH - 8U) ? SDIO_STA_TXFIFOHE : 0U;
        sta |= (sd.fifo_level >= SD_FIFO_DEPTH) ? SDIO_STA_TXFIFOF : 0U;
        sta |= SDIO_STA_TXACT;
    }
    else
    {
        sta |= SDIO_STA_TXFIFOE | SDIO_STA_TXFIFOHE | SDIO_STA_RXFIFOE;
    }
    ALIAS(SDIO->STA) = sta;
}

/** @brief The card moves up to 8 words per look at STA. */
static void sd_advance(uintptr_t addr)
{
    if (sd.xfer == SD_X_READ)
    {
        unsigned n = 0;
        while (n < 8U && sd.moved_words < sd.total_words)
        {
            if (sd.fifo_level >= SD_FIFO_DEPTH)
            {
                violation(addr, "SDIO receive FIFO overrun: 32 words unread while the card kept sending");
                ALIAS(SDIO->STA) |= SDIO_STA_RXOVERR;
                sd.xfer = SD_X_NONE;
                return;
            }
            sd_fifo_push(sd_storage_word(sd.moved_words));
            sd.moved_words++;
            n++;
        }
        if (sd.moved_words == sd.total_words)
        {
            ALIAS(SDIO->STA) |= SDIO_STA_DATAEND | SDIO_STA_DBCKEND;
            sd.xfer = SD_X_NONE; /* the FIFO still holds the tail; RXDAVL says so */
            sd.state = sd.multi_open ? SD_S_DATA : SD_S_TRAN;
        }
    }
    else if (sd.xfer == SD_X_WRITE)
    {
        unsigned n = 0;
        while (n < 8U && sd.fifo_level > 0U)
        {
            sd_storage_store(sd.moved_words, sd_fifo_pop());
            sd.moved_words++;
            n++;
        }
        if (sd.moved_words == sd.total_words)
        {
            ALIAS(SDIO->STA) |= SDIO_STA_DATAEND | SDIO_STA_DBCKEND;
            sd.xfer = SD_X_NONE;
            sd.state = sd.multi_open ? SD_S_RCV : SD_S_PRG;
            sd.busy_polls = 2; /* programming: CMD13 says so twice */
        }
    }
    sd_update_fifo_flags();
}

static void sd_build_csd(uint32_t csd[4])
{
    memset(csd, 0, 16);
    if (sd.high_capacity)
    {
        /* CSD v2: CSD_STRUCTURE = 1 at [127:126]; C_SIZE at [69:48]. */
        csd[0] = 1UL << 30;
        csd[1] |= (SD_MODEL_HC_CSIZE >> 16) & 0x3FU;     /* bits 69:64 */
        csd[2] |= (SD_MODEL_HC_CSIZE & 0xFFFFU) << 16;   /* bits 63:48 */
    }
    else
    {
        /* CSD v1: READ_BL_LEN [83:80], C_SIZE [73:62], C_SIZE_MULT [49:47]. */
        csd[1] |= SD_MODEL_SC_READ_BL_LEN << 16;         /* bits 83:80 */
        csd[1] |= (SD_MODEL_SC_CSIZE >> 2) & 0x3FFU;     /* bits 73:64 */
        csd[2] |= (SD_MODEL_SC_CSIZE & 0x3U) << 30;      /* bits 63:62 */
        csd[2] |= SD_MODEL_SC_CSIZE_MULT << 15;          /* bits 49:47 */
    }
}

/** @brief Block count the model's CSD encodes, derived by SD-spec arithmetic. */
static uint32_t sd_model_block_count(bool high_capacity)
{
    if (high_capacity)
    {
        return (SD_MODEL_HC_CSIZE + 1U) * 1024U;
    }
    const uint64_t bytes = (uint64_t)(SD_MODEL_SC_CSIZE + 1U) *
                           (1ULL << (SD_MODEL_SC_CSIZE_MULT + 2U)) *
                           (1ULL << SD_MODEL_SC_READ_BL_LEN);
    return (uint32_t)(bytes / 512U);
}

/** @brief Address argument -> block number, checking the addressing mode. */
static uint32_t sd_block_of(uintptr_t addr, uint32_t arg)
{
    if (sd.high_capacity)
    {
        return arg;
    }
    if (arg % 512U)
    {
        violation(addr, "SDSC card given byte address 0x%08X that is not block aligned", arg);
    }
    return arg / 512U;
}

static void sd_command(uintptr_t addr, uint32_t cmd)
{
    const uint8_t index = (uint8_t)FIELD(cmd, SDIO_CMD_CMDINDEX_MSK, SDIO_CMD_CMDINDEX_POS);
    const uint32_t arg = ALIAS(SDIO->ARG);
    const uint32_t clkcr = ALIAS(SDIO->CLKCR);
    const bool acmd = sd.app_cmd;
    uint32_t r[4] = {0, 0, 0, 0};
    sd.app_cmd = false;
    sd.commands++;

    /* Controller-side preconditions. */
    if (FIELD(ALIAS(SDIO->POWER), SDIO_POWER_PWRCTRL_MSK, SDIO_POWER_PWRCTRL_POS) != SDIO_PWRCTRL_ON)
    {
        violation(addr, "command sent with SDIO POWER off");
    }
    if (!(clkcr & SDIO_CLKCR_CLKEN))
    {
        violation(addr, "command sent with CLKCR.CLKEN=0: no SDIO_CK, nothing leaves the pin");
    }
    if (ALIAS(SDIO->STA) & SDIO_STA_CMDACT)
    {
        violation(addr, "command sent while the previous one is still active");
    }
    if (!sd.present)
    {
        sd_timeout();
        return;
    }

    /* Identification runs at <= 400 kHz until the card has an address. */
    const bool ident_cmd = (index == 0U) || (index == 8U) || (index == 55U) || (index == 2U) ||
                           (index == 3U) || (acmd && index == 41U);
    if (ident_cmd && sd.state <= SD_S_IDENT && sd_clock_hz() > 400000U)
    {
        violation(addr, "CMD%u during identification at %u Hz; SD spec caps it at 400 kHz", index,
                  sd_clock_hz());
    }
    if (index == 0U && sd.commands == 1U)
    {
        /* SD spec 6.4.1: 1 ms after power then 74 clocks before the first command. */
        const uint64_t ck = sd_clock_hz();
        const uint64_t needed = 74ULL * BSP_HCLK_HZ / (ck ? ck : 1U);
        if (sim_cycles - sd.clock_on_at < needed)
        {
            violation(addr, "CMD0 %llu cycles after CLKEN; the card needs 74 SDIO_CK = %llu cycles",
                      (unsigned long long)(sim_cycles - sd.clock_on_at), (unsigned long long)needed);
        }
    }

    if (acmd)
    {
        switch (index)
        {
            case 41U: /* SD_SEND_OP_COND: R3 OCR */
                if ((arg & SD_ACMD41_HCS) && !sd.saw_cmd8)
                {
                    violation(addr, "ACMD41 with HCS set but no successful CMD8 first (SD spec 4.2.3)");
                }
                sd.acmd41_polls++;
                r[0] = SD_OCR_VOLTAGE_WINDOW;
                if (sd.acmd41_polls >= 3U) /* takes a couple of polls, like real cards */
                {
                    r[0] |= SD_OCR_BUSY | (sd.high_capacity ? SD_OCR_CCS : 0U);
                    sd.state = SD_S_READY;
                }
                sd_respond(index, SD_RESP_R3, r);
                return;
            case 6U: /* SET_BUS_WIDTH */
                if (sd.state != SD_S_TRAN)
                {
                    violation(addr, "ACMD6 outside transfer state");
                    r[0] = sd_r1(SD_R1_ILLEGAL_COMMAND);
                }
                else if (FIELD(clkcr, SDIO_CLKCR_WIDBUS_MSK, SDIO_CLKCR_WIDBUS_POS) != SDIO_WIDBUS_1BIT)
                {
                    violation(addr, "ACMD6 sent after WIDBUS was already widened: the card still answers on one line");
                    r[0] = sd_r1(0);
                }
                else
                {
                    sd.wide_bus = (arg == SD_BUS_WIDTH_4);
                    r[0] = sd_r1(0);
                }
                sd_respond(index, SD_RESP_R1, r);
                return;
            default:
                break; /* fall through to the plain command set */
        }
    }

    switch (index)
    {
        case 0U: /* GO_IDLE_STATE */
            sd.state = SD_S_IDLE;
            sd.saw_cmd8 = false;
            sd.acmd41_polls = 0;
            sd.wide_bus = false;
            sd_respond(index, SD_RESP_NONE, r);
            return;

        case 8U: /* SEND_IF_COND: v2 only */
            if (!sd.high_capacity)
            {
                sd_timeout(); /* a v1 card does not know CMD8 */
                return;
            }
            if (sd.state != SD_S_IDLE)
            {
                violation(addr, "CMD8 outside idle state");
            }
            sd.saw_cmd8 = true;
            r[0] = arg & 0xFFFU; /* echo the voltage window and check pattern */
            sd_respond(index, SD_RESP_R7, r);
            return;

        case 55U: /* APP_CMD */
            sd.app_cmd = true;
            r[0] = sd_r1(SD_R1_APP_CMD);
            sd_respond(index, SD_RESP_R1, r);
            return;

        case 2U: /* ALL_SEND_CID */
            if (sd.state != SD_S_READY)
            {
                violation(addr, "CMD2 before ACMD41 reported power-up done");
            }
            r[0] = 0x03534453U; /* MID 0x03, OID "SD" */
            r[1] = 0x53554E4BU;
            r[2] = 0x80123456U;
            r[3] = 0x00001234U;
            sd.state = SD_S_IDENT;
            sd_respond(index, SD_RESP_R2, r);
            return;

        case 3U: /* SEND_RELATIVE_ADDR: R6 */
            if (sd.state != SD_S_IDENT)
            {
                violation(addr, "CMD3 before CMD2");
            }
            sd.state = SD_S_STBY;
            r[0] = ((uint32_t)SD_MODEL_RCA << 16) | ((uint32_t)sd.state << 9) | (1UL << 8);
            sd_respond(index, SD_RESP_R6, r);
            return;

        case 9U: /* SEND_CSD: standby only */
            if ((arg >> 16) != SD_MODEL_RCA)
            {
                sd_timeout(); /* not addressed to us */
                return;
            }
            if (sd.state != SD_S_STBY)
            {
                violation(addr, "CMD9 sent to a selected card (SD spec: SEND_CSD is a stand-by command)");
            }
            sd_build_csd(r);
            sd_respond(index, SD_RESP_R2, r);
            return;

        case 7U: /* SELECT/DESELECT */
            if ((arg >> 16) == SD_MODEL_RCA)
            {
                if (sd.state != SD_S_STBY)
                {
                    violation(addr, "CMD7 select outside stand-by");
                }
                r[0] = sd_r1(0); /* status is the state before the command */
                sd.state = SD_S_TRAN;
                sd_respond(index, SD_RESP_R1B, r);
            }
            else
            {
                sd.state = SD_S_STBY;
                sd_timeout(); /* deselect has no response */
            }
            return;

        case 13U: /* SEND_STATUS */
            if (sd.state == SD_S_PRG && sd.busy_polls > 0U)
            {
                sd.busy_polls--;
                r[0] = sd_r1(0); /* programming, not ready */
                if (sd.busy_polls == 0U)
                {
                    sd.state = SD_S_TRAN;
                }
            }
            else
            {
                r[0] = sd_r1(0);
            }
            sd_respond(index, SD_RESP_R1, r);
            return;

        case 16U: /* SET_BLOCKLEN */
            if (sd.state != SD_S_TRAN)
            {
                violation(addr, "CMD16 outside transfer state");
                r[0] = sd_r1(SD_R1_ILLEGAL_COMMAND);
            }
            else if (arg != 512U && !sd.high_capacity)
            {
                r[0] = sd_r1(SD_R1_BLOCK_LEN_ERROR);
            }
            else
            {
                r[0] = sd_r1(0);
            }
            sd_respond(index, SD_RESP_R1, r);
            return;

        case 17U: case 18U: /* READ_SINGLE / READ_MULTIPLE */
        case 24U: case 25U: /* WRITE_BLOCK / WRITE_MULTIPLE */
        {
            const bool read = (index == 17U) || (index == 18U);
            const bool multi = (index == 18U) || (index == 25U);
            const uint32_t dctrl = ALIAS(SDIO->DCTRL);
            const uint32_t dlen = ALIAS(SDIO->DLEN);

            if (sd.state != SD_S_TRAN)
            {
                violation(addr, "CMD%u while the card is in state %u, not transfer (4)", index, sd.state);
                r[0] = sd_r1(SD_R1_ILLEGAL_COMMAND);
                sd_respond(index, SD_RESP_R1, r);
                return;
            }
            if (sd.multi_open)
            {
                violation(addr, "CMD%u while a multiple-block transfer is still open (no CMD12)", index);
            }
            if (!sd.high_capacity && (arg % 512U))
            {
                r[0] = sd_r1(SD_R1_ADDRESS_ERROR);
                sd_respond(index, SD_RESP_R1, r);
                return;
            }
            sd.block = sd_block_of(addr, arg);
            if (sd.block >= sd_model_block_count(sd.high_capacity))
            {
                r[0] = sd_r1(SD_R1_OUT_OF_RANGE);
                sd_respond(index, SD_RESP_R1, r);
                return;
            }

            /* RM0383 21.3.2 / 21.4: DPSM before the read command, after the
             * write command. */
            if (read)
            {
                if (!(dctrl & SDIO_DCTRL_DTEN) || !(dctrl & SDIO_DCTRL_DTDIR))
                {
                    violation(addr, "read CMD%u sent before DCTRL was armed with DTEN|DTDIR: the first block is lost", index);
                }
                if (FIELD(dctrl, SDIO_DCTRL_DBLOCKSIZE_MSK, SDIO_DCTRL_DBLOCKSIZE_POS) != 9U)
                {
                    violation(addr, "DBLOCKSIZE=%u, blocks are 2^9 bytes",
                              (unsigned)FIELD(dctrl, SDIO_DCTRL_DBLOCKSIZE_MSK, SDIO_DCTRL_DBLOCKSIZE_POS));
                }
                if (dlen == 0U || (dlen % 512U))
                {
                    violation(addr, "DLEN=%u is not a whole number of blocks", dlen);
                }
                if (ALIAS(SDIO->DTIMER) == 0U)
                {
                    violation(addr, "DTIMER=0: the data path would time out immediately");
                }
                sd.xfer = SD_X_READ;
                sd.state = SD_S_DATA;
            }
            else
            {
                if (dctrl & SDIO_DCTRL_DTEN)
                {
                    violation(addr, "write CMD%u sent with DCTRL.DTEN already set (RM0383 21.3.2: command first, then DCTRL)", index);
                }
                sd.xfer = SD_X_WRITE_WAIT_DCTRL;
                sd.state = SD_S_RCV;
            }
            sd.multi_open = multi;
            sd.byte_offset = 0;
            sd.total_words = dlen / 4U;
            sd.moved_words = 0;
            sd_fifo_reset();
            r[0] = ((uint32_t)SD_S_TRAN << SD_R1_CURRENT_STATE_POS) | SD_R1_READY_FOR_DATA;
            sd_respond(index, SD_RESP_R1, r);
            sd_update_fifo_flags();
            return;
        }

        case 12U: /* STOP_TRANSMISSION */
            if (!sd.multi_open)
            {
                violation(addr, "CMD12 with no multiple-block transfer open");
            }
            r[0] = sd_r1(0);
            sd.multi_open = false;
            sd.xfer = SD_X_NONE;
            if (sd.state == SD_S_RCV)
            {
                sd.state = SD_S_PRG;
                sd.busy_polls = 2;
            }
            else
            {
                sd.state = SD_S_TRAN;
            }
            sd_respond(index, SD_RESP_R1B, r);
            return;

        default:
            violation(addr, "CMD%u is not something this card model knows", index);
            r[0] = sd_r1(SD_R1_ILLEGAL_COMMAND);
            sd_respond(index, SD_RESP_R1, r);
            return;
    }
}

static void sim_sdio(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    if (!write)
    {
        if (addr == (uintptr_t)&SDIO->STA)
        {
            sd_advance(addr);
        }
        else if (addr == (uintptr_t)&SDIO->FIFO)
        {
            if (sd.fifo_level == 0U)
            {
                violation(addr, "SDIO FIFO read while empty (RXDAVL=0)");
            }
            else
            {
                (void)sd_fifo_pop();
                sd_update_fifo_flags();
            }
        }
        return;
    }

    if (addr == (uintptr_t)&SDIO->POWER)
    {
        if (FIELD(after, SDIO_POWER_PWRCTRL_MSK, SDIO_POWER_PWRCTRL_POS) == SDIO_PWRCTRL_ON &&
            FIELD(before, SDIO_POWER_PWRCTRL_MSK, SDIO_POWER_PWRCTRL_POS) != SDIO_PWRCTRL_ON)
        {
            sd.power_on_at = sim_cycles;
            sd.clock_was_on = false;
        }
    }
    else if (addr == (uintptr_t)&SDIO->CLKCR)
    {
        if (sd.xfer != SD_X_NONE)
        {
            violation(addr, "CLKCR changed during a data transfer");
        }
        if ((after & SDIO_CLKCR_CLKEN) && !sd.clock_was_on)
        {
            sd.clock_was_on = true;
            sd.clock_on_at = sim_cycles;
            /* SD spec 6.4.1.1: VDD must be stable for 1 ms before the clock. */
            if (sim_cycles - sd.power_on_at < BSP_HCLK_HZ / 1000U)
            {
                violation(addr, "CLKEN %llu cycles after power on; the card needs 1 ms = %u cycles",
                          (unsigned long long)(sim_cycles - sd.power_on_at), (unsigned)(BSP_HCLK_HZ / 1000U));
            }
        }
        const uint32_t widbus = FIELD(after, SDIO_CLKCR_WIDBUS_MSK, SDIO_CLKCR_WIDBUS_POS);
        if (widbus == SDIO_WIDBUS_4BIT && !sd.wide_bus)
        {
            violation(addr, "WIDBUS set to 4-bit before the card accepted ACMD6");
        }
        if (widbus == SDIO_WIDBUS_8BIT)
        {
            violation(addr, "WIDBUS 8-bit: SD cards have four data lines");
        }
        if (sd_clock_hz() > 25000000U && sd.state >= SD_S_STBY)
        {
            violation(addr, "SDIO_CK %u Hz exceeds 25 MHz default-speed mode", sd_clock_hz());
        }
        if (sd_clock_hz() > BSP_HCLK_HZ / 2U)
        {
            violation(addr, "SDIO_CK %u Hz exceeds HCLK/2 (RM0383 21.3)", sd_clock_hz());
        }
    }
    else if (addr == (uintptr_t)&SDIO->CMD)
    {
        if (after & SDIO_CMD_CPSMEN)
        {
            sd_command(addr, after);
        }
    }
    else if (addr == (uintptr_t)&SDIO->ICR)
    {
        /* Write-one-to-clear; only the static flags survive. */
        ALIAS(SDIO->STA) &= ~(after & SDIO_ICR_ALL_FLAGS);
        ALIAS(SDIO->ICR) = 0;
    }
    else if (addr == (uintptr_t)&SDIO->DCTRL)
    {
        if (after & SDIO_DCTRL_DTEN)
        {
            if (!(after & SDIO_DCTRL_DTDIR))
            {
                if (sd.xfer != SD_X_WRITE_WAIT_DCTRL)
                {
                    violation(addr, "DCTRL armed for a write with no write command pending");
                }
                else
                {
                    sd.xfer = SD_X_WRITE;
                    sd.total_words = ALIAS(SDIO->DLEN) / 4U;
                    sd.moved_words = 0;
                    sd_fifo_reset();
                }
            }
            if (ALIAS(SDIO->DLEN) % 512U)
            {
                violation(addr, "DTEN with DLEN not a multiple of the block size");
            }
            sd_update_fifo_flags();
        }
        else if (before & SDIO_DCTRL_DTEN)
        {
            sd.xfer = SD_X_NONE; /* aborted */
            sd_fifo_reset();
            sd_update_fifo_flags();
        }
    }
    else if (addr == (uintptr_t)&SDIO->DLEN)
    {
        if (ALIAS(SDIO->DCTRL) & SDIO_DCTRL_DTEN)
        {
            violation(addr, "DLEN written while DTEN=1");
        }
    }
    else if (addr == (uintptr_t)&SDIO->FIFO)
    {
        if (sd.xfer != SD_X_WRITE)
        {
            violation(addr, "SDIO FIFO written with no write transfer armed");
        }
        else if (sd.fifo_level >= SD_FIFO_DEPTH)
        {
            violation(addr, "SDIO transmit FIFO written while full");
        }
        else
        {
            sd_fifo_push(after);
            sd_update_fifo_flags();
        }
    }
}

/* -- OTG_FS: fake USB host plus RM0383 22.17 rules ------------------------- */

/*
 * The device core is interrupt driven, so the model plays the NVIC as well
 * as the host: every bus event lands in GINTSTS and OTG_FS_IRQHandler() is
 * called until nothing unmasked is pending, exactly as a level-sensitive
 * interrupt line would behave. Packets travel through a model of the shared
 * receive FIFO (status entries popped via GRXSTSP, data words behind them)
 * and per-endpoint transmit FIFOs whose contents the host collects on IN.
 */

void OTG_FS_IRQHandler(void); /* the vector the NVIC would call; bsp_usb.c */

#define USB_RX_QUEUE 16
#define USB_FIFO_PAGE 0x1000UL

typedef struct
{
    uint32_t status;
    uint32_t words[16];
    unsigned nwords;
} usb_rx_entry_t;

static struct
{
    /* receive FIFO model */
    usb_rx_entry_t queue[USB_RX_QUEUE];
    unsigned head, count;
    usb_rx_entry_t current; /* popped status; its words are read next     */
    unsigned current_read;

    /* transmit FIFO model, per IN endpoint */
    struct
    {
        uint8_t bytes[1024];
        unsigned nbytes;
        uint32_t xfrsiz, pktcnt;
        bool armed;
        uint64_t armed_at;
    } tx[OTG_FS_EP_COUNT];

    uint64_t fdmod_at;
    bool fdmod_seen;
    bool connected;   /* SDIS cleared                                  */
    unsigned irqs;
    unsigned in_tokens, out_tokens;
} usb;

static inline uint32_t* usb_alias(uintptr_t a)
{
    return (uint32_t*)alias_of((const volatile void*)a);
}

static uint32_t usb_tx_depth_words(unsigned ep)
{
    if (ep == 0U)
    {
        return FIELD(ALIAS(OTG_FS_GLOBAL->DIEPTXF0), OTG_DIEPTXF0_TX0FD_MSK, OTG_DIEPTXF0_TX0FD_POS);
    }
    const uint32_t reg = (ep == 1U) ? ALIAS(OTG_FS_GLOBAL->DIEPTXF1)
                       : (ep == 2U) ? ALIAS(OTG_FS_GLOBAL->DIEPTXF2)
                                    : ALIAS(OTG_FS_GLOBAL->DIEPTXF3);
    return FIELD(reg, OTG_DIEPTXF_INEPTXFD_MSK, OTG_DIEPTXF_INEPTXFD_POS);
}

static void usb_refresh_ep_summary(void)
{
    uint32_t daint = 0;
    for (unsigned ep = 0; ep < OTG_FS_EP_COUNT; ep++)
    {
        if (ALIAS(OTG_FS_DEVICE->INEP[ep].DIEPINT) & ~OTG_DIEPINT_TXFE)
        {
            daint |= 1UL << (OTG_DAINT_IEPINT_POS + ep);
        }
        if (ALIAS(OTG_FS_DEVICE->OUTEP[ep].DOEPINT))
        {
            daint |= 1UL << (OTG_DAINT_OEPINT_POS + ep);
        }
    }
    ALIAS(OTG_FS_DEVICE->DAINT) = daint;
    uint32_t gint = ALIAS(OTG_FS_GLOBAL->GINTSTS) & ~(OTG_GINTSTS_IEPINT | OTG_GINTSTS_OEPINT);
    if (daint & OTG_DAINT_IEPINT_MSK) { gint |= OTG_GINTSTS_IEPINT; }
    if (daint & OTG_DAINT_OEPINT_MSK) { gint |= OTG_GINTSTS_OEPINT; }
    ALIAS(OTG_FS_GLOBAL->GINTSTS) = gint;
}

static void usb_raise_in(unsigned ep, uint32_t flags)
{
    ALIAS(OTG_FS_DEVICE->INEP[ep].DIEPINT) |= flags;
    usb_refresh_ep_summary();
}

static void usb_raise_out(unsigned ep, uint32_t flags)
{
    ALIAS(OTG_FS_DEVICE->OUTEP[ep].DOEPINT) |= flags;
    usb_refresh_ep_summary();
}

/** @brief Put the head of the receive queue where GRXSTSP/GRXSTSR and the FIFO show it. */
static void usb_rx_expose(void)
{
    if (usb.count == 0U)
    {
        ALIAS(OTG_FS_GLOBAL->GINTSTS) &= ~OTG_GINTSTS_RXFLVL;
        ALIAS(OTG_FS_GLOBAL->GRXSTSP) = 0;
        ALIAS(OTG_FS_GLOBAL->GRXSTSR) = 0;
        return;
    }
    const usb_rx_entry_t* e = &usb.queue[usb.head];
    ALIAS(OTG_FS_GLOBAL->GRXSTSP) = e->status;
    ALIAS(OTG_FS_GLOBAL->GRXSTSR) = e->status;
    ALIAS(OTG_FS_GLOBAL->GINTSTS) |= OTG_GINTSTS_RXFLVL;
}

static void usb_rx_push(uint32_t pktsts, unsigned ep, const uint8_t* data, unsigned bcnt)
{
    if (usb.count >= USB_RX_QUEUE)
    {
        fprintf(stderr, "usb model: receive queue overflow\n");
        abort();
    }
    usb_rx_entry_t* e = &usb.queue[(usb.head + usb.count) % USB_RX_QUEUE];
    memset(e, 0, sizeof(*e));
    e->status = (pktsts << OTG_GRXSTSP_PKTSTS_POS) | (bcnt << OTG_GRXSTSP_BCNT_POS) | ep;
    e->nwords = (bcnt + 3U) / 4U;
    if (bcnt)
    {
        memcpy(e->words, data, bcnt);
    }
    usb.count++;
    usb_rx_expose();
}

/** @brief Word the next FIFO read returns: the head of the popped entry's payload. */
static void usb_fifo_expose(void)
{
    *usb_alias(OTG_FS_FIFO_BASE) =
        (usb.current_read < usb.current.nwords) ? usb.current.words[usb.current_read] : 0xDEADBEEFU;
}

static void usb_on_grxstsp_pop(uintptr_t addr)
{
    if (usb.count == 0U)
    {
        violation(addr, "GRXSTSP popped with RXFLVL=0: nothing in the receive FIFO");
        return;
    }
    if (usb.current_read < usb.current.nwords)
    {
        violation(addr, "next status popped before the previous packet's %u words were read",
                  usb.current.nwords - usb.current_read);
    }
    usb.current = usb.queue[usb.head];
    usb.current_read = 0;
    usb.head = (usb.head + 1U) % USB_RX_QUEUE;
    usb.count--;
    usb_fifo_expose();
    usb_rx_expose();

    const unsigned ep = FIELD(usb.current.status, OTG_GRXSTSP_EPNUM_MSK, OTG_GRXSTSP_EPNUM_POS);
    switch (FIELD(usb.current.status, OTG_GRXSTSP_PKTSTS_MSK, OTG_GRXSTSP_PKTSTS_POS))
    {
        case OTG_PKTSTS_SETUP_COMPLETE:
            /* RM0383 22.17.5: STUP follows the setup-complete pop. */
            usb_raise_out(0, OTG_DOEPINT_STUP);
            break;
        case OTG_PKTSTS_OUT_COMPLETE:
            ALIAS(OTG_FS_DEVICE->OUTEP[ep].DOEPCTL) &= ~OTG_DOEPCTL_EPENA;
            usb_raise_out(ep, OTG_DOEPINT_XFRC);
            break;
        default:
            break;
    }
}

static void usb_irq(void)
{
    for (int i = 0; i < 32; i++)
    {
        if (!(ALIAS(OTG_FS_GLOBAL->GAHBCFG) & OTG_GAHBCFG_GINT) ||
            !(ALIAS(OTG_FS_GLOBAL->GINTSTS) & ALIAS(OTG_FS_GLOBAL->GINTMSK)))
        {
            return;
        }
        usb.irqs++;
        DRIVE(OTG_FS_IRQHandler());
    }
    /* Something is pending that the handler never clears: report it once. */
    violation(OTG_FS_GLOBAL_BASE + offsetof(otg_fs_global_regs_t, GINTSTS),
              "interrupt storm: GINTSTS=0x%08X still pending after 32 handler runs",
              ALIAS(OTG_FS_GLOBAL->GINTSTS) & ALIAS(OTG_FS_GLOBAL->GINTMSK));
    ALIAS(OTG_FS_GLOBAL->GINTMSK) = 0;
}

/* -- register-level model and rules -------------------------------------- */

static void usb_check_fifo_layout(uintptr_t addr)
{
    const uint32_t rx = FIELD(ALIAS(OTG_FS_GLOBAL->GRXFSIZ), OTG_GRXFSIZ_RXFD_MSK, OTG_GRXFSIZ_RXFD_POS);
    uint32_t next = rx;
    if (rx < 16U)
    {
        violation(addr, "GRXFSIZ %u words; the receive FIFO must be at least 16", rx);
    }
    for (unsigned ep = 0; ep < OTG_FS_EP_COUNT; ep++)
    {
        const uint32_t reg = (ep == 0U) ? ALIAS(OTG_FS_GLOBAL->DIEPTXF0)
                           : (ep == 1U) ? ALIAS(OTG_FS_GLOBAL->DIEPTXF1)
                           : (ep == 2U) ? ALIAS(OTG_FS_GLOBAL->DIEPTXF2)
                                        : ALIAS(OTG_FS_GLOBAL->DIEPTXF3);
        const uint32_t start = reg & 0xFFFFU;
        const uint32_t depth = reg >> 16;
        if (depth < 16U)
        {
            violation(addr, "TX FIFO %u is %u words deep; minimum is 16", ep, depth);
        }
        if (start < next)
        {
            violation(addr, "TX FIFO %u starts at word %u, overlapping the FIFO before it (ends at %u)",
                      ep, start, next);
        }
        next = start + depth;
    }
    if (next > OTG_FS_FIFO_RAM_WORDS)
    {
        violation(addr, "FIFO layout ends at word %u; the core has %u words of packet RAM", next,
                  OTG_FS_FIFO_RAM_WORDS);
    }
}

static void usb_on_connect(uintptr_t addr)
{
    const uint32_t gccfg = ALIAS(OTG_FS_GLOBAL->GCCFG);
    const uint32_t gusbcfg = ALIAS(OTG_FS_GLOBAL->GUSBCFG);
    const uint32_t gintmsk = ALIAS(OTG_FS_GLOBAL->GINTMSK);

    if (!(gccfg & OTG_GCCFG_PWRDWN))
    {
        violation(addr, "SDIS cleared with GCCFG.PWRDWN=0: the transceiver is powered down");
    }
    if (!(gccfg & OTG_GCCFG_NOVBUSSENS) && !(gccfg & OTG_GCCFG_VBUSBSEN))
    {
        violation(addr, "neither NOVBUSSENS nor VBUSBSEN set: the core will never see a session");
    }
    if (!(gusbcfg & OTG_GUSBCFG_FDMOD))
    {
        violation(addr, "connecting without forcing device mode (GUSBCFG.FDMOD)");
    }
    else if (sim_cycles - usb.fdmod_at < (uint64_t)BSP_HCLK_HZ / 40U)
    {
        violation(addr, "SDIS cleared %llu cycles after FDMOD; RM0383 22.15.4 says wait 25 ms = %u cycles",
                  (unsigned long long)(sim_cycles - usb.fdmod_at), (unsigned)(BSP_HCLK_HZ / 40U));
    }
    if (!(gusbcfg & OTG_GUSBCFG_PHYSEL))
    {
        violation(addr, "GUSBCFG.PHYSEL not set; the F411 only has the full-speed serial transceiver");
    }
    if (FIELD(ALIAS(OTG_FS_DEVICE->DCFG), OTG_DCFG_DSPD_MSK, OTG_DCFG_DSPD_POS) != OTG_DSPD_FULL_SPEED)
    {
        violation(addr, "DCFG.DSPD is not 0b11 (full speed with the internal PHY)");
    }
    if (!(ALIAS(OTG_FS_GLOBAL->GAHBCFG) & OTG_GAHBCFG_GINT))
    {
        violation(addr, "connected with GAHBCFG.GINT=0: no interrupt will ever fire");
    }
    if (!(gintmsk & OTG_GINTMSK_USBRST) || !(gintmsk & OTG_GINTMSK_ENUMDNEM) ||
        !(gintmsk & OTG_GINTMSK_RXFLVLM) || !(gintmsk & OTG_GINTMSK_IEPINT) ||
        !(gintmsk & OTG_GINTMSK_OEPINT))
    {
        violation(addr, "connected with GINTMSK=0x%08X; USBRST, ENUMDNE, RXFLVL, IEPINT and OEPINT are all needed", gintmsk);
    }
    if (FIELD(ALIAS(OTG_FS_DEVICE->DCFG), OTG_DCFG_DAD_MSK, OTG_DCFG_DAD_POS) != 0U)
    {
        violation(addr, "connected with a non-zero device address");
    }
    usb_check_fifo_layout(addr);
    usb.connected = true;
}

static void sim_usb_global(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    otg_fs_global_regs_t* g = OTG_FS_GLOBAL;

    if (!write)
    {
        if (addr == (uintptr_t)&g->GRXSTSP)
        {
            usb_on_grxstsp_pop(addr);
        }
        return;
    }

    if (addr == (uintptr_t)&g->GRSTCTL)
    {
        /* Resets and flushes complete at once; AHBIDL is always true here. */
        uint32_t v = after;
        if (after & OTG_GRSTCTL_CSRST)
        {
            if (!(before & OTG_GRSTCTL_AHBIDL))
            {
                violation(addr, "CSRST issued without checking AHBIDL first");
            }
            if (!(ALIAS(g->GUSBCFG) & OTG_GUSBCFG_PHYSEL))
            {
                violation(addr, "core soft reset before PHYSEL: the PHY choice is latched by the reset");
            }
            v &= ~OTG_GRSTCTL_CSRST;
            usb.fdmod_seen = false;
        }
        if (after & OTG_GRSTCTL_TXFFLSH)
        {
            const uint32_t num = FIELD(after, OTG_GRSTCTL_TXFNUM_MSK, OTG_GRSTCTL_TXFNUM_POS);
            for (unsigned ep = 0; ep < OTG_FS_EP_COUNT; ep++)
            {
                if (num == OTG_TXFNUM_ALL || num == ep)
                {
                    usb.tx[ep].nbytes = 0;
                    ALIAS(OTG_FS_DEVICE->INEP[ep].DTXFSTS) = usb_tx_depth_words(ep);
                }
            }
            v &= ~OTG_GRSTCTL_TXFFLSH;
        }
        if (after & OTG_GRSTCTL_RXFFLSH)
        {
            usb.count = 0;
            usb.current.nwords = 0;
            usb.current_read = 0;
            usb_rx_expose();
            v &= ~OTG_GRSTCTL_RXFFLSH;
        }
        ALIAS(g->GRSTCTL) = v | OTG_GRSTCTL_AHBIDL;
    }
    else if (addr == (uintptr_t)&g->GUSBCFG)
    {
        if ((after & OTG_GUSBCFG_FDMOD) && !(before & OTG_GUSBCFG_FDMOD))
        {
            usb.fdmod_at = sim_cycles;
            usb.fdmod_seen = true;
        }
        if ((after & OTG_GUSBCFG_FDMOD) && (after & OTG_GUSBCFG_FHMOD))
        {
            violation(addr, "FDMOD and FHMOD both set");
        }
        /* RM0383 22.15.4 TRDT table for the FS core, from the AHB clock. */
        const uint32_t trdt = FIELD(after, OTG_GUSBCFG_TRDT_MSK, OTG_GUSBCFG_TRDT_POS);
        const uint32_t hclk_mhz = BSP_HCLK_HZ / 1000000U;
        const uint32_t want = hclk_mhz >= 32U ? 6U : hclk_mhz >= 27U ? 7U : hclk_mhz >= 24U ? 8U
                            : hclk_mhz >= 21U ? 9U : hclk_mhz >= 20U ? 10U : hclk_mhz >= 18U ? 11U
                            : hclk_mhz >= 17U ? 12U : hclk_mhz >= 16U ? 13U : hclk_mhz >= 15U ? 14U : 15U;
        if ((after & OTG_GUSBCFG_FDMOD) && trdt != want)
        {
            violation(addr, "TRDT=%u for HCLK %u MHz; RM0383 22.15.4 table wants %u", trdt, hclk_mhz, want);
        }
    }
    else if (addr == (uintptr_t)&g->GINTSTS)
    {
        /* Write one to clear, except the pure status bits. */
        const uint32_t ro = OTG_GINTSTS_CMOD | OTG_GINTSTS_OTGINT | OTG_GINTSTS_RXFLVL |
                            OTG_GINTSTS_NPTXFE | OTG_GINTSTS_GINAKEFF | OTG_GINTSTS_GOUTNAKEFF |
                            OTG_GINTSTS_IEPINT | OTG_GINTSTS_OEPINT | OTG_GINTSTS_HPRTINT |
                            OTG_GINTSTS_PTXFE;
        ALIAS(g->GINTSTS) = before & ~(after & ~ro);
    }
    else if (addr == (uintptr_t)&g->GRXFSIZ || addr == (uintptr_t)&g->DIEPTXF0 ||
             addr == (uintptr_t)&g->DIEPTXF1 || addr == (uintptr_t)&g->DIEPTXF2 ||
             addr == (uintptr_t)&g->DIEPTXF3)
    {
        if (usb.connected)
        {
            violation(addr, "FIFO layout changed while connected");
        }
        for (unsigned ep = 0; ep < OTG_FS_EP_COUNT; ep++)
        {
            ALIAS(OTG_FS_DEVICE->INEP[ep].DTXFSTS) = usb_tx_depth_words(ep);
        }
    }
    else if (addr == (uintptr_t)&g->GCCFG)
    {
        if ((after & OTG_GCCFG_PWRDWN) && !(before & OTG_GCCFG_PWRDWN) &&
            !(ALIAS(g->GUSBCFG) & OTG_GUSBCFG_PHYSEL))
        {
            violation(addr, "transceiver powered up before PHYSEL was chosen");
        }
    }
}

static void sim_usb_device(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    otg_fs_device_regs_t* d = OTG_FS_DEVICE;
    if (!write)
    {
        return;
    }

    if (addr == (uintptr_t)&d->DCTL)
    {
        if ((before & OTG_DCTL_SDIS) && !(after & OTG_DCTL_SDIS))
        {
            usb_on_connect(addr);
        }
        if (!(before & OTG_DCTL_SDIS) && (after & OTG_DCTL_SDIS))
        {
            usb.connected = false;
        }
        uint32_t gint = ALIAS(OTG_FS_GLOBAL->GINTSTS);
        if (after & OTG_DCTL_SGONAK) { gint |= OTG_GINTSTS_GOUTNAKEFF; }
        if (after & OTG_DCTL_CGONAK) { gint &= ~OTG_GINTSTS_GOUTNAKEFF; }
        if (after & OTG_DCTL_SGINAK) { gint |= OTG_GINTSTS_GINAKEFF; }
        if (after & OTG_DCTL_CGINAK) { gint &= ~OTG_GINTSTS_GINAKEFF; }
        ALIAS(OTG_FS_GLOBAL->GINTSTS) = gint;
        /* The set/clear pulse bits read back as zero. */
        ALIAS(d->DCTL) = after & ~(OTG_DCTL_SGONAK | OTG_DCTL_CGONAK | OTG_DCTL_SGINAK | OTG_DCTL_CGINAK);
        return;
    }
    if (addr == (uintptr_t)&d->DCFG)
    {
        if (usb.connected && ((before ^ after) & OTG_DCFG_DSPD_MSK))
        {
            violation(addr, "DCFG.DSPD changed while connected");
        }
        return;
    }

    /* Endpoint registers. */
    if (addr >= (uintptr_t)&d->INEP[0] && addr < (uintptr_t)&d->INEP[OTG_FS_EP_COUNT])
    {
        const size_t off = addr - (uintptr_t)&d->INEP[0];
        const unsigned ep = (unsigned)(off / sizeof(otg_fs_inep_regs_t));
        const size_t reg = off % sizeof(otg_fs_inep_regs_t);
        otg_fs_inep_regs_t* in = &d->INEP[ep];

        if (reg == offsetof(otg_fs_inep_regs_t, DIEPCTL))
        {
            /* NAKSTS is read-only: whatever a read-modify-write copies back is
             * ignored; only SNAK/CNAK move it. */
            uint32_t v = (after & ~(OTG_DIEPCTL_CNAK | OTG_DIEPCTL_SNAK | OTG_DIEPCTL_SD0PID_SEVNFRM |
                                    OTG_DIEPCTL_NAKSTS)) | (before & OTG_DIEPCTL_NAKSTS);
            if (after & OTG_DIEPCTL_SNAK)
            {
                v |= OTG_DIEPCTL_NAKSTS;
                usb_raise_in(ep, OTG_DIEPINT_INEPNE);
            }
            if (after & OTG_DIEPCTL_CNAK)
            {
                v &= ~OTG_DIEPCTL_NAKSTS;
            }
            if ((after & OTG_DIEPCTL_EPDIS) && (before & OTG_DIEPCTL_EPENA))
            {
                v &= ~(OTG_DIEPCTL_EPENA | OTG_DIEPCTL_EPDIS);
                usb.tx[ep].armed = false;
                usb_raise_in(ep, OTG_DIEPINT_EPDISD);
            }
            else if (after & OTG_DIEPCTL_EPDIS)
            {
                v &= ~OTG_DIEPCTL_EPDIS;
            }
            if ((after & OTG_DIEPCTL_EPENA) && !(before & OTG_DIEPCTL_EPENA))
            {
                const uint32_t tsiz = ALIAS(in->DIEPTSIZ);
                usb.tx[ep].armed = true;
                usb.tx[ep].armed_at = sim_cycles;
                usb.tx[ep].nbytes = 0;
                usb.tx[ep].xfrsiz = FIELD(tsiz, OTG_DIEPTSIZ_XFRSIZ_MSK, OTG_DIEPTSIZ_XFRSIZ_POS);
                usb.tx[ep].pktcnt = FIELD(tsiz, OTG_DIEPTSIZ_PKTCNT_MSK, OTG_DIEPTSIZ_PKTCNT_POS);
                if (ep == 0U)
                {
                    usb.tx[ep].xfrsiz &= 0x7FU;
                    usb.tx[ep].pktcnt &= 0x3U;
                }
                if (usb.tx[ep].pktcnt == 0U)
                {
                    violation(addr, "IN EP%u enabled with PKTCNT=0: nothing would be sent", ep);
                }
                if (ep != 0U && !(after & OTG_DIEPCTL_USBAEP))
                {
                    violation(addr, "IN EP%u enabled without USBAEP", ep);
                }
                const uint32_t mps = (ep == 0U) ? 64U : FIELD(after, OTG_DIEPCTL_MPSIZ_MSK, OTG_DIEPCTL_MPSIZ_POS);
                const uint32_t need = (usb.tx[ep].xfrsiz + mps - 1U) / mps;
                if (usb.tx[ep].xfrsiz != 0U && need != usb.tx[ep].pktcnt)
                {
                    violation(addr, "IN EP%u: XFRSIZ=%u needs PKTCNT=%u at MPS %u, got %u", ep,
                              usb.tx[ep].xfrsiz, need, mps, usb.tx[ep].pktcnt);
                }
                if (ep != 0U && FIELD(after, OTG_DIEPCTL_TXFNUM_MSK, OTG_DIEPCTL_TXFNUM_POS) != ep)
                {
                    violation(addr, "IN EP%u uses TX FIFO %u; this driver's layout gives each EP its own", ep,
                              (unsigned)FIELD(after, OTG_DIEPCTL_TXFNUM_MSK, OTG_DIEPCTL_TXFNUM_POS));
                }
            }
            if ((before & OTG_DIEPCTL_EPENA) && (after & OTG_DIEPCTL_EPENA) &&
                ((before ^ after) & (OTG_DIEPCTL_MPSIZ_MSK | OTG_DIEPCTL_EPTYP_MSK | OTG_DIEPCTL_TXFNUM_MSK)))
            {
                violation(addr, "IN EP%u type/size/FIFO changed while EPENA=1", ep);
            }
            ALIAS(in->DIEPCTL) = v;
        }
        else if (reg == offsetof(otg_fs_inep_regs_t, DIEPINT))
        {
            ALIAS(in->DIEPINT) = before & ~(after & ~OTG_DIEPINT_TXFE);
            usb_refresh_ep_summary();
        }
        else if (reg == offsetof(otg_fs_inep_regs_t, DIEPTSIZ))
        {
            if (before != after && (ALIAS(in->DIEPCTL) & OTG_DIEPCTL_EPENA))
            {
                violation(addr, "IN EP%u DIEPTSIZ written while EPENA=1", ep);
            }
        }
        return;
    }

    if (addr >= (uintptr_t)&d->OUTEP[0] && addr < (uintptr_t)&d->OUTEP[OTG_FS_EP_COUNT])
    {
        const size_t off = addr - (uintptr_t)&d->OUTEP[0];
        const unsigned ep = (unsigned)(off / sizeof(otg_fs_outep_regs_t));
        const size_t reg = off % sizeof(otg_fs_outep_regs_t);
        otg_fs_outep_regs_t* out = &d->OUTEP[ep];

        if (reg == offsetof(otg_fs_outep_regs_t, DOEPCTL))
        {
            uint32_t v = (after & ~(OTG_DOEPCTL_CNAK | OTG_DOEPCTL_SNAK | OTG_DOEPCTL_SD0PID_SEVNFRM |
                                    OTG_DOEPCTL_NAKSTS)) | (before & OTG_DOEPCTL_NAKSTS);
            if (after & OTG_DOEPCTL_SNAK) { v |= OTG_DOEPCTL_NAKSTS; }
            if (after & OTG_DOEPCTL_CNAK) { v &= ~OTG_DOEPCTL_NAKSTS; }
            if ((after & OTG_DOEPCTL_EPDIS) && (before & OTG_DOEPCTL_EPENA))
            {
                if (!(ALIAS(OTG_FS_GLOBAL->GINTSTS) & OTG_GINTSTS_GOUTNAKEFF))
                {
                    violation(addr, "OUT EP%u disabled without global OUT NAK in effect (RM0383 22.17.6)", ep);
                }
                v &= ~(OTG_DOEPCTL_EPENA | OTG_DOEPCTL_EPDIS);
                usb_raise_out(ep, OTG_DOEPINT_EPDISD);
            }
            else if (after & OTG_DOEPCTL_EPDIS)
            {
                v &= ~OTG_DOEPCTL_EPDIS;
            }
            if ((after & OTG_DOEPCTL_EPENA) && !(before & OTG_DOEPCTL_EPENA))
            {
                const uint32_t tsiz = ALIAS(out->DOEPTSIZ);
                if (FIELD(tsiz, OTG_DOEPTSIZ_PKTCNT_MSK, OTG_DOEPTSIZ_PKTCNT_POS) == 0U)
                {
                    violation(addr, "OUT EP%u enabled with PKTCNT=0", ep);
                }
                if (ep != 0U && !(after & OTG_DOEPCTL_USBAEP))
                {
                    violation(addr, "OUT EP%u enabled without USBAEP", ep);
                }
                if (v & OTG_DOEPCTL_NAKSTS)
                {
                    violation(addr, "OUT EP%u enabled while still NAKing (no CNAK)", ep);
                }
            }
            ALIAS(out->DOEPCTL) = v;
        }
        else if (reg == offsetof(otg_fs_outep_regs_t, DOEPINT))
        {
            ALIAS(out->DOEPINT) = before & ~after;
            usb_refresh_ep_summary();
        }
        return;
    }
}

static void sim_usb_fifo(uintptr_t addr, int write, uint32_t after)
{
    const unsigned ep = (unsigned)((addr - OTG_FS_FIFO_BASE) / USB_FIFO_PAGE);

    if (!write)
    {
        /* Any FIFO address pops the shared receive FIFO. */
        if (usb.current_read >= usb.current.nwords)
        {
            violation(addr, "FIFO read past the %u words of the current packet", usb.current.nwords);
            return;
        }
        usb.current_read++;
        usb_fifo_expose();
        return;
    }

    if (ep >= OTG_FS_EP_COUNT)
    {
        violation(addr, "write to FIFO %u; the core has 4", ep);
        return;
    }
    otg_fs_inep_regs_t* in = &OTG_FS_DEVICE->INEP[ep];
    if (!(ALIAS(in->DIEPCTL) & OTG_DIEPCTL_EPENA) || !usb.tx[ep].armed)
    {
        violation(addr, "TX FIFO %u written while IN EP%u is not enabled", ep, ep);
        return;
    }
    if (usb.tx[ep].nbytes + 4U > ((usb.tx[ep].xfrsiz + 3U) & ~3U))
    {
        violation(addr, "TX FIFO %u: more words written than XFRSIZ=%u needs", ep, usb.tx[ep].xfrsiz);
        return;
    }
    const uint32_t free_words = ALIAS(in->DTXFSTS) & OTG_DTXFSTS_INEPTFSAV_MSK;
    if (free_words == 0U)
    {
        violation(addr, "TX FIFO %u overflow: written with INEPTFSAV=0", ep);
        return;
    }
    ALIAS(in->DTXFSTS) = free_words - 1U;
    memcpy(&usb.tx[ep].bytes[usb.tx[ep].nbytes], &after, 4U);
    usb.tx[ep].nbytes += 4U;
}

static void sim_usb(uintptr_t addr, int write, uint32_t before, uint32_t after)
{
    if (addr >= OTG_FS_FIFO_BASE)
    {
        sim_usb_fifo(addr, write, after);
    }
    else if (addr >= OTG_FS_DEVICE_BASE && addr < OTG_FS_PWRCLK_BASE)
    {
        sim_usb_device(addr, write, before, after);
    }
    else if (addr >= OTG_FS_HOST_BASE && addr < OTG_FS_DEVICE_BASE)
    {
        violation(addr, "host-mode register touched by a device-only driver");
    }
    else if (addr < OTG_FS_HOST_BASE)
    {
        sim_usb_global(addr, write, before, after);
    }
}

/* -- host-side operations ------------------------------------------------ */

/** @brief Bus reset followed by speed enumeration, as the hub does on attach. */
static void host_reset(void)
{
    if (ALIAS(OTG_FS_DEVICE->DCTL) & OTG_DCTL_SDIS)
    {
        violation(OTG_FS_DEVICE_BASE, "host reset while the device is soft-disconnected (nothing to reset)");
        return;
    }
    usb.count = 0;
    usb.current.nwords = 0;
    usb.current_read = 0;
    usb_rx_expose();
    ALIAS(OTG_FS_GLOBAL->GINTSTS) |= OTG_GINTSTS_USBRST;
    usb_irq();
    ALIAS(OTG_FS_DEVICE->DSTS) = OTG_DSPD_FULL_SPEED << OTG_DSTS_ENUMSPD_POS;
    ALIAS(OTG_FS_GLOBAL->GINTSTS) |= OTG_GINTSTS_ENUMDNE;
    usb_irq();
}

typedef enum { HOST_ACK = 0, HOST_STALL = 1, HOST_NAK = 2 } host_result_t;

/**
 * @brief IN token on @p ep: collect one transfer as the device has it armed.
 *
 * Returns the bytes the device had in its FIFO, checking that they cover
 * XFRSIZ, then completes the transfer the way the core does (EPENA clears,
 * XFRC fires) and runs the interrupt handler.
 */
static host_result_t host_in(unsigned ep, uint8_t* data, unsigned* length)
{
    otg_fs_inep_regs_t* in = &OTG_FS_DEVICE->INEP[ep];
    const uint32_t ctl = ALIAS(in->DIEPCTL);
    usb.in_tokens++;
    *length = 0;

    if (ctl & OTG_DIEPCTL_STALL)
    {
        return HOST_STALL;
    }
    if (!(ctl & OTG_DIEPCTL_EPENA) || (ctl & OTG_DIEPCTL_NAKSTS) || !usb.tx[ep].armed)
    {
        return HOST_NAK;
    }
    if (usb.tx[ep].nbytes < usb.tx[ep].xfrsiz)
    {
        violation((uintptr_t)&in->DIEPCTL,
                  "IN EP%u token with %u of %u bytes in the FIFO: the core would NAK until the rest is written",
                  ep, usb.tx[ep].nbytes, usb.tx[ep].xfrsiz);
        return HOST_NAK;
    }
    memcpy(data, usb.tx[ep].bytes, usb.tx[ep].xfrsiz);
    *length = usb.tx[ep].xfrsiz;

    usb.tx[ep].armed = false;
    usb.tx[ep].nbytes = 0;
    ALIAS(in->DIEPCTL) = ctl & ~OTG_DIEPCTL_EPENA;
    ALIAS(in->DIEPTSIZ) = 0;
    ALIAS(in->DTXFSTS) = usb_tx_depth_words(ep);
    usb_raise_in(ep, OTG_DIEPINT_XFRC);
    usb_irq();
    return HOST_ACK;
}

/** @brief OUT token with one data packet on @p ep. */
static host_result_t host_out(unsigned ep, const uint8_t* data, unsigned length)
{
    otg_fs_outep_regs_t* out = &OTG_FS_DEVICE->OUTEP[ep];
    const uint32_t ctl = ALIAS(out->DOEPCTL);
    usb.out_tokens++;

    if (ctl & OTG_DOEPCTL_STALL)
    {
        return HOST_STALL;
    }
    if (!(ctl & OTG_DOEPCTL_EPENA) || (ctl & OTG_DOEPCTL_NAKSTS))
    {
        return HOST_NAK;
    }
    const uint32_t tsiz = ALIAS(out->DOEPTSIZ);
    const uint32_t xfrsiz = FIELD(tsiz, OTG_DOEPTSIZ_XFRSIZ_MSK, OTG_DOEPTSIZ_XFRSIZ_POS) & (ep == 0U ? 0x7FU : ~0U);
    const uint32_t mps = (ep == 0U) ? 64U : FIELD(ctl, OTG_DOEPCTL_MPSIZ_MSK, OTG_DOEPCTL_MPSIZ_POS);
    if (length > mps)
    {
        fprintf(stderr, "host model: packet of %u bytes exceeds MPS %u\n", length, mps);
        abort();
    }
    if (length > xfrsiz)
    {
        violation((uintptr_t)&out->DOEPTSIZ, "OUT EP%u armed for %u bytes but a %u-byte packet arrived: overflow",
                  ep, xfrsiz, length);
    }

    usb_rx_push(OTG_PKTSTS_OUT_DATA, ep, data, length);
    usb_irq();
    /* One packet per armed transfer in this driver, or a short packet: either
     * way the transfer completes now. */
    usb_rx_push(OTG_PKTSTS_OUT_COMPLETE, ep, NULL, 0);
    usb_irq();
    return HOST_ACK;
}

/** @brief Full control transfer on EP0, host side. */
static host_result_t host_control(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
                                  uint16_t wIndex, uint16_t wLength, const uint8_t* out_data,
                                  uint8_t* in_data, unsigned* in_length)
{
    otg_fs_inep_regs_t* in0 = &OTG_FS_DEVICE->INEP[0];
    otg_fs_outep_regs_t* out0 = &OTG_FS_DEVICE->OUTEP[0];
    uint8_t setup[8] = {bmRequestType, bRequest, (uint8_t)wValue, (uint8_t)(wValue >> 8),
                        (uint8_t)wIndex, (uint8_t)(wIndex >> 8), (uint8_t)wLength, (uint8_t)(wLength >> 8)};
    unsigned total = 0;
    if (in_length) { *in_length = 0; }

    /* SETUP: accepted regardless of EPENA, as long as STUPCNT allows it. The
     * core drops any STALL on EP0 when a SETUP arrives. */
    if (FIELD(ALIAS(out0->DOEPTSIZ), OTG_DOEPTSIZ_RXDPID_STUPCNT_MSK, OTG_DOEPTSIZ_RXDPID_STUPCNT_POS) == 0U)
    {
        violation((uintptr_t)&out0->DOEPTSIZ, "SETUP arrived with DOEPTSIZ0.STUPCNT=0: the core cannot store it");
    }
    ALIAS(in0->DIEPCTL) &= ~OTG_DIEPCTL_STALL;
    ALIAS(out0->DOEPCTL) &= ~OTG_DOEPCTL_STALL;
    usb.tx[0].armed = false;
    usb_rx_push(OTG_PKTSTS_SETUP_DATA, 0, setup, 8);
    usb_irq();
    usb_rx_push(OTG_PKTSTS_SETUP_COMPLETE, 0, NULL, 0);
    usb_irq();

    if ((ALIAS(in0->DIEPCTL) & OTG_DIEPCTL_STALL) || (ALIAS(out0->DOEPCTL) & OTG_DOEPCTL_STALL))
    {
        return HOST_STALL;
    }

    if (bmRequestType & 0x80U)
    {
        /* Data stage IN: keep asking until a short packet or wLength. */
        for (int guard = 0; guard < 64; guard++)
        {
            uint8_t packet[64];
            unsigned got = 0;
            const host_result_t r = host_in(0, packet, &got);
            if (r != HOST_ACK)
            {
                return r;
            }
            if (in_data && total + got <= 1024U)
            {
                memcpy(in_data + total, packet, got);
            }
            total += got;
            if (got < 64U || total >= wLength)
            {
                break;
            }
        }
        if (in_length) { *in_length = total; }

        /* Status stage: an empty OUT packet. */
        if (!(ALIAS(out0->DOEPCTL) & OTG_DOEPCTL_EPENA))
        {
            violation((uintptr_t)&out0->DOEPCTL, "control IN done but EP0 OUT not armed for the status stage");
            return HOST_NAK;
        }
        return host_out(0, NULL, 0);
    }

    if (wLength != 0U)
    {
        /* Data stage OUT (single packet is all the driver supports). */
        const host_result_t r = host_out(0, out_data, wLength);
        if (r != HOST_ACK)
        {
            return r;
        }
    }

    /* Status stage: an empty IN packet from the device. */
    if (bRequest == USB_REQ_SET_ADDRESS && (bmRequestType & 0x7FU) == 0U)
    {
        /* RM0383 22.17.5: DCFG.DAD must already hold the new address when
         * the status IN goes out; the core applies it on that handshake. */
        const uint32_t dad = FIELD(ALIAS(OTG_FS_DEVICE->DCFG), OTG_DCFG_DAD_MSK, OTG_DCFG_DAD_POS);
        if (dad != wValue)
        {
            violation((uintptr_t)&OTG_FS_DEVICE->DCFG, "status IN for SET_ADDRESS(%u) with DCFG.DAD=%u", wValue, dad);
        }
    }
    uint8_t dummy[64];
    unsigned got = 0;
    const host_result_t r = host_in(0, dummy, &got);
    if (r == HOST_ACK && got != 0U)
    {
        violation((uintptr_t)&in0->DIEPTSIZ, "status IN carried %u bytes instead of a zero-length packet", got);
    }
    if (r == HOST_ACK && (ALIAS(in0->DIEPCTL) & OTG_DIEPCTL_STALL))
    {
        return HOST_STALL;
    }
    return r;
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
        case SDIO_BASE:
            sim_sdio(addr, write, before, after);
            break;
        case OTG_FS_BASE:
            sim_usb(addr, write, before, after);
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

/* -- SDIO / SD card ------------------------------------------------------- */

static void check_sd_block_pattern(const char* what, const uint8_t* data, uint32_t first_block, uint32_t count)
{
    uint64_t mismatches = 0;
    for (uint32_t b = 0; b < count; b++)
    {
        for (uint32_t i = 0; i < 512U; i++)
        {
            if (data[b * 512U + i] != (uint8_t)(0xA0U + first_block + b + i))
            {
                mismatches++;
            }
        }
    }
    check_eq(what, mismatches, 0);
}

static void test_sdio(void)
{
    group("SDIO / SD card");

    static uint8_t buf[3 * 512];

    check_eq("read before init reports ENOTREADY",
             (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(0, buf, 1)), (uint32_t)BSP_SD_ENOTREADY);

    /* --- SDHC card, physical layer v2 ------------------------------------ */
    sd_insert(true);
    DRIVE(bsp_sdio_init());
    check_eq("SDIO clock enabled in APB2ENR", RCC->APB2ENR & RCC_APB2ENR_SDIOEN, RCC_APB2ENR_SDIOEN);
    check_eq("bsp_sd_init() on an SDHC card", (uint64_t)(uint32_t)DRIVE(bsp_sd_init()), 0);
    check_eq("card ready", bsp_sd_ready(), 1);

    const bsp_sd_card_t* card = bsp_sd_card();
    check_eq("SDHC: high_capacity", card->high_capacity, 1);
    check_eq("SDHC: version_2", card->version_2, 1);
    check_eq("SDHC: RCA from CMD3", card->rca, SD_MODEL_RCA);
    check_eq("SDHC: block count from CSD v2", card->block_count, sd_model_block_count(true));

    /* CLKDIV: SDIOCLK = PLL48CK; CK = SDIOCLK / (CLKDIV + 2). */
    const uint32_t want_div = (BSP_PLL_Q_OUT_HZ + BSP_SDIO_CLK_HZ - 1U) / BSP_SDIO_CLK_HZ;
    check_eq("CLKCR.CLKDIV for the data-transfer clock",
             FIELD(SDIO->CLKCR, SDIO_CLKCR_CLKDIV_MSK, SDIO_CLKCR_CLKDIV_POS),
             want_div >= 2U ? want_div - 2U : 0U);
    check_eq("CLKCR.WIDBUS = 4-bit", FIELD(SDIO->CLKCR, SDIO_CLKCR_WIDBUS_MSK, SDIO_CLKCR_WIDBUS_POS),
             BSP_SDIO_BUS_WIDTH == 4U ? 1U : 0U);
    check_eq("CLKCR.CLKEN", SDIO->CLKCR & SDIO_CLKCR_CLKEN, SDIO_CLKCR_CLKEN);
    check_eq("POWER.PWRCTRL = on", FIELD(SDIO->POWER, SDIO_POWER_PWRCTRL_MSK, SDIO_POWER_PWRCTRL_POS), 3);

    memset(buf, 0, sizeof(buf));
    check_eq("read 1 block", (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(2, buf, 1)), 0);
    check_sd_block_pattern("block 2 contents", buf, 2, 1);

    memset(buf, 0, sizeof(buf));
    check_eq("read 3 blocks (CMD18 + CMD12)", (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(4, buf, 3)), 0);
    check_sd_block_pattern("blocks 4..6 contents", buf, 4, 3);

    for (uint32_t i = 0; i < 2U * 512U; i++)
    {
        buf[i] = (uint8_t)(0xA0U + 1U + (i / 512U) + i);
    }
    check_eq("write 2 blocks (CMD25 + CMD12)", (uint64_t)(uint32_t)DRIVE(bsp_sd_write_blocks(1, buf, 2)), 0);
    memset(buf, 0, sizeof(buf));
    check_eq("read back the 2 written blocks", (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(1, buf, 2)), 0);
    check_sd_block_pattern("written data survived the round trip", buf, 1, 2);

    check_eq("write past the end reports EPARAM",
             (uint64_t)(uint32_t)DRIVE(bsp_sd_write_blocks(card->block_count - 1U, buf, 2)), (uint32_t)BSP_SD_EPARAM);
    check_eq("count of 0 reports EPARAM", (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(0, buf, 0)), (uint32_t)BSP_SD_EPARAM);

    /* --- SDSC card, physical layer v1 (no CMD8, byte addressing) --------- */
    sd_insert(false);
    check_eq("bsp_sd_init() on an SDSC v1 card", (uint64_t)(uint32_t)DRIVE(bsp_sd_init()), 0);
    card = bsp_sd_card();
    check_eq("SDSC: high_capacity clear", card->high_capacity, 0);
    check_eq("SDSC: version_2 clear", card->version_2, 0);
    check_eq("SDSC: block count from CSD v1", card->block_count, sd_model_block_count(false));
    memset(buf, 0, sizeof(buf));
    check_eq("SDSC: read block 3 (byte address)", (uint64_t)(uint32_t)DRIVE(bsp_sd_read_blocks(3, buf, 1)), 0);
    check_sd_block_pattern("SDSC: block 3 contents", buf, 3, 1);

    /* --- no card ----------------------------------------------------------- */
    sd.present = false;
    check_eq("bsp_sd_init() with no card reports ENOCARD", (uint64_t)(uint32_t)DRIVE(bsp_sd_init()),
             (uint32_t)BSP_SD_ENOCARD);
    check_eq("not ready after the failure", bsp_sd_ready(), 0);

    group_end();
}

/* -- USB OTG_FS device + CDC-ACM ------------------------------------------ */

static uint16_t le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void test_usb(void)
{
    group("USB OTG_FS device / CDC-ACM");

    static uint8_t in[1024];
    unsigned got = 0;

    DRIVE(bsp_usb_cdc_init());
    check_eq("OTG_FS clock enabled in AHB2ENR", RCC->AHB2ENR & RCC_AHB2ENR_OTGFSEN, RCC_AHB2ENR_OTGFSEN);
    check_eq("PA11 AF10 (OTG_FS_DM)", FIELD(GPIOA->AFR[1], 0xFU << 12, 12), 10);
    check_eq("PA12 AF10 (OTG_FS_DP)", FIELD(GPIOA->AFR[1], 0xFU << 16, 16), 10);
    check_eq("device mode forced (GUSBCFG.FDMOD)", OTG_FS_GLOBAL->GUSBCFG & OTG_GUSBCFG_FDMOD, OTG_GUSBCFG_FDMOD);
    check_eq("full speed internal PHY (DCFG.DSPD=3)", FIELD(OTG_FS_DEVICE->DCFG, OTG_DCFG_DSPD_MSK, OTG_DCFG_DSPD_POS), 3);
    check_eq("connected (DCTL.SDIS clear)", OTG_FS_DEVICE->DCTL & OTG_DCTL_SDIS, 0);
    check_eq("not configured before enumeration", bsp_usb_configured(), 0);

    host_reset();
    check_eq("after reset: EP0 OUT armed for SETUP (STUPCNT=3)",
             FIELD(OTG_FS_DEVICE->OUTEP[0].DOEPTSIZ, OTG_DOEPTSIZ_RXDPID_STUPCNT_MSK, OTG_DOEPTSIZ_RXDPID_STUPCNT_POS), 3);
    check_eq("after reset: EP0 MPS = 64 (MPSIZ=0)",
             FIELD(OTG_FS_DEVICE->INEP[0].DIEPCTL, OTG_DIEPCTL_MPSIZ_MSK, OTG_DIEPCTL_MPSIZ_POS) & 0x3U, 0);

    /* Device descriptor: first the 8-byte probe a real host does, then all 18. */
    check_eq("GET_DESCRIPTOR(device, 8) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 8, NULL, in, &got), HOST_ACK);
    check_eq("  8 bytes returned", got, 8);
    check_eq("  bMaxPacketSize0 = 64", in[7], 64);
    check_eq("GET_DESCRIPTOR(device, 18) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 18, NULL, in, &got), HOST_ACK);
    check_eq("  18 bytes returned", got, 18);
    check_eq("  bLength/bDescriptorType", ((uint32_t)in[0] << 8) | in[1], 0x1201);
    check_eq("  bcdUSB = 2.00", le16(&in[2]), 0x0200);
    check_eq("  bDeviceClass = CDC (2)", in[4], 2);
    check_eq("  idVendor", le16(&in[8]), BSP_USB_VID);
    check_eq("  idProduct", le16(&in[10]), BSP_USB_PID);
    check_eq("  bNumConfigurations = 1", in[17], 1);

    check_eq("SET_ADDRESS(5) ACKed", host_control(0x00, USB_REQ_SET_ADDRESS, 5, 0, 0, NULL, NULL, NULL), HOST_ACK);
    check_eq("  DCFG.DAD = 5", FIELD(OTG_FS_DEVICE->DCFG, OTG_DCFG_DAD_MSK, OTG_DCFG_DAD_POS), 5);

    check_eq("GET_DESCRIPTOR(config, 9) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, 9, NULL, in, &got), HOST_ACK);
    check_eq("  9 bytes returned", got, 9);
    const uint16_t total = le16(&in[2]);
    check_eq("  wTotalLength = 67", total, 67);
    check_eq("  bNumInterfaces = 2", in[4], 2);
    check_eq("  bConfigurationValue = 1", in[5], 1);
    check_eq("  bmAttributes bit 7 set", in[7] & 0x80U, 0x80);
    check_eq("  bMaxPower = mA/2", in[8], BSP_USB_MAX_POWER_MA / 2U);
    check_eq("GET_DESCRIPTOR(config, full) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, total, NULL, in, &got), HOST_ACK);
    check_eq("  wTotalLength bytes returned", got, total);
    /* Walk the descriptor chain: every bLength must land exactly on the end. */
    unsigned off = 0, endpoints = 0, cs_ifaces = 0;
    while (off < got && in[off] != 0U)
    {
        if (in[off + 1] == USB_DESC_ENDPOINT) { endpoints++; }
        if (in[off + 1] == USB_DESC_CS_INTERFACE) { cs_ifaces++; }
        off += in[off];
    }
    check_eq("  descriptor chain is self-consistent", off, got);
    check_eq("  3 endpoint descriptors", endpoints, 3);
    check_eq("  4 CDC functional descriptors", cs_ifaces, 4);

    check_eq("GET_DESCRIPTOR(string 0) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_STRING << 8, 0, 255, NULL, in, &got), HOST_ACK);
    check_eq("  LANGID 0x0409", le16(&in[2]), 0x0409);
    check_eq("GET_DESCRIPTOR(string 2 = product) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, (USB_DESC_STRING << 8) | 2U, 0x0409, 255, NULL, in, &got), HOST_ACK);
    check_eq("  UTF-16 length matches", in[0], 2U + 2U * strlen(BSP_USB_PRODUCT));
    check_eq("  first character", in[2], (uint8_t)BSP_USB_PRODUCT[0]);
    check_eq("GET_DESCRIPTOR(string 3 = serial) ACKed",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, (USB_DESC_STRING << 8) | 3U, 0x0409, 255, NULL, in, &got), HOST_ACK);
    check_eq("  serial is 24 hex digits from the UID", in[0], 2U + 2U * 24U);

    check_eq("GET_DESCRIPTOR(device qualifier) STALLs (FS only)",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE_QUALIFIER << 8, 0, 10, NULL, in, &got), HOST_STALL);
    check_eq("unsupported vendor request STALLs",
             host_control(0xC0, 0x42, 0, 0, 4, NULL, in, &got), HOST_STALL);
    check_eq("device still answers after the STALL",
             host_control(0x80, USB_REQ_GET_STATUS, 0, 0, 2, NULL, in, &got), HOST_ACK);
    check_eq("  GET_STATUS(device) self-powered bit", in[0] & 1U, BSP_USB_SELF_POWERED);

    check_eq("SET_CONFIGURATION(1) ACKed", host_control(0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0, NULL, NULL, NULL), HOST_ACK);
    check_eq("  bsp_usb_configured()", bsp_usb_configured(), 1);
    check_eq("  EP1 IN active, bulk, MPS 64",
             OTG_FS_DEVICE->INEP[1].DIEPCTL & (OTG_DIEPCTL_USBAEP | OTG_DIEPCTL_EPTYP_MSK | OTG_DIEPCTL_MPSIZ_MSK),
             OTG_DIEPCTL_USBAEP | (2UL << OTG_DIEPCTL_EPTYP_POS) | 64UL);
    check_eq("  EP1 IN uses TX FIFO 1", FIELD(OTG_FS_DEVICE->INEP[1].DIEPCTL, OTG_DIEPCTL_TXFNUM_MSK, OTG_DIEPCTL_TXFNUM_POS), 1);
    check_eq("  EP1 OUT active, bulk, MPS 64, armed",
             OTG_FS_DEVICE->OUTEP[1].DOEPCTL & (OTG_DOEPCTL_USBAEP | OTG_DOEPCTL_EPTYP_MSK | OTG_DOEPCTL_MPSIZ_MSK | OTG_DOEPCTL_EPENA),
             OTG_DOEPCTL_USBAEP | (2UL << OTG_DOEPCTL_EPTYP_POS) | 64UL | OTG_DOEPCTL_EPENA);
    check_eq("  EP2 IN active, interrupt, MPS 8",
             OTG_FS_DEVICE->INEP[2].DIEPCTL & (OTG_DIEPCTL_USBAEP | OTG_DIEPCTL_EPTYP_MSK | OTG_DIEPCTL_MPSIZ_MSK),
             OTG_DIEPCTL_USBAEP | (3UL << OTG_DIEPCTL_EPTYP_POS) | 8UL);
    check_eq("GET_CONFIGURATION returns 1",
             host_control(0x80, USB_REQ_GET_CONFIGURATION, 0, 0, 1, NULL, in, &got), HOST_ACK);
    check_eq("  value", in[0], 1);
    check_eq("not connected until DTR", bsp_usb_cdc_connected(), 0);

    /* CDC class requests on interface 0. */
    const uint8_t coding[7] = {0x00, 0xC2, 0x01, 0x00, 0, 0, 8}; /* 115200 8N1 */
    check_eq("SET_LINE_CODING ACKed",
             host_control(0x21, CDC_REQ_SET_LINE_CODING, 0, 0, 7, coding, NULL, NULL), HOST_ACK);
    bsp_usb_cdc_line_coding_t lc = bsp_usb_cdc_line_coding();
    check_eq("  baud seen by the application", lc.baud, 115200);
    check_eq("  data bits", lc.data_bits, 8);
    const uint8_t coding2[7] = {0x80, 0x25, 0x00, 0x00, 2, 1, 7}; /* 9600 7O2 */
    check_eq("SET_LINE_CODING(9600 7O2) ACKed",
             host_control(0x21, CDC_REQ_SET_LINE_CODING, 0, 0, 7, coding2, NULL, NULL), HOST_ACK);
    lc = bsp_usb_cdc_line_coding();
    check_eq("  baud/stop/parity/bits", ((uint64_t)lc.baud << 24) | (lc.stop_bits << 16) | (lc.parity << 8) | lc.data_bits,
             (9600ULL << 24) | (2U << 16) | (1U << 8) | 7U);
    check_eq("GET_LINE_CODING ACKed",
             host_control(0xA1, CDC_REQ_GET_LINE_CODING, 0, 0, 7, NULL, in, &got), HOST_ACK);
    check_eq("  echoes the 7 bytes", got == 7U && memcmp(in, coding2, 7) == 0, 1);

    check_eq("SET_CONTROL_LINE_STATE(DTR|RTS) ACKed",
             host_control(0x21, CDC_REQ_SET_CONTROL_LINE_STATE, 0x0003, 0, 0, NULL, NULL, NULL), HOST_ACK);
    check_eq("  DTR seen", bsp_usb_cdc_dtr(), 1);
    check_eq("  RTS seen", bsp_usb_cdc_rts(), 1);
    check_eq("  connected", bsp_usb_cdc_connected(), 1);

    /* Host -> device on EP1 OUT. */
    const uint8_t hello[] = "hello, blackpill";
    check_eq("OUT EP1 packet ACKed", host_out(1, hello, sizeof(hello) - 1U), HOST_ACK);
    check_eq("  bytes available to the application", bsp_usb_cdc_available(), sizeof(hello) - 1U);
    uint8_t rx[32];
    check_eq("  read returns them", bsp_usb_cdc_read(rx, sizeof(rx)), sizeof(hello) - 1U);
    check_eq("  contents", memcmp(rx, hello, sizeof(hello) - 1U), 0);
    check_eq("  EP1 OUT re-armed", OTG_FS_DEVICE->OUTEP[1].DOEPCTL & OTG_DOEPCTL_EPENA, OTG_DOEPCTL_EPENA);
    check_eq("  first byte via read_byte after a second packet",
             (host_out(1, (const uint8_t*)"Z", 1) == HOST_ACK) ? bsp_usb_cdc_read_byte() : 0, 'Z');

    /* Device -> host on EP1 IN: a short message, then a 64-byte multiple
     * which owes the host a zero-length packet. */
    const uint8_t msg[] = "pong";
    check_eq("write of 4 bytes accepted", DRIVE(bsp_usb_cdc_write(msg, 4)), 4);
    check_eq("IN EP1 delivers them", host_in(1, in, &got), HOST_ACK);
    check_eq("  4 bytes", got, 4);
    check_eq("  contents", memcmp(in, msg, 4), 0);
    check_eq("IN EP1 NAKs when idle", host_in(1, in, &got), HOST_NAK);

    uint8_t big[128];
    for (unsigned i = 0; i < sizeof(big); i++) { big[i] = (uint8_t)i; }
    check_eq("write of 128 bytes accepted", DRIVE(bsp_usb_cdc_write(big, sizeof(big))), 128);
    check_eq("IN EP1 delivers the transfer", host_in(1, in, &got), HOST_ACK);
    check_eq("  128 bytes in one transfer (2 packets)", got, 128);
    check_eq("  contents", memcmp(in, big, 128), 0);
    check_eq("  zero-length packet follows (length % 64 == 0)", host_in(1, in, &got) == HOST_ACK && got == 0U, 1);
    check_eq("IN EP1 NAKs again", host_in(1, in, &got), HOST_NAK);

    /* Larger than one chunk: the ring drains over successive IN tokens. */
    static uint8_t huge[700];
    for (unsigned i = 0; i < sizeof(huge); i++) { huge[i] = (uint8_t)(i * 7U); }
    const size_t accepted = DRIVE(bsp_usb_cdc_write(huge, sizeof(huge)));
    check_eq("write of 700 bytes accepted (ring + FIFO)", accepted, 700);
    unsigned collected = 0;
    for (int guard = 0; guard < 8 && collected < 700U; guard++)
    {
        if (host_in(1, in + collected, &got) != HOST_ACK) { break; }
        collected += got;
    }
    check_eq("  all 700 bytes reach the host", collected, 700);
    check_eq("  contents", memcmp(in, huge, 700), 0);

    /* DTR dropped: writes are discarded, nothing queued. */
    check_eq("SET_CONTROL_LINE_STATE(0) ACKed",
             host_control(0x21, CDC_REQ_SET_CONTROL_LINE_STATE, 0, 0, 0, NULL, NULL, NULL), HOST_ACK);
    check_eq("  disconnected", bsp_usb_cdc_connected(), 0);
    check_eq("  write while disconnected is discarded (0 accepted)", DRIVE(bsp_usb_cdc_write(msg, 4)), 0);
    while (host_in(1, in, &got) == HOST_ACK && got != 0U) { }
    check_eq("  nothing reaches the host", got, 0);

    check_eq("SET_CONFIGURATION(0) ACKed", host_control(0x00, USB_REQ_SET_CONFIGURATION, 0, 0, 0, NULL, NULL, NULL), HOST_ACK);
    check_eq("  unconfigured", bsp_usb_configured(), 0);
    check_eq("  EP1 IN deactivated", OTG_FS_DEVICE->INEP[1].DIEPCTL & OTG_DIEPCTL_USBAEP, 0);
    check_eq("  EP1 OUT deactivated", OTG_FS_DEVICE->OUTEP[1].DOEPCTL & (OTG_DOEPCTL_USBAEP | OTG_DOEPCTL_EPENA), 0);
    check_eq("OUT EP1 NAKs when closed", host_out(1, hello, 4), HOST_NAK);

    /* A second bus reset must bring the device back to the default state. */
    host_reset();
    check_eq("after re-reset: DAD = 0", FIELD(OTG_FS_DEVICE->DCFG, OTG_DCFG_DAD_MSK, OTG_DCFG_DAD_POS), 0);
    check_eq("after re-reset: device descriptor still served",
             host_control(0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 18, NULL, in, &got), HOST_ACK);

    printf("        (%u interrupt handler runs, %u IN tokens, %u OUT tokens)\n", usb.irqs, usb.in_tokens,
           usb.out_tokens);
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
    OTG_FS_GLOBAL->GRSTCTL = 0x80000000UL; /* AHBIDL: the AHB master is idle */
    OTG_FS_GLOBAL->GUSBCFG = 0x00001440UL; /* TRDT=5, TOCAL=0                */
    OTG_FS_DEVICE->DCTL = 0x00000002UL;    /* SDIS: soft-disconnected        */
    ((volatile uint32_t*)UID_BASE)[0] = 0x00230041UL; /* a plausible 96-bit unique ID */
    ((volatile uint32_t*)UID_BASE)[1] = 0x30395110UL;
    ((volatile uint32_t*)UID_BASE)[2] = 0x20313436UL;

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
    test_sdio();
    test_usb();

    monitor_disarm();
    atomic_store(&sim_stop, 1);
    pthread_join(th, NULL);

    printf("\n%u checks, %u failed; %lu register accesses traced\n", tests_run, tests_failed,
           mon_accesses);
    return tests_failed == 0 ? 0 : 1;
}
