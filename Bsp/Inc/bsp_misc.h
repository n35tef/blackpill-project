/**
 * @file    bsp_misc.h
 * @brief   CRC unit, watchdogs and RTC.
 *
 * These peripherals are small enough that one module covers all of them; each
 * section still compiles away independently via its own BSP_USE_* switch.
 */
#ifndef BSP_MISC_H
#define BSP_MISC_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "device/stm32f411.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------------ */
/* CRC                                                                       */
/* ------------------------------------------------------------------------ */
#if BSP_USE_CRC

/** @brief Enable the CRC unit's clock and reset its accumulator. */
void bsp_crc_init(void);

/** @brief Restart the calculation from the 0xFFFFFFFF seed. */
void bsp_crc_reset(void);

/**
 * @brief Accumulate words into the hardware CRC-32 (Ethernet polynomial).
 * @return The running CRC after the given words.
 */
uint32_t bsp_crc_accumulate(const uint32_t* data, uint32_t word_count);

/** @brief Reset then accumulate, giving a standalone CRC of the buffer. */
uint32_t bsp_crc_calculate(const uint32_t* data, uint32_t word_count);

#endif /* BSP_USE_CRC */

/* ------------------------------------------------------------------------ */
/* Independent watchdog                                                      */
/* ------------------------------------------------------------------------ */
#if BSP_USE_IWDG

/**
 * @brief Start the independent watchdog with the timeout from bsp_config.h.
 *
 * Once started the IWDG cannot be stopped in software; the only way out is a
 * reset. Its clock is the LSI, whose frequency varies with temperature and
 * supply, so treat the timeout as approximate.
 */
void bsp_iwdg_init(void);

/** @brief Pet the watchdog. Call at least once per timeout period. */
void bsp_iwdg_refresh(void);

#endif /* BSP_USE_IWDG */

/* ------------------------------------------------------------------------ */
/* Window watchdog                                                           */
/* ------------------------------------------------------------------------ */
#if BSP_USE_WWDG

/**
 * @brief Start the window watchdog.
 * @param window  Upper bound of the refresh window, 0x40..0x7F.
 * @param counter Starting counter value, must be > window and <= 0x7F.
 */
void bsp_wwdg_init(uint8_t window, uint8_t counter);

/** @brief Reload the counter. Doing this too early also triggers a reset. */
void bsp_wwdg_refresh(uint8_t counter);

#endif /* BSP_USE_WWDG */

/* ------------------------------------------------------------------------ */
/* RTC                                                                       */
/* ------------------------------------------------------------------------ */
#if BSP_USE_RTC

/** Wall-clock time held by the RTC calendar. */
typedef struct
{
    uint8_t hours;   /**< 0..23 */
    uint8_t minutes; /**< 0..59 */
    uint8_t seconds; /**< 0..59 */
    uint8_t day;     /**< 1..31 */
    uint8_t month;   /**< 1..12 */
    uint8_t year;    /**< 0..99, offset from 2000 */
    uint8_t weekday; /**< 1 = Monday .. 7 = Sunday */
} bsp_rtc_datetime_t;

/**
 * @brief Configure the RTC clock source and prescalers.
 *
 * Skips re-initialising the calendar if it is already running, so the time
 * survives a reset as long as VBAT is maintained.
 *
 * @return 0 on success, non-zero if the RTC could not be unlocked.
 */
int bsp_rtc_init(void);

/** @brief Write the calendar. */
int bsp_rtc_set(const bsp_rtc_datetime_t* datetime);

/** @brief Read the calendar. */
void bsp_rtc_get(bsp_rtc_datetime_t* datetime);

#endif /* BSP_USE_RTC */

#ifdef __cplusplus
}
#endif

#endif /* BSP_MISC_H */
