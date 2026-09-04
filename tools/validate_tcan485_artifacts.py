#!/usr/bin/env python3
"""Validate that T-CAN485 vehicle and bench artifacts stay unmistakable."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"{label} is missing: {needle}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    vehicle_cmake = (root / "firmware/tcan485/CMakeLists.txt").read_text(encoding="utf-8")
    vehicle_main = (root / "firmware/tcan485/main/main.cpp").read_text(encoding="utf-8")
    vehicle_component = (
        root / "firmware/tcan485/main/idf_component.yml"
    ).read_text(encoding="utf-8")
    bench_cmake = (
        root / "firmware/tcan485-bench-ack-only/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    bench_main = (
        root / "firmware/tcan485-bench-ack-only/main/main.cpp"
    ).read_text(encoding="utf-8")
    bench_component = (
        root / "firmware/tcan485-bench-ack-only/main/idf_component.yml"
    ).read_text(encoding="utf-8")
    can_source = (root / "components/can_bus/src/can_bus.cpp").read_text(encoding="utf-8")
    mode_source = (root / "components/can_bus/src/driver_mode.cpp").read_text(encoding="utf-8")

    require(vehicle_cmake, "project(tcan485_vehicle_listen_only)", "vehicle project name", failures)
    require(vehicle_cmake, "TCAN485_BENCH_ACK_ONLY OFF", "vehicle build guard", failures)
    require(vehicle_main, "STRICT LISTEN-ONLY", "vehicle startup warning", failures)
    require(vehicle_component, "T-CAN485 vehicle listen-only firmware", "vehicle artifact label", failures)
    require(bench_cmake, "project(tcan485_bench_ack_only)", "bench project name", failures)
    require(bench_cmake, "TCAN485_BENCH_ACK_ONLY ON", "bench build guard", failures)
    can_cmake = (root / "components/can_bus/CMakeLists.txt").read_text(encoding="utf-8")
    require(
        can_cmake,
        'idf_build_get_property(tcan485_app_name PROJECT_NAME)',
        "bench project guard",
        failures,
    )
    require(
        can_cmake,
        'tcan485_app_name STREQUAL "tcan485_bench_ack_only"',
        "bench app-name guard",
        failures,
    )
    require(bench_main, "BENCH_ACK_ONLY", "bench startup warning", failures)
    require(bench_main, "never connect to a vehicle", "bench isolation warning", failures)
    require(bench_main, "can_bus::receive", "bench receive loop", failures)
    require(bench_main, "frame received", "bench serial receipt indication", failures)
    require(bench_main, "count=", "bench receipt count", failures)
    require(bench_main, "kMaxFramesPerBatch", "bench receive batch bound", failures)
    require(bench_main, "kSummaryPeriodTicks", "bench log rate limit", failures)
    require(bench_main, "summary_deadline", "bench summary deadline", failures)
    require(bench_main, "received_count == 1", "bench immediate first receipt", failures)
    require(bench_main, "kSchedulerDelayTicks", "bench scheduler delay duration", failures)
    require(bench_main, "vTaskDelay", "bench scheduler delay", failures)
    require(
        bench_main,
        "taskYIELD alone is not enough",
        "bench scheduler safety rationale",
        failures,
    )
    require(bench_main, "kTimeout", "bench quiet timeout handling", failures)
    require(bench_main, "receive failure", "bench receive failure handling", failures)
    if "frame.data" in bench_main or "frame.data[" in bench_main:
        failures.append("bench receipt log exposes CAN payload bytes")
    require(bench_component, "BENCH_ACK_ONLY", "bench artifact label", failures)

    require(can_source, "TWAI_MODE_LISTEN_ONLY", "vehicle CAN mode", failures)
    require(can_source, "general.tx_queue_len = 0", "CAN TX queue guard", failures)
    if "TWAI_MODE_NORMAL" in can_source or "TWAI_MODE_NO_ACK" in can_source:
        failures.append("vehicle CAN source contains an active or no-ACK mode")
    if "twai_transmit" in can_source or "twai_transmit_v2" in can_source:
        failures.append("CAN source contains a data-frame transmit call")
    require(mode_source, "TCAN485_BENCH_ACK_ONLY", "mode guard", failures)
    require(mode_source, "TCAN485_BENCH_TARGET", "bench target guard", failures)
    require(mode_source, "TWAI_MODE_NORMAL", "bench normal mode", failures)
    require(mode_source, "TWAI_MODE_LISTEN_ONLY", "mode fail-closed branch", failures)
    if "twai_transmit" in mode_source or "twai_transmit_v2" in mode_source:
        failures.append("mode selector contains a data-frame transmit call")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("T-CAN485 vehicle and BENCH_ACK_ONLY artifacts validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
