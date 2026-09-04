#!/usr/bin/env python3
"""Validate local ARGB semantic isolation and fixed WeAct hardware binding."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    root = parser.parse_args().root.resolve()
    failures: list[str] = []

    component = root / "components/local_argb"
    sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in component.rglob("*")
        if path.suffix in {".h", ".hpp", ".cpp"}
    )
    public_header = (component / "include/local_argb/local_argb.h").read_text(encoding="utf-8")
    idf_source = (component / "src/local_argb_idf.cpp").read_text(encoding="utf-8")
    board_header = (root / "components/board/include/board/board_config.h").read_text(
        encoding="utf-8"
    )
    bench_cmake = (root / "firmware/tcan485-bench-ack-only/CMakeLists.txt").read_text(
        encoding="utf-8"
    )

    for forbidden in (
        "RawCanFrame",
        "mazda_candidate",
        "kTurnSwitchId",
        "can_bus/",
        "driver/twai",
        "twai_",
    ):
        if forbidden in sources:
            failures.append(f"local_argb contains forbidden transport/decoder dependency: {forbidden}")
    if '"board/board_config.h"' in public_header or '"led_strip.h"' in public_header:
        failures.append("portable local_argb public API exposes hardware dependencies")

    requirements = {
        "board::kWeActCan485V11.onboard_rgb.data": "central GPIO binding",
        "board::kWeActCan485V11.onboard_rgb.pixel_count": "central pixel-count binding",
        "LED_MODEL_WS2812": "WS2812 model",
        "LED_STRIP_COLOR_COMPONENT_FMT_GRB": "GRB component order",
        "kRmtResolutionHz = 10'000'000": "10 MHz RMT resolution",
        "rmt_config.flags.with_dma = false": "DMA-disabled RMT",
        "xQueueCreateStatic(1": "length-one queue",
        "xQueueOverwrite": "nonblocking overwrite submission",
        "kWorkerPriority = tskIDLE_PRIORITY + 2": "lower-priority worker",
        "g_controller.tick": "independent timeout tick",
        "g_controller.start": "explicit startup black frame",
    }
    for needle, label in requirements.items():
        if needle not in idf_source:
            failures.append(f"{label} is missing: {needle}")
    for needle, label in (("OnboardRgb{4, 1}", "GPIO4/count=1 board record"),):
        if needle not in board_header:
            failures.append(f"{label} is missing: {needle}")
    if '"${CMAKE_CURRENT_LIST_DIR}/../../components"' in bench_cmake:
        failures.append("isolated bench discovers local_argb through the whole components tree")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("local ARGB semantic and hardware boundary validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
