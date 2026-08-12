#!/usr/bin/env python3
"""Validate structural receive-only invariants for the vehicle CAN component."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--public-header", type=Path, required=True)
    parser.add_argument("--implementation", type=Path, required=True)
    args = parser.parse_args()

    header = args.public_header.read_text(encoding="utf-8")
    implementation = args.implementation.read_text(encoding="utf-8")
    failures: list[str] = []

    public_functions = re.findall(
        r"^(?:Result|Statistics)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(",
        header,
        flags=re.MULTILINE,
    )
    if public_functions != ["start", "stop", "receive", "statistics"]:
        failures.append(f"unexpected public CAN functions: {public_functions}")

    forbidden_symbols = ("twai_transmit", "twai_transmit_v2")
    for symbol in forbidden_symbols:
        if symbol in implementation or symbol in header:
            failures.append(f"forbidden driver symbol present: {symbol}")

    if "TWAI_MODE_LISTEN_ONLY" not in implementation:
        failures.append("strict listen-only mode is not installed")
    if "TWAI_MODE_NORMAL" in implementation or "TWAI_MODE_NO_ACK" in implementation:
        failures.append("an alternate TWAI mode is present")
    if not re.search(r"general\.tx_queue_len\s*=\s*0\s*;", implementation):
        failures.append("the hardware TX queue is not explicitly disabled")
    if "twai_get_status_info" not in implementation or "twai_status_info_t" not in implementation:
        failures.append("driver loss/error counters are not sampled from TWAI status")
    if implementation.count("set_can_transceiver_power(false)") < 5:
        failures.append("startup and stop paths do not visibly reassert transceiver power off")
    forbidden_public_terms = (
        r"\btwai_handle_t\b",
        r"\bdriver[_ ]?handle\b",
        r"\b(send|transmit|recover|mode)\s*\(",
    )
    for pattern in forbidden_public_terms:
        if re.search(pattern, header, flags=re.IGNORECASE):
            failures.append(f"forbidden public CAN surface matched: {pattern}")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("CAN receive-only structure validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
