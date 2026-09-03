# MCAN-15 normalized turn and hazard state

`vehicle_core::mazda_candidate::decode_turn_switch()` accepts only a valid,
standard, non-RTR classic-CAN frame with ID `0x091` and DLC 8. It extracts the
candidate `HAZARD`, `TURN_RIGHT_SWITCH`, and `TURN_LEFT_SWITCH` fields using
byte/LSB numbering (byte 1 bits 2, 4, and 5 respectively), updates the three
raw request signals, and normalizes them to `TurnState`:

| HAZARD | LEFT | RIGHT | normalized state |
| --- | --- | --- | --- |
| 1 | either | either | Hazard |
| 0 | 1 | 0 | Left |
| 0 | 0 | 1 | Right |
| 0 | 1 | 1 | Unknown (conflict) |
| 0 | 0 | 0 | Off |

Hazard therefore takes precedence over both directions. A fresh frame keeps
the state valid for 250,000 microseconds; after more than 250,000
microseconds without an accepted update, the signal is `Stale`. Unknown and
stale states are non-actionable: `VehicleState::effective_turn_state()` returns
`Unknown`, which gives indicator consumers fail-off semantics. The stored raw
value is retained for diagnostics while its status is stale.

`VehicleState::update_turn()` emits a `TurnEdgeEvent` only when the semantic
state changes. Equal states and duplicate frames do not create duplicate
edges. A stale value is not actionable, so a recovered frame produces an edge
from effective `Unknown` when its new state is different.

`BLINK_INFO` (`0x09A`) remains diagnostic-only. Its phase/lamp candidates are
not decoded into request or turn state. All definitions above are upstream
leads from comma.ai/opendbc `mazda_2017.dbc` at commit
`95f3d52f474b677c28fc8f10fef3f2f0386aff92`, and remain pending MCAN-19
validation for the Australian target. Tests use synthetic frames and a
simulated monotonic replay clock; no capture or vehicle-derived data is
included. The decoder only receives frames and writes in-memory state; it has
no CAN transmit or vehicle-control interface.
