#!/usr/bin/env python3
"""Check the public C++ header and target boundaries.

The check is intentionally independent from the project's normal host test
build.  Each public entry point is compiled in its own translation unit and
the compiler's dependency file is inspected; this catches transitive
dependencies even when a header happens to compile successfully.  A small
CMake consumer project then exercises the exported interface of the real
``vehicle_telemetry_contracts`` target.

The checker is diagnostic at this stage of the migration.  The current
baseline is expected to report violations; it is not wired into required CI
until the public/private header split is complete.
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
from dataclasses import dataclass
from typing import Iterable, List, Optional, Sequence, Tuple


@dataclass(frozen=True)
class PublicHeader:
    include: str
    label: str


# These are the application-facing entry points, including the frozen Mazda
# compatibility umbrella.  The compatibility path remains public even while
# its internal handoff includes are being moved behind an internal target.
PUBLIC_HEADERS: Tuple[PublicHeader, ...] = (
    PublicHeader("mazda/vehicle_telemetry.hpp", "vehicle telemetry facade"),
    PublicHeader("mazda/facade_contracts.hpp", "facade contracts"),
    PublicHeader("mazda/reading.hpp", "reading contract"),
    PublicHeader("mazda/notification.hpp", "notification contract"),
    PublicHeader("mazda/telemetry_contracts.hpp", "Mazda compatibility contracts"),
    PublicHeader("vehicle_core/telemetry_contracts.hpp", "public telemetry contracts"),
)


# Match path names rather than source spellings.  These are the boundaries
# that must remain outside every application-facing header's include closure.
# A suffix match keeps the checker usable for a temporary copied fixture root.
FORBIDDEN_DEPENDENCIES: Tuple[Tuple[str, Tuple[str, ...]], ...] = (
    ("raw frame", ("vehicle_core/frame.hpp",)),
    (
        "decoder contracts/definitions",
        ("vehicle_core/decoder_contracts.hpp", "mazda/decoder.hpp", "mazda/definitions.hpp"),
    ),
    ("mutable signal/state", ("vehicle_core/signal.hpp", "mazda/state.hpp")),
    ("notification implementation", ("vehicle_core/notification_channel.hpp",)),
    ("lighting implementation", ("lighting_sink.hpp",)),
    ("board dependency", ("board/board_config.h",)),
    (
        "CAN driver dependency",
        ("can_bus.h", "driver_binding.hpp", "driver/twai.h", "twai.h"),
    ),
    (
        "RTOS/SDK dependency",
        ("freertos/", "freertos.h", "esp_idf/", "esp/", "esp_system.h", "sdkconfig.h"),
    ),
)


@dataclass
class CommandResult:
    returncode: int
    output: str


def _run(command: Sequence[str], *, cwd: Path, timeout: int = 90) -> CommandResult:
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
        return CommandResult(127, str(error))
    output = "\n".join(part for part in (result.stdout, result.stderr) if part).strip()
    return CommandResult(result.returncode, output)


def _compiler_command(requested: Optional[str]) -> Tuple[str, ...]:
    value = requested or os.environ.get("CXX") or "c++"
    command = tuple(shlex.split(value))
    if not command:
        raise ValueError("the C++ compiler command is empty")
    if shutil.which(command[0]) is None and not Path(command[0]).is_file():
        raise FileNotFoundError(f"C++ compiler not found: {command[0]}")
    return command


def _include_dirs(root: Path) -> Tuple[Path, ...]:
    candidates = (
        root / "components/vehicle_telemetry/include",
        root / "lib/mazda/include",
        root / "lib/vehicle_core/include",
    )
    return tuple(path.resolve() for path in candidates if path.is_dir())


def _compiler_is_msvc(compiler: Sequence[str]) -> bool:
    result = _run((*compiler, "--version"), cwd=Path.cwd(), timeout=15)
    text = result.output.lower()
    return result.returncode == 0 and ("microsoft" in text or Path(compiler[0]).name.lower() in {"cl", "cl.exe"})


def _make_tokens(text: str) -> List[str]:
    """Parse a GCC/Clang make-style dependency file.

    Paths containing spaces are escaped by the compiler.  ``shlex`` handles
    those escapes after line continuations are removed.  The target is
    discarded before tokenization so a Windows drive colon cannot be mistaken
    for a dependency separator.
    """

    flattened = text.replace("\\\n", " ")
    separator = -1
    escaped = False
    for index, char in enumerate(flattened):
        if char == ":" and not escaped:
            separator = index
            break
        if char == "\\" and not escaped:
            escaped = True
        else:
            escaped = False
    if separator < 0:
        return []
    try:
        return shlex.split(flattened[separator + 1 :], posix=True)
    except ValueError:
        # A malformed depfile is a boundary failure, not a reason to silently
        # fall back to source-text inspection.
        return []


def _dependency_paths(depfile: Path, cwd: Path) -> Tuple[Path, ...]:
    if not depfile.is_file():
        return ()
    paths: List[Path] = []
    for token in _make_tokens(depfile.read_text(encoding="utf-8")):
        path = Path(token)
        if not path.is_absolute():
            path = cwd / path
        resolved = path.resolve()
        if resolved not in paths:
            paths.append(resolved)
    return tuple(paths)


def _relative_name(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix().lower()
    except ValueError:
        return path.as_posix().lower()


def _dependency_violation(path: Path, root: Path) -> Optional[str]:
    name = _relative_name(path, root)
    # Do not treat the host's temporary directory (often /private/var on
    # macOS) as a repository-private include.  Only the deliberate component
    # marker is a boundary signal.
    try:
        relative_parts = {part.lower() for part in path.resolve().relative_to(root.resolve()).parts}
    except ValueError:
        relative_parts = set()
    if (
        "private_include" in relative_parts
        or "internal_include" in relative_parts
        or "private" in relative_parts
    ):
        return "private/internal include path"
    if name.endswith("mazda/internal_contracts.hpp"):
        return "private internal contract"
    for category, suffixes in FORBIDDEN_DEPENDENCIES:
        if any(name.endswith(suffix) or suffix in name for suffix in suffixes):
            return category
    return None


def _compile_translation_unit(
    compiler: Sequence[str],
    source: Path,
    root: Path,
    include_dirs: Iterable[Path],
    work_dir: Path,
    *,
    depfile_name: str,
    msvc: bool = False,
) -> CommandResult:
    output = work_dir / (source.stem + ".o")
    depfile = work_dir / depfile_name
    if msvc:
        command = [*compiler, "/nologo", "/std:c++17", "/c", str(source), f"/Fo{output}"]
        command.extend(f"/I{path}" for path in include_dirs)
    else:
        command = [
            *compiler,
            "-std=c++17",
            "-fsyntax-only",
            "-MD",
            "-MF",
            str(depfile),
            "-MT",
            str(output),
        ]
        command.extend(f"-I{path}" for path in include_dirs)
        command.append(str(source))
    result = _run(command, cwd=root)
    if not msvc and result.returncode == 0 and not depfile.is_file():
        return CommandResult(1, "compiler succeeded without producing a dependency file")
    return result


def _header_source(header: PublicHeader, directory: Path) -> Path:
    source = directory / (header.include.replace("/", "_") + ".cpp")
    source.write_text(f'#include "{header.include}"\nint main() {{ return 0; }}\n', encoding="utf-8")
    return source


def check_public_headers(root: Path, compiler: Sequence[str], work_dir: Path) -> List[str]:
    failures: List[str] = []
    include_dirs = _include_dirs(root)
    if not include_dirs:
        return ["no public include directories were found"]
    msvc = _compiler_is_msvc(compiler)
    for index, header in enumerate(PUBLIC_HEADERS):
        source = _header_source(header, work_dir)
        depfile = work_dir / f"header_{index}.d"
        result = _compile_translation_unit(
            compiler,
            source,
            root,
            include_dirs,
            work_dir,
            depfile_name=depfile.name,
            msvc=msvc,
        )
        prefix = f"{header.include} ({header.label})"
        if result.returncode != 0:
            detail = result.output[-3000:] if result.output else "compiler failed"
            failures.append(f"{prefix}: compile failed\n{detail}")
            print(f"FAIL {prefix}: compile failed")
            if detail:
                print(f"      {detail}")
            continue
        if msvc:
            # MSVC's /showIncludes format is intentionally not guessed here;
            # the supported host checker uses GCC/Clang dependency files.
            print(f"OK   {prefix}: compiled (MSVC dependency inspection unavailable)")
            continue
        dependencies = _dependency_paths(depfile, root)
        if not dependencies:
            failures.append(f"{prefix}: compiler dependency output was empty or malformed")
            print(f"FAIL {prefix}: dependency output was empty or malformed")
            continue
        violations = []
        for dependency in dependencies:
            category = _dependency_violation(dependency, root)
            if category is not None:
                violations.append((category, _relative_name(dependency, root)))
        if violations:
            unique = list(dict.fromkeys(violations))
            detail = "; ".join(f"{category}: {name}" for category, name in unique)
            failures.append(f"{prefix}: forbidden transitive dependency ({detail})")
            print(f"FAIL {prefix}: forbidden transitive dependency")
            print(f"      {detail}")
        else:
            print(f"OK   {prefix}: compiled with a clean dependency closure")
    return failures


def _cmake_probe_files(probe_dir: Path, root: Path) -> Tuple[Path, Path]:
    source = probe_dir / "consumer.cpp"
    source.write_text(
        '#include "mazda/vehicle_telemetry.hpp"\n'
        '#include "mazda/facade_contracts.hpp"\n'
        '#include "mazda/reading.hpp"\n'
        '#include "mazda/notification.hpp"\n'
        '#include "mazda/telemetry_contracts.hpp"\n'
        '#include "vehicle_core/telemetry_contracts.hpp"\n'
        "int main() { mazda::VehicleTelemetry telemetry; (void)telemetry; return 0; }\n",
        encoding="utf-8",
    )
    cmake = probe_dir / "CMakeLists.txt"
    cmake.write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(public_header_consumer LANGUAGES CXX)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        "set(BUILD_TESTING OFF CACHE BOOL \"\" FORCE)\n"
        "set(MAZDA_BUILD_HOST_TESTS OFF CACHE BOOL \"\" FORCE)\n"
        f'add_subdirectory({json.dumps(root.as_posix())} project)\n'
        f'add_executable(public_header_consumer {json.dumps(source.as_posix())})\n'
        "target_link_libraries(public_header_consumer PRIVATE vehicle_telemetry_contracts)\n",
        encoding="utf-8",
    )
    return cmake, source


def check_consumer_target(root: Path, cmake: str, compiler: Sequence[str], work_dir: Path) -> List[str]:
    failures: List[str] = []
    probe_dir = work_dir / "cmake_probe"
    build_dir = work_dir / "cmake_build"
    probe_dir.mkdir()
    _cmake_probe_files(probe_dir, root)
    configure = [cmake, "-S", str(probe_dir), "-B", str(build_dir), "-DBUILD_TESTING=OFF"]
    if len(compiler) == 1:
        configure.append(f"-DCMAKE_CXX_COMPILER={compiler[0]}")
    configured = _run(configure, cwd=root, timeout=120)
    if configured.returncode != 0:
        detail = configured.output[-3000:] if configured.output else "CMake configure failed"
        failures.append(f"CMake consumer probe configure failed\n{detail}")
        print("FAIL CMake consumer target: configure failed")
        if detail:
            print(f"      {detail}")
        return failures
    built = _run([cmake, "--build", str(build_dir), "--target", "public_header_consumer"], cwd=root, timeout=120)
    if built.returncode != 0:
        detail = built.output[-3000:] if built.output else "CMake build failed"
        failures.append(f"CMake consumer probe build failed\n{detail}")
        print("FAIL CMake consumer target: build failed")
        if detail:
            print(f"      {detail}")
        return failures

    compile_commands = build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        failures.append("CMake consumer probe did not emit compile_commands.json")
        print("FAIL CMake consumer target: no compile_commands.json")
        return failures
    try:
        entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        failures.append(f"CMake consumer probe compile database is unreadable: {error}")
        print("FAIL CMake consumer target: compile database unreadable")
        return failures
    consumer_entries = [entry for entry in entries if Path(entry.get("file", "")).name == "consumer.cpp"]
    if not consumer_entries:
        failures.append("CMake consumer probe did not expose the consumer compile command")
        print("FAIL CMake consumer target: consumer compile command missing")
        return failures
    command_text = " ".join(
        str(value) for entry in consumer_entries for value in (entry.get("command", ""), entry.get("arguments", []))
    ).lower()
    # Check the command rather than CMake source spelling: this is the actual
    # interface consumed by a downstream target.
    if "private_include" in command_text or "internal_include" in command_text:
        failures.append("CMake consumer target exports a private/internal include path")
        print("FAIL CMake consumer target: exported private/internal include path")
    else:
        print("OK   CMake consumer target: isolated public interface compiled")
    return failures


def check_internal_access(
    root: Path, compiler: Sequence[str], work_dir: Path
) -> List[str]:
    failures: List[str] = []
    public_internal = root / "lib/mazda/include/mazda/internal_contracts.hpp"
    authorized_candidates = (
        root / "lib/mazda/internal_include/mazda/internal_contracts.hpp",
        root / "lib/mazda/private_include/mazda/internal_contracts.hpp",
    )
    authorized_internal = next((path for path in authorized_candidates if path.is_file()), None)
    if not public_internal.is_file() and authorized_internal is None:
        print("SKIP internal contract access probes: no mazda/internal_contracts.hpp")
        return failures
    normal_source = work_dir / "internal_normal.cpp"
    normal_source.write_text('#include "mazda/internal_contracts.hpp"\nint main() { return 0; }\n', encoding="utf-8")
    include_dirs = _include_dirs(root)
    normal = _compile_translation_unit(
        compiler,
        normal_source,
        root,
        include_dirs,
        work_dir,
        depfile_name="internal_normal.d",
        msvc=_compiler_is_msvc(compiler),
    )
    if normal.returncode == 0:
        failures.append("normal consumer access to mazda/internal_contracts.hpp unexpectedly succeeded")
        print("FAIL internal contract probe: normal access unexpectedly succeeded")
    else:
        print("OK   internal contract probe: normal access failed as expected")

    authorized_source = work_dir / "internal_authorized.cpp"
    authorized_source.write_text(
        '#include "mazda/internal_contracts.hpp"\nint main() { return 0; }\n', encoding="utf-8"
    )
    authorized_dirs = list(include_dirs)
    # The include argument is mazda/internal_contracts.hpp, so the include
    # directory is the parent of the mazda/ directory, not mazda/ itself.
    explicit_private = (
        authorized_internal.parent.parent
        if authorized_internal is not None
        else public_internal.parent.parent
    )
    if explicit_private not in authorized_dirs:
        authorized_dirs.append(explicit_private)
    authorized = _compile_translation_unit(
        compiler,
        authorized_source,
        root,
        authorized_dirs,
        work_dir,
        depfile_name="internal_authorized.d",
        msvc=_compiler_is_msvc(compiler),
    )
    if authorized.returncode != 0:
        detail = authorized.output[-3000:] if authorized.output else "compiler failed"
        failures.append(f"authorized internal contract probe failed\n{detail}")
        print("FAIL internal contract probe: authorized access failed")
    else:
        mode = (
            "internal include path"
            if authorized_internal is not None
            else "baseline compatibility path"
        )
        print(f"OK   internal contract probe: authorized access succeeded ({mode})")
    return failures


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--compiler", help="C++ compiler command (defaults to $CXX or c++)")
    parser.add_argument("--cmake", default="cmake", help="CMake executable (default: cmake)")
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="keep generated probe files and print their directory",
    )
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f"root is not a directory: {root}")
    if shutil.which(args.cmake) is None and not Path(args.cmake).is_file():
        parser.error(f"CMake not found: {args.cmake}")
    try:
        compiler = _compiler_command(args.compiler)
    except (FileNotFoundError, ValueError) as error:
        parser.error(str(error))

    if args.keep_temp:
        temp_context = None
        work_dir = Path(tempfile.mkdtemp(prefix="mazda-header-boundary-"))
    else:
        temp_context = tempfile.TemporaryDirectory(prefix="mazda-header-boundary-")
        work_dir = Path(temp_context.name)
    try:
        print(f"Public header boundary check: {root}")
        failures = check_public_headers(root, compiler, work_dir)
        failures.extend(check_consumer_target(root, args.cmake, compiler, work_dir))
        failures.extend(check_internal_access(root, compiler, work_dir))
        if args.keep_temp:
            print(f"Probe files: {work_dir}")
        if failures:
            print(f"Header boundary check found {len(failures)} violation(s).", file=sys.stderr)
            for failure in failures:
                print(f"ERROR: {failure}", file=sys.stderr)
            return 1
        print("Header boundary check passed.")
        return 0
    finally:
        if temp_context is not None:
            temp_context.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
