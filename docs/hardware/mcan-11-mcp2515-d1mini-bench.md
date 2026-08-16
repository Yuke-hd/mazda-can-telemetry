# MCAN-11 MCP2515/D1 Mini isolated-bench procedure

Status: preparation only. Every measurement and pass/fail result below is
`NOT RUN / PENDING HARDWARE`. This procedure is for a supervised, isolated
bench and must never be connected to a vehicle or an in-vehicle harness.

## Fixed test configuration

The PlatformIO environment `bench_mcp2515` is explicitly bench-only and is not
the default environment. It builds only the register-level MCP2515 harness;
the default `d1mini` environment excludes that source. The harness is bounded
to one synthetic, zero-data standard-ID one-shot attempt (`0x123`) after reset,
then only observes INT transitions. It does not retry, continuously transmit,
initialize Wi-Fi, or expose a vehicle-firmware API.

The intended component is a shared-VCC MCP2515/TJA1050 module with a confirmed
8 MHz oscillator. The oscillator is **NOT RUN / PENDING HARDWARE** until the
component marking and (if needed) a scope/frequency-counter check are recorded.
The harness uses SPI mode 0 at exactly 1 MHz and the 8 MHz / 500 kbit/s MCP2515
timing bytes in its source. These configured values are not physical evidence.

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
directly connected. If the TJA1050 physical layer is not valid at the measured
3.3 V supply, stop and use a 5 V Arduino with level-safe interfacing or a
native-3.3-V CAN module; do not improvise a vehicle connection.

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

## Bring-up and evidence worksheet

Run `pio run -e bench_mcp2515` and observe the serial log only on the isolated
bench. Record evidence without raw vehicle captures, VINs, absolute times, or
location metadata.

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

## Exit criteria and exclusions

MCAN-11 remains open until supervised physical evidence confirms wiring,
termination, repeatable SPI access, reset/INT behavior, one-shot/no-ACK
behavior, and the TJA1050 3.3 V physical-layer result. This document does not
claim any of those results. It does not authorize a vehicle connection, active
vehicle firmware, diagnostic polling, raw-capture publication, or a release
artifact.
