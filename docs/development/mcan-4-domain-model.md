# MCAN-4 portable domain model

The `vehicle_core` public header defines the domain boundary shared by host
tools and device firmware. It is C++17 and has no ESP-IDF, Arduino, FreeRTOS,
transport, display, or LED dependency.

## Raw frames

`RawCanFrame` is a value type for a classic CAN receive record. It retains the
monotonic timestamp, `bus_id`, identifier and standard/extended format, RTR
flag, DLC, and an eight-byte fixed payload. The fixed array is intentional:
frames can be copied through a bounded queue without transferring ownership or
allocating. CAN-FD payloads are outside this API. `is_valid()` checks the
classic-CAN DLC and identifier limits; it does not claim that a frame was
observed on a particular Mazda network.

There is no transmit operation, CAN driver handle, or raw-frame forwarding
interface in this component.

## Signals and freshness

`Signal<T>` stores a value, `SignalUnit`, last-update monotonic timestamp, and
`SignalStatus` (`Unknown`, `Valid`, or `Stale`). Status is explicit, so a valid
zero speed or RPM is never confused with an uninitialized value. `update()`
rejects an older timestamp. `refresh(now, timeout)` marks a valid signal stale
only after the elapsed time is greater than the configured timeout; unknown
signals remain unknown until their first update.

`VehicleState` currently provides the initial speed, RPM, gear, turn, hazard,
left-turn, and right-turn signal slots. More signals can be added without
introducing transport or board types. `snapshot(now, timeout)` evaluates
freshness on a value copy and leaves the source state unchanged.

## Semantic events and time

`TurnEdgeEvent` contains only semantic previous/current turn states and a
monotonic timestamp. It deliberately has no CAN ID or payload. A
`MonotonicClock` adapter supplies time, while `VehicleStateStore` owns one
state by value and provides deterministic snapshots through the
`SnapshotProvider` interface. Host tests can provide a fixed clock; firmware
can adapt its monotonic timer without changing the domain model.

All ownership is explicit and non-owning at interfaces: `VehicleStateStore`
keeps a pointer to the caller-owned clock, which must outlive the store. Frames,
signals, events, and states own only fixed-size value data. The model performs
no heap allocation, and callers must use bounded queues/ring buffers around it
when crossing task or transport boundaries. Snapshot values are independent
copies and may be handed to a slow consumer without holding the receive path.

The unit tests cover construction, identifier/DLC validation, timestamp
ordering, valid-zero distinction, stale transitions, semantic turn edges, and
deterministic non-mutating snapshots. No bench or vehicle validation is
claimed by MCAN-4.
