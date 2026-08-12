# MCAN-9 host capture parser and replay

`raw_capture` provides a host-only reader, writer, and replay harness for the
versioned USB raw capture format. It has no serial, CAN-driver, transmit, or
injection API and is not linked into the vehicle firmware.

`CaptureReader` parses v1 session, frame, drop, statistics, and discontinuity
records. Strict mode stops at the first malformed record. Recovery mode records
the one-based line and complete line context, discards that line, and resumes at
the next LF. Known records are committed only after every field and ordering
invariant has passed, so malformed records cannot alter replay state.

`CaptureWriter` emits canonical v1 records and preserves standard/extended,
data/remote, DLC, and zero-length frame identity. `ReplayHarness` loads records
and releases frames only through explicit monotonic `advance_to` or
`advance_by` calls. Pausing prevents delivery; discontinuity markers reset the
simulated clock before the following segment. The clock can be adapted to
`vehicle_core::VehicleStateStore` in host tests to exercise freshness timeouts
without sleeping or hardware.
