# Mazda CAN Telemetry

A receive-only Mazda CX-5 KF CAN telemetry project built around a portable vehicle-state decoder library. An ESP32/T-CAN485 exporter listens to and exports vehicle data, supported by an isolated CAN bench simulator. A real-time dashboard and independent ARGB frontend remain decoupled from the vehicle side through a versioned protocol.

The current target vehicle is an Australian-market 2019 Mazda CX-5 Akera with the 2.5T engine, six-speed automatic transmission, AWD, and MRCC. Third-party DBC definitions are candidate leads only; every signal must be validated against this vehicle.

## Safety Boundary

- Vehicle firmware is CAN listen-only. It performs no control, diagnostic polling, or frame injection.
- Active transmission is permitted only in a physically isolated and prominently marked bench target.
- This project is not a replacement for the factory instrument cluster and must not be used for safety-critical decisions.
- Wiring, termination, power, listen-only behavior, and fail-silent operation must pass bench validation before the first vehicle connection.

See [AGENTS.md](AGENTS.md) for the complete project plan and safety rules, and [CONTRIBUTING.md](CONTRIBUTING.md) for collaboration requirements. The first formal work item is [repository bootstrap and governance](docs/work-items/0001-repository-bootstrap.md).

## License and data policy

Project-authored source, documentation, tests, and tooling are licensed under
the [Apache License 2.0](LICENSE). Candidate signal material from
[comma.ai/opendbc](https://github.com/commaai/opendbc) remains under its MIT
license and is attributed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Raw vehicle captures are private analysis data and must not be committed,
attached to Issues, or shared externally. Only a reviewed anonymized fixture
may be published, and it must contain no VIN, credentials, precise location,
absolute timestamp, or reconstructable trip pattern. See the complete
[license and vehicle-data policy](docs/policies/license-and-vehicle-data.md)
and the contributor checklist in [CONTRIBUTING.md](CONTRIBUTING.md).
