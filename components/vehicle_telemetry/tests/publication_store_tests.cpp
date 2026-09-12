#include "mazda/publication_store.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "vehicle_core/time.hpp"

namespace {

class FakeClock final : public vehicle_core::MonotonicClock {
public:
  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override {
    return now_us_.load(std::memory_order_relaxed);
  }

  void set(const vehicle_core::MonotonicTimestamp now_us) noexcept {
    now_us_.store(now_us, std::memory_order_relaxed);
  }

private:
  std::atomic<vehicle_core::MonotonicTimestamp> now_us_{0};
};

int failures = 0;

void expect(const bool condition, const char *expression, const char *file, const int line) {
  if (!condition) {
    std::cerr << file << ':' << line << ": failed: " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(condition) expect((condition), #condition, __FILE__, __LINE__)

mazda::Diagnostics diagnostics(const mazda::LifecycleState lifecycle,
                               const vehicle_core::TransportHealth transport,
                               const std::uint64_t frames_received = 0) {
  mazda::Diagnostics result{};
  result.lifecycle = lifecycle;
  result.transport = transport;
  result.acquisition.frames_received = frames_received;
  return result;
}

void test_availability_and_reset() {
  FakeClock clock;
  mazda::TelemetryConfig config{};
  config.freshness.speed_kph_timeout_us = 100;
  // RPM intentionally remains unconfigured: it must report
  // FreshnessUnverified rather than acquiring an invented timeout.
  mazda::internal::PublicationStore store{clock, config};

  store.reset(diagnostics(mazda::LifecycleState::Running,
                          vehicle_core::TransportHealth::AwaitingTraffic));
  EXPECT(store.speed_kph().availability == mazda::Availability::NoData);
  EXPECT(store.engine_rpm().availability == mazda::Availability::NoData);

  mazda::VehicleState state{};
  EXPECT(state.speed_kph.update(42.5F, 10));
  EXPECT(state.engine_rpm.update(2'000.0F, 10));
  store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                   vehicle_core::TransportHealth::Live, 1));

  clock.set(110);
  const auto speed = store.speed_kph();
  const auto rpm = store.engine_rpm();
  EXPECT(speed.availability == mazda::Availability::Fresh);
  EXPECT(rpm.availability == mazda::Availability::FreshnessUnverified);
  EXPECT(speed.validation == mazda::ValidationStatus::Reference);
  EXPECT(rpm.validation == mazda::ValidationStatus::Confirmed);
  EXPECT(speed.value.has_value() && *speed.value == 42.5F);
  EXPECT(rpm.value.has_value() && *rpm.value == 2'000.0F);

  clock.set(111);
  EXPECT(store.speed_kph().availability == mazda::Availability::Stale);
  EXPECT(store.engine_rpm().availability == mazda::Availability::FreshnessUnverified);

  // Transport silence is evaluated from the copied observation watermark, so
  // a stalled publisher cannot keep a live transport (or a value) fresh.
  clock.set(1'000'111);
  EXPECT(store.diagnostics().transport == vehicle_core::TransportHealth::TimedOut);
  EXPECT(store.speed_kph().availability == mazda::Availability::Unavailable);
  clock.set(111);

  // A copied message fault makes both fields unavailable while retaining the
  // last accepted values for diagnostics/consumer display.
  vehicle_core::RawCanFrame malformed{};
  malformed.identifier = mazda::candidate::kEngineDataId;
  malformed.timestamp_us = 20;
  EXPECT(state.observe_message(malformed, vehicle_core::DecodeValidity::Malformed) ==
         mazda::MessageObservationResult::Accepted);
  store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                   vehicle_core::TransportHealth::Live, 2));
  const auto unavailable_message = store.engine_rpm();
  EXPECT(unavailable_message.availability == mazda::Availability::Unavailable);
  EXPECT(unavailable_message.value.has_value() && *unavailable_message.value == 2'000.0F);

  store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                   vehicle_core::TransportHealth::Faulted, 3));
  const auto unavailable_speed = store.speed_kph();
  EXPECT(unavailable_speed.availability == mazda::Availability::Unavailable);
  EXPECT(unavailable_speed.value.has_value() && *unavailable_speed.value == 42.5F);

  // A restart handoff must clear old-run observations before awaiting traffic.
  store.reset(diagnostics(mazda::LifecycleState::Running,
                          vehicle_core::TransportHealth::AwaitingTraffic));
  EXPECT(store.speed_kph().availability == mazda::Availability::NoData);
  EXPECT(!store.speed_kph().value.has_value());
  EXPECT(store.engine_rpm().availability == mazda::Availability::NoData);

  // Configuration is lifecycle-gated by the private handoff, not by a public
  // mutable policy API.
  EXPECT(!store.configure(config).ok());
  store.reset();
  EXPECT(store.configure(config).ok());
}

void test_unrelated_receive_keeps_transport_live() {
  FakeClock clock;
  mazda::TelemetryConfig config{};
  config.transport_silence_timeout_us = 100;
  mazda::internal::PublicationStore store{clock, config};
  store.reset(diagnostics(mazda::LifecycleState::Running,
                          vehicle_core::TransportHealth::AwaitingTraffic));

  mazda::VehicleState state{};
  EXPECT(state.speed_kph.update(8.0F, 10));
  store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                   vehicle_core::TransportHealth::Live, 1), 10);

