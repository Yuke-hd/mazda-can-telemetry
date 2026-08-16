#!/usr/bin/env python3
"""Validate the structural safety fences around the MCAN-11 bench target."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()

    config = args.config.read_text(encoding="utf-8")
    source = args.source.read_text(encoding="utf-8")
    failures: list[str] = []

    if not re.search(r"default_envs\s*=\s*d1mini", config):
        failures.append("the safe d1mini environment is not the default")
    if not re.search(r"\[env:bench_mcp2515\]", config):
        failures.append("bench_mcp2515 environment is missing")
    if not re.search(r"build_flags\s*=\s*-DMCAN_BENCH_ONLY=1", config):
        failures.append("bench target lacks the MCAN_BENCH_ONLY compile fence")
    if not re.search(r"\+<mcp2515_bench\.cpp>\s+-<main\.cpp>", config):
        failures.append("bench target source filter is not isolated")
    if not re.search(r"\+<main\.cpp>\s+-<mcp2515_bench\.cpp>", config):
        failures.append("default target source filter is not CAN-free")

    required_fragments = (
        '#if !defined(MCAN_BENCH_ONLY) || MCAN_BENCH_ONLY != 1',
        "kSpiFrequencyHz = 1000000UL",
        "kOscillatorFrequencyHz = 8000000UL",
        "kAttemptTimeoutMs = 50UL",
        "kNormalModeOneShot = 0x08",
        "request_one_shot_transmission();",
        "if (configure_controller() && !attempt_made)",
        "No retry is permitted",
    )
    for fragment in required_fragments:
        if fragment not in source:
            failures.append(f"bench source is missing required safety marker: {fragment}")
    if source.count("SPI.transfer(0x81)") != 1:
        failures.append("bench source must issue exactly one TXB0 request command")
    if "vehicle" not in source.lower():
        failures.append("bench source must explicitly forbid vehicle connection")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("MCAN-11 MCP2515 bench target safety structure validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
