/**
 * @file    otg_fs.h
 * @brief   USB on-the-go full-speed core registers (RM0383 sec. 22.15).
 *
 * The core is one 1 KB-stride address space split into blocks: global
 * (0x000), host (0x400), device (0x800), per-endpoint (0x900 IN, 0xB00 OUT),
 * power/clock gating (0xE00) and one 4 KB data FIFO window per endpoint from
 * 0x1000. Each block is its own struct so the offsets stay readable; the
 * endpoint and host-channel blocks repeat at a 0x20 stride and are arrays.
 *
 * Bit names follow RM0383. Where EP0 has a narrower field than EP1..3 (MPSIZ,
 * XFRSIZ, PKTCNT) the macro describes EP1..3 and the EP0 variant is named
 * separately.
 */
#ifndef DEVICE_REGS_OTG_FS_H
#define DEVICE_REGS_OTG_FS_H

#include "device/stm32f411_memmap.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- Global block (0x000) ----------------------------------------------- */
typedef struct
{
    __IO uint32_t GOTGCTL;   /**< 0x000 OTG control and status      */
    __IO uint32_t GOTGINT;   /**< 0x004 OTG interrupt               */
    __IO uint32_t GAHBCFG;   /**< 0x008 AHB configuration           */
    __IO uint32_t GUSBCFG;   /**< 0x00C USB configuration           */
    __IO uint32_t GRSTCTL;   /**< 0x010 reset                       */
    __IO uint32_t GINTSTS;   /**< 0x014 core interrupt              */
    __IO uint32_t GINTMSK;   /**< 0x018 interrupt mask              */
    __I uint32_t GRXSTSR;    /**< 0x01C receive status debug read   */
    __I uint32_t GRXSTSP;    /**< 0x020 receive status read and pop */
    __IO uint32_t GRXFSIZ;   /**< 0x024 receive FIFO size           */
    __IO uint32_t DIEPTXF0;  /**< 0x028 EP0 tx FIFO size (HNPTXFSIZ in host mode) */
    __I uint32_t HNPTXSTS;   /**< 0x02C non-periodic tx FIFO/queue status         */
    uint32_t RESERVED0[2];   /*   0x030 */
    __IO uint32_t GCCFG;     /**< 0x038 general core configuration */
    __IO uint32_t CID;       /**< 0x03C core ID                    */
    uint32_t RESERVED1[48];  /*   0x040 */
    __IO uint32_t HPTXFSIZ;  /**< 0x100 host periodic tx FIFO size */
    __IO uint32_t DIEPTXF1;  /**< 0x104 device IN EP1 tx FIFO size */
    __IO uint32_t DIEPTXF2;  /**< 0x108 device IN EP2 tx FIFO size */
    __IO uint32_t DIEPTXF3;  /**< 0x10C device IN EP3 tx FIFO size */
} otg_fs_global_regs_t;

/* ---- Host block (0x400) ------------------------------------------------- */
typedef struct
{
    __IO uint32_t HCCHAR;   /**< 0x00 channel characteristics */
    uint32_t RESERVED0;     /*   0x04 */
    __IO uint32_t HCINT;    /**< 0x08 channel interrupt       */
    __IO uint32_t HCINTMSK; /**< 0x0C channel interrupt mask  */
    __IO uint32_t HCTSIZ;   /**< 0x10 channel transfer size   */
    uint32_t RESERVED1[3];  /*   0x14 */
} otg_fs_hc_regs_t;

typedef struct
{
    __IO uint32_t HCFG;     /**< 0x400 host configuration            */
    __IO uint32_t HFIR;     /**< 0x404 frame interval                */
    __I uint32_t HFNUM;     /**< 0x408 frame number / time remaining */
    uint32_t RESERVED0;     /*   0x40C */
    __I uint32_t HPTXSTS;   /**< 0x410 periodic tx FIFO/queue status */
    __I uint32_t HAINT;     /**< 0x414 all channels interrupt        */
    __IO uint32_t HAINTMSK; /**< 0x418 all channels interrupt mask   */
    uint32_t RESERVED1[9];  /*   0x41C */
    __IO uint32_t HPRT;     /**< 0x440 port control and status       */
    uint32_t RESERVED2[47]; /*   0x444 */
    otg_fs_hc_regs_t HC[8]; /**< 0x500 host channels, 0x20 apart     */
} otg_fs_host_regs_t;

/* ---- Device block (0x800) ----------------------------------------------- */
typedef struct
{
    __IO uint32_t DIEPCTL;  /**< 0x00 IN endpoint control       */
    uint32_t RESERVED0;     /*   0x04 */
    __IO uint32_t DIEPINT;  /**< 0x08 IN endpoint interrupt     */
    uint32_t RESERVED1;     /*   0x0C */
    __IO uint32_t DIEPTSIZ; /**< 0x10 IN endpoint transfer size */
    uint32_t RESERVED2;     /*   0x14 */
    __I uint32_t DTXFSTS;   /**< 0x18 IN endpoint tx FIFO status*/
    uint32_t RESERVED3;     /*   0x1C */
} otg_fs_inep_regs_t;

