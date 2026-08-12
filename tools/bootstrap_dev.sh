#!/usr/bin/env bash
# Bootstrap the user-space tools that can be installed reproducibly.
# ESP-IDF remains an official upstream installation because it also installs
# the Xtensa toolchain and must be activated in the caller's shell.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
venv="${MCAN_VENV:-${repo_root}/.venv}"

python3 -m venv "${venv}"
"${venv}/bin/python" -m pip install --upgrade "platformio==6.1.18"

cat <<EOF
PlatformIO 6.1.18 is installed in ${venv}.
Activate it before the simulator build:
  source ${venv}/bin/activate

Install and activate ESP-IDF v5.5.4 using the official guide, then run the
same checker from the repository root. Host packages (CMake >=3.20, Ninja,
clang-format-14, a C++17 compiler, Python 3, and ripgrep) should be installed
with the supported package manager for your platform.
EOF

"${venv}/bin/python" "${repo_root}/tools/check_toolchain.py" --scope host
"${venv}/bin/python" "${repo_root}/tools/check_toolchain.py" --scope simulator
