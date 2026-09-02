/**
 * @file    bsp_misc.c
 * @brief   CRC unit, watchdogs and RTC.
 */

#include "bsp_misc.h"

/* ------------------------------------------------------------------------ */
/* CRC                                                                       */
/* ------------------------------------------------------------------------ */
#if BSP_USE_CRC

void bsp_crc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;
    bsp_crc_reset();
}

void bsp_crc_reset(void)
{
    CRC->CR = CRC_CR_RESET;
}

uint32_t bsp_crc_accumulate(const uint32_t* data, uint32_t word_count)
{
    for (uint32_t i = 0U; i < word_count; i++)
    {
        CRC->DR = data[i];
    }
    return CRC->DR;
}

uint32_t bsp_crc_calculate(const uint32_t* data, uint32_t word_count)
{
    bsp_crc_reset();
    return bsp_crc_accumulate(data, word_count);
}

#endif /* BSP_USE_CRC */

/* ------------------------------------------------------------------------ */
/* Independent watchdog                                                      */
/* ------------------------------------------------------------------------ */
#if BSP_USE_IWDG

void bsp_iwdg_init(void)
{
    /*
     * Pick the smallest prescaler that can express the requested timeout, so
     * the reload value keeps as much resolution as possible.
     * timeout_ms = reload * prescaler * 1000 / LSI.
     */
    uint32_t prescaler_code = 0U; /* divider = 4 << code */
    uint32_t reload = 0U;

    for (; prescaler_code <= 6U; prescaler_code++)
    {
        const uint32_t divider = 4UL << prescaler_code;
        reload = (BSP_IWDG_TIMEOUT_MS * (LSI_FREQ_HZ / 1000UL)) / divider;
        if (reload <= 0xFFFUL)
        {
            break;
        }
    }

    if (reload > 0xFFFUL)
    {
        reload = 0xFFFUL;
    }
    if (reload != 0U)
    {
        reload -= 1U;
    }

    IWDG->KR = IWDG_KEY_WRITE;
    IWDG->PR = prescaler_code;
    IWDG->RLR = reload;

    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0U)
    {
    }

    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_ENABLE;
}