typedef struct
{
    __IO uint32_t DOEPCTL;  /**< 0x00 OUT endpoint control       */
    uint32_t RESERVED0;     /*   0x04 */
    __IO uint32_t DOEPINT;  /**< 0x08 OUT endpoint interrupt     */
    uint32_t RESERVED1;     /*   0x0C */
    __IO uint32_t DOEPTSIZ; /**< 0x10 OUT endpoint transfer size */
    uint32_t RESERVED2[3];  /*   0x14 */
} otg_fs_outep_regs_t;

typedef struct
{
    __IO uint32_t DCFG;              /**< 0x800 device configuration           */
    __IO uint32_t DCTL;              /**< 0x804 device control                 */
    __I uint32_t DSTS;               /**< 0x808 device status                  */
    uint32_t RESERVED0;              /*   0x80C */
    __IO uint32_t DIEPMSK;           /**< 0x810 IN endpoint common int mask    */
    __IO uint32_t DOEPMSK;           /**< 0x814 OUT endpoint common int mask   */
    __I uint32_t DAINT;              /**< 0x818 all endpoints interrupt        */
    __IO uint32_t DAINTMSK;          /**< 0x81C all endpoints interrupt mask   */
    uint32_t RESERVED1[2];           /*   0x820 */
    __IO uint32_t DVBUSDIS;          /**< 0x828 VBUS discharge time            */
    __IO uint32_t DVBUSPULSE;        /**< 0x82C VBUS pulsing time              */
    uint32_t RESERVED2;              /*   0x830 */
    __IO uint32_t DIEPEMPMSK;        /**< 0x834 IN endpoint FIFO empty int mask*/
    uint32_t RESERVED3[50];          /*   0x838 */
    otg_fs_inep_regs_t INEP[4];      /**< 0x900 IN endpoints 0..3, 0x20 apart  */
    uint32_t RESERVED4[96];          /*   0x980 */
    otg_fs_outep_regs_t OUTEP[4];    /**< 0xB00 OUT endpoints 0..3, 0x20 apart */
} otg_fs_device_regs_t;

/* ---- Power and clock gating block (0xE00) ------------------------------- */
typedef struct
{
    __IO uint32_t PCGCCTL; /**< 0xE00 power and clock gating control */
} otg_fs_pwrclk_regs_t;

#define OTG_FS_BASE        AHB2PERIPH_BASE
#define OTG_FS_GLOBAL_BASE (OTG_FS_BASE + 0x000UL)
#define OTG_FS_HOST_BASE   (OTG_FS_BASE + 0x400UL)
#define OTG_FS_DEVICE_BASE (OTG_FS_BASE + 0x800UL)
#define OTG_FS_PWRCLK_BASE (OTG_FS_BASE + 0xE00UL)
#define OTG_FS_FIFO_BASE   (OTG_FS_BASE + 0x1000UL)
#define OTG_FS_FIFO_STRIDE 0x1000UL

#define OTG_FS_GLOBAL ((otg_fs_global_regs_t*)OTG_FS_GLOBAL_BASE)
#define OTG_FS_HOST   ((otg_fs_host_regs_t*)OTG_FS_HOST_BASE)
#define OTG_FS_DEVICE ((otg_fs_device_regs_t*)OTG_FS_DEVICE_BASE)
#define OTG_FS_PWRCLK ((otg_fs_pwrclk_regs_t*)OTG_FS_PWRCLK_BASE)

/** Data FIFO window for endpoint @p ep; any word write pushes, any read pops. */
#define OTG_FS_FIFO(ep) ((__IO uint32_t*)(OTG_FS_FIFO_BASE + ((uint32_t)(ep) * OTG_FS_FIFO_STRIDE)))

/** Number of device endpoints in each direction, and the shared FIFO RAM. */
#define OTG_FS_EP_COUNT       4U
#define OTG_FS_FIFO_RAM_WORDS 320U /**< 1.25 KB of packet RAM, in 32-bit words */

/* ---- GOTGCTL ------------------------------------------------------------ */
#define OTG_GOTGCTL_SRQSCS  (1UL << 0)
#define OTG_GOTGCTL_SRQ     (1UL << 1)
#define OTG_GOTGCTL_HNGSCS  (1UL << 8)
#define OTG_GOTGCTL_HNPRQ   (1UL << 9)
#define OTG_GOTGCTL_HSHNPEN (1UL << 10)
#define OTG_GOTGCTL_DHNPEN  (1UL << 11)
#define OTG_GOTGCTL_CIDSTS  (1UL << 16)
#define OTG_GOTGCTL_DBCT    (1UL << 17)
#define OTG_GOTGCTL_ASVLD   (1UL << 18)
#define OTG_GOTGCTL_BSVLD   (1UL << 19)

