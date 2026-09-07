# Supported host builds

Stage 0-B keeps the host build reproducible across the supported CMake floor
and a modern CMake release. The supported minimum is CMake `3.20`; CI tests
that floor with `3.20.5` and the modern compatibility leg with `4.4.3`.
Both legs use Ninja, C++17, and the exact doctest `v2.5.0` commit pinned in
`tests/host/CMakeLists.txt`.

Run the checker before configuring:

```text
python3 tools/check_toolchain.py --scope host
```

Use a fresh temporary build directory for each verification. The following
commands are the baseline evidence recorded for this change (the build path
may be changed to another empty temporary directory):

```text
cmake -S . -B /tmp/mazda-can-telemetry-host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/mazda-can-telemetry-host --parallel
ctest --test-dir /tmp/mazda-can-telemetry-host --output-on-failure
```

The doctest dependency is fetched by CMake at configure time. Network access
is therefore required on a clean build unless the pinned source is already in
the CMake FetchContent cache. No workaround policy flags are required. If a
compiler, Ninja, network, or CMake leg is unavailable, record that exact
command and mark the leg unavailable rather than claiming it passed.

The firmware builds remain separately pinned to ESP-IDF `v5.5.4`; see the
[MCAN-3 scaffold](mcan-3-scaffold.md) for the isolated vehicle and bench
commands.
