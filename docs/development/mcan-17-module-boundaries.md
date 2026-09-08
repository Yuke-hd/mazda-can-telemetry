# S1-A module and header boundaries

Stage 1-A separates portable transport primitives from Mazda semantics while
keeping the Stage 0 contract paths stable.

## Ownership map

| Module | Public headers | Owns | Must not depend on |
| --- | --- | --- | --- |
| `vehicle_core` | `vehicle_core/time.hpp`, `frame.hpp`, `signal.hpp` (and the `vehicle_core.hpp` umbrella) | monotonic time aliases/adapter, validated fixed classic-CAN frames, status-bearing value signals | Mazda enums, signal definitions, decoders, ESP-IDF, RTOS |
| `mazda` | `mazda/types.hpp`, `freshness.hpp`, `state.hpp`, `definitions.hpp`, `decoder.hpp` | Mazda model enums, per-signal freshness policy, semantic state/snapshots, capture-derived definitions and pure decoders | CAN driver, board/display code, RTOS |
| `mazda` contracts | `mazda/facade_contracts.hpp`, `availability.hpp`, `telemetry_contracts.hpp`, `notification.hpp`, `reading.hpp` | application-facing result, polling, notification, diagnostics and configuration contracts | raw frames and decoder implementation details |
| `mazda` internals | `mazda/internal_contracts.hpp` | raw decoder/service/lighting handoff values for later implementation stages | application facade users |
| `raw_capture` | `raw_capture/*.hpp` | host parsing/writing/replay of `vehicle_core::RawCanFrame` values | Mazda decoder ownership |

The dependency direction is intentionally one-way:

```text
vehicle_core (time/frame/signal)
        ↑
        │
mazda (types/freshness/state/definitions/decoder)
        ↑                         ↑
        │                         │
vehicle_telemetry facade      raw_capture host replay
```

The facade includes `mazda/facade_contracts.hpp`, which includes only
value-copy contract types, generic health metadata, model callback types and
configuration policy. `mazda/telemetry_contracts.hpp` remains a Stage 0
compatibility umbrella and additionally exposes the internal handoff values.
The facade does not include raw frames, decoder definitions, decoder state, CAN
headers, or RTOS headers. Later service code should include
`mazda/internal_contracts.hpp` only inside its implementation boundary.

## Include/source compatibility

`vehicle_core/vehicle_core.hpp` remains an umbrella for the portable core
primitives, so frame/time/signal users retain their old include path. Mazda
users must migrate from the former combined header to explicit includes:

```cpp
#include "vehicle_core/frame.hpp" // RawCanFrame
#include "mazda/state.hpp"        // VehicleState and VehicleStateStore
#include "mazda/decoder.hpp"      // mazda::candidate decoder API
```

The former `vehicle_core::mazda_candidate` namespace and Mazda model types in
`vehicle_core` were intentionally removed; their owned names are now
`mazda::candidate`, `mazda::VehicleState`, `mazda::TurnState`,
`mazda::SelectorPosition`, `mazda::ActualGear`, and
`mazda::FrontWiperPosition`. This is an intentional source-compatibility
change required to prevent the generic library from acquiring Mazda
dependencies. Existing runtime behavior and value layouts are unchanged.
