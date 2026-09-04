# MCAN-16 local semantic ARGB indicator

## Semantic boundary and colors

`local_argb` consumes only a copied turn value, its `SignalStatus`, last semantic
update time, and semantic health (`Online`, `CanOffline`, or `DecoderError`). It
does not include or inspect `RawCanFrame`, CAN identifiers, Mazda decoder
constants, `can_bus`, TWAI, or driver handles.

The one onboard WeAct CAN485 DevBoard V1.1 WS2812B is diagnostic-only:

| Semantic input | RGB output |
| --- | --- |
| Valid left | Green `(0, 16, 0)` |
| Valid right | Blue `(0, 0, 16)` |
| Valid hazard | Amber `(16, 8, 0)` |
| Off, unknown, stale, CAN offline, or decoder error | Black `(0, 0, 0)` |

Every channel is capped by the compile-time value 16. The LED is not a safety
indicator and must not be used instead of the factory cluster.

## Runtime isolation and fail-off behavior

The ESP-IDF runtime binds only to `board::kWeActCan485V11.onboard_rgb`: GPIO4
and exactly one pixel. It configures `led_strip` v3.0.3 for WS2812, explicit GRB
component order, a 10 MHz RMT resolution, and DMA disabled. It sends an explicit
black frame and refreshes it before CAN startup. Merely holding GPIO4 low does
not clear a previously latched WS2812B color.

LED writes run in a dedicated `local_argb` task at `tskIDLE_PRIORITY + 2`, below
the `can_rx` task. Producers copy semantic snapshots into a statically allocated
length-one queue with `xQueueOverwrite`, so they never wait for LED/RMT work and
new state replaces obsolete backpressure. The worker checks freshness at least
every 10 ms and clears when elapsed time becomes greater than 250,000 us even
if semantic updates stop. The controller suppresses redundant refreshes.

Every pixel-set and refresh result is checked. A failed colored write
immediately attempts black, remains fail-off for that submission, and retries
black on later worker ticks. A fresh semantic submission is required before a
color can be attempted again. Failure to create or initially clear the strip
prevents CAN startup. CAN startup/runtime failure and semantic submission
failure request black and stop acquisition.

The isolated T-CAN485 bench project discovers only the shared `board` and
`can_bus` components, so it neither resolves nor links `local_argb` or
`led_strip`. Its ACK-only hardware behavior remains separate.

`DecodeStatus::Ignored` is normal unrelated traffic. It establishes initial CAN
online health without changing turn state and does not erase an existing
decoder error. `DecodeStatus::Invalid` sets decoder-error health and therefore
black; a subsequent valid semantic update recovers.

## Evidence and physical limitation

Deterministic host tests cover startup black, every mapping, the exact
250,000/250,001 us boundary, same-direction recovery, offline/error fail-off,
brightness limits, duplicate coalescing, length-one overwrite behavior, and
driver failure/black retry. A structural validator enforces semantic isolation
and the fixed WeAct/RMT configuration. CI builds both ESP-IDF projects.

No physical bench or vehicle test is claimed by this change. A total CPU/RMT
failure cannot transmit a black frame, and a WS2812B retains its last color
while powered; instantaneous physical fail-off therefore cannot be guaranteed
without hardware LED power gating.

Before vehicle release, run concurrent classic-CAN load and repeated LED state
changes on the WeAct V1.1 board. Record generated/received counts, application
drops, queue overflows/high-watermark, driver missed/overrun counts, bus errors,
LED transition results, startup/warm-reset clear behavior, and native USB logs.
Keep K3 OFF, add no vehicle termination, and include no raw payloads or private
vehicle data in public evidence.

The component API and configuration were reviewed on 2026-09-04 against the
[Espressif `led_strip` v3.0.3 registry release](https://components.espressif.com/components/espressif/led_strip/versions/3.0.3).
