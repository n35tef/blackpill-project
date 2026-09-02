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

Current result: **43 base addresses, 241 registers and 757 bit fields agree**
(the SDIO and OTG_FS maps added 5 bases, 64 registers and 383 fields).

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
- **SDIO.** A fake SD card behind the controller: it answers CMD0/8/55/ACMD41/
  2/3/9/7/16/ACMD6/13/17/18/24/25/12 with spec-shaped R1/R2/R3/R6/R7 responses,
  moves data through a 32-word FIFO model (8 words per `STA` read, so the
  `RXFIFOHF`/`TXFIFOHE` handshakes are real) and can be inserted as an SDHC v2
  or an SDSC v1 card, or removed. Rules: command with power off, `CLKEN=0` or
  the previous command still active; identification above 400 kHz; `CLKEN` less
  than 1 ms after power-on or CMD0 less than 74 clocks after `CLKEN`; `HCS`
  without a prior CMD8; `WIDBUS` widened before the card accepted ACMD6, or set
  to 8-bit; `SDIO_CK` above 25 MHz or above HCLK/2; a read command sent before
  `DCTRL` was armed with `DTEN|DTDIR`, or a write command sent with `DTEN`
  already set (RM0383 21.3.2 orders them the other way round); `DBLOCKSIZE`
  other than 512; `DLEN` not a block multiple or written while `DTEN=1`;
  `DTIMER=0`; `CLKCR` touched mid-transfer; FIFO read empty or written full;
  CMD12 without an open multi-block transfer; a byte address that is not
  block-aligned on an SDSC card. `ICR` is write-one-to-clear and the static
  flags survive it, as on silicon.
- **OTG_FS.** A fake USB host plus the NVIC: bus events land in `GINTSTS` and
  `OTG_FS_IRQHandler()` is run until nothing unmasked is pending, as a
  level-sensitive line would. Packets go through a model of the shared receive
  FIFO (status entries popped via `GRXSTSP`, payload words behind them) and
  per-endpoint transmit FIFOs whose contents the host collects on IN.
  Rules: `CSRST` without checking `AHBIDL`; `PHYSEL` changed after the reset;
  `SDIS` cleared with `PWRDWN` clear, without `NOVBUSSENS`/`VBUSBSEN`, less than
  25 ms after `FDMOD`, with `DSPD` not full-speed, with `GINT` off or the
  essential `GINTMSK` bits clear, or with a stale `DAD`; a FIFO layout where
  any FIFO is under 16 words, overlaps another or exceeds the 320-word RAM;
  `TRDT` not matching HCLK (table 133); a host-mode register touched at all;
  an endpoint enabled with `PKTCNT=0`, without `USBAEP`, with `XFRSIZ`/`PKTCNT`
  inconsistent for its MPS, while still NAKing, or with `TXFNUM` not its own;
  type/size/FIFO changed or `DIEPTSIZ` rewritten while `EPENA=1`; a TX FIFO
  written for an endpoint that is not enabled, or past its `XFRSIZ`; an OUT
  endpoint disabled without `GOUTNAKEFF`; `GRXSTSP` popped with nothing pending
  or before the previous packet's words were read; a FIFO read past the packet;
  `DCFG.DAD` not holding the new address when the SET_ADDRESS status IN goes
  out; a status IN that carries data; an interrupt that the handler never
  clears (storm). `NAKSTS` is read-only and only moves on `SNAK`/`CNAK`;
  `GINTSTS`/`DIEPINT`/`DOEPINT` are write-one-to-clear; `DCTL` pulse bits do
  not stick.

Every test group ends with one extra check that fails if any violation was
recorded during it, printing which access broke which rule and which driver
call was executing.

Current result: **254 checks covering RCC/PLL/flash latency, GPIO, SPI, USART,
ADC, TIM, DMA, I2C, SDIO and USB, with ~4300 traced register accesses and no
sequencing violations**. The I2C driver completes real write, 1/2/N-byte read,
register-read and ping (present and absent slave) transactions against the
fake slave. The SD driver brings up an SDHC card (CMD8/ACMD41 with HCS, CSD v2
capacity, 4-bit bus, 24 MHz), reads single and multiple blocks, writes two
blocks with CMD25/CMD12 and reads them back, then repeats the bring-up on an
SDSC v1 card (no CMD8 answer, byte addressing, CSD v1 capacity arithmetic) and
reports `ENOCARD` on an empty slot. The USB device enumerates against the host
model: bus reset, 8- and 18-byte device descriptor, SET_ADDRESS, configuration
descriptor (67 bytes, walked descriptor by descriptor), string descriptors
including the 24-digit UID serial, STALL on a device qualifier and on an
unknown request, SET_CONFIGURATION opening EP1/EP2 with the right type, size
and FIFO, the four CDC class requests, data both ways on EP1 including a
2-packet transfer that owes a zero-length packet and a 700-byte write drained
over several IN tokens, DTR drop, deconfiguration and a second reset. 134
interrupt handler runs are exercised in the process.

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
| SDIO read arms `DCTRL` without `DTDIR` | caught (monitor) |
| SDIO `WIDBUS` widened before ACMD6 | caught (monitor) |
| USB 25 ms wait after `FDMOD` removed | caught (monitor) |
| USB TX FIFO written before `EPENA` | caught (monitor + enumeration fails) |

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
- **CRC.** The SD card model never produces a wrong CRC and the controller
  model never checks one; `CCRCFAIL` on R3 is the only CRC behaviour modelled.
  Card-to-card variance (slow ACMD41, odd CSDs, cards that need CMD8 retried)
  is not modelled either: the fake is one well-behaved card of each type.
- **The USB host.** Real hosts reset twice, poll with real timing, send
  requests this model never sends (SET_FEATURE, GET_INTERFACE, class requests
  from other drivers) and stop enumerating on the first mistake. The host
  model is one polite host issuing one request at a time; suspend/resume and
  SOF are not exercised. Nothing here proves that Linux, Windows or macOS
  will bind a `ttyACM`/COM port to this device.
- **Interrupt behaviour.** The USB handler is called by the model in a loop,
  not by an NVIC; nothing here exercises preemption, priorities or the
  `PRIMASK` critical sections in the CDC ring buffers under real contention.

Configuration and transfer *sequencing* are now covered. Treat wire-level
behaviour as unproven until it has run on the board with a logic analyser.
