#pragma once

namespace tcan485_bench_board {

// These helpers belong only to the retired LILYGO/TTGO isolated-bench target.
// They must never be included by the WeAct vehicle firmware.
bool initialize();
void disable();

} // namespace tcan485_bench_board
