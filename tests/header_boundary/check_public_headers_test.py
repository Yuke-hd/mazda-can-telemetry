#!/usr/bin/env python3
"""Exercise the public-header checker against temporary fixture mutations."""

from __future__ import annotations

import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools/check_public_headers.py"
FIXTURE = Path(__file__).resolve().parent / "fixtures/clean"


def run_checker(root: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root)],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        timeout=180,
    )


class PublicHeaderCheckerTests(unittest.TestCase):
    def copy_fixture(self, temporary: Path) -> Path:
        destination = temporary / "fixture"
        shutil.copytree(FIXTURE, destination)
        return destination

    def test_clean_fixture_and_access_probes_pass(self) -> None:
        with tempfile.TemporaryDirectory(prefix="header-boundary-test-") as directory:
            result = run_checker(self.copy_fixture(Path(directory)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Header boundary check passed", result.stdout)

    def test_transitive_forbidden_include_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="header-boundary-test-") as directory:
            root = self.copy_fixture(Path(directory))
            frame = root / "lib/vehicle_core/include/vehicle_core/frame.hpp"
            frame.write_text("#pragma once\nstruct RawCanFrame {};\n", encoding="utf-8")
            facade = root / "lib/mazda/include/mazda/facade_contracts.hpp"
            facade.write_text(
                '#include "vehicle_core/frame.hpp"\n' + facade.read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            result = run_checker(root)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("raw frame", output)

    def test_exported_private_path_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="header-boundary-test-") as directory:
            root = self.copy_fixture(Path(directory))
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8")
                + "\ntarget_include_directories(vehicle_telemetry_contracts INTERFACE\n"
                + "  ${CMAKE_CURRENT_SOURCE_DIR}/lib/mazda/private_include)\n",
                encoding="utf-8",
            )
            result = run_checker(root)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("exported private/internal include path", output)

    def test_exported_internal_path_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="header-boundary-test-") as directory:
            root = self.copy_fixture(Path(directory))
            source = root / "lib/mazda/private_include/mazda/internal_contracts.hpp"
            destination = root / "lib/mazda/internal_include/mazda/internal_contracts.hpp"
            destination.parent.mkdir(parents=True)
            destination.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                cmake.read_text(encoding="utf-8")
                + "\ntarget_include_directories(vehicle_telemetry_contracts INTERFACE\n"
                + "  ${CMAKE_CURRENT_SOURCE_DIR}/lib/mazda/internal_include)\n",
                encoding="utf-8",
            )
            result = run_checker(root)
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("exported private/internal include path", output)


if __name__ == "__main__":
    unittest.main()
