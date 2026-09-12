#!/usr/bin/env python3
"""Keep the ordinary local_argb include and target boundary generic."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    root = args.root.resolve()
    public_root = root / "components/local_argb/include/local_argb"
    cmake = root / "components/local_argb/CMakeLists.txt"
    failures: list[str] = []

    ordinary_headers = sorted(public_root.glob("*.h"))
    ordinary_headers += sorted(public_root.glob("*.hpp"))
    for header in ordinary_headers:
        # legacy_compat.hpp is deliberately not an ordinary renderer header:
        # it is the named, temporary Stage 1 application adapter needed by
        # the current firmware target. Keep the exception explicit and test
        # every other public header as the consumer-facing boundary.
        if header.name == "legacy_compat.hpp":
            continue
        public_text = header.read_text(encoding="utf-8")
        for forbidden in (
            "mazda/",
            "SemanticHealth",
            "SemanticSnapshot",
            "VehicleState",
            "decoder",
        ):
            if forbidden in public_text:
                failures.append(
                    "ordinary local_argb header exposes compatibility detail: "
                    f"{header.name}: {forbidden}"
                )

    cmake_text = cmake.read_text(encoding="utf-8")
    if re.search(r"\bREQUIRES\b[^\n]*\bmazda\b", cmake_text):
        failures.append("IDF local_argb REQUIRES exports Mazda to ordinary consumers")
    if re.search(r"target_link_libraries\(local_argb\s+PUBLIC[^\)]*\bmazda\b", cmake_text):
        failures.append("host local_argb target exports Mazda as a public dependency")

    compiler = shutil.which(args.compiler) or args.compiler
    with tempfile.TemporaryDirectory(prefix="local-argb-boundary-") as directory:
        directory_path = Path(directory)
        probe = directory_path / "probe.cpp"
        object_file = directory_path / "probe.o"
        probe.write_text(
            '#include "local_argb/local_argb.h"\n'
            "int main() { local_argb::Rgb color{}; return color.red; }\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(root / "components/local_argb/include"),
                "-I",
                str(root / "lib/vehicle_core/include"),
                "-c",
                str(probe),
                "-o",
                str(object_file),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            failures.append(f"generic local_argb header failed without Mazda: {result.stderr.strip()}")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("local_argb public boundary validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
