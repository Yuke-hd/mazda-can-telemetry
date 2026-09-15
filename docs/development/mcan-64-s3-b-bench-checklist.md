# MCAN-64 S3-B isolated bench checklist

This is the separate physical follow-up for the `BENCH_ACK_ONLY` firmware. It
is not part of the WeAct vehicle image and must never be connected to a
vehicle. S3-A software tests and host builds do not establish any item below.

## Exact software record

Record these values on the run sheet before connecting the isolated bench:

| Item | Required value |
| --- | --- |
| Bench project | `firmware/tcan485-bench-ack-only` |
| Bench board | LILYGO/TTGO T-CAN485 |
| CAN mode | TWAI normal mode for hardware ACK observation |
| CAN bitrate | 500 kbit/s |
| Hardware TX queue | 0 |
| ESP-IDF | v5.5.4 |
| Target | `esp32` |
| Source revision | `git rev-parse HEAD` output for the tested checkout |
| Firmware image | SHA-256 and `esptool.py image_info` output for the exact flashed binary |

The vehicle image has a separate record: project
`firmware/weact-can485-v1.1`, WeAct Studio CAN485 DevBoard V1.1, strict
listen-only mode, 500 kbit/s, target `esp32`, and its own exact source/image
revision. Never substitute its revision for the bench image revision.

## Preflight

- [ ] Confirm the two boards and firmware labels are physically distinct:
  `BENCH_ACK_ONLY — ISOLATED BENCH ONLY` for the T-CAN485 board and
  `WeAct CAN485 DevBoard V1.1 — STRICT LISTEN-ONLY` for the vehicle board.
- [ ] Use a current-limited isolated bench supply and a correctly terminated
  two-ended classic-CAN test bus.
- [ ] Keep the T-CAN485 board disconnected from every vehicle harness,
  automotive supply, and unprotected external CAN network.
- [ ] Verify the flashed image hashes against the exact software record above.
- [ ] Verify no private vehicle capture, VIN, credential, location, or trip
  data is copied into the evidence bundle.

## ACK-only procedure

- [ ] Power the isolated bench and confirm the startup warning is visible.
- [ ] Attach a known-good classic-CAN peer that generates valid frames at
  500 kbit/s; do not use the WeAct vehicle board as the peer.
- [ ] Confirm the bench reports a received frame and only metadata (format,
  identifier, DLC, bus number, and bounded counters); payload bytes must not
  appear in logs.
- [ ] Confirm the receiver stays quiet during an idle interval and does not
  transmit a data frame.
- [ ] Confirm a non-timeout receive fault stops CAN and disables the bench
  safely.
- [ ] Power-cycle and repeat once to check the isolated startup path.

## Evidence boundary

The run sheet may record board labels, exact source/image revisions, tool
versions, wiring/termination state, timestamps, pass/fail observations, and
sanitized serial metadata. It must not be used as evidence of vehicle
telemetry correctness, WeAct ACK behavior, CAN-load tolerance, or physical LED
behavior. Those claims require their own explicitly authorized procedure.

The software change does not add SavvyCAN capture infrastructure, a custom
capture adapter, active vehicle transmission, or new signal research.
