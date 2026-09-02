# blackpill-project

A minimal, hand-written base project for the WeAct BlackPill (STM32F411CEU6)
board, meant to be reused for new projects instead of re-generating
boilerplate from STM32CubeMX (or an AI tool) every time.

## Idea

* Peripherals are turned on/off in a **single file**: `Bsp/Inc/bsp_config.h`.
* Each peripheral lives in its own self-contained module under `Bsp/`
  (`bsp_xxx.h` / `bsp_xxx.c`). Its init code, MSP callbacks and IRQ handlers
  all live together and are wrapped in `#if BSP_USE_XXX`, so flipping the
  flag to `0` removes that peripheral entirely from the build - no other
  file, and no CMakeLists.txt, needs to change.
* `Core/` only contains the minimum every project needs: the fault/system
  interrupt handlers, `system_stm32f4xx.c`, syscalls/sysmem for newlib, and
  `main.c`, which you're expected to grow into your actual application.
* `Drivers/` contains the (unmodified) ST CMSIS device headers and a trimmed
  STM32F4xx HAL driver - just the modules currently used.

## Layout

```
Bsp/Inc/bsp_config.h   <- edit this to enable/disable peripherals
Bsp/Inc, Bsp/Src        one bsp_xxx.h/.c pair per peripheral
Core/Inc, Core/Src      fault handlers, startup glue, main.c
Drivers/CMSIS           ARM CMSIS + ST device headers (vendor, unmodified)
Drivers/STM32F4xx_HAL_Driver  ST HAL driver (vendor, trimmed to used modules)
cmake/mcu/CMakeLists.txt     builds Drivers + Core + Bsp into libblackpill-mcu
cmake/gcc-arm-none-eabi.cmake  arm-none-eabi-gcc toolchain file
startup_stm32f411xe.s, STM32F411XX_FLASH.ld  startup code / linker script
```

## Currently available peripherals

| Flag                 | Module          | Notes                                   |
|----------------------|-----------------|------------------------------------------|
| `BSP_USE_LED`         | `bsp_gpio`      | Onboard LED, default PC13 (active low)   |
| `BSP_USE_BUTTON`      | `bsp_gpio`      | Onboard/user button, default PA0         |
| `BSP_USE_SPI2`        | `bsp_spi2`      | SPI2 master: PB13/PB14/PB15 (SCK/MISO/MOSI) |
| `BSP_USE_SPI2_DMA`    | `bsp_spi2`      | DMA1 Stream3 (RX) / Stream4 (TX) for SPI2 |

The clock config (`bsp_clock`, 100 MHz from the internal HSI through the
main PLL) always runs - every project needs it.

## Adding a new peripheral

1. Add a `BSP_USE_xxx` flag (and any parameters) to `Bsp/Inc/bsp_config.h`.
2. Create `Bsp/Inc/bsp_xxx.h` + `Bsp/Src/bsp_xxx.c`. Wrap the whole `.c` file
   body in `#if BSP_USE_XXX ... #endif` so disabling the flag removes it
   completely, including any IRQ handlers it defines.
3. Call `bsp_xxx_init()` from `bsp_init()` in `Bsp/Src/bsp.c` (guarded by the
   same flag).

No CMakeLists.txt changes are required: `cmake/mcu/CMakeLists.txt` globs
every file under `Bsp/Src/*.c`.

## Building

```sh
cmake --preset Debug      # or Release
cmake --build --preset Debug
```

Requires `arm-none-eabi-gcc`, `cmake` and `ninja`.
