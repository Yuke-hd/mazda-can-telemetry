# MCAN-3 scaffold

This change establishes build boundaries for the first firmware milestone and
portable host library. It does not decode Mazda signals, record captures, export
telemetry, or provide a CAN data-generation path.

## Pinned toolchains

- ESP32 firmware: ESP-IDF `v5.5.4`, target `esp32`.
- Host library and tests: C++17, CMake `3.20`, clang-format major version `14`,
  doctest tag `v2.4.11` at commit
  `ae7a13539fb71f270b87eb2e874fbac80bc8dda2` (MIT license).

The ESP-IDF manifest requires exactly `5.5.4`; it does not download an
unrelated SDK at configure time. The host test dependency is fetched by CMake
using the exact doctest commit above.

Before running a build, check the active executable versions. The checker is
non-interactive and reports all failures in the selected scope:

```text
python3 tools/check_toolchain.py --scope host
python3 tools/check_toolchain.py --scope firmware
```

The checker does not install tools or change system packages. Install and
activate ESP-IDF `v5.5.4` with the official Espressif guide before the firmware
check.

## Reproducible commands

From the repository root:

```text
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

For the ESP-IDF application, install and activate ESP-IDF `v5.5.4`, verify the
firmware scope, and run:

```text
cd firmware/weact-can485-v1.1
python3 ../../tools/check_toolchain.py --scope firmware
idf.py set-target esp32
idf.py build
```

## Safety and scope

The MCAN-3 baseline was deliberately a structure-only scaffold. The board
component now contains the exact WeAct CAN485 DevBoard V1.1 capability record
and applies fail-closed electrical defaults: CAN TX recessive, RS485 disabled,
and the single onboard WS2812B data line low with no new command pulses. MCAN-16
adds the required RMT black frame before CAN startup. MCAN-7 extends that
baseline with a bounded strict listen-only receive path; it still provides no
transmit API.

CI runs the host checks, WeAct vehicle build, and isolated T-CAN485 bench build
on every pull request.
These are compile/configuration checks, not bench or vehicle validation.


Dependency source, exact version/commit, role, and upstream license links are recorded in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). The upstream notices remain authoritative.
