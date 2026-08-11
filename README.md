# Mazda CAN Telemetry

A receive-only Mazda CX-5 KF CAN telemetry project built around a portable vehicle-state decoder library. An ESP32/T-CAN485 exporter listens to and exports vehicle data, supported by an isolated CAN bench simulator. A real-time dashboard and independent ARGB frontend remain decoupled from the vehicle side through a versioned protocol.

The current target vehicle is an Australian-market 2019 Mazda CX-5 Akera with the 2.5T engine, six-speed automatic transmission, AWD, and MRCC. Third-party DBC definitions are candidate leads only; every signal must be validated against this vehicle.

## MCAN-3 build scaffold

The portable C++17 `vehicle_core` library and its host tests are configured at
the repository root. The T-CAN485 application lives under
`firmware/tcan485`, and the isolated-bench D1 Mini Web simulator scaffold lives
under `simulators/d1mini_can_web`. See
[`docs/development/mcan-3-scaffold.md`](docs/development/mcan-3-scaffold.md)
for pinned tool versions and reproducible commands.

The current T-CAN485 application is deliberately a structure-only skeleton; it
does not initialize CAN, decode, capture, or export frames. The D1 Mini project
only establishes an Arduino setup/loop boundary; it does not initialize a
network or produce CAN frames. Neither is a vehicle release artifact.

## Safety Boundary

- Vehicle firmware is CAN listen-only. It performs no control, diagnostic polling, or frame injection.
- Active transmission is permitted only in a physically isolated and prominently marked bench target.
- This project is not a replacement for the factory instrument cluster and must not be used for safety-critical decisions.
- Wiring, termination, power, listen-only behavior, and fail-silent operation must pass bench validation before the first vehicle connection.

See [AGENTS.md](AGENTS.md) for the complete project plan and safety rules, and [CONTRIBUTING.md](CONTRIBUTING.md) for collaboration requirements. The first formal work item is [repository bootstrap and governance](docs/work-items/0001-repository-bootstrap.md).

> No project license has been selected. The repository is public by explicit project decision; review licensing, opendbc attribution, and vehicle-data anonymization before publishing sensitive material.
