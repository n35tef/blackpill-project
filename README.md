# blackpill-project

A base project for the **WeAct BlackPill v3.x** (STM32F411CEU6) that contains
**no vendor code at all** — no CMSIS, no ST HAL, no CubeMX, no CubeIDE. The ARM
Cortex-M4 core definitions, the STM32F411 register map, the startup code, the
vector table and the linker script are all written from scratch in this
repository.

Peripherals are turned on and off from a single file, `Bsp/Inc/bsp_config.h`.
Anything switched off compiles to nothing: no code, no interrupt handlers, no
RAM.

## Why

CubeMX regenerates code, fights version control, and assumes an IDE. This
project exists to get the same job done from a plain Linux shell with nothing
but `arm-none-eabi-gcc`, `cmake`, `ninja` and `openocd` — all packaged by every
distribution, all scriptable, all diffable.

## Requirements

```bash
# Fedora
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-newlib cmake ninja-build openocd

# Debian / Ubuntu
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build openocd
```

For flashing, any one of `openocd`, `st-flash` (stlink-tools) or `probe-rs`
will do; the build picks whichever it finds.

## Build

```bash
cmake --preset Debug          # or Release
cmake --build build/Debug
```

You get `blackpill-project.elf`, `.hex` and `.bin`, plus a memory usage report.
`compile_commands.json` is written automatically, so `clangd` works with no
extra setup.

## Flash and debug

```bash
cmake --build build/Debug --target flash        # program and reset

cmake --build build/Debug --target gdb-server   # terminal 1
arm-none-eabi-gdb build/Debug/blackpill-project.elf \
    -ex "target extended-remote :3333"          # terminal 2
```

The BlackPill can also be programmed over USB DFU by holding **BOOT0**, tapping
**NRST**, then releasing BOOT0:

```bash
dfu-util -a 0 -s 0x08000000:leave -D build/Debug/blackpill-project.bin
```

## Configuring the board

Everything lives in `Bsp/Inc/bsp_config.h`.

### Clocks

The default is the 25 MHz crystal through the PLL to **96 MHz**, with the PLLQ
output landing on exactly 48 MHz for USB full speed:

```c
#define BSP_CLOCK_SOURCE BSP_CLOCK_SOURCE_HSE_PLL
#define BSP_HSE_FREQ_HZ  25000000UL
#define BSP_PLL_M 25U
#define BSP_PLL_N 192U
#define BSP_PLL_P 2U
#define BSP_PLL_Q 4U
```

100 MHz is reachable, but then PLLQ can no longer produce 48 MHz and USB stops
working — pick one. Flash wait states and the voltage scale are derived from
the resulting frequency automatically.

Every constraint is checked at **compile time**. A VCO input outside 1–2 MHz, an
APB1 domain over 50 MHz, or an impossible prescaler is a build error with a
readable message, not a board that mysteriously fails to boot.

The resulting frequencies are exported as compile-time constants you can use in
your own code:

```c
BSP_SYSCLK_HZ  BSP_HCLK_HZ  BSP_PCLK1_HZ  BSP_PCLK2_HZ
BSP_TIMER_PCLK1_HZ  BSP_TIMER_PCLK2_HZ  BSP_PLL_Q_OUT_HZ
```

### Peripherals

```c
#define BSP_USE_SPI2        1
#define BSP_USE_USART1      1
#define BSP_USE_DMA1        1
```

That is the whole procedure. `bsp_init()` brings up everything enabled, in the
right order, and the modules you left off are not compiled.

| Switch | Module | What you get |
|---|---|---|
| `BSP_USE_SYSTICK` | `bsp_systick` | `bsp_tick()`, `bsp_delay_ms()`, `bsp_delay_us()` |
| `BSP_USE_LED` / `BSP_USE_BUTTON` | `bsp_gpio` | `bsp_led_toggle()`, `bsp_button_pressed()` |
| `BSP_USE_DMA1` / `BSP_USE_DMA2` | `bsp_dma` | stream config, start/stop, completion polling |
| `BSP_USE_SPI1..5` | `bsp_spi` | master mode, blocking transfers, optional MISO |
| `BSP_USE_USART1/2/6` | `bsp_usart` | 8N1, blocking TX/RX, `printf` retargeting |
| `BSP_USE_I2C1..3` | `bsp_i2c` | blocking master, register read/write, bus scan |
| `BSP_USE_TIM1..5, 9..11` | `bsp_tim` | time bases, update interrupts, PWM |
| `BSP_USE_ADC1` | `bsp_adc` | single conversions, temperature sensor, VREF/VBAT |
| `BSP_USE_EXTI` | `bsp_exti` | pin interrupts with per-line callbacks |
| `BSP_USE_RTC` | `bsp_misc` | calendar on LSE or LSI |
| `BSP_USE_IWDG` / `BSP_USE_WWDG` | `bsp_misc` | watchdogs |
| `BSP_USE_CRC` | `bsp_misc` | hardware CRC-32 |

### Pins

Every bus takes its pins from `bsp_config.h` as a port / pin / alternate
function triplet, so moving a peripheral never means editing driver code:

```c
#define BSP_SPI2_SCK_PORT   GPIOB
#define BSP_SPI2_SCK_PIN    13U
#define BSP_SPI2_SCK_AF     5U
```

