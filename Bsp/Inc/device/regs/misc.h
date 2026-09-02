/**
 * @file    misc.h
 * @brief   CRC unit, watchdogs and RTC registers (RM0383 sec. 4, 16, 17, 22).
 *
 * Grouped together because each of these peripherals is only a handful of
 * registers.
 */
#ifndef DEVICE_REGS_MISC_H
#define DEVICE_REGS_MISC_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------------ */
/* CRC calculation unit                                                      */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t DR;     /**< 0x00 data (write to feed, read for result) */
    __IO uint8_t IDR;     /**< 0x04 independent scratch byte              */
    uint8_t RESERVED0;    /*   0x05                                       */
    uint16_t RESERVED1;   /*   0x06                                       */
    __IO uint32_t CR;     /**< 0x08 control                               */
} crc_regs_t;

#define CRC_BASE (AHB1PERIPH_BASE + 0x3000UL)
#define CRC      ((crc_regs_t*)CRC_BASE)

#define CRC_CR_RESET (1UL << 0)

/* ------------------------------------------------------------------------ */
/* Independent watchdog                                                      */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __O uint32_t KR;  /**< 0x00 key      */
    __IO uint32_t PR; /**< 0x04 prescaler*/
    __IO uint32_t RLR;/**< 0x08 reload   */
    __I uint32_t SR;  /**< 0x0C status   */
} iwdg_regs_t;

#define IWDG_BASE (APB1PERIPH_BASE + 0x3000UL)
#define IWDG      ((iwdg_regs_t*)IWDG_BASE)

#define IWDG_KEY_RELOAD 0x0000AAAAUL /**< pet the dog                */
#define IWDG_KEY_ENABLE 0x0000CCCCUL /**< start the counter          */
#define IWDG_KEY_WRITE  0x00005555UL /**< unlock PR/RLR for writing  */

#define IWDG_SR_PVU (1UL << 0) /**< prescaler update in progress */
#define IWDG_SR_RVU (1UL << 1) /**< reload update in progress    */

/** LSI nominal frequency; the datasheet allows 17-47 kHz, so treat timeouts
 *  derived from it as approximate. */
#define LSI_FREQ_HZ 32000UL

/* ------------------------------------------------------------------------ */
/* Window watchdog                                                           */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t CR;  /**< 0x00 control       */
    __IO uint32_t CFR; /**< 0x04 configuration */
    __IO uint32_t SR;  /**< 0x08 status        */
} wwdg_regs_t;

#define WWDG_BASE (APB1PERIPH_BASE + 0x2C00UL)
#define WWDG      ((wwdg_regs_t*)WWDG_BASE)

#define WWDG_CR_T_MSK   (0x7FUL << 0)
#define WWDG_CR_WDGA    (1UL << 7)
#define WWDG_CFR_W_MSK  (0x7FUL << 0)
#define WWDG_CFR_WDGTB_POS 7U
#define WWDG_CFR_WDGTB_MSK (0x3UL << WWDG_CFR_WDGTB_POS)
#define WWDG_CFR_EWI    (1UL << 9)
#define WWDG_SR_EWIF    (1UL << 0)

/* ------------------------------------------------------------------------ */
/* Real-time clock                                                           */
/* ------------------------------------------------------------------------ */
typedef struct
{
    __IO uint32_t TR;      /**< 0x00 time                     */
    __IO uint32_t DR;      /**< 0x04 date                     */
    __IO uint32_t CR;      /**< 0x08 control                  */
    __IO uint32_t ISR;     /**< 0x0C initialisation/status    */
    __IO uint32_t PRER;    /**< 0x10 prescaler                */
    __IO uint32_t WUTR;    /**< 0x14 wakeup timer             */
    __IO uint32_t CALIBR;  /**< 0x18 calibration              */
    __IO uint32_t ALRMAR;  /**< 0x1C alarm A                  */
    __IO uint32_t ALRMBR;  /**< 0x20 alarm B                  */
    __O uint32_t WPR;      /**< 0x24 write protection         */
    __I uint32_t SSR;      /**< 0x28 sub second               */
    __O uint32_t SHIFTR;   /**< 0x2C shift control            */
    __I uint32_t TSTR;     /**< 0x30 timestamp time           */
    __I uint32_t TSDR;     /**< 0x34 timestamp date           */
    __I uint32_t TSSSR;    /**< 0x38 timestamp sub second     */
    __IO uint32_t CALR;    /**< 0x3C calibration              */
    __IO uint32_t TAFCR;   /**< 0x40 tamper and alt function  */
    __IO uint32_t ALRMASSR;/**< 0x44 alarm A sub second       */
    __IO uint32_t ALRMBSSR;/**< 0x48 alarm B sub second       */
    uint32_t RESERVED0;    /*   0x4C                          */
    __IO uint32_t BKP[20]; /**< 0x50 backup registers 0-19    */
} rtc_regs_t;

#define RTC_BASE (APB1PERIPH_BASE + 0x2800UL)
#define RTC      ((rtc_regs_t*)RTC_BASE)

#define RTC_CR_FMT     (1UL << 6) /**< 1 = 12 hour format */
#define RTC_CR_ALRAE   (1UL << 8)
#define RTC_CR_WUTE    (1UL << 10)
#define RTC_CR_ALRAIE  (1UL << 12)

#define RTC_ISR_ALRAWF (1UL << 0)
#define RTC_ISR_INITS  (1UL << 4) /**< calendar has been initialised */
#define RTC_ISR_RSF    (1UL << 5) /**< shadow registers synchronised */
#define RTC_ISR_INITF  (1UL << 6) /**< initialisation mode entered   */
#define RTC_ISR_INIT   (1UL << 7) /**< request initialisation mode   */
#define RTC_ISR_ALRAF  (1UL << 8)

#define RTC_WPR_KEY1 0xCAUL /**< write these two in order to unlock */
#define RTC_WPR_KEY2 0x53UL

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_MISC_H */