/* ---- GOTGINT ------------------------------------------------------------ */
#define OTG_GOTGINT_SEDET   (1UL << 2)
#define OTG_GOTGINT_SRSSCHG (1UL << 8)
#define OTG_GOTGINT_HNSSCHG (1UL << 9)
#define OTG_GOTGINT_HNGDET  (1UL << 17)
#define OTG_GOTGINT_ADTOCHG (1UL << 18)
#define OTG_GOTGINT_DBCDNE  (1UL << 19)

/* ---- GAHBCFG ------------------------------------------------------------ */
#define OTG_GAHBCFG_GINT     (1UL << 0) /**< global interrupt enable            */
#define OTG_GAHBCFG_TXFELVL  (1UL << 7) /**< 1 = TXFE fires when completely empty */
#define OTG_GAHBCFG_PTXFELVL (1UL << 8)

/* ---- GUSBCFG ------------------------------------------------------------ */
#define OTG_GUSBCFG_TOCAL_POS 0U
#define OTG_GUSBCFG_TOCAL_MSK (0x7UL << OTG_GUSBCFG_TOCAL_POS)
#define OTG_GUSBCFG_PHYSEL    (1UL << 6)  /**< always 1: full-speed transceiver */
#define OTG_GUSBCFG_SRPCAP    (1UL << 8)
#define OTG_GUSBCFG_HNPCAP    (1UL << 9)
#define OTG_GUSBCFG_TRDT_POS  10U
#define OTG_GUSBCFG_TRDT_MSK  (0xFUL << OTG_GUSBCFG_TRDT_POS) /**< turnaround time, from AHB clock */
#define OTG_GUSBCFG_FHMOD     (1UL << 29) /**< force host mode   */
#define OTG_GUSBCFG_FDMOD     (1UL << 30) /**< force device mode */
#define OTG_GUSBCFG_CTXPKT    (1UL << 31)

/* ---- GRSTCTL ------------------------------------------------------------ */
#define OTG_GRSTCTL_CSRST      (1UL << 0) /**< core soft reset       */
#define OTG_GRSTCTL_HSRST      (1UL << 1)
#define OTG_GRSTCTL_FCRST      (1UL << 2)
#define OTG_GRSTCTL_RXFFLSH    (1UL << 4) /**< rx FIFO flush         */
#define OTG_GRSTCTL_TXFFLSH    (1UL << 5) /**< tx FIFO flush         */
#define OTG_GRSTCTL_TXFNUM_POS 6U
#define OTG_GRSTCTL_TXFNUM_MSK (0x1FUL << OTG_GRSTCTL_TXFNUM_POS)
#define OTG_GRSTCTL_AHBIDL     (1UL << 31) /**< AHB master idle      */

#define OTG_TXFNUM_ALL 0x10UL /**< TXFNUM value that flushes every tx FIFO */

/* ---- GINTSTS ------------------------------------------------------------ */
#define OTG_GINTSTS_CMOD                (1UL << 0) /**< 1 = host mode      */
#define OTG_GINTSTS_MMIS                (1UL << 1) /**< mode mismatch      */
#define OTG_GINTSTS_OTGINT              (1UL << 2)
#define OTG_GINTSTS_SOF                 (1UL << 3)
#define OTG_GINTSTS_RXFLVL              (1UL << 4)  /**< rx FIFO non-empty    */
#define OTG_GINTSTS_NPTXFE              (1UL << 5)
#define OTG_GINTSTS_GINAKEFF            (1UL << 6)
#define OTG_GINTSTS_GOUTNAKEFF          (1UL << 7)
#define OTG_GINTSTS_ESUSP               (1UL << 10) /**< early suspend         */
#define OTG_GINTSTS_USBSUSP             (1UL << 11) /**< USB suspend           */
#define OTG_GINTSTS_USBRST              (1UL << 12) /**< USB reset             */
#define OTG_GINTSTS_ENUMDNE             (1UL << 13) /**< enumeration done      */
#define OTG_GINTSTS_ISOODRP             (1UL << 14)
#define OTG_GINTSTS_EOPF                (1UL << 15)
#define OTG_GINTSTS_IEPINT              (1UL << 18) /**< IN endpoint interrupt */
#define OTG_GINTSTS_OEPINT              (1UL << 19) /**< OUT endpoint interrupt*/
#define OTG_GINTSTS_IISOIXFR            (1UL << 20)
#define OTG_GINTSTS_IPXFR_INCOMPISOOUT  (1UL << 21)
#define OTG_GINTSTS_HPRTINT             (1UL << 24)
#define OTG_GINTSTS_HCINT               (1UL << 25)
#define OTG_GINTSTS_PTXFE               (1UL << 26)
#define OTG_GINTSTS_CIDSCHG             (1UL << 28)
#define OTG_GINTSTS_DISCINT             (1UL << 29)
#define OTG_GINTSTS_SRQINT              (1UL << 30)
#define OTG_GINTSTS_WKUPINT             (1UL << 31) /**< resume/remote wakeup  */