The comment above each block lists **every** pin the UFQFPN48 package actually
bonds out for that peripheral, which is a smaller set than the datasheet's
full-package tables suggest. Watch out for the traps this package creates:

- **I2C2_SDA is not PB11** — that pin is not bonded. Use PB3 or PB9, and note
  they need **AF9**, not AF4.
- **I2C3_SDA** (PB4/PB8) is also **AF9**.
- **SPI4 mixes alternate functions**: SCK/MISO/NSS are AF6 but MOSI on PA1 is
  AF5.
- **SPI3_SCK on PB12 is AF7**, while the rest of SPI3 is AF6.
- **ADC channels 10–15 do not exist here** — they live on PC0–PC5, which are not
  bonded. Only IN0–IN9 (PA0–PA7, PB0–PB1) are usable.
- **The temperature sensor is on ADC channel 18**, shared with VBAT, not on
  channel 16 as it is on the F405/F407.
- **USART6 and SPI4/SPI5 collide with USB**, all of them wanting PA11/PA12.

## Printing

Enable a USART, point stdout at it, and `printf` works:

```c
#define BSP_USE_USART1   1
#define BSP_STDOUT_USART 1
```

The newlib stubs in `Bsp/Src/bsp_syscalls.c` route output there; `\n` is
expanded to `\r\n`. With `BSP_STDOUT_USART 0` the stubs swallow output and cost
nothing.

## Example

```c
#include "bsp.h"

int main(void)
{
    if (bsp_init() != BSP_OK)
    {
        bsp_error_handler();
    }

    while (1)
    {
        bsp_led_toggle();
        bsp_delay_ms(500);
    }
}
```

## Layout

```
Bsp/Inc/bsp_config.h          the only file you normally edit
Bsp/Inc/cpu/cortex_m4.h       NVIC, SCB, SysTick, barriers, intrinsics
Bsp/Inc/device/
    stm32f411.h               master device header
    stm32f411_irq.h           interrupt numbers
    stm32f411_memmap.h        bus and memory bases
    regs/*.h                  per-peripheral register structs and bit masks
Bsp/Src/device/
    startup_stm32f411.c       reset handler and vector table, in C
    stm32f411_reg_check.c     compile-time verification of the register map
Bsp/Inc, Bsp/Src              the bsp_* peripheral modules
Core/Src/main.c               your application
linker/stm32f411ce.ld         512 KB flash, 128 KB RAM
cmake/                        toolchain file and MCU library
```

## How the register map keeps itself honest

A hand-written register map is only useful if a mistake is caught at build time
rather than by a board that behaves strangely. `stm32f411_reg_check.c` asserts
the offset and size of every field of every register block, plus every
peripheral base address:

```c
CHECK_OFFSET(rcc_regs_t, CFGR, 0x08);
CHECK_SIZE(spi_regs_t, 0x24);
_Static_assert(SPI2_BASE == 0x40003800UL, "SPI2 base address");
```

These are `_Static_assert`s, so a wrong offset fails the compile. Nothing ships
unverified.

That only proves the map is self-consistent, though. Two further checks compare
it and the drivers against the outside world, neither of which needs a board:

```sh
./tools/verify.sh
```

- **`tools/svdcheck`** compares every base address, register offset and bit
  field against ST's published CMSIS-SVD description of the STM32F411 - an
  independent statement of the same facts, generated from the same database as
  ST's own headers. 38 base addresses, 177 registers and 375 bit fields
  currently agree.
- **`tools/hostsim`** maps the peripheral region into a host process at its
  real addresses and runs the unmodified drivers against it, with a thread
  playing the part of the silicon. Every register access is trapped and checked
  against RM0383 sequencing rules (clock gating, flash latency before a clock
  switch, no reconfiguring an enabled SPI/DMA, ADC settling time, the I2C
  ADDR-clearing read sequence). 127 checks confirm both the bits the drivers
  write and the order they write them in, with expected values recomputed from
  RM0383 rather than from the driver's own macros.

Both tools, what they cover, and - importantly - what they cannot tell you are
documented in [tools/README.md](tools/README.md). The short version:
configuration and register sequencing are covered, the rules are one reading of
RM0383 with no external oracle, and wire-level timing is unproven until it has
run on the board.

## Adding a peripheral

1. Add the register block to `Bsp/Inc/device/regs/` and include it from
   `stm32f411.h`.
2. Add the matching `CHECK_OFFSET` / `CHECK_SIZE` lines to
   `stm32f411_reg_check.c`.
3. Write `Bsp/Inc/bsp_x.h` and `Bsp/Src/bsp_x.c`, wrapping the whole body in
   `#if BSP_USE_X`.
4. Add `BSP_USE_X` to `bsp_config.h` and a call to `bsp_x_init()` in
   `bsp_init()`.
5. Run `./tools/verify.sh` - the SVD check picks the new registers up on its
   own, and adding a few assertions to `tools/hostsim/sim.c` covers the driver.

The CMake build globs `Bsp/Src`, so there is no build file to edit. Re-run
`cmake --preset Debug` to pick up the new file.

## Licence

No vendor code is included or derived from, so this repository carries no
third-party licence obligations — pick whatever licence you like for your own
work and drop a `LICENSE` file in at the root.
