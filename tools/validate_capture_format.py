#!/usr/bin/env python3
"""Validate the MCAN-5 v1 capture example and golden fixture.

This intentionally validates syntax and lossless RawCanFrame fields only. It
is not the complete capture parser planned for MCAN-9.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


HEADER = "MCAN-CAPTURE 1"
KEY_RE = re.compile(r"^[a-z][a-z0-9_]*$")
TOKEN_RE = re.compile(r"^[^\s=]+=[^\s=]+$")
UINT_RE = re.compile(r"^(0|[1-9][0-9]*)$")
HEX_ID_RE = re.compile(r"^0x[0-9a-f]+$")
PERCENT_RE = re.compile(r"^(?:[A-Za-z0-9._~-]|%[0-9A-F]{2})+$")
DATA_RE = re.compile(r"^[0-9A-Fa-f]+$")


class FormatError(ValueError):
    pass


def uint(value: str, field: str) -> int:
    if not UINT_RE.fullmatch(value):
        raise FormatError(f"{field} is not canonical unsigned decimal: {value!r}")
    return int(value)


def encoded(value: str, field: str) -> None:
    if not PERCENT_RE.fullmatch(value):
        raise FormatError(f"{field} is not canonical percent encoding: {value!r}")


def fields(tokens: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in tokens:
        if not TOKEN_RE.fullmatch(token) or "=" not in token:
            raise FormatError(f"invalid field token: {token!r}")
        key, value = token.split("=", 1)
        if not KEY_RE.fullmatch(key) or not value:
            raise FormatError(f"invalid field: {token!r}")
        if key in result:
            raise FormatError(f"duplicate field: {key}")
        result[key] = value
    return result


def required(values: dict[str, str], names: set[str], kind: str) -> None:
    missing = names - values.keys()
    if missing:
        raise FormatError(f"{kind} missing required fields: {', '.join(sorted(missing))}")


def validate_frame(value: dict[str, str]) -> None:
    required(value, {"t_us", "bus", "id", "format", "rtr", "dlc", "data"}, "FRAME")
    timestamp = uint(value["t_us"], "t_us")
    if timestamp > 0xFFFFFFFFFFFFFFFF:
        raise FormatError("t_us exceeds uint64")
    bus = uint(value["bus"], "bus")
    if bus > 255:
        raise FormatError("bus exceeds uint8")
    if value["format"] not in {"std", "ext"}:
        raise FormatError("format must be std or ext")
    if not HEX_ID_RE.fullmatch(value["id"]):
        raise FormatError("id is not lower-case hexadecimal")
    identifier = int(value["id"], 16)
    expected_digits = 3 if value["format"] == "std" else 8
    if len(value["id"]) != expected_digits + 2:
        raise FormatError("id must use the canonical width for its format")
    if identifier > (0x7FF if value["format"] == "std" else 0x1FFFFFFF):
        raise FormatError("id exceeds selected CAN format")
    if value["rtr"] not in {"0", "1"}:
        raise FormatError("rtr must be 0 or 1")
    dlc = uint(value["dlc"], "dlc")
    if dlc > 8:
        raise FormatError("classic CAN dlc exceeds 8")
    if value["data"] == "-":
        data_length = 0
    elif DATA_RE.fullmatch(value["data"]):
        if len(value["data"]) % 2:
            raise FormatError("data must contain complete bytes")
        data_length = len(value["data"]) // 2
    else:
        raise FormatError("data is not hexadecimal or '-'")
    if value["rtr"] == "1":
        if value["data"] != "-":
            raise FormatError("remote frame must not contain payload")
    elif data_length != dlc:
        raise FormatError("data length does not match dlc")


def validate_stream(text: str) -> None:
    if text.startswith("\ufeff"):
        raise FormatError("UTF-8 BOM is forbidden")
    lines = text.splitlines()
    if not lines or lines[0] != HEADER:
        raise FormatError("first line must be the v1 header")
    session_index = next((i for i, line in enumerate(lines[1:], 1) if line and not line.startswith("#")), None)
    if session_index is None or lines[session_index].split(" ", 1)[0] != "SESSION":
        raise FormatError("SESSION must follow the header")
    session = fields(lines[session_index].split()[1:])
    required(
        session,
        {"firmware", "board", "bitrate_bps", "clock", "clock_unit", "byte_order", "clock_hz", "dropped_frames", "dropped_records"},
        "SESSION",
    )
    for name in ("firmware", "board"):
        encoded(session[name], name)
    if session["clock"] != "monotonic" or session["clock_unit"] != "us" or session["byte_order"] != "big-endian":
        raise FormatError("unsupported v1 session clock or byte order")
    if uint(session["bitrate_bps"], "bitrate_bps") == 0 or uint(session["clock_hz"], "clock_hz") == 0:
        raise FormatError("bitrate_bps and clock_hz must be non-zero")
    uint(session["dropped_frames"], "dropped_frames")
    uint(session["dropped_records"], "dropped_records")
    previous_timestamp: int | None = None
    segment = 0
    for line_number, line in enumerate(lines, 1):
        if line_number == 1 or line_number == session_index + 1 or not line or line.startswith("#"):
            continue
        tokens = line.split()
        kind = tokens[0]
        value = fields(tokens[1:])
        if kind == "FRAME":
            validate_frame(value)
            timestamp = int(value["t_us"])
            if previous_timestamp is not None and timestamp < previous_timestamp:
                raise FormatError(f"line {line_number}: timestamp moved backwards")
            previous_timestamp = timestamp
        elif kind == "DROP":
            required(value, {"t_us", "bus", "count", "reason"}, "DROP")
            uint(value["t_us"], "t_us")
            if value["bus"] != "all" and uint(value["bus"], "bus") > 255:
                raise FormatError("DROP bus exceeds uint8")
            if uint(value["count"], "count") == 0:
                raise FormatError("DROP count must be non-zero")
            encoded(value["reason"], "reason")
        elif kind == "DISCONTINUITY":
            required(value, {"t_us", "bus", "segment", "reason"}, "DISCONTINUITY")
            uint(value["t_us"], "t_us")
            if value["bus"] != "all" and uint(value["bus"], "bus") > 255:
                raise FormatError("DISCONTINUITY bus exceeds uint8")
            new_segment = uint(value["segment"], "segment")
            if new_segment <= segment:
                raise FormatError("DISCONTINUITY segment must increase")
            segment = new_segment
            encoded(value["reason"], "reason")
            previous_timestamp = None
        else:
            # Unknown records are intentionally skipped for forward compatibility.
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", kind):
                raise FormatError(f"line {line_number}: invalid record type")


def extract_example(spec: str) -> str:
    blocks = re.findall(r"```text\n(.*?)```", spec, flags=re.DOTALL)
    complete = [
        block.rstrip("\n")
        for block in blocks
        if block.startswith(HEADER + "\n") and "\nSESSION " in block
    ]
    if len(complete) != 1:
        raise FormatError("expected exactly one complete text example in specification")
    return complete[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    args = parser.parse_args()
    try:
        spec_example = extract_example(args.spec.read_text(encoding="utf-8"))
        fixture = args.fixture.read_text(encoding="utf-8").rstrip("\n")
        if spec_example != fixture:
            raise FormatError("normative example and golden fixture differ")
        validate_stream(fixture)
    except (OSError, UnicodeError, FormatError) as error:
        print(f"capture format validation failed: {error}", file=sys.stderr)
        return 1
    print("capture format v1 example and golden fixture are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