/* ---- GINTMSK ------------------------------------------------------------ */
#define OTG_GINTMSK_MMISM             (1UL << 1)
#define OTG_GINTMSK_OTGINT            (1UL << 2)
#define OTG_GINTMSK_SOFM              (1UL << 3)
#define OTG_GINTMSK_RXFLVLM           (1UL << 4)
#define OTG_GINTMSK_NPTXFEM           (1UL << 5)
#define OTG_GINTMSK_GINAKEFFM         (1UL << 6)
#define OTG_GINTMSK_GONAKEFFM         (1UL << 7)
#define OTG_GINTMSK_ESUSPM            (1UL << 10)
#define OTG_GINTMSK_USBSUSPM          (1UL << 11)
#define OTG_GINTMSK_USBRST            (1UL << 12)
#define OTG_GINTMSK_ENUMDNEM          (1UL << 13)
#define OTG_GINTMSK_ISOODRPM          (1UL << 14)
#define OTG_GINTMSK_EOPFM             (1UL << 15)
#define OTG_GINTMSK_EPMISM            (1UL << 17)
#define OTG_GINTMSK_IEPINT            (1UL << 18)
#define OTG_GINTMSK_OEPINT            (1UL << 19)
#define OTG_GINTMSK_IISOIXFRM         (1UL << 20)
#define OTG_GINTMSK_IPXFRM_IISOOXFRM  (1UL << 21)
#define OTG_GINTMSK_PRTIM             (1UL << 24)
#define OTG_GINTMSK_HCIM              (1UL << 25)
#define OTG_GINTMSK_PTXFEM            (1UL << 26)
#define OTG_GINTMSK_CIDSCHGM          (1UL << 28)
#define OTG_GINTMSK_DISCINT           (1UL << 29)
#define OTG_GINTMSK_SRQIM             (1UL << 30)
#define OTG_GINTMSK_WUIM              (1UL << 31)

/* ---- GRXSTSR / GRXSTSP (device mode view) ------------------------------- */
#define OTG_GRXSTSP_EPNUM_POS  0U
#define OTG_GRXSTSP_EPNUM_MSK  (0xFUL << OTG_GRXSTSP_EPNUM_POS)
#define OTG_GRXSTSP_BCNT_POS   4U
#define OTG_GRXSTSP_BCNT_MSK   (0x7FFUL << OTG_GRXSTSP_BCNT_POS)
#define OTG_GRXSTSP_DPID_POS   15U
#define OTG_GRXSTSP_DPID_MSK   (0x3UL << OTG_GRXSTSP_DPID_POS)
#define OTG_GRXSTSP_PKTSTS_POS 17U
#define OTG_GRXSTSP_PKTSTS_MSK (0xFUL << OTG_GRXSTSP_PKTSTS_POS)
#define OTG_GRXSTSP_FRMNUM_POS 21U
#define OTG_GRXSTSP_FRMNUM_MSK (0xFUL << OTG_GRXSTSP_FRMNUM_POS)

/** PKTSTS values in device mode (RM0383 22.15.12). */
#define OTG_PKTSTS_GLOBAL_OUT_NAK 0x1UL
#define OTG_PKTSTS_OUT_DATA       0x2UL /**< BCNT bytes follow in the FIFO   */
#define OTG_PKTSTS_OUT_COMPLETE   0x3UL
#define OTG_PKTSTS_SETUP_COMPLETE 0x4UL
#define OTG_PKTSTS_SETUP_DATA     0x6UL /**< 8-byte SETUP follows            */

/* ---- GRXFSIZ / DIEPTXFx / HNPTXSTS -------------------------------------- */
#define OTG_GRXFSIZ_RXFD_POS 0U
#define OTG_GRXFSIZ_RXFD_MSK (0xFFFFUL << OTG_GRXFSIZ_RXFD_POS) /**< depth in words, 16..256 */

#define OTG_DIEPTXF0_TX0FSA_POS 0U
#define OTG_DIEPTXF0_TX0FSA_MSK (0xFFFFUL << OTG_DIEPTXF0_TX0FSA_POS) /**< start address, words */
#define OTG_DIEPTXF0_TX0FD_POS  16U
#define OTG_DIEPTXF0_TX0FD_MSK  (0xFFFFUL << OTG_DIEPTXF0_TX0FD_POS) /**< depth, words, 16..256 */

#define OTG_DIEPTXF_INEPTXSA_POS 0U
#define OTG_DIEPTXF_INEPTXSA_MSK (0xFFFFUL << OTG_DIEPTXF_INEPTXSA_POS)
#define OTG_DIEPTXF_INEPTXFD_POS 16U
#define OTG_DIEPTXF_INEPTXFD_MSK (0xFFFFUL << OTG_DIEPTXF_INEPTXFD_POS)

