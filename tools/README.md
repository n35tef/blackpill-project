# Verification tooling

The BSP is written from scratch against RM0383, so the obvious question is
whether the numbers in it are right. These two tools answer as much of that as
can be answered without a board on the desk.

```sh
./tools/verify.sh          # runs both
```

Neither tool needs hardware, a debugger, or any ST software. `hostsim` needs
x86-64 Linux, because its access monitor is built on page faults and the CPU
trap flag.

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

## 2. `hostsim` - do the drivers write the right bits, in the right order?

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

### The access monitor

Checking the final register state is not enough: a driver can leave every bit
correct and still have written them in an order the hardware rejects. So the
peripheral pages are mapped `PROT_NONE` and every load and store the drivers
make is trapped (SIGSEGV to see the access, single-step to let it through, then
re-protect). Each access is reported with its address, direction, old and new
value to a set of rules taken from RM0383:

- **Clock gating.** Any access to a peripheral whose `RCC->xxxENR` bit is clear
  is a violation. On silicon that read returns garbage or bus-faults.
- **RCC.** `PLLCFGR` written while `PLLON`; SYSCLK switched to a source whose
  ready flag is clear; SYSCLK switched to a frequency the current `FLASH->ACR`
  latency does not cover (table 6); HCLK above what the `PWR->CR` VOS scale
  allows.
- **SPI.** `CR1` reconfigured while `SPE=1`; `DR` written while `SPE=0`.
- **USART.** `DR` written with `UE` or `TE` clear.
- **DMA.** Any stream register reconfigured while `EN=1`.
- **ADC.** `SWSTART` with `ADON=0`, or less than tSTAB after `ADON` was set.
  The driver's settling delays go through `__busy_wait()`, which on the host
  advances a cycle counter and on the target is a plain `NOP` loop.
- **I2C.** A fake slave driven by the trace itself: START sets `SB`, the
  address byte sets `ADDR` (or `AF` for an absent address), data flows on
  `RXNE`/`TXE`/`BTF`. Touching `DR` or requesting STOP while `ADDR` has not been
  cleared by the SR1-then-SR2 read sequence is a violation, as is writing
  `CCR`/`TRISE`/`CR2` while `PE=1`.

Every test group ends with one extra check that fails if any violation was
recorded during it, printing which access broke which rule and which driver
call was executing.

Current result: **127 checks covering RCC/PLL/flash latency, GPIO, SPI, USART,
ADC, TIM, DMA and I2C, with ~1000 traced register accesses and no sequencing
violations**. The I2C driver completes real write, 1/2/N-byte read,
register-read and ping (present and absent slave) transactions against the
fake slave.

### Does it actually catch anything?

A test suite that has never failed proves nothing, so the drivers were broken
on purpose one change at a time. The first eleven mutations targeted values;
the next four targeted ordering and timing, chosen because they leave every
register holding its correct final value and so are invisible to a state-only
check.

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
| Flash latency written *after* the SYSCLK switch | caught (monitor) |
| ADC tSTAB delay removed | caught (monitor) |
| I2C `SR2` read that clears `ADDR` removed | caught (monitor + transfer hangs) |
| SPI `SPE` set before `CR1` is configured | caught (monitor) |

Before the monitor existed the last four all passed, with identical output to
an unmutated build. That is the gap it was written to close.

The remaining miss is the fake silicon holding TXE permanently set for SPI, so
it cannot tell whether the driver waited for it. The SPI model is still a
status oracle, not a clocked shift register.

## What neither tool can tell you

- **Analogue and electrical behaviour.** Crystal startup, drive strength, ADC
  accuracy, whether the I2C pull-ups are right.
- **Real timing.** The monitor knows that a delay *happened*, not how long it
  took in nanoseconds on the target at a given clock. Setup and hold, the
  actual flash wait states at temperature, bus turnaround.
- **The rules themselves.** They are my reading of RM0383. `svdcheck` has an
  external oracle (ST's SVD); the sequencing rules do not. A rule that is wrong
  in the same way as the driver passes.
- **Anything the fake slave does not model.** Clock stretching, arbitration
  loss, bus errors, a slave that NACKs mid-transfer.
- **Interrupt behaviour.** Nothing here executes a vector table or exercises
  preemption.

Configuration and transfer *sequencing* are now covered. Treat wire-level
behaviour as unproven until it has run on the board with a logic analyser.
