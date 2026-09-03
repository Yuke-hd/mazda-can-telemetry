# MCAN-14 candidate decoders

Status: portable candidate implementation; pending MCAN-19 validation.

This change adds host/firmware-independent decoders for the candidate
`ENGINE_DATA` (`0x202`) and `GEAR` (`0x228`) classic-CAN messages. A decoder
accepts a `vehicle_core::RawCanFrame` and writes only semantic fields in a
`VehicleState`; it has no CAN driver, transport, polling, injection, board
header, or global clock dependency. `raw_capture::ReplayHarness` can call the
same functions used by a receive path, so replay tests do not have a second
decoder implementation.

## Candidate fields

| Message | Field | Bytes/bits | Scale + offset | Unit | Invalid / undefined values |
| --- | --- | --- | --- | --- | --- |
| `0x202` | RPM | bytes 0..1, big-endian | `0.25 + 0` | rpm | raw `>34000` (`>8500 rpm`) |
| `0x202` | SPEED | bytes 2..3, big-endian | `0.01 + 0` | km/h | no source sentinel; zero is valid |
| `0x228` | selector | byte 0 low 3 bits | `1 + 0` | selector position | `0` means shifting/unknown; `5..7` undefined |
| `0x228` | actual gear | byte 4 bits 4..1 | `1 + 0` | actual gear | valid raw domain `0..14` (with `7..13` undefined); `15` shifting/unknown |

Selector and actual transmission gear are separate signals. Selector values
are `1=P`, `2=R`, `3=N`, and `4=D`. Actual gear values are `0=P`, `14=R`, and
`1..6` for first through sixth. Invalid values leave the corresponding signal
untouched and cannot create a valid value. Frames must be standard, non-RTR,
exactly eight bytes, and have the expected identifier.

The field leads are cross-checked against the pinned comma.ai/opendbc
`mazda_2017.dbc` candidate evidence at commit
`95f3d52f474b677c28fc8f10fef3f2f0386aff92`; see
[`mcan-10-opendbc-signal-evidence.md`](mcan-10-opendbc-signal-evidence.md).
The older `skyactiv.kcd` reference is not used as verification because it is
malformed/outdated, as recorded in the issue context.
The opendbc DBC has no cycle-time declaration. Consequently each definition
exposes an unspecified expected period and freshness timeout. Callers must
provide a per-signal `VehicleFreshnessPolicy` when evidence supports one;
`Signal::refresh()` still marks a value stale when an unconfigured timeout has
elapsed, and snapshots never mutate the source state.

## Golden vectors and provenance

The following vectors are synthetic, privacy-safe test data. The ENGINE_DATA
vector is derived from the upstream field layout and scale; the GEAR vector is
the issue's example cross-check. Neither is a vehicle capture or a claim of
Australian-market compatibility.

| Message | Payload | Expected result | Provenance |
| --- | --- | --- | --- |
| `0x202` | `09 5B 00 00 00 00 00 00` | `598.75 rpm`, `0 km/h` | upstream field layout/scale, synthetic raw values |
| `0x202` | `84 D0 FF FF 00 00 00 00` | `8500 rpm`, `655.35 km/h` | synthetic boundary vector |
| `0x228` | `24 81 07 FF 04 F0 00 00` | selector `D`, actual `2nd` | MCAN-14 acceptance example, synthetic |

Private captures, including `/Users/yuke/Documents/0309-running.csv` and GVRET
CSV exports, were used only as out-of-repository analysis context. They are not
read, copied, derived, or committed by this change.