#define OTG_HNPTXSTS_NPTXFSAV_POS 0U
#define OTG_HNPTXSTS_NPTXFSAV_MSK (0xFFFFUL << OTG_HNPTXSTS_NPTXFSAV_POS)
#define OTG_HNPTXSTS_NPTQXSAV_POS 16U
#define OTG_HNPTXSTS_NPTQXSAV_MSK (0xFFUL << OTG_HNPTXSTS_NPTQXSAV_POS)
#define OTG_HNPTXSTS_NPTXQTOP_POS 24U
#define OTG_HNPTXSTS_NPTXQTOP_MSK (0x7FUL << OTG_HNPTXSTS_NPTXQTOP_POS)

/* ---- GCCFG -------------------------------------------------------------- */
#define OTG_GCCFG_PWRDWN     (1UL << 16) /**< 1 = transceiver powered up         */
#define OTG_GCCFG_VBUSASEN   (1UL << 18)
#define OTG_GCCFG_VBUSBSEN   (1UL << 19) /**< enable VBUS sensing on PA9 ("B")   */
#define OTG_GCCFG_SOFOUTEN   (1UL << 20)
#define OTG_GCCFG_NOVBUSSENS (1UL << 21) /**< 1 = VBUS assumed present, PA9 free */

/* ---- HCFG / HFIR / HFNUM / HPTXSTS / HAINT / HPRT ----------------------- */
#define OTG_HCFG_FSLSPCS_POS 0U
#define OTG_HCFG_FSLSPCS_MSK (0x3UL << OTG_HCFG_FSLSPCS_POS)
#define OTG_HCFG_FSLSS       (1UL << 2)

#define OTG_HFIR_FRIVL_POS 0U
#define OTG_HFIR_FRIVL_MSK (0xFFFFUL << OTG_HFIR_FRIVL_POS)

#define OTG_HFNUM_FRNUM_POS 0U
#define OTG_HFNUM_FRNUM_MSK (0xFFFFUL << OTG_HFNUM_FRNUM_POS)
#define OTG_HFNUM_FTREM_POS 16U
#define OTG_HFNUM_FTREM_MSK (0xFFFFUL << OTG_HFNUM_FTREM_POS)

#define OTG_HPTXSTS_PTXFSAVL_POS 0U
#define OTG_HPTXSTS_PTXFSAVL_MSK (0xFFFFUL << OTG_HPTXSTS_PTXFSAVL_POS)
#define OTG_HPTXSTS_PTXQSAV_POS  16U
#define OTG_HPTXSTS_PTXQSAV_MSK  (0xFFUL << OTG_HPTXSTS_PTXQSAV_POS)
#define OTG_HPTXSTS_PTXQTOP_POS  24U
#define OTG_HPTXSTS_PTXQTOP_MSK  (0xFFUL << OTG_HPTXSTS_PTXQTOP_POS)

#define OTG_HAINT_HAINT_POS     0U
#define OTG_HAINT_HAINT_MSK     (0xFFFFUL << OTG_HAINT_HAINT_POS)
#define OTG_HAINTMSK_HAINTM_POS 0U
#define OTG_HAINTMSK_HAINTM_MSK (0xFFFFUL << OTG_HAINTMSK_HAINTM_POS)

#define OTG_HPRT_PCSTS     (1UL << 0)
#define OTG_HPRT_PCDET     (1UL << 1)
#define OTG_HPRT_PENA      (1UL << 2)
#define OTG_HPRT_PENCHNG   (1UL << 3)
#define OTG_HPRT_POCA      (1UL << 4)
#define OTG_HPRT_POCCHNG   (1UL << 5)
#define OTG_HPRT_PRES      (1UL << 6)
#define OTG_HPRT_PSUSP     (1UL << 7)
#define OTG_HPRT_PRST      (1UL << 8)
#define OTG_HPRT_PLSTS_POS 10U
#define OTG_HPRT_PLSTS_MSK (0x3UL << OTG_HPRT_PLSTS_POS)
#define OTG_HPRT_PPWR      (1UL << 12)
#define OTG_HPRT_PTCTL_POS 13U
#define OTG_HPRT_PTCTL_MSK (0xFUL << OTG_HPRT_PTCTL_POS)
#define OTG_HPRT_PSPD_POS  17U
#define OTG_HPRT_PSPD_MSK  (0x3UL << OTG_HPRT_PSPD_POS)

/* ---- HCCHARx / HCINTx / HCINTMSKx / HCTSIZx ----------------------------- */
#define OTG_HCCHAR_MPSIZ_POS 0U
#define OTG_HCCHAR_MPSIZ_MSK (0x7FFUL << OTG_HCCHAR_MPSIZ_POS)
#define OTG_HCCHAR_EPNUM_POS 11U
#define OTG_HCCHAR_EPNUM_MSK (0xFUL << OTG_HCCHAR_EPNUM_POS)
#define OTG_HCCHAR_EPDIR     (1UL << 15)
#define OTG_HCCHAR_LSDEV     (1UL << 17)
#define OTG_HCCHAR_EPTYP_POS 18U
#define OTG_HCCHAR_EPTYP_MSK (0x3UL << OTG_HCCHAR_EPTYP_POS)
#define OTG_HCCHAR_MCNT_POS  20U
#define OTG_HCCHAR_MCNT_MSK  (0x3UL << OTG_HCCHAR_MCNT_POS)
#define OTG_HCCHAR_DAD_POS   22U
#define OTG_HCCHAR_DAD_MSK   (0x7FUL << OTG_HCCHAR_DAD_POS)
#define OTG_HCCHAR_ODDFRM    (1UL << 29)
#define OTG_HCCHAR_CHDIS     (1UL << 30)
#define OTG_HCCHAR_CHENA     (1UL << 31)

