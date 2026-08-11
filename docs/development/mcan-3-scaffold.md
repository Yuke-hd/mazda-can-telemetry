# MCAN-3 scaffold

This change establishes build boundaries for the first firmware and simulator
milestone. It does not decode Mazda signals, record captures, export telemetry,
or provide a CAN data-generation path.

## Pinned toolchains

- T-CAN485 firmware: ESP-IDF `v5.5.4`, target `esp32`.
- Host library and tests: C++17, CMake `3.20` or newer, doctest `2.4.11`
  (MIT license).
- D1 Mini simulator scaffold: PlatformIO `6.1.18` or newer, Espressif8266
  platform `4.2.1`, Arduino core package `3.1.2`.

The ESP-IDF version is selected by the local ESP-IDF installation; the firmware
project records the required version in this document and does not download an
unrelated SDK at configure time. The host test dependency is fetched by CMake
using the exact doctest tag above. PlatformIO resolves the exact framework
package listed in `platformio.ini`.

## Reproducible commands

From the repository root:

```text
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

For the simulator scaffold:

```text
cd simulators/d1mini_can_web
pio run
```

For the ESP-IDF application, install ESP-IDF `v5.5.4`, export its environment,
and run:

```text
cd firmware/tcan485
idf.py set-target esp32
idf.py build
```

## Safety and scope

The T-CAN485 component configures TWAI in `TWAI_MODE_LISTEN_ONLY` and exposes
only start, stop, receive, and statistics operations. It has no application
path for sending vehicle frames. The D1 Mini project is a station-mode Web UI
skeleton with no MCP2515 driver and no CAN output. It must remain an isolated
bench-only tool when frame generation is introduced in a later, explicitly
marked change.

No ESP-IDF or PlatformIO hardware build is claimed by this change. No bench or
vehicle validation was performed.
