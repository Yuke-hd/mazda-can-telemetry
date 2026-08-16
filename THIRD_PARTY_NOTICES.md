# Third-party notices

This file records third-party material that this repository uses or may
incorporate. It is separate from the project's Apache License 2.0 and does
not relicense third-party material.

## comma.ai/opendbc

- **License:** MIT, as published by the upstream project.
- **Copyright notice:** `Copyright (c) 2020, Comma.ai, Inc.` The exact MIT
  permission and disclaimer text is preserved locally in
  [`third_party/licenses/opendbc-MIT.txt`](third_party/licenses/opendbc-MIT.txt).
- **License text:** <https://github.com/commaai/opendbc/blob/95f3d52f474b677c28fc8f10fef3f2f0386aff92/LICENSE>
- **Repository:** <https://github.com/commaai/opendbc>
- **Exact commit:** `95f3d52f474b677c28fc8f10fef3f2f0386aff92` (`master` at
  access time), accessed 2026-08-16.
- **Source paths used:**
  `opendbc/dbc/mazda_2017.dbc`, `opendbc/car/mazda/carstate.py`,
  `opendbc/car/mazda/values.py`, and `docs/CARS.md` at that commit.
- **Role:** candidate Mazda signal definitions and decoder-reference material;
  see [the MCAN-10 evidence matrix](docs/development/mcan-10-opendbc-signal-evidence.md).
- **Current status:** the evidence matrix is an opendbc-derived field
  extraction distributed as project documentation. No full opendbc source tree,
  DBC file, generated definition, or vehicle capture is vendored. The pin is
  documentation provenance only; CMake, CI, tests, and firmware do not fetch a
  branch or depend on opendbc.
- **Local modifications:** none were made to upstream source. The derived
  matrix is not a redistribution of the full upstream DBC.

Before importing or generating material from opendbc, record the exact upstream
commit, access date, files used, and any local modifications in this file and
in the relevant decoder/evidence document. Preserve the upstream MIT notice and
copyright statement with copied or distributed material. A generated signal
definition remains subject to its source material's license and attribution
requirements; it is not automatically relicensed as Apache-2.0.

Other dependencies must be added here before they are committed, with their
source, exact version or commit, license, and required notices.

## doctest

- **Source:** <https://github.com/doctest/doctest>
- **Exact version/commit:** `v2.4.11`, `ae7a13539fb71f270b87eb2e874fbac80bc8dda2`
- **License:** MIT, upstream [LICENSE.txt](https://github.com/doctest/doctest/blob/v2.4.11/LICENSE.txt)
- **Role/status:** CMake FetchContent host-test dependency; the exact commit is pinned in `tests/host/CMakeLists.txt`.

## ESP-IDF

- **Source:** <https://github.com/espressif/esp-idf/tree/v5.5.4>
- **Exact version:** `v5.5.4`
- **License:** Apache-2.0, upstream [LICENSE](https://github.com/espressif/esp-idf/blob/v5.5.4/LICENSE)
- **Role/status:** required firmware toolchain and manifest dependency; no ESP-IDF source is copied into this repository. The upstream notices remain authoritative.

## PlatformIO Espressif8266 platform

- **Source:** <https://github.com/platformio/platform-espressif8266/tree/v4.2.1>
- **Exact version:** `v4.2.1`
- **License:** Apache-2.0, upstream [LICENSE](https://github.com/platformio/platform-espressif8266/blob/v4.2.1/LICENSE)
- **Role/status:** exact PlatformIO platform for the D1 Mini scaffold in `simulators/d1mini_can_web/platformio.ini`.

## Arduino core for ESP8266

- **Source:** <https://github.com/esp8266/Arduino/tree/3.1.2>
- **Exact upstream version:** `3.1.2`; PlatformIO package: `platformio/framework-arduinoespressif8266` `3.30102.0`
- **License:** LGPL-2.1, upstream [LICENSE](https://github.com/esp8266/Arduino/blob/3.1.2/LICENSE)
- **Role/status:** framework package resolved by the D1 Mini scaffold; no Arduino core source is copied into this repository. The upstream notices remain authoritative.

## GitHub Actions checkout

- **Source:** <https://github.com/actions/checkout>
- **Exact version/commit:** `v4.2.2`, `11bd71901bbe5b1630ceea73d27597364c9af683`
- **License:** MIT, upstream [LICENSE](https://github.com/actions/checkout/blob/v4.2.2/LICENSE)
- **Role/status:** pinned CI checkout action in `.github/workflows/ci.yml`.

No third-party source files, vehicle captures, credentials, or private data are
included by this scaffold.