#define OTG_HCINT_XFRC  (1UL << 0)
#define OTG_HCINT_CHH   (1UL << 1)
#define OTG_HCINT_STALL (1UL << 3)
#define OTG_HCINT_NAK   (1UL << 4)
#define OTG_HCINT_ACK   (1UL << 5)
#define OTG_HCINT_TXERR (1UL << 7)
#define OTG_HCINT_BBERR (1UL << 8)
#define OTG_HCINT_FRMOR (1UL << 9)
#define OTG_HCINT_DTERR (1UL << 10)

#define OTG_HCINTMSK_XFRCM  (1UL << 0)
#define OTG_HCINTMSK_CHHM   (1UL << 1)
#define OTG_HCINTMSK_STALLM (1UL << 3)
#define OTG_HCINTMSK_NAKM   (1UL << 4)
#define OTG_HCINTMSK_ACKM   (1UL << 5)
#define OTG_HCINTMSK_NYET   (1UL << 6)
#define OTG_HCINTMSK_TXERRM (1UL << 7)
#define OTG_HCINTMSK_BBERRM (1UL << 8)
#define OTG_HCINTMSK_FRMORM (1UL << 9)
#define OTG_HCINTMSK_DTERRM (1UL << 10)

#define OTG_HCTSIZ_XFRSIZ_POS 0U
#define OTG_HCTSIZ_XFRSIZ_MSK (0x7FFFFUL << OTG_HCTSIZ_XFRSIZ_POS)
#define OTG_HCTSIZ_PKTCNT_POS 19U
#define OTG_HCTSIZ_PKTCNT_MSK (0x3FFUL << OTG_HCTSIZ_PKTCNT_POS)
#define OTG_HCTSIZ_DPID_POS   29U
#define OTG_HCTSIZ_DPID_MSK   (0x3UL << OTG_HCTSIZ_DPID_POS)

/* ---- DCFG --------------------------------------------------------------- */
#define OTG_DCFG_DSPD_POS  0U
#define OTG_DCFG_DSPD_MSK  (0x3UL << OTG_DCFG_DSPD_POS)
#define OTG_DCFG_NZLSOHSK  (1UL << 2) /**< STALL a non-zero-length status OUT */
#define OTG_DCFG_DAD_POS   4U
#define OTG_DCFG_DAD_MSK   (0x7FUL << OTG_DCFG_DAD_POS) /**< device address */
#define OTG_DCFG_PFIVL_POS 11U
#define OTG_DCFG_PFIVL_MSK (0x3UL << OTG_DCFG_PFIVL_POS)

#define OTG_DSPD_FULL_SPEED 0x3UL /**< the only legal DSPD on this core */

/* ---- DCTL --------------------------------------------------------------- */
#define OTG_DCTL_RWUSIG   (1UL << 0)  /**< remote wakeup signalling      */
#define OTG_DCTL_SDIS     (1UL << 1)  /**< soft disconnect (D+ pull-up off) */
#define OTG_DCTL_GINSTS   (1UL << 2)
#define OTG_DCTL_GONSTS   (1UL << 3)
#define OTG_DCTL_TCTL_POS 4U
#define OTG_DCTL_TCTL_MSK (0x7UL << OTG_DCTL_TCTL_POS)
#define OTG_DCTL_SGINAK   (1UL << 7)
#define OTG_DCTL_CGINAK   (1UL << 8)
#define OTG_DCTL_SGONAK   (1UL << 9)
#define OTG_DCTL_CGONAK   (1UL << 10)
#define OTG_DCTL_POPRGDNE (1UL << 11)

/* ---- DSTS --------------------------------------------------------------- */
#define OTG_DSTS_SUSPSTS     (1UL << 0)
#define OTG_DSTS_ENUMSPD_POS 1U
#define OTG_DSTS_ENUMSPD_MSK (0x3UL << OTG_DSTS_ENUMSPD_POS) /**< 3 = full speed */
#define OTG_DSTS_EERR        (1UL << 3)
#define OTG_DSTS_FNSOF_POS   8U
#define OTG_DSTS_FNSOF_MSK   (0x3FFFUL << OTG_DSTS_FNSOF_POS)

