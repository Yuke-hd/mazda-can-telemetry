# Mazda CAN Telemetry

A receive-only Mazda CX-5 KF CAN telemetry project built around a portable vehicle-state decoder library. An ESP32/T-CAN485 exporter listens to and exports vehicle data, supported by host-side capture and replay tools. A real-time dashboard and independent ARGB frontend remain decoupled from the vehicle side through a versioned protocol.

The current target vehicle is an Australian-market 2019 Mazda CX-5 Akera with the 2.5T engine, six-speed automatic transmission, AWD, and MRCC. Third-party DBC definitions are candidate leads only; every signal must be validated against this vehicle.

## MCAN-3 build scaffold

The portable C++17 `vehicle_core` library and its host tests are configured at
the repository root. The T-CAN485 application lives under
`firmware/tcan485`. See
[`docs/development/mcan-3-scaffold.md`](docs/development/mcan-3-scaffold.md)
for pinned tool versions and reproducible commands.

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
| T-CAN485 firmware | ESP-IDF and `idf.py` | Exactly v5.5.4, target `esp32` | Configure and build the ESP32 receive-only firmware ([official guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/)) |

Host tools are required for the common library, formatting, and test workflow;
ESP-IDF and its ESP32 toolchain are firmware-only. CMake's doctest dependency
and ESP-IDF managed components are resolved by their respective build systems,
not installed as separate repository prerequisites. ESP-IDF must be installed
and activated using its upstream guide.

The T-CAN485 application initializes a bounded strict listen-only CAN
acquisition path. It does not decode, capture, export, or transmit frames. It is
not a vehicle release artifact.

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
