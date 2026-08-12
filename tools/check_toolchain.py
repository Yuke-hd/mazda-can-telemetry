#!/usr/bin/env python3
"""Check the tools required by the MCAN-3 build scaffold.

This checker intentionally does not install anything.  It is safe to run in
CI and before a build, and reports every missing or incompatible executable in
one pass so that a developer can fix the environment without guesswork.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Callable, Sequence


Version = tuple[int, ...]


@dataclass(frozen=True)
class Requirement:
    name: str
    executable: str
    arguments: tuple[str, ...]
    scopes: frozenset[str]
    check: Callable[[str], tuple[bool, str]]
    install_hint: str


def _version(text: str) -> Version | None:
    match = re.search(r"(?<!\d)(\d+(?:\.\d+)+)", text)
    return tuple(int(part) for part in match.group(1).split(".")) if match else None


def _at_least(expected: Version) -> Callable[[str], tuple[bool, str]]:
    def check(output: str) -> tuple[bool, str]:
        actual = _version(output)
        if actual is None:
            return False, f"could not parse a version (need >= {'.'.join(map(str, expected))})"
        return actual >= expected, f"reported {'.'.join(map(str, actual))}; need >= {'.'.join(map(str, expected))}"

    return check


def _exact(expected: Version) -> Callable[[str], tuple[bool, str]]:
    def check(output: str) -> tuple[bool, str]:
        actual = _version(output)
        expected_text = ".".join(map(str, expected))
        if actual is None:
            return False, f"could not parse a version (need exactly {expected_text})"
        return actual == expected, f"reported {'.'.join(map(str, actual))}; need exactly {expected_text}"

    return check


def _major_exact(expected: int) -> Callable[[str], tuple[bool, str]]:
    def check(output: str) -> tuple[bool, str]:
        actual = _version(output)
        if actual is None:
            return False, f"could not parse a version (need major version {expected})"
        return actual[0] == expected, f"reported {'.'.join(map(str, actual))}; need major version {expected}"

    return check


def _present(output: str) -> tuple[bool, str]:
    return True, output.strip().splitlines()[0] if output.strip() else "executable responded"


REQUIREMENTS = (
    Requirement("Git", "git", ("--version",), frozenset({"host", "simulator", "firmware"}), _present, "https://git-scm.com/book/en/v2/Getting-Started-Installing-Git"),
    Requirement("Bash", "bash", ("--version",), frozenset({"host", "simulator", "firmware"}), _present, "https://www.gnu.org/software/bash/"),
    Requirement("CMake", "cmake", ("--version",), frozenset({"host"}), _at_least((3, 20)), "https://cmake.org/download/"),
    Requirement("Ninja", "ninja", ("--version",), frozenset({"host"}), _present, "https://ninja-build.org/"),
    Requirement("C++ compiler", "c++", ("--version",), frozenset({"host"}), _present, "https://gcc.gnu.org/install/"),
    Requirement("clang-format", "clang-format-14", ("--version",), frozenset({"host"}), _major_exact(14), "https://clang.llvm.org/docs/ClangFormat.html"),
    Requirement("Python", "python3", ("--version",), frozenset({"host", "simulator", "firmware"}), _at_least((3, 8)), "https://www.python.org/downloads/"),
    Requirement("ripgrep", "rg", ("--version",), frozenset({"host"}), _present, "https://github.com/BurntSushi/ripgrep#installation"),
    Requirement("PlatformIO Core", "pio", ("--version",), frozenset({"simulator"}), _exact((6, 1, 18)), "https://docs.platformio.org/en/stable/core/installation/methods/pypi.html"),
    Requirement("ESP-IDF", "idf.py", ("--version",), frozenset({"firmware"}), _exact((5, 5, 4)), "https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/"),
)


def _run(executable: str, arguments: Sequence[str]) -> tuple[int, str]:
    try:
        result = subprocess.run(
            [executable, *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return 127, str(error)
    return result.returncode, f"{result.stdout}\n{result.stderr}".strip()


def check(scope: str) -> int:
    failures = 0
    print(f"MCAN toolchain check: {scope}")
    for requirement in REQUIREMENTS:
        if scope not in requirement.scopes:
            continue
        path = shutil.which(requirement.executable)
        if path is None:
            print(f"FAIL {requirement.name}: '{requirement.executable}' not found")
            print(f"     Install: {requirement.install_hint}")
            failures += 1
            continue
        return_code, output = _run(path, requirement.arguments)
        if return_code != 0:
            print(f"FAIL {requirement.name}: {output or 'command failed'}")
            print(f"     Install: {requirement.install_hint}")
            failures += 1
            continue
        valid, detail = requirement.check(output)
        marker = "OK" if valid else "FAIL"
        print(f"{marker}   {requirement.name}: {detail}")
        if not valid:
            print(f"     Install: {requirement.install_hint}")
            failures += 1
    if failures:
        print(f"Toolchain check failed: {failures} requirement(s) need attention.", file=sys.stderr)
        return 1
    print("Toolchain check passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--scope",
        choices=("host", "simulator", "firmware", "all"),
        default="all",
        help="requirements to check (default: all)",
    )
    args = parser.parse_args()
    scopes = ("host", "simulator", "firmware") if args.scope == "all" else (args.scope,)
    return max(check(scope) for scope in scopes)


if __name__ == "__main__":
    raise SystemExit(main())