/* ---- DIEPMSK / DOEPMSK -------------------------------------------------- */
#define OTG_DIEPMSK_XFRCM     (1UL << 0) /**< transfer completed         */
#define OTG_DIEPMSK_EPDM      (1UL << 1) /**< endpoint disabled          */
#define OTG_DIEPMSK_TOM       (1UL << 3) /**< timeout (control IN)       */
#define OTG_DIEPMSK_ITTXFEMSK (1UL << 4) /**< IN token with tx FIFO empty*/
#define OTG_DIEPMSK_INEPNMM   (1UL << 5)
#define OTG_DIEPMSK_INEPNEM   (1UL << 6)

#define OTG_DOEPMSK_XFRCM  (1UL << 0)
#define OTG_DOEPMSK_EPDM   (1UL << 1)
#define OTG_DOEPMSK_STUPM  (1UL << 3) /**< SETUP phase done            */
#define OTG_DOEPMSK_OTEPDM (1UL << 4) /**< OUT token when EP disabled  */

/* ---- DAINT / DAINTMSK --------------------------------------------------- */
#define OTG_DAINT_IEPINT_POS 0U
#define OTG_DAINT_IEPINT_MSK (0xFFFFUL << OTG_DAINT_IEPINT_POS)
#define OTG_DAINT_OEPINT_POS 16U
#define OTG_DAINT_OEPINT_MSK (0xFFFFUL << OTG_DAINT_OEPINT_POS)

#define OTG_DAINTMSK_IEPM_POS   0U
#define OTG_DAINTMSK_IEPM_MSK   (0xFFFFUL << OTG_DAINTMSK_IEPM_POS)
#define OTG_DAINTMSK_OEPINT_POS 16U
#define OTG_DAINTMSK_OEPINT_MSK (0xFFFFUL << OTG_DAINTMSK_OEPINT_POS)

/* ---- DVBUSDIS / DVBUSPULSE / DIEPEMPMSK --------------------------------- */
#define OTG_DVBUSDIS_VBUSDT_POS     0U
#define OTG_DVBUSDIS_VBUSDT_MSK     (0xFFFFUL << OTG_DVBUSDIS_VBUSDT_POS)
#define OTG_DVBUSPULSE_DVBUSP_POS   0U
#define OTG_DVBUSPULSE_DVBUSP_MSK   (0xFFFUL << OTG_DVBUSPULSE_DVBUSP_POS)
#define OTG_DIEPEMPMSK_INEPTXFEM_POS 0U
#define OTG_DIEPEMPMSK_INEPTXFEM_MSK (0xFFFFUL << OTG_DIEPEMPMSK_INEPTXFEM_POS)

/* ---- DIEPCTLx ----------------------------------------------------------- */
#define OTG_DIEPCTL_MPSIZ_POS      0U
#define OTG_DIEPCTL_MPSIZ_MSK      (0x7FFUL << OTG_DIEPCTL_MPSIZ_POS) /**< EP1..3: bytes    */
#define OTG_DIEPCTL0_MPSIZ_MSK     (0x3UL << OTG_DIEPCTL_MPSIZ_POS)   /**< EP0: encoded     */
#define OTG_DIEPCTL_USBAEP         (1UL << 15) /**< endpoint active                          */
#define OTG_DIEPCTL_EONUM_DPID     (1UL << 16)
#define OTG_DIEPCTL_NAKSTS         (1UL << 17)
#define OTG_DIEPCTL_EPTYP_POS      18U
#define OTG_DIEPCTL_EPTYP_MSK      (0x3UL << OTG_DIEPCTL_EPTYP_POS)
#define OTG_DIEPCTL_STALL          (1UL << 21)
#define OTG_DIEPCTL_TXFNUM_POS     22U
#define OTG_DIEPCTL_TXFNUM_MSK     (0xFUL << OTG_DIEPCTL_TXFNUM_POS)
#define OTG_DIEPCTL_CNAK           (1UL << 26)
#define OTG_DIEPCTL_SNAK           (1UL << 27)
#define OTG_DIEPCTL_SD0PID_SEVNFRM (1UL << 28)
#define OTG_DIEPCTL_SODDFRM        (1UL << 29)
#define OTG_DIEPCTL_EPDIS          (1UL << 30)
#define OTG_DIEPCTL_EPENA          (1UL << 31)

/** EP0 MPSIZ encodings. */
#define OTG_EP0_MPSIZ_64 0x0UL
#define OTG_EP0_MPSIZ_32 0x1UL
#define OTG_EP0_MPSIZ_16 0x2UL
#define OTG_EP0_MPSIZ_8  0x3UL

/** EPTYP values, shared by IN and OUT. */
#define OTG_EPTYP_CONTROL     0x0UL
#define OTG_EPTYP_ISOCHRONOUS 0x1UL
#define OTG_EPTYP_BULK        0x2UL
#define OTG_EPTYP_INTERRUPT   0x3UL

