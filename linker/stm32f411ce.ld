/*
 * Linker script for STM32F411CEU6 (WeAct BlackPill).
 *
 *   512 KB flash @ 0x08000000
 *   128 KB SRAM  @ 0x20000000
 *
 * Written for this project; no vendor script involved. Symbol names match
 * Bsp/Src/device/startup_stm32f411.c and the newlib _sbrk in Core/Src/sysmem.c.
 */

ENTRY(reset_handler)

_Min_Heap_Size  = 0x200;  /* 512 B  */
_Min_Stack_Size = 0x400;  /* 1 KB   */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Stack grows down from the top of RAM. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/*
 * Explicit program headers. Without these the linker derives segment
 * permissions from the input sections and ends up marking the flash segment
 * read/write/execute (because .init_array is writable), which newer binutils
 * warns about.
 */
PHDRS
{
  text PT_LOAD FLAGS(5); /* read + execute */
  data PT_LOAD FLAGS(6); /* read + write   */
  bss  PT_LOAD FLAGS(6); /* read + write   */
}

SECTIONS
{
  /* Vector table must be the first thing in flash. */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH :text

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH :text

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH :text

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH :text
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH :text

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH :text

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH :text

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH :text

  /* Initialiser image for .data, stored in flash and copied to RAM at reset. */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH :data

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;

    /*
     * Reserve heap and stack here so the build fails at link time rather than
     * overflowing RAM at run time. _end is where _sbrk starts handing memory
     * to malloc; _Min_Stack_Size is subtracted from _estack as its ceiling.
     */
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM :bss

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
