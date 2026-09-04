# Mazda CAN Telemetry

A receive-only Mazda CX-5 KF CAN telemetry project built around a portable vehicle-state decoder library. The approved product/vehicle target is WeAct Studio CAN485 DevBoard V1.1. Its `firmware/weact-can485-v1.1` image is strict classic-CAN listen-only. The separately named `firmware/tcan485-bench-ack-only` target is reserved for isolated LILYGO/TTGO bench ACK testing and must never be connected to a vehicle. Host-side capture and replay tools support analysis, while a real-time dashboard and independent ARGB frontend remain decoupled from the vehicle side through a versioned protocol.

The current validation vehicle is an Australian-market 2019 Mazda CX-5 Akera with the 2.5T engine, six-speed automatic transmission, AWD, and MRCC. Third-party DBC definitions are candidate leads only; every signal must be validated against this vehicle.

## MCAN-3 build scaffold

The portable C++17 `vehicle_core` library and its host tests are configured at
the repository root. The WeAct V1.1 vehicle firmware lives under
`firmware/weact-can485-v1.1`. See
[`docs/development/mcan-3-scaffold.md`](docs/development/mcan-3-scaffold.md)
for pinned tool versions and reproducible commands.
Candidate `ENGINE_DATA` and `GEAR` decoding, replay vectors, provenance, and
freshness boundaries are documented in
[`docs/development/mcan-14-candidate-decoders.md`](docs/development/mcan-14-candidate-decoders.md).

## Development prerequisites

The following tools are needed to configure, build, format, validate, and test
the repository. The [MCAN-3 scaffold](docs/development/mcan-3-scaffold.md)
contains the complete commands and exact dependency pins; run
`tools/check_toolchain.py --scope <host|firmware|all>` before a
build. Missing tools and version mismatches are reported together with an
official installation link.

| Workflow | Required tool | Supported version or constraint | Purpose and installation |
| --- | --- | --- | --- |
| Host library/tests | Bash/Linux shell, Git, Python 3, ripgrep | Bash and Git current supported releases; Python >= 3.8; ripgrep current supported release | Repository commands and source discovery ([Bash](https://www.gnu.org/software/bash/), [Git](https://git-scm.com/book/en/v2/Getting-Started-Installing-Git), [Python](https://www.python.org/downloads/), [ripgrep](https://github.com/BurntSushi/ripgrep#installation)) |
| Host library/tests | C++17 compiler, CMake, Ninja | C++17; CMake >= 3.20; Ninja current supported release | Configure and build the portable library/tests ([compiler](https://gcc.gnu.org/install/), [CMake](https://cmake.org/download/), [Ninja](https://ninja-build.org/)) |
| Host library/tests | clang-format | Major version 14 (`clang-format-14`) | Enforce C++ formatting ([LLVM documentation](https://clang.llvm.org/docs/ClangFormat.html)) |
| ESP32 firmware targets | ESP-IDF and `idf.py` | Exactly v5.5.4, target `esp32` | Build the WeAct V1.1 strict vehicle listen-only target or the separately named T-CAN485 isolated `BENCH_ACK_ONLY` target ([official guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/)) |

Host tools are required for the common library, formatting, and test workflow;
ESP-IDF and its ESP32 toolchain apply to both firmware targets.
CMake's doctest dependency and ESP-IDF managed components are resolved by their
respective build systems, not installed as separate repository prerequisites.
ESP-IDF must be installed and activated using its upstream guide.

The `firmware/weact-can485-v1.1` target is named
`weact_can485_v11_vehicle_listen_only` and
initializes a bounded strict listen-only CAN acquisition path. It does not
decode, capture, export, or transmit frames. The separately named
`firmware/tcan485-bench-ack-only` target is an LILYGO/TTGO isolated bench
receiver that uses normal mode only to ACK classic-CAN traffic; it has no
public frame-transmission API and must never be connected to a vehicle. See
[`docs/development/mcan-13-bench-ack-only.md`](docs/development/mcan-13-bench-ack-only.md)
for its build, release, and physical-isolation boundary.

The exact WeAct V1.1 pin map, CA-IS2062A/CA-IS2092A/CH343P/FPC-18 identity,
K3 OFF requirement, and evidence limitations are recorded in
[`docs/hardware/weact-can485-v1.1.md`](docs/hardware/weact-can485-v1.1.md).

## Safety Boundary

- Vehicle firmware is CAN listen-only. It performs no control, diagnostic polling, or frame injection.
- Active transmission is permitted only in a physically isolated and prominently marked bench target.
- This project is not a replacement for the factory instrument cluster and must not be used for safety-critical decisions.
- Wiring, termination, power, listen-only behavior, and fail-silent operation must pass bench validation before the first vehicle connection.

The authoritative product and engineering requirements are maintained in
[Notion](https://app.notion.com/p/mazda-can-telemetry-3bab6bac581680bea756f017dc3dc347).
See [CONTRIBUTING.md](CONTRIBUTING.md) for collaboration rules and repository
safety boundaries. The
first formal work item is [repository bootstrap and governance](docs/work-items/0001-repository-bootstrap.md).

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