/* ---- DOEPCTLx ----------------------------------------------------------- */
#define OTG_DOEPCTL_MPSIZ_POS      0U
#define OTG_DOEPCTL_MPSIZ_MSK      (0x7FFUL << OTG_DOEPCTL_MPSIZ_POS)
#define OTG_DOEPCTL0_MPSIZ_MSK     (0x3UL << OTG_DOEPCTL_MPSIZ_POS)
#define OTG_DOEPCTL_USBAEP         (1UL << 15)
#define OTG_DOEPCTL_EONUM_DPID     (1UL << 16)
#define OTG_DOEPCTL_NAKSTS         (1UL << 17)
#define OTG_DOEPCTL_EPTYP_POS      18U
#define OTG_DOEPCTL_EPTYP_MSK      (0x3UL << OTG_DOEPCTL_EPTYP_POS)
#define OTG_DOEPCTL_SNPM           (1UL << 20)
#define OTG_DOEPCTL_STALL          (1UL << 21)
#define OTG_DOEPCTL_CNAK           (1UL << 26)
#define OTG_DOEPCTL_SNAK           (1UL << 27)
#define OTG_DOEPCTL_SD0PID_SEVNFRM (1UL << 28)
#define OTG_DOEPCTL_SODDFRM        (1UL << 29)
#define OTG_DOEPCTL_EPDIS          (1UL << 30)
#define OTG_DOEPCTL_EPENA          (1UL << 31)

/* ---- DIEPINTx / DOEPINTx ------------------------------------------------ */
#define OTG_DIEPINT_XFRC   (1UL << 0)
#define OTG_DIEPINT_EPDISD (1UL << 1)
#define OTG_DIEPINT_TOC    (1UL << 3)
#define OTG_DIEPINT_ITTXFE (1UL << 4)
#define OTG_DIEPINT_INEPNE (1UL << 6)
#define OTG_DIEPINT_TXFE   (1UL << 7)

#define OTG_DOEPINT_XFRC    (1UL << 0)
#define OTG_DOEPINT_EPDISD  (1UL << 1)
#define OTG_DOEPINT_STUP    (1UL << 3)
#define OTG_DOEPINT_OTEPDIS (1UL << 4)
#define OTG_DOEPINT_B2BSTUP (1UL << 6)

/* ---- DIEPTSIZx / DOEPTSIZx ---------------------------------------------- */
#define OTG_DIEPTSIZ_XFRSIZ_POS  0U
#define OTG_DIEPTSIZ_XFRSIZ_MSK  (0x7FFFFUL << OTG_DIEPTSIZ_XFRSIZ_POS) /**< EP1..3 */
#define OTG_DIEPTSIZ0_XFRSIZ_MSK (0x7FUL << OTG_DIEPTSIZ_XFRSIZ_POS)    /**< EP0    */
#define OTG_DIEPTSIZ_PKTCNT_POS  19U
#define OTG_DIEPTSIZ_PKTCNT_MSK  (0x3FFUL << OTG_DIEPTSIZ_PKTCNT_POS)   /**< EP1..3 */
#define OTG_DIEPTSIZ0_PKTCNT_MSK (0x3UL << OTG_DIEPTSIZ_PKTCNT_POS)     /**< EP0    */
#define OTG_DIEPTSIZ_MCNT_POS    29U
#define OTG_DIEPTSIZ_MCNT_MSK    (0x3UL << OTG_DIEPTSIZ_MCNT_POS)

#define OTG_DOEPTSIZ_XFRSIZ_POS         0U
#define OTG_DOEPTSIZ_XFRSIZ_MSK         (0x7FFFFUL << OTG_DOEPTSIZ_XFRSIZ_POS)
#define OTG_DOEPTSIZ0_XFRSIZ_MSK        (0x7FUL << OTG_DOEPTSIZ_XFRSIZ_POS)
#define OTG_DOEPTSIZ_PKTCNT_POS         19U
#define OTG_DOEPTSIZ_PKTCNT_MSK         (0x3FFUL << OTG_DOEPTSIZ_PKTCNT_POS)
#define OTG_DOEPTSIZ0_PKTCNT_MSK        (0x1UL << OTG_DOEPTSIZ_PKTCNT_POS)
#define OTG_DOEPTSIZ_RXDPID_STUPCNT_POS 29U
#define OTG_DOEPTSIZ_RXDPID_STUPCNT_MSK (0x3UL << OTG_DOEPTSIZ_RXDPID_STUPCNT_POS)

/* ---- DTXFSTSx ----------------------------------------------------------- */
#define OTG_DTXFSTS_INEPTFSAV_POS 0U
#define OTG_DTXFSTS_INEPTFSAV_MSK (0xFFFFUL << OTG_DTXFSTS_INEPTFSAV_POS) /**< free words */

/* ---- PCGCCTL ------------------------------------------------------------ */
#define OTG_PCGCCTL_STPPCLK  (1UL << 0) /**< stop PHY clock      */
#define OTG_PCGCCTL_GATEHCLK (1UL << 1) /**< gate HCLK           */
#define OTG_PCGCCTL_PHYSUSP  (1UL << 4) /**< PHY suspended       */

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_REGS_OTG_FS_H */
