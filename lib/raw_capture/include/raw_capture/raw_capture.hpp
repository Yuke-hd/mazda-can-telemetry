#pragma once

// Host-only capture parsing, canonical writing, and deterministic replay.
// This API has no CAN device, serial, transmit, or injection operations.
#include "raw_capture/capture_reader.hpp"
#include "raw_capture/capture_writer.hpp"
#include "raw_capture/replay.hpp"