void bsp_iwdg_refresh(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

#endif /* BSP_USE_IWDG */

/* ------------------------------------------------------------------------ */
/* Window watchdog                                                           */
/* ------------------------------------------------------------------------ */
#if BSP_USE_WWDG

void bsp_wwdg_init(uint8_t window, uint8_t counter)
{
    RCC->APB1ENR |= RCC_APB1ENR_WWDGEN;
    (void)RCC->APB1ENR;

    /* Slowest time base gives the longest window the hardware can express. */
    WWDG->CFR = (0x3UL << WWDG_CFR_WDGTB_POS) | (window & WWDG_CFR_W_MSK);

    /* Bit 6 of the counter must stay set: the reset fires the moment it is
     * cleared, so a counter written below 0x40 resets the part immediately. */
    WWDG->CR = WWDG_CR_WDGA | 0x40U | (counter & 0x3FU);
}

void bsp_wwdg_refresh(uint8_t counter)
{
    WWDG->CR = WWDG_CR_WDGA | 0x40U | (counter & 0x3FU);
}

#endif /* BSP_USE_WWDG */

/* ------------------------------------------------------------------------ */
/* RTC                                                                       */
/* ------------------------------------------------------------------------ */
#if BSP_USE_RTC

#if BSP_RTC_CLOCK_LSE && !BSP_USE_LSE
#error "BSP_RTC_CLOCK_LSE needs BSP_USE_LSE to be enabled"
#endif
#if !BSP_RTC_CLOCK_LSE && !BSP_USE_LSI
#error "Clocking the RTC from the LSI needs BSP_USE_LSI to be enabled"
#endif

/* Asynchronous / synchronous prescalers producing a 1 Hz calendar clock. */
#if BSP_RTC_CLOCK_LSE
#define RTC_ASYNC_PRESCALER 127U
#define RTC_SYNC_PRESCALER  255U /* 32768 / 128 / 256 = 1 Hz */
#define RTC_SOURCE_SELECT   (0x1UL << RCC_BDCR_RTCSEL_POS)
#else
#define RTC_ASYNC_PRESCALER 127U
#define RTC_SYNC_PRESCALER  249U /* 32000 / 128 / 250 = 1 Hz */
#define RTC_SOURCE_SELECT   (0x2UL << RCC_BDCR_RTCSEL_POS)
#endif

#define RTC_TIMEOUT 0x00100000UL

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static void rtc_unlock(void)
{
    RTC->WPR = RTC_WPR_KEY1;
    RTC->WPR = RTC_WPR_KEY2;
}

static void rtc_lock(void)
{
    RTC->WPR = 0xFFU;
}

/** @brief Enter initialisation mode, where the calendar can be written. */
static int rtc_enter_init(void)
{
    if ((RTC->ISR & RTC_ISR_INITF) != 0U)
    {
        return 0;
    }

    RTC->ISR = 0xFFFFFFFFU; /* INIT is set by writing 1; other bits are rc_w0 */

    uint32_t timeout = RTC_TIMEOUT;
    while ((RTC->ISR & RTC_ISR_INITF) == 0U)
    {
        if (timeout-- == 0U)
        {
            return -1;
        }
    }
    return 0;
}

static void rtc_exit_init(void)
{
    RTC->ISR &= ~RTC_ISR_INIT;
}

/**
 * @brief Wait for the calendar shadow registers to reload.
 *
 * With BYPSHAD clear, TR and DR are copies refreshed twice per RTCCLK period.
 * Right after leaving init mode, or after any halt of the clock, they hold
 * stale values until RSF comes back up.
 */
static int rtc_wait_sync(void)
{
    RTC->ISR &= ~RTC_ISR_RSF;

    uint32_t timeout = RTC_TIMEOUT;
    while ((RTC->ISR & RTC_ISR_RSF) == 0U)
    {
        if (timeout-- == 0U)
        {
            return -1;
        }
    }
    return 0;
}

int bsp_rtc_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;

    if ((RCC->BDCR & RCC_BDCR_RTCSEL_MSK) != RTC_SOURCE_SELECT)
    {
        /* Changing the source requires a backup domain reset. */
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;

#if BSP_RTC_CLOCK_LSE
        RCC->BDCR |= RCC_BDCR_LSEON;
        uint32_t timeout = RTC_TIMEOUT;
        while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U)
        {
            if (timeout-- == 0U)
            {
                return -1;
            }
        }
#endif
        RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL_MSK) | RTC_SOURCE_SELECT;
    }

    RCC->BDCR |= RCC_BDCR_RTCEN;

    rtc_unlock();

    /* Leave a running calendar alone so the time survives a reset. */
    if ((RTC->ISR & RTC_ISR_INITS) != 0U)
    {
        const int synced = rtc_wait_sync();
        rtc_lock();
        return (synced == 0) ? 0 : -3;
    }

    if (rtc_enter_init() != 0)
    {
        rtc_lock();
        return -2;
    }

    /* PRER must be written as two separate accesses, synchronous prescaler
     * first, or the second write is ignored (RM0383 sec. 22.6.6). */
    RTC->PRER = RTC_SYNC_PRESCALER;
    RTC->PRER = ((uint32_t)RTC_ASYNC_PRESCALER << 16U) | RTC_SYNC_PRESCALER;
    RTC->CR &= ~RTC_CR_FMT; /* 24 hour format */

    rtc_exit_init();

    const int synced = rtc_wait_sync();
    rtc_lock();
    return (synced == 0) ? 0 : -3;
}

int bsp_rtc_set(const bsp_rtc_datetime_t* datetime)
{
    rtc_unlock();
    if (rtc_enter_init() != 0)
    {
        rtc_lock();
        return -1;
    }

    RTC->TR = ((uint32_t)to_bcd(datetime->hours) << 16U) |
              ((uint32_t)to_bcd(datetime->minutes) << 8U) | to_bcd(datetime->seconds);

    RTC->DR = ((uint32_t)to_bcd(datetime->year) << 16U) |
              ((uint32_t)(datetime->weekday & 0x7U) << 13U) |
              ((uint32_t)to_bcd(datetime->month) << 8U) | to_bcd(datetime->day);

    rtc_exit_init();

    const int synced = rtc_wait_sync();
    rtc_lock();
    return (synced == 0) ? 0 : -2;
}

void bsp_rtc_get(bsp_rtc_datetime_t* datetime)
{
    /* Read TR first: doing so freezes DR until DR is read (RM0383 sec. 22.3.6). */
    const uint32_t tr = RTC->TR;
    const uint32_t dr = RTC->DR;

    datetime->hours = from_bcd((uint8_t)((tr >> 16U) & 0x3FU));
    datetime->minutes = from_bcd((uint8_t)((tr >> 8U) & 0x7FU));
    datetime->seconds = from_bcd((uint8_t)(tr & 0x7FU));

    datetime->year = from_bcd((uint8_t)((dr >> 16U) & 0xFFU));
    datetime->weekday = (uint8_t)((dr >> 13U) & 0x7U);
    datetime->month = from_bcd((uint8_t)((dr >> 8U) & 0x1FU));
    datetime->day = from_bcd((uint8_t)(dr & 0x3FU));
}

#endif /* BSP_USE_RTC */
