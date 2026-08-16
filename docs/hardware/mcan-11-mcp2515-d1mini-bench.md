# MCAN-11 MCP2515/D1 Mini isolated-bench procedure

Status: preparation only. Every measurement and pass/fail result below is
`NOT RUN / PENDING HARDWARE`. This procedure is for a supervised, isolated
bench and must never be connected to a vehicle or an in-vehicle harness.

Authoritative references:

- [Microchip MCP2515 Family Data Sheet, DS20001801K](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf), sections 3.4/3.6, 5.7, and 10.5, for one-shot mode, abort behavior, bit timing, and register definitions.
- [NXP TJA1050 High speed CAN transceiver data sheet](https://www.nxp.com/docs/en/data-sheet/TJA1050.pdf), Table 1 and the Characteristics table, for supply and logic/line characterization limits.

## Fixed test configuration

The PlatformIO environment `bench_mcp2515` is explicitly bench-only and is not
the default environment. It builds only the register-level MCP2515 harness;
the default `d1mini` environment excludes that source. The harness is bounded
to one synthetic, zero-data standard-ID one-shot attempt (`0x123`) after reset,
then only observes INT transitions. It does not retry, continuously transmit,
initialize Wi-Fi, or expose a vehicle-firmware API. If TXREQ remains pending
after the timeout, the harness asserts MCP2515 ABAT, verifies TXREQ clears,
clears ABAT only after that verification, and uses a controller reset as the
last local cleanup if required; a persistent TXREQ requires power removal.

The intended component is a shared-VCC MCP2515/TJA1050 module with a confirmed
8 MHz oscillator. The oscillator is **NOT RUN / PENDING HARDWARE** until the
component marking and (if needed) a scope/frequency-counter check are recorded.
The harness uses SPI mode 0 at exactly 1 MHz and the 8 MHz / 500 kbit/s MCP2515
timing bytes in its source. These configured values are not physical evidence.

### CNF timing derivation

For the MCP2515 settings in the harness, `FOSC = 8 MHz`, so `TOSC = 125 ns`.
The data sheet defines the minimum time quantum as `2 x TOSC` when `BRP = 0`;
therefore `CNF1 = 0x00` gives `TQ = 250 ns` and `SJW = 1 TQ`. With
`CNF2 = 0x90`, `BTLMODE = 1`, `SAM = 0`, `PRSEG = 1 TQ`, and `PHSEG1 = 3 TQ`.
With `CNF3 = 0x02`, `PHSEG2 = 3 TQ`. The nominal bit is therefore
`1 + 1 + 3 + 3 = 8 TQ = 2 us`, or `500 kbit/s`, with a 62.5% nominal sample
point. This is a configured calculation pending oscillator and bus evidence;
it is not a claim that the module's fitted oscillator is 8 MHz.

| D1 Mini signal | ESP8266 GPIO | MCP2515 module | Required connection |
| --- | ---: | --- | --- |
| 3V3 | — | VCC | 3.3 V only while SPI/INT are connected |
| GND | — | GND | Common bench ground |
| D5 / SCK | 14 | SCK | SPI clock |
| D6 / MISO | 12 | SO | SPI data to D1 Mini |
| D7 / MOSI | 13 | SI | SPI data from D1 Mini |
| D8 / CS | 15 | CS | Chip select |
| D1 | 5 | INT | Open-drain active-low interrupt |

Do not power this shared-VCC module at 5 V while D1 Mini SPI or INT lines are
directly connected. The NXP TJA1050 data sheet specifies `VCC = 4.75–5.25 V`;
3.3 V is outside that specified operating range. Therefore any 3.3 V result
is out-of-spec characterization only, not a TJA1050 compliance or reliability
pass. If the 3.3 V physical layer is unstable or any required electrical/logic
behavior is outside the worksheet comparison, stop and use a 5 V Arduino with
level-safe interfacing or a native-3.3-V CAN module; do not improvise a vehicle
connection.

## Wiring and termination gate

1. With every supply disconnected, inspect the module, D1 Mini, and isolated
   second CAN node for damage and record board markings. The MCP2515 crystal
   marking and exact board revision are **NOT RUN / PENDING HARDWARE**.
2. Use only a current-limited 3.3 V bench supply for the shared-VCC module.
   Complete and review the table above before power is applied.
3. Build a two-ended isolated CAN bus between the module and a second bench
   CAN node/analyzer. Place exactly one 120 ohm resistor across CANH-CANL at
   each physical end; do not add a third terminator or use a vehicle harness.
4. Before power, measure CANH-to-CANL with the bus unpowered. The expected
   value for two verified 120 ohm end resistors in parallel is approximately
   60 ohms. Record the meter value and resistor locations here:

   - Measured resistance: **NOT RUN / PENDING HARDWARE**
   - End A 120 ohm verified: **NOT RUN / PENDING HARDWARE**
   - End B 120 ohm verified: **NOT RUN / PENDING HARDWARE**
   - Extra termination absent: **NOT RUN / PENDING HARDWARE**

Do not proceed if the wiring review or termination measurement is incomplete.

## Build, upload, and run sequence

PlatformIO Core 6.1.18 command syntax follows the official [`pio run` CLI
reference](https://docs.platformio.org/en/stable/core/userguide/cmd_run.html)
and [`pio device monitor` CLI reference](https://docs.platformio.org/en/stable/core/userguide/device/cmd_monitor.html).
Run the build from the simulator project directory; compilation does not touch
the hardware:

```text
cd simulators/d1mini_can_web
pio run -e bench_mcp2515
```

Before uploading or running anything on the bench, remove all power and pass
the complete wiring/termination gate above. Then connect the D1 Mini and
shared-VCC module exactly as documented, using 3.3 V only while SPI and INT are
connected. List available ports with `pio device list`, substitute the reviewed
bench port for `<PORT>`. Arm the isolated analyzer and oscilloscope capture
before uploading, with a fresh run label ready for the upload-triggered boot.
The wiring/termination and 3.3 V safety gates must remain satisfied before
both runs below.

Run 1 is the upload-triggered supervised run: `-t upload` normally resets and
boots the D1 Mini after programming, so execute this command only after the
analyzer/scope is armed:

```text
pio run -e bench_mcp2515 -t upload --upload-port <PORT>
```

Record Run 1 separately, including the analyzer/scope result and whether early
serial output was missed; a missed serial line does not mean that no attempt
occurred. After the upload completes, attach the environment-specific monitor:

```text
pio device monitor -e bench_mcp2515 -b 115200 --port <PORT>
```

Run 2 is a separate supervised run. Only after the monitor is attached and the
same wiring/termination and 3.3 V gates remain satisfied, perform one deliberate
reset (or power-cycle) to trigger the boot attempt and record its analyzer,
scope, and serial evidence under a new run label. The harness performs its one
active attempt during each boot/reset and does not retry in its loop.

Do not use the default environment for this procedure. Do not press reset or
cycle power until the wiring review and unpowered approximately-60-ohm
termination measurement have been recorded. Every repeated ACK/no-ACK run
requires a deliberate supervised power-cycle/reset and a separate evidence
record; never rely on the loop to retry a transmission. Record both Run 1 and
Run 2 separately without raw vehicle captures, VINs, absolute times, or
location metadata.

## Bring-up and evidence worksheet

| Check | Result | Evidence / instrument reference |
| --- | --- | --- |
| D1 Mini pin continuity and 3.3 V rail | NOT RUN / PENDING HARDWARE | — |
| MCP2515 reset returns configuration mode | NOT RUN / PENDING HARDWARE | — |
| SPI register read/write at 1 MHz | NOT RUN / PENDING HARDWARE | — |
| 8 MHz oscillator marking/frequency | NOT RUN / PENDING HARDWARE | — |
| MCP2515 configured normal + one-shot mode | NOT RUN / PENDING HARDWARE | — |
| INT is high idle and changes on controller event | NOT RUN / PENDING HARDWARE | — |
| Exactly one bounded one-shot attempt | NOT RUN / PENDING HARDWARE | — |
| ACK observed by the isolated second node | NOT RUN / PENDING HARDWARE | — |
| No-ACK/error flags when no receiver is present | NOT RUN / PENDING HARDWARE | — |
| TJA1050 3.3 V physical-layer pass/fail | NOT RUN / PENDING HARDWARE | — |
| Fallback selected if 3.3 V physical layer fails | NOT RUN / PENDING HARDWARE | — |

The no-ACK result is expected only when the isolated setup intentionally has
no other active CAN participant. Record controller flags and the analyzer's
observation, not a made-up pass/fail. The harness's 50 ms timeout bounds the
attempt; power-cycle before any repeat and document each run separately.

## TJA1050 electrical worksheet

Record instrument, probe reference, and bench run label with each value. Do not
fill a field from a nominal calculation. The comparison column is the cited
NXP data-sheet limit; because the planned test is at 3.3 V, it cannot establish
component compliance even if every observed value appears plausible.

| Measurement | Measured result | NXP reference / comparison | Status |
| --- | --- | --- | --- |
| Module VCC, recessive/idle | NOT RUN / PENDING HARDWARE | 4.75–5.25 V specified operating range | PENDING |
| Module VCC under load during one-shot | NOT RUN / PENDING HARDWARE | 4.75–5.25 V specified operating range | PENDING |
| CANH recessive | NOT RUN / PENDING HARDWARE | 2.0–3.0 V at VCC 4.75–5.25 V, no load | PENDING |
| CANL recessive | NOT RUN / PENDING HARDWARE | 2.0–3.0 V at VCC 4.75–5.25 V, no load | PENDING |
| CANH dominant | NOT RUN / PENDING HARDWARE | 3.0–4.25 V at TXD=0 V | PENDING |
| CANL dominant | NOT RUN / PENDING HARDWARE | 0.5–1.75 V at TXD=0 V | PENDING |
| CANH-CANL differential, dominant | NOT RUN / PENDING HARDWARE | 1.5–3.0 V for 42.5–60 ohm load | PENDING |
| CANH-CANL differential, recessive | NOT RUN / PENDING HARDWARE | -50–+50 mV, no load | PENDING |
| TXD HIGH / recessive logic | NOT RUN / PENDING HARDWARE | VIH >= 2.0 V | PENDING |
| TXD LOW / dominant logic | NOT RUN / PENDING HARDWARE | VIL <= 0.8 V | PENDING |
| RXD HIGH / recessive logic | NOT RUN / PENDING HARDWARE | Function table: HIGH | PENDING |
| RXD LOW / dominant logic | NOT RUN / PENDING HARDWARE | Function table: LOW | PENDING |
| Analyzer decode at configured bitrate | NOT RUN / PENDING HARDWARE | Isolated analyzer observation | PENDING |
| ACK/error observation | NOT RUN / PENDING HARDWARE | Record ACK, TXERR/ABTF, and analyzer error state | PENDING |

Explicit fallback criteria: select the 5 V Arduino path with level-safe SPI/
INT interfacing or a native-3.3-V CAN transceiver/module if VCC sags, CANH or
CANL does not show the expected recessive/dominant relationship, the dominant
differential is not clearly detected, TXD/RXD levels are not reliably logical,
the analyzer cannot decode the isolated frame at the configured bitrate, or
the TJA1050 becomes unstable or thermally abnormal. Stop the 3.3 V run at the
first such condition; do not compensate by connecting to a vehicle. Any
successful 3.3 V observation remains characterization outside the TJA1050
specified VCC range.

## Exit criteria and exclusions

MCAN-11 remains open until supervised physical evidence confirms wiring,
termination, repeatable SPI access, reset/INT behavior, one-shot/no-ACK
behavior, and the TJA1050 3.3 V physical-layer characterization and fallback
decision. This document does not claim any of those results. It does not
authorize a vehicle connection, active vehicle firmware, diagnostic polling,
raw-capture publication, or a release artifact.
