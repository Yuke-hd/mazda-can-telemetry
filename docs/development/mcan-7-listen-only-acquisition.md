# MCAN-7 strict listen-only acquisition

## Safety boundary

The `tcan485_vehicle_listen_only` target installs the ESP-IDF v5.5.4 TWAI
driver with `TWAI_MODE_LISTEN_ONLY`. The mode is a compile-time statement in
the private implementation, not a public configuration value. The driver TX
queue is explicitly set to zero. Invalid bitrates fail before the transceiver
is powered or the driver is installed. There is no normal-mode or no-ACK
fallback in the vehicle target. The separately named
`tcan485-bench-ack-only` project is the only guarded exception and is governed
by [MCAN-13's isolation record](mcan-13-bench-ack-only.md).

The public `can_bus` header exposes exactly four operations:

- `start(Configuration)`;
- `stop()`;
- `receive(RawCanFrame &, timeout_ms)`;
- `statistics(StatisticsOperation)`.

It exposes no TWAI handle and no send, transmit, recovery, mode-selection, or
arbitrary-driver operation. The implementation does not reference
`twai_transmit` or `twai_transmit_v2`. A structural host test enforces these
constraints. This evidence establishes the software structure; it does not
replace the isolated-bench no-ACK measurement below.

The allowed nominal bitrates are 125 kbit/s, 250 kbit/s, 500 kbit/s, and
1 Mbit/s. These are explicit classic-CAN timing presets in ESP-IDF v5.5.4. A
bitrate outside that set is rejected. The initial firmware configuration is
500 kbit/s, bus 0; this is a bring-up setting and is not evidence that a target
vehicle network uses that bitrate.

## Receive path and ownership

A dedicated `can_rx` task calls `twai_receive()` with a bounded poll interval.
Its priority is `configMAX_PRIORITIES - 2`, above the other planned application
consumers. Immediately after a successful receive it obtains microseconds since
boot from `esp_timer_get_time()` and copies the bus number, timestamp,
standard/extended identifier format, RTR flag, DLC, identifier, and fixed
eight-byte payload into a `RawCanFrame` value.

The task is the only producer for a fixed-capacity, 64-frame SPSC ring. One
consumer task owns the public `receive()` calls; callers must serialize those
calls and stop that consumer before restarting acquisition. The consumer
receives a copy and never obtains storage owned by the producer. The ring
allocates no memory after static initialization. A full ring uses a
**drop-newest** policy: the arriving frame is discarded, existing FIFO order is
preserved, and both `frames_dropped` and `queue_overflows` increment. Ring push
never waits for a consumer. The notification semaphore is also statically
allocated and is given only after a successful push.

The ESP-IDF driver has a separate 64-frame RX queue. `driver_rx_missed` is the
delta of the driver's cumulative `rx_missed_count` and `rx_overrun_count`
status counters; application-ring drops are reported separately. This
distinction prevents a slow consumer from hiding a driver-level loss.

## Statistics semantics

`statistics(kSnapshot)` returns cumulative values since start or the last
reset. `statistics(kSnapshotAndReset)` returns the pre-reset interval and then
zeros receive, queued, delivered, dropped, overflow, bus-error, driver-missed,
and controller-reset counters. It does not remove queued frames. Queue depth
remains live, and the next interval's high watermark begins at the depth that
existed at reset. The producer publishes queue depth before its watermark, and
reset reconciles one fresh depth observation after clearing the interval
counter. If reset wins the watermark exchange, it can observe the producer's
published depth; if the producer publishes after reconciliation, it raises the
watermark afterward. Consequently, a concurrent producer cannot make the new
watermark claim that an already occupied queue started empty.

`controller_resets` is one on an acquisition interval that follows a prior
successful start, and zero on the first interval. An unexpected bus-off alert is
also counted because it represents an abnormal controller lifecycle in strict
listen-only operation; the component does not initiate active bus recovery.
`bus_errors` is the delta of the driver's cumulative `bus_error_count` status
counter. Driver loss and error counters are sampled by the receive task before
each alert poll, so they represent driver-reported counts rather than
coalesced alert occurrences. An unexpected bus-off remains an independent
`controller_resets` event; the component never initiates active recovery.

## References

The implementation API choices were checked on 2026-08-12 against the primary
ESP-IDF v5.5.4 documentation:

- [TWAI driver](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/peripherals/twai.html)
- [ESP Timer](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/system/esp_timer.html)
- [FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/system/freertos.html)

## Validation status

Host tests exercise bitrate rejection, full frame fidelity, FIFO order,
drop-newest behavior, overflow and watermark accounting, statistics reset, and
1,000 producer calls with an absent consumer. The structural check verifies
the public operation set, strict listen-only token, disabled TX queue, absence
of alternate modes in the vehicle CAN implementation, and absence of TWAI
transmit calls. The MCAN-13 artifact check additionally verifies that normal
mode is confined to the separately named bench project and that its labels
cannot be mistaken for vehicle firmware.

Integrated isolated-bench validation is intentionally out of scope for MCAN-7.
MCAN-33 owns the future physical receiver, wiring, PCB-revision, and no-ACK
evidence after the software work and MCAN-12 are complete. Vehicle validation
has also not been executed; this software must not be connected to a vehicle
until the MCAN-33 safety record and T-CAN485 PCB revision/wiring verification
are complete.
