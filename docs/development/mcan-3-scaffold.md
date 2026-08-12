# MCAN-3 scaffold

This change establishes build boundaries for the first firmware and simulator
milestone. It does not decode Mazda signals, record captures, export telemetry,
or provide a CAN data-generation path.

## Pinned toolchains

- T-CAN485 firmware: ESP-IDF `v5.5.4`, target `esp32`.
- Host library and tests: C++17, CMake `3.20`, clang-format major version `14`,
  doctest tag `v2.4.11` at commit
  `ae7a13539fb71f270b87eb2e874fbac80bc8dda2` (MIT license).
- D1 Mini simulator scaffold: PlatformIO `6.1.18`, Espressif8266 platform
  `4.2.1`, package `platformio/framework-arduinoespressif8266` `3.30102.0`
  (Arduino core `3.1.2`).

The ESP-IDF manifest requires exactly `5.5.4`; it does not download an
unrelated SDK at configure time. The host test dependency is fetched by CMake
using the exact doctest commit above. PlatformIO resolves the exact framework
package listed in `platformio.ini`.

Before running a build, check the active executable versions. The checker is
non-interactive and reports all failures in the selected scope:

```text
python3 tools/check_toolchain.py --scope host
python3 tools/check_toolchain.py --scope simulator
python3 tools/check_toolchain.py --scope firmware
```

On Linux, `tools/bootstrap_dev.sh` creates a local virtual environment and
installs exactly PlatformIO Core `6.1.18`, then checks the host and simulator
scopes. It does not use `sudo`, change system packages, or install ESP-IDF;
install and activate ESP-IDF `v5.5.4` with the official Espressif guide before
the firmware check. Set `MCAN_VENV` to choose another virtual-environment
location.

## Reproducible commands

From the repository root:

```text
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

For the simulator scaffold (PlatformIO `6.1.18`), after activating the
bootstrap environment:

```text
cd simulators/d1mini_can_web
pio run
```

For the ESP-IDF application, install and activate ESP-IDF `v5.5.4`, verify the
firmware scope, and run:

```text
cd firmware/tcan485
python3 ../../tools/check_toolchain.py --scope firmware
idf.py set-target esp32
idf.py build
```

## Safety and scope

The MCAN-3 baseline was deliberately a structure-only scaffold. The board
component contains the revision-pending T-CAN485 capability record and applies
fail-closed electrical defaults (CAN TX recessive, boost disabled, and onboard
LED off) before the application stops. MCAN-7 extends that baseline with a
bounded strict listen-only receive path; it still provides no transmit API.
The D1 Mini project is an Arduino setup/loop scaffold with no CAN driver and no
network initialization. Any future active bench simulator must be a separate,
explicitly marked bench-only change.

CI runs the host checks, the D1 Mini PlatformIO build, and the T-CAN485
ESP-IDF build on every pull request. These are compile/configuration checks,
not bench or vehicle validation.


Dependency source, exact version/commit, role, and upstream license links are recorded in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). The upstream notices remain authoritative.
