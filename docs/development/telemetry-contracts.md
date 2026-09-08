# Telemetry contract and test seams

Stage 0-A freezes the interfaces used by the later background service work.
The declarations are intentionally compile-only seams; they do not start tasks,
touch CAN, or provide firmware behavior.

## Contract locations

- `lib/vehicle_core/include/vehicle_core/telemetry_contracts.hpp` contains the
  value-copy `Reading`, `Notification`, callback and availability/evidence
  types.
- `lib/vehicle_core/include/vehicle_core/decoder_contracts.hpp` separates
  decoder validity from transport, message and signal health.
- `lib/mazda/include/mazda/telemetry_contracts.hpp` contains Mazda-facing
  configuration, typed results, subscription generations, diagnostics and
  decoder/service/lighting handoff values.
- `components/vehicle_telemetry/include/mazda/vehicle_telemetry.hpp` declares
  the non-copyable facade and its fixed polling/notification channels.
- `components/local_argb/private_include/local_argb/lighting_sink.hpp` is the
  private value-only lighting sink; no Mazda enum crosses this boundary.

The facade has two fixed subscriber slots per notification channel. Handles
carry channel, slot and generation internally, so a stale handle cannot remove
a later registration. Configuration and subscription mutation are stopped-only
operations. Runtime task and driver ownership is deliberately left to later
stages.

## Deterministic host seams

`tests/support/fake_clock.hpp` provides an injectable monotonic clock and
`tests/support/direct_frame_feeder.hpp` delivers synthetic frames directly to
decoder tests. Decoder/freshness tests use these helpers and do not depend on
the retired capture parser. Capture-format tests were removed with the custom
capture product in S1-D; no parser or replay API remains in test
infrastructure.

## Baseline verification

From a fresh build directory (CMake 3.20+ and a C++17 compiler):

```text
cmake -S . -B /tmp/mazda-can-s0a-build -DMAZDA_BUILD_HOST_TESTS=ON
cmake --build /tmp/mazda-can-s0a-build -j2
ctest --test-dir /tmp/mazda-can-s0a-build --output-on-failure
```

The Stage 0-A host suite builds 8 executables and reports 71 discovered tests
on the baseline toolchain. Physical firmware and isolated-bench checks are not
claimed by host contract tests. No credentials, private captures, or other
sensitive data are included in this change.
