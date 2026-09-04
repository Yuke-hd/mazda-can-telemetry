# MCAN-13 isolated T-CAN485 BENCH_ACK_ONLY

## Purpose and hard boundary

`firmware/tcan485-bench-ack-only` is a separately named ESP-IDF project for an
LILYGO/TTGO T-CAN485 board on an isolated, protected classic-CAN bench. It is
not WeAct CAN485 V1.1 firmware, is not a vehicle artifact, and must never be
connected to a vehicle. The project name, component description, startup
warning, and build guard all carry the `BENCH_ACK_ONLY` label.

The target uses TWAI normal mode so a compliant classic-CAN frame can be
acknowledged by the controller. It accepts frames through the existing
receive-only public API and keeps the hardware TX queue at zero. There is no
business-level frame transmit operation, TWAI handle, diagnostic operation, or
recovery API. The acceptance filter remains receive-all for bench protocol
observation; CAN FD is not enabled or supported.

After startup, the bench application waits on the public receive boundary with
a one-second timeout and drains at most 16 frames per batch. Timeouts are
silent so an idle bench does not spam the serial console. The first received
frame emits an immediate serial proof; subsequent output is aggregated to at
most one summary per second, containing only cumulative/interval counts and
the latest identifier format, identifier, DLC, and bus number. Payload bytes
are never logged. A 10 ms `vTaskDelay` follows every non-empty batch so a
permanently ready receive queue cannot starve the ESP-IDF idle task/watchdog;
`taskYIELD` alone is insufficient. Any non-timeout receive failure is reported
and the CAN component is stopped before the application exits.

The private mode selector requires both `TCAN485_BENCH_ACK_ONLY` and
`TCAN485_BENCH_TARGET`. Any missing or ambiguous guard selects
`TWAI_MODE_LISTEN_ONLY`, so a configuration mistake fails closed. The vehicle
project explicitly forces the guard off and is built as
`weact_can485_v11_vehicle_listen_only`.

## Build and artifact checks

From an ESP-IDF v5.5.4 environment, build each project in its own directory:

```text
python3 tools/validate_weact_vehicle_artifacts.py
cd firmware/weact-can485-v1.1
idf.py set-target esp32
idf.py build
cd ../tcan485-bench-ack-only
idf.py set-target esp32
idf.py build
```

The validation script is a release gate. It checks the project names and
labels, the vehicle listen-only token and zero TX queue, the bench warning and
normal-mode guard, and the absence of transmit calls. Do not rename or package
the bench output as vehicle firmware.

## Physical isolation and test alternatives

Use only a current-limited, isolated bench supply with a correctly terminated
two-ended test bus. Keep the board disconnected from every vehicle harness and
from any unprotected automotive supply. Mark the board and its firmware
artifact `T-CAN485 BENCH_ACK_ONLY — ISOLATED BENCH ONLY`.

For final vehicle behavior, use the strict vehicle/listen-only target or a
one-shot/no-ACK test setup. Those paths do not require an ACK-capable receiver
and preserve the vehicle-side no-transmit boundary. No raw vehicle captures,
VIN, credentials, precise location, or reconstructable trip data belong in
build evidence, Issues, PRs, or releases.

Integrated hardware ACK validation is not claimed by this software change; it
requires the physical bench procedure. It is not evidence for the WeAct V1.1
vehicle board or its always-powered CA-IS2062A receive-only behavior.
