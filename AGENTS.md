# Agent Manual

## Commands

Run from repository root (Bash/Linux):

```bash
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
mapfile -t sources < <(rg --files lib components firmware simulators tests -g '*.h' -g '*.hpp' -g '*.cpp')
clang-format-14 --dry-run --Werror "${sources[@]}"
python3 tools/validate_capture_format.py --spec docs/protocol/raw-capture-format.md --fixture tests/fixtures/capture/golden-v1.txt
python3 tools/validate_github_app_wrappers.py
git diff --check
```

Run `pio run` from `simulators/d1mini_can_web`. With ESP-IDF v5.5.4 exported, run `idf.py set-target esp32` then `idf.py build` from `firmware/tcan485`.

## Map and workflow

`lib/vehicle_core` is portable C++17. `components` and `firmware/tcan485` use ESP-IDF v5.5.4. Simulator: PlatformIO 6.1.18, Espressif8266 4.2.1, Arduino ESP8266 3.1.2. Tests and fixtures are under `tests`.

Requirements: [Notion](https://app.notion.com/p/mazda-can-telemetry-3bab6bac581680bea756f017dc3dc347). Follow `CONTRIBUTING.md` for MCAN Issues, branches, commits, PRs, and squash merges. Consult `docs/development/mcan-3-scaffold.md`, `docs/hardware/tcan485-board.md`, `docs/protocol/raw-capture-format.md`, and `docs/policies/license-and-vehicle-data.md`.

## Boundaries

Repository artifacts are English. Vehicle firmware is receive-only, exposes no transmit API, and fails silent. Active CAN exists only in prominently marked isolated-bench targets. Never publish raw captures, VIN, location, absolute trip time, credentials, or reconstructable trips. Never inspect credential variables or private-key files; only `.agents/skills/github-app-pr-flow` may consume them. Never use owner authentication for GitHub writes.

## Personas

**Coding:** implement one approved MCAN ticket with minimal files, tests, replay or hardware evidence. Use the `coding` role for GitHub writes. Never self-approve, merge, inspect credentials, or add vehicle transmission.

**Reviewer:** inspect requirements, diff, tests, hardware evidence, privacy, and CAN safety; report actionable findings. Use the `reviewer` role for review writes. Never push fixes, weaken checks, or merge without owner approval.