  // This is a receive-only transport watermark for an unrelated/unsupported
  // frame. It must not modify state, but it does prove the receiver is live.
  clock.set(90);
  store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                   vehicle_core::TransportHealth::Live, 2), 90);
  EXPECT(store.diagnostics().transport == vehicle_core::TransportHealth::Live);
  EXPECT(store.speed_kph().availability == mazda::Availability::FreshnessUnverified);
  EXPECT(store.speed_kph().value.has_value() && *store.speed_kph().value == 8.0F);

  clock.set(191);
  EXPECT(store.diagnostics().transport == vehicle_core::TransportHealth::TimedOut);
  EXPECT(store.speed_kph().availability == mazda::Availability::Unavailable);
}

void test_coherent_snapshot_under_concurrent_publication() {
  FakeClock clock;
  mazda::TelemetryConfig config{};
  config.freshness.speed_kph_timeout_us = 1'000'000;
  config.freshness.engine_rpm_timeout_us = 1'000'000;
  mazda::internal::PublicationStore store{clock, config};
  store.reset(diagnostics(mazda::LifecycleState::Running,
                          vehicle_core::TransportHealth::Live));

  std::atomic<bool> writer_done{false};
  std::atomic<bool> inconsistent{false};
  std::thread writer{[&] {
    mazda::VehicleState state{};
    for (std::uint64_t sequence = 1; sequence <= 50'000; ++sequence) {
      const auto value = static_cast<float>(sequence);
      if (!state.speed_kph.update(value, sequence) ||
          !state.engine_rpm.update(value, sequence)) {
        inconsistent.store(true, std::memory_order_release);
        break;
      }
      store.publish(state, diagnostics(mazda::LifecycleState::Running,
                                       vehicle_core::TransportHealth::Live, sequence));
    }
    writer_done.store(true, std::memory_order_release);
  }};

  std::thread reader{[&] {
    while (!writer_done.load(std::memory_order_acquire)) {
      const auto snapshot = store.snapshot();
      if (!snapshot.state.speed_kph.has_value || !snapshot.state.engine_rpm.has_value)
        continue;
      if (snapshot.state.speed_kph.value != snapshot.state.engine_rpm.value ||
          snapshot.diagnostics.acquisition.frames_received !=
              static_cast<std::uint64_t>(snapshot.state.speed_kph.value)) {
        inconsistent.store(true, std::memory_order_release);
        break;
      }
    }
  }};

  writer.join();
  reader.join();
  EXPECT(!inconsistent.load(std::memory_order_acquire));
  EXPECT(store.speed_kph().value.has_value());
  EXPECT(store.engine_rpm().value.has_value());
}

} // namespace

int main() {
  test_availability_and_reset();
  test_unrelated_receive_keeps_transport_live();
  test_coherent_snapshot_under_concurrent_publication();
  if (failures != 0)
    std::cerr << failures << " publication-store assertion(s) failed\n";
  return failures == 0 ? 0 : 1;
}
