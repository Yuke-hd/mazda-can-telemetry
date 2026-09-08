#pragma once

#include <cstdint>

namespace vehicle_core {

// Health vocabulary is value-only and transport-independent. Decoder
// observations add frame identity separately in decoder_contracts.hpp.
enum class TransportHealth : std::uint8_t { AwaitingTraffic, Live, TimedOut, Faulted, Stopped };
enum class MessageHealth : std::uint8_t { Unknown, Healthy, Faulted };
enum class SignalHealth : std::uint8_t { NoData, Available, Unavailable };

} // namespace vehicle_core
