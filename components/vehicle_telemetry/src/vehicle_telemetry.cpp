#include "mazda/vehicle_telemetry.hpp"

#include "mazda/publication_store.hpp"

#include <chrono>
#include <new>

namespace mazda::internal {
namespace {

constexpr std::uint64_t kNanosecondsPerMicrosecond = 1'000;

} // namespace

vehicle_core::MonotonicTimestamp SteadyClock::now() const noexcept {
  // steady_clock is monotonic by contract. The cast is intentionally kept
  // local to this implementation seam; public callers only receive values.
  const auto duration = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<vehicle_core::MonotonicTimestamp>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() /
      kNanosecondsPerMicrosecond);
}

PublicationStore::PublicationStore() noexcept : clock_(&steady_clock_) {}

PublicationStore::PublicationStore(vehicle_core::MonotonicClock &clock,
                                   TelemetryConfig config) noexcept
    : clock_(&clock), config_(config) {}

vehicle_core::TransportHealth PublicationStore::effective_transport(
    const vehicle_core::TransportHealth transport,
    const std::optional<vehicle_core::MonotonicTimestamp> transport_reference_us,
    const vehicle_core::Microseconds transport_silence_timeout_us,
    const vehicle_core::MonotonicTimestamp now_us) noexcept {
  if ((transport != vehicle_core::TransportHealth::AwaitingTraffic &&
       transport != vehicle_core::TransportHealth::Live) ||
      !transport_reference_us.has_value()) {
    return transport;
  }

  const auto age_us = now_us >= *transport_reference_us
                          ? now_us - *transport_reference_us
                          : static_cast<vehicle_core::Microseconds>(0);
  if (age_us > transport_silence_timeout_us)
    return vehicle_core::TransportHealth::TimedOut;
  return transport;
}

StatusResult PublicationStore::configure(const TelemetryConfig &config) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  if (published_.diagnostics.lifecycle != LifecycleState::Stopped) {
    return StatusResult{ResultCode::InvalidState};
  }
  config_ = config;
  published_.state.apply_freshness_policy(config_.freshness);
  return StatusResult{ResultCode::Ok};
}

void PublicationStore::publish(
    const VehicleState &state, const Diagnostics &diagnostics,
    const std::optional<vehicle_core::MonotonicTimestamp> last_transport_receive_us) noexcept {
  std::lock_guard<std::mutex> lock{mutex_};
  published_.state = state;
  published_.state.apply_freshness_policy(config_.freshness);
  published_.diagnostics = diagnostics;
  if (diagnostics.transport == vehicle_core::TransportHealth::Live &&
      last_transport_receive_us.has_value() &&
      (!transport_reference_us_.has_value() ||
       *last_transport_receive_us > *transport_reference_us_)) {
    transport_reference_us_ = last_transport_receive_us;
  }
}

void PublicationStore::publish(
    const VehicleState &state, const LifecycleState lifecycle,
    const vehicle_core::TransportHealth transport, const AcquisitionMetrics &acquisition,
    const std::optional<vehicle_core::MonotonicTimestamp> last_transport_receive_us) noexcept {
  Diagnostics diagnostics{};
  diagnostics.lifecycle = lifecycle;
  diagnostics.transport = transport;
  diagnostics.acquisition = acquisition;
  publish(state, diagnostics, last_transport_receive_us);
}

void PublicationStore::reset(const Diagnostics &diagnostics) noexcept {
  const auto reset_time_us = clock_->now();
  std::lock_guard<std::mutex> lock{mutex_};
  published_.state = VehicleState{};
  published_.state.apply_freshness_policy(config_.freshness);
  published_.diagnostics = diagnostics;
  transport_reference_us_ = reset_time_us;
}

Reading<float> PublicationStore::speed_kph() const noexcept {
  return read_signal(&VehicleState::speed_kph, candidate::kEngineDataId,
                     ValidationStatus::Reference);
}

Reading<float> PublicationStore::engine_rpm() const noexcept {
  return read_signal(&VehicleState::engine_rpm, candidate::kEngineDataId,
                     ValidationStatus::Confirmed);
}

Diagnostics PublicationStore::diagnostics() const noexcept {
  Diagnostics result{};
  std::optional<vehicle_core::MonotonicTimestamp> transport_reference_us{};
  TelemetryConfig config{};
  {
    std::lock_guard<std::mutex> lock{mutex_};
    result = published_.diagnostics;
    transport_reference_us = transport_reference_us_;
    config = config_;
  }
  const auto now_us = clock_->now();
  result.transport = effective_transport(result.transport, transport_reference_us,
                                         config.transport_silence_timeout_us, now_us);
  return result;
}

PublishedSnapshot PublicationStore::snapshot() const noexcept {
  PublishedSnapshot result{};
  std::optional<vehicle_core::MonotonicTimestamp> transport_reference_us{};
  TelemetryConfig config{};
  {
    std::lock_guard<std::mutex> lock{mutex_};
    result = published_;
    transport_reference_us = transport_reference_us_;
    config = config_;
  }
  const auto now_us = clock_->now();
  result.diagnostics.transport =
      effective_transport(result.diagnostics.transport, transport_reference_us,
                          config.transport_silence_timeout_us, now_us);
  return result;
}

} // namespace mazda::internal

namespace mazda {

static_assert(sizeof(internal::PublicationStore) <= sizeof(VehicleTelemetry));
static_assert(alignof(internal::PublicationStore) <= alignof(std::max_align_t));

VehicleTelemetry::VehicleTelemetry() noexcept {
  ::new (static_cast<void *>(implementation_storage_)) internal::PublicationStore{};
}

VehicleTelemetry::~VehicleTelemetry() noexcept {
  reinterpret_cast<internal::PublicationStore *>(implementation_storage_)->~PublicationStore();
}

StatusResult VehicleTelemetry::configure(const TelemetryConfig &config) noexcept {
  return reinterpret_cast<internal::PublicationStore *>(implementation_storage_)->configure(config);
}

Reading<float> VehicleTelemetry::speed_kph() const noexcept {
  return reinterpret_cast<const internal::PublicationStore *>(implementation_storage_)->speed_kph();
}

Reading<float> VehicleTelemetry::engine_rpm() const noexcept {
  return reinterpret_cast<const internal::PublicationStore *>(implementation_storage_)
      ->engine_rpm();
}

Diagnostics VehicleTelemetry::diagnostics() const noexcept {
  return reinterpret_cast<const internal::PublicationStore *>(implementation_storage_)
      ->diagnostics();
}

} // namespace mazda
