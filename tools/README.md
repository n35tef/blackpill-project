# Verification tooling

The BSP is written from scratch against RM0383, so the obvious question is
whether the numbers in it are right. These two tools answer as much of that as
can be answered without a board on the desk.

```sh
./tools/verify.sh          # runs both
```

Neither tool needs hardware, a debugger, or any ST software.

## 1. `svdcheck` - is the register map right?

Compares every base address, register offset and bit field in
`Bsp/Inc/device/` against ST's published CMSIS-SVD description of the
STM32F411. The SVD is generated from the same internal database as ST's own
headers and the reference manual, so it is an independent statement of the
same facts.

`extract_bsp.py` scans the headers for *names* only and emits a C program that
prints every value using `offsetof` and the macros themselves. The host
compiler therefore produces the numbers, not a regex - if the scan is wrong the
program fails to compile instead of quietly comparing the wrong thing.

Current result: **38 base addresses, 177 registers and 374 bit fields agree**.

Not everything is comparable. Roughly a hundred macros are encodings rather
than register fields (`SPI_BR_DIV8`, `GPIO_AF5_SPI1_2_3_4_5`, `ADC_SMP_480CYCLES`
and so on) and have no SVD counterpart by definition. A handful of registers
exist on the F411 but are missing from this SVD revision - `RCC_DCKCFGR`,
`I2C_FLTR`, ADC common `CDR`, `RCC_APB2ENR.SPI5EN`, `RCC_BDCR.LSEMOD`,
`PWR_CR.LPLVDS/MRLVDS`. Those are listed separately by the tool rather than
silently skipped, and were confirmed against ST's own device header.

The check is only as good as the SVD, so a disagreement means a human reads
both sources before either changes. That has happened once so far:

> The tool flagged `FLASH_ACR_LATENCY_MSK` as `0xF` against the SVD's `0x7`.
> ST's own F411 header agrees with the SVD (3 bits, stopping at 7 wait
> states); the 16-state form belongs to the F42x/F43x/F446/F469 parts, while
> RM0383's prose prints `LATENCY[3:0]`. The mask was narrowed to `0x7`. It
> makes no practical difference - the F411 tops out at 100 MHz, which is 3
> wait states - but the narrower mask avoids writing a bit ST treats as
> reserved on this part.

The same review established that ADC common `CDR` is a dual/triple-ADC result
register with no function on the single-ADC F411, so the word is now marked as
merely reserved.

## 2. `hostsim` - do the drivers write the right bits?

Maps the STM32 peripheral region into a host process at its real addresses, so
`RCC->CFGR = x` inside a driver writes into ordinary memory that the test can
inspect. A background thread plays the part of the silicon, servicing the
handshakes the drivers block on - oscillator ready flags, the clock switch
status, peripheral status bits.

The drivers are compiled unmodified and for real. There are no mocks and
nothing is reimplemented, so what the tests observe is what the firmware would
write to the peripheral bus. Expected values are recalculated in the test from
RM0383 with plain arithmetic rather than by reusing the driver's own macros,
so a wrong formula cannot agree with itself.

Current result: **86 checks covering RCC/PLL/flash latency, GPIO, SPI, USART,
ADC, TIM, DMA and I2C**.

### Does it actually catch anything?

A test suite that has never failed proves nothing, so the drivers were broken
on purpose one change at a time. Eleven mutations, ten caught:

| Injected bug | Result |
| --- | --- |
| SPI baud divider loop off by one | caught |
| USART BRR loses its rounding term | caught |
| ADC internal channels lose the 480-cycle override | caught |
| GPIO alternate-function register index wrong | caught |
| I2C standard-mode CCR divisor wrong | caught |
| I2C fast-mode TRISE constant wrong | caught |
| TIM PWM enables the output before the forced update | caught |
| PLL P encoding drops the `- 1` | caught |
| PLLQ written at the PLLN bit position | caught |
| Flash latency one wait state too low | caught |
| `bsp_spi_wait_idle()` stops waiting for TXE | **missed** |

The miss is the honest and expected one. The fake silicon holds TXE
permanently set, so it cannot tell whether the driver waited for it. That is
the shape of the tool's blind spot in general: it checks the *values* a driver
writes, not the *order* it writes them in or how it reacts to a peripheral that
is slow.

## What neither tool can tell you

- **Analogue and electrical behaviour.** Crystal startup, drive strength, ADC
  accuracy, whether the I2C pull-ups are right.
- **Real timing.** Setup and hold, the actual flash wait states at temperature,
  whether a bus turnaround is fast enough.
- **Protocol sequencing on the wire.** The I2C receive tails and SPI chip
  select timing are the parts most likely to still be wrong, and they are
  exactly what a RAM-backed model cannot judge. A logic analyser is the tool
  for those.
- **Interrupt behaviour.** Nothing here executes a vector table or exercises
  preemption.

Configuration paths are well covered. Transfer paths are not. Treat anything
that moves bytes over a wire as unproven until it has run on the board.
