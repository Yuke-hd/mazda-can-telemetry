#!/usr/bin/env python3
"""Run project-owned architecture contracts once per host suite.

This host-only gate owns repository-wide checks that cannot live in one
production target: the portable core must build without Mazda or RTOS inputs,
vehicle and isolated-bench bindings must compile and exercise their
project-owned mode contracts, and retired capture code must stay absent. The
existing source-safety validators are run here rather than duplicated in
CTest and firmware CI. Public-header positive/negative checks remain the
separate ``public_header_boundary`` and ``public_header_checker_regression``
gates from Stage 1.5.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable, List, Optional, Sequence, Tuple


class ArchitectureFailure(RuntimeError):
    """A required architecture contract did not pass."""


def _run(command: Sequence[str], *, cwd: Path, timeout: int = 180) -> Tuple[int, str]:
    try:
        result = subprocess.run(
            list(command),
            cwd=str(cwd),
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return 127, str(error)
    output = "\n".join(part for part in (result.stdout, result.stderr) if part).strip()
    return result.returncode, output


def _compiler_command(requested: Optional[str]) -> Tuple[str, ...]:
    command = tuple(shlex.split(requested or os.environ.get("CXX") or "c++"))
    if not command:
        raise ValueError("the C++ compiler command is empty")
    if shutil.which(command[0]) is None and not Path(command[0]).is_file():
        raise FileNotFoundError(f"C++ compiler not found: {command[0]}")
    return command


def _quoted(path: Path) -> str:
    return json.dumps(path.resolve().as_posix())


def _write_core_probe(probe_dir: Path, root: Path) -> Path:
    source = probe_dir / "core_only_consumer.cpp"
    source.write_text(
        '#include "vehicle_core/vehicle_core.hpp"\n'
        '#include "vehicle_core/notification.hpp"\n'
        '#include "vehicle_core/reading.hpp"\n'
        '#include "vehicle_core/telemetry_contracts.hpp"\n'
        "#include <type_traits>\n"
        "static_assert(std::is_trivially_copyable_v<vehicle_core::Reading<float>>);\n"
        "static_assert(std::is_trivially_copyable_v<vehicle_core::Notification<bool>>);\n"
        "int main() {\n"
        "  vehicle_core::RawCanFrame frame{};\n"
        "  return vehicle_core::library_is_available() && frame.is_valid() ? 0 : 1;\n"
        "}\n",
        encoding="utf-8",
    )
    (probe_dir / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(vehicle_core_only_consumer LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 17)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "set(CMAKE_CXX_EXTENSIONS OFF)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        "set(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n"
        "add_subdirectory(" + _quoted(root / "lib/vehicle_core") + " vehicle_core)\n"
        "add_executable(core_only_consumer " + _quoted(source) + ")\n"
        "target_link_libraries(core_only_consumer PRIVATE vehicle_core)\n"
        "target_compile_features(core_only_consumer PRIVATE cxx_std_17)\n",
        encoding="utf-8",
    )
    return source


def _compile_database_source(entry: object) -> Optional[Path]:
    if not isinstance(entry, dict):
        return None
    value = entry.get("file")
    if not isinstance(value, str) or not value:
        return None
    path = Path(value)
    directory = entry.get("directory")
    if not path.is_absolute() and isinstance(directory, str) and directory:
        path = Path(directory) / path
    return path.resolve()


def _command_tokens(entry: object) -> List[str]:
    if not isinstance(entry, dict):
        return []
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(value, str) for value in arguments):
        return list(arguments)
    command = entry.get("command")
    return shlex.split(command) if isinstance(command, str) else []


def _dependency_paths(depfile: Path, cwd: Path) -> Tuple[Path, ...]:
    try:
        flattened = depfile.read_text(encoding="utf-8").replace("\\\n", " ")
    except OSError:
        return ()
    separator = flattened.find(":")
    if separator < 0:
        return ()
    try:
        tokens = shlex.split(flattened[separator + 1 :], posix=True)
    except ValueError:
        return ()
    paths: List[Path] = []
    for token in tokens:
        path = Path(token)
        if not path.is_absolute():
            path = cwd / path
        resolved = path.resolve()
        if resolved not in paths:
            paths.append(resolved)
    return tuple(paths)


def _core_dependency_violations(
    compile_database: Path, source: Path, root: Path, work_dir: Path
) -> List[str]:
    try:
        entries = json.loads(compile_database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"core-only compile database is unreadable: {error}"]
    matching = [entry for entry in entries if _compile_database_source(entry) == source.resolve()]
    if not matching:
        return ["core-only consumer compile command is missing"]

    violations: List[str] = []
    forbidden_parts = (
        "lib/mazda",
        "components/",
        "freertos",
        "esp-idf",
        "esp/",
        "driver/twai",
        "sdkconfig",
    )
    for entry in matching:
        directory = entry.get("directory") if isinstance(entry, dict) else None
        cwd = Path(directory).resolve() if isinstance(directory, str) and directory else root
        tokens = _command_tokens(entry)
        for token in tokens:
            normalized = token.replace("\\", "/").lower()
            if any(part in normalized for part in forbidden_parts):
                violations.append(f"forbidden core-only compile dependency: {token}")
        command_text = " ".join(tokens).replace("\\", "/").lower()
        if "/lib/mazda/" in command_text or "lib/mazda/" in command_text:
            violations.append("core-only consumer compile command mentions Mazda")
        depfile = work_dir / "core_only_consumer.d"
        dependency_probe = _run(
            [*tokens, "-MMD", "-MF", str(depfile), "-MT", str(source)],
            cwd=cwd,
            timeout=120,
        )
        if dependency_probe[0] != 0:
            violations.append(
                "core-only dependency probe failed\n" + dependency_probe[1][-3000:]
            )
            continue
        dependencies = _dependency_paths(depfile, cwd)
        if not dependencies:
            violations.append("core-only dependency probe produced no dependency data")
            continue
        for dependency in dependencies:
            normalized = dependency.as_posix().lower()
            if any(part in normalized for part in forbidden_parts):
                violations.append(f"forbidden core-only dependency: {dependency}")
    return list(dict.fromkeys(violations))


def _check_core_only(root: Path, cmake: str, compiler: Sequence[str], work_dir: Path) -> None:
    probe_dir = work_dir / "core_only_probe"
    probe_dir.mkdir()
    source = _write_core_probe(probe_dir, root)
    configure = [cmake, "-S", str(probe_dir), "-B", str(probe_dir / "build"), "-G", "Ninja"]
    if len(compiler) == 1:
        configure.append(f"-DCMAKE_CXX_COMPILER={compiler[0]}")
    result = _run(configure, cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure("vehicle_core-only configure failed\n" + result[1][-3000:])
    build_dir = probe_dir / "build"
    result = _run([cmake, "--build", str(build_dir), "--target", "core_only_consumer"], cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure("vehicle_core-only build failed\n" + result[1][-3000:])
    executable = build_dir / "core_only_consumer"
    result = _run([str(executable)], cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure("vehicle_core-only consumer execution failed\n" + result[1][-3000:])
    violations = _core_dependency_violations(
        build_dir / "compile_commands.json", source, root, work_dir
    )
    if violations:
        raise ArchitectureFailure("\n".join(violations))
    print("OK   vehicle_core builds and links without Mazda/RTOS dependencies")


def _check_adapter(
    root: Path,
    cmake: str,
    compiler: Sequence[str],
    source_dir: Path,
    work_dir: Path,
) -> None:
    label = source_dir.parent.name
    build_dir = work_dir / f"{label}_adapter_tests"
    configure = [cmake, "-S", str(source_dir), "-B", str(build_dir), "-G", "Ninja"]
    if len(compiler) == 1:
        configure.append(f"-DCMAKE_CXX_COMPILER={compiler[0]}")
    result = _run(configure, cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure(f"{label} adapter configure failed\n" + result[1][-3000:])
    result = _run([cmake, "--build", str(build_dir), "--parallel"], cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure(f"{label} adapter build failed\n" + result[1][-3000:])
    result = _run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"], cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure(f"{label} adapter test failed\n" + result[1][-3000:])
    print(f"OK   {label} project-owned mode adapter compiled and passed")


def _run_validator(root: Path, label: str, command: Sequence[str]) -> None:
    result = _run(command, cwd=root)
    if result[0] != 0:
        raise ArchitectureFailure(f"{label} failed\n" + result[1][-3000:])
    print(f"OK   {label}")


def _check_capture_removal(root: Path) -> None:
    patterns = (
        "raw_capture",
        "validate_capture_format.py",
        "capture_reader",
        "capture_writer",
        "replay.hpp",
    )
    roots: Iterable[Path] = (
        root / "CMakeLists.txt",
        root / ".github/workflows/ci.yml",
        root / "components",
        root / "firmware",
        root / "lib",
        root / "tests",
        root / "tools",
    )
    violations: List[str] = []
    for entry in roots:
        paths = [entry] if entry.is_file() else entry.rglob("*")
        for path in paths:
            if not path.is_file() or path.resolve() == Path(__file__).resolve():
                continue
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".cmake", ".yml"}:
                continue
            text = path.read_text(encoding="utf-8")
            for pattern in patterns:
                if pattern in text:
                    violations.append(f"{path.relative_to(root)} contains retired capture marker {pattern}")
    if violations:
        raise ArchitectureFailure("\n".join(violations))
    print("OK   retired raw_capture product has no active code/build dependency")


def _check_validator_ownership(root: Path) -> None:
    workflow = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    host_cmake = (root / "tests/host/CMakeLists.txt").read_text(encoding="utf-8")
    scripts = (
        "validate_can_receive_only.py",
        "validate_weact_vehicle_artifacts.py",
        "validate_local_argb_boundary.py",
    )
    failures: List[str] = []
    for script in scripts:
        if script in workflow:
            failures.append(f"{script} is directly invoked by CI; architecture_contracts must own it")
        if script in host_cmake:
            failures.append(f"{script} is separately registered in host CTest")
    if failures:
        raise ArchitectureFailure("\n".join(failures))
    print("OK   architecture validators have one consolidated CTest owner")


def check(root: Path, cmake: str, compiler: Sequence[str]) -> int:
    root = root.resolve()
    with tempfile.TemporaryDirectory(prefix="mazda-architecture-") as directory:
        work_dir = Path(directory)
        try:
            _check_core_only(root, cmake, compiler, work_dir)
            _check_adapter(root, cmake, compiler, root / "components/vehicle_can_rx/tests", work_dir)
            _check_adapter(root, cmake, compiler, root / "components/bench_can_ack/tests", work_dir)
            _run_validator(
                root,
                "CAN receive-only safety validator",
                (
                    sys.executable,
                    str(root / "tools/validate_can_receive_only.py"),
                    "--public-header",
                    str(root / "components/can_bus/include/can_bus/can_bus.h"),
                    "--implementation",
                    str(root / "components/can_bus/src/can_bus.cpp"),
                    "--vehicle-binding",
                    str(root / "components/vehicle_can_rx/src/driver_binding.cpp"),
                ),
            )
            _run_validator(
                root,
                "vehicle/bench artifact validator",
                (sys.executable, str(root / "tools/validate_weact_vehicle_artifacts.py"), "--root", str(root)),
            )
            _run_validator(
                root,
                "local ARGB semantic boundary validator",
                (sys.executable, str(root / "tools/validate_local_argb_boundary.py"), "--root", str(root)),
            )
            _check_capture_removal(root)
            _check_validator_ownership(root)
        except (ArchitectureFailure, OSError, ValueError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 1
    print("Architecture contract check passed.")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--compiler", help="C++ compiler command (defaults to $CXX or c++)")
    parser.add_argument("--cmake", default="cmake", help="CMake executable (default: cmake)")
    args = parser.parse_args(argv)
    if shutil.which(args.cmake) is None and not Path(args.cmake).is_file():
        parser.error(f"CMake not found: {args.cmake}")
    try:
        compiler = _compiler_command(args.compiler)
    except (FileNotFoundError, ValueError) as error:
        parser.error(str(error))
    return check(args.root, args.cmake, compiler)


if __name__ == "__main__":
    raise SystemExit(main())
