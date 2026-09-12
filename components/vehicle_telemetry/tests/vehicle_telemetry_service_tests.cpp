#include "mazda/vehicle_telemetry.hpp"

#include "mazda/vehicle_telemetry_service.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

class FakeClock final : public vehicle_core::MonotonicClock {
public:
  [[nodiscard]] vehicle_core::MonotonicTimestamp now() const noexcept override {
    return now_us_.load(std::memory_order_relaxed);
  }

  void set(const vehicle_core::MonotonicTimestamp value) noexcept {
    now_us_.store(value, std::memory_order_relaxed);
  }

private:
  std::atomic<vehicle_core::MonotonicTimestamp> now_us_{0};
};

class FakeLightingSink final : public mazda::internal::LightingSink {
public:
  [[nodiscard]] bool publish(const mazda::LightingUpdate &update) noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (size_ < updates_.size())
      updates_[size_++] = update;
    const bool accepted = !fail_next_;
    fail_next_ = false;
    return accepted;
  }

  void fail_next() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    fail_next_ = true;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return size_;
  }

  [[nodiscard]] mazda::LightingUpdate at(const std::size_t index) const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return updates_[index];
  }

private:
  mutable std::mutex mutex_{};
  std::array<mazda::LightingUpdate, 32> updates_{};
  std::size_t size_{0};
  bool fail_next_{false};
};

vehicle_core::RawCanFrame frame(const std::uint32_t identifier,
                                const vehicle_core::MonotonicTimestamp timestamp_us,
                                std::initializer_list<std::uint8_t> bytes) {
  vehicle_core::RawCanFrame result{};
  result.identifier = identifier;
  result.timestamp_us = timestamp_us;
  result.dlc = static_cast<std::uint8_t>(bytes.size());
  std::size_t index = 0;
  for (const auto byte : bytes)
    result.data[index++] = byte;
  return result;
}

struct TurnRecorder final {
  mutable std::mutex mutex{};
  mutable std::condition_variable changed{};
  std::array<mazda::Notification<mazda::TurnState>, 32> notices{};
  std::size_t count{0};
  bool block_left{false};
  bool entered_block{false};
  bool release_block{false};
};

void record_turn(void *context, const mazda::Notification<mazda::TurnState> &notice) noexcept {
  auto &recorder = *static_cast<TurnRecorder *>(context);
  std::unique_lock<std::mutex> lock{recorder.mutex};
  if (recorder.count < recorder.notices.size())
    recorder.notices[recorder.count++] = notice;
  if (recorder.block_left && notice.current.value == mazda::TurnState::Left) {
    recorder.entered_block = true;
    recorder.changed.notify_all();
    recorder.changed.wait(lock, [&recorder] { return recorder.release_block; });
  }
  recorder.changed.notify_all();
}

bool wait_for_count(const TurnRecorder &recorder, const std::size_t count,
                    const std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  std::unique_lock<std::mutex> lock{recorder.mutex};
  return recorder.changed.wait_for(lock, timeout,
                                   [&recorder, count] { return recorder.count >= count; });
}

bool wait_for_lifecycle(const mazda::internal::VehicleTelemetryService &service,
                        const mazda::LifecycleState expected,
                        const std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (service.diagnostics().lifecycle == expected)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return service.diagnostics().lifecycle == expected;
}

bool wait_for_reading(const mazda::internal::VehicleTelemetryService &service,
                      const std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (service.speed_kph().value.has_value())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return service.speed_kph().value.has_value();
}

int failures = 0;

void expect(const bool condition, const char *expression, const char *file, const int line) {
  if (!condition) {
    std::cerr << file << ':' << line << ": failed: " << expression << '\n';
    ++failures;
  }
}

#define EXPECT(condition) expect((condition), #condition, __FILE__, __LINE__)

void test_lifecycle_and_subscription_state() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  auto invalid = config;
  invalid.max_frames_per_batch = 17;
  EXPECT(!service.configure(invalid).ok());

  TurnRecorder recorder{};
  const auto subscription = service.subscribe_turn(&record_turn, &recorder);
  const auto second_subscription = service.subscribe_turn(&record_turn, &recorder);
  EXPECT(subscription.ok());
  EXPECT(service.subscribe_turn(&record_turn, &recorder).status ==
         mazda::ResultCode::CapacityExceeded);
  EXPECT(service.start().ok());
  EXPECT(service.start().status == mazda::ResultCode::AlreadyRunning);
  EXPECT(service.subscribe_turn(&record_turn, &recorder).status == mazda::ResultCode::InvalidState);
  EXPECT(service.unsubscribe(subscription).status == mazda::ResultCode::InvalidState);
  EXPECT(wait_for_count(recorder, 2));
  EXPECT(service.stop().ok());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
  EXPECT(service.configure(config).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_count(recorder, 4));
  EXPECT(service.stop().ok());
  EXPECT(service.unsubscribe(subscription).ok());
  EXPECT(service.unsubscribe(second_subscription).ok());
}

void test_unknown_frame_is_transport_traffic_and_expires() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.transport_silence_timeout_us = 100;
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  EXPECT(service.start().ok());
  clock.set(50);
  EXPECT(source.inject(frame(0x7ff, 50, {0, 0, 0, 0, 0, 0, 0, 0})) == mazda::ResultCode::Ok);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
  while (std::chrono::steady_clock::now() < deadline &&
         service.diagnostics().acquisition.frames_processed == 0)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT(service.diagnostics().acquisition.frames_received == 1);
  EXPECT(service.diagnostics().acquisition.frames_processed == 1);
  EXPECT(service.diagnostics().transport == vehicle_core::TransportHealth::Live);
  clock.set(151);
  EXPECT(service.diagnostics().transport == vehicle_core::TransportHealth::TimedOut);
  EXPECT(service.stop().ok());
}

void test_notifications_coalesce_and_recover() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.transport_silence_timeout_us = 100;
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
  TurnRecorder recorder{};
  EXPECT(service.subscribe_turn(&record_turn, &recorder).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_count(recorder, 1));

  recorder.block_left = true;
  clock.set(10);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 10, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  {
    std::unique_lock<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.changed.wait_for(lock, std::chrono::milliseconds{500},
                                     [&recorder] { return recorder.entered_block; }));
  }

  // The dispatcher is blocked in the user callback while the processing owner
  // accepts two newer semantic states. NotificationChannel keeps one bounded
  // latest notice and marks that transition coalesced.
  clock.set(11);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 11, {0, 0x10, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  clock.set(12);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 12, {0, 0x04, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    recorder.release_block = true;
  }
  recorder.changed.notify_all();
  EXPECT(wait_for_count(recorder, 3));
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.notices[2].current.value == mazda::TurnState::Hazard);
    EXPECT(recorder.notices[2].coalesced);
  }

  // Silence makes an observed value unavailable; a newer frame recovers it.
  clock.set(113);
  const auto timeout_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
  while (std::chrono::steady_clock::now() < timeout_deadline &&
         service.diagnostics().transport != vehicle_core::TransportHealth::TimedOut)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT(service.diagnostics().transport == vehicle_core::TransportHealth::TimedOut);
  EXPECT(wait_for_count(recorder, 4));
  clock.set(114);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 114, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  EXPECT(wait_for_count(recorder, 5));
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.notices[4].recovered);
  }
  EXPECT(service.stop().ok());
}

void test_blocked_callback_does_not_block_polling_and_stop_is_retryable() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 10'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
  TurnRecorder recorder{};
  EXPECT(service.subscribe_turn(&record_turn, &recorder).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_count(recorder, 1));
  recorder.block_left = true;
  clock.set(20);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 20, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  {
    std::unique_lock<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.changed.wait_for(lock, std::chrono::milliseconds{500},
                                     [&recorder] { return recorder.entered_block; }));
  }

  clock.set(21);
  EXPECT(source.inject(frame(mazda::candidate::kEngineDataId, 21,
                             {0x09, 0x5b, 0, 0, 0, 0, 0, 0})) == mazda::ResultCode::Ok);
  EXPECT(wait_for_reading(service));
  EXPECT(service.speed_kph().value.has_value());
  EXPECT(*service.speed_kph().value == 0.0F);

  EXPECT(service.stop().status == mazda::ResultCode::Timeout);
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    recorder.release_block = true;
  }
  recorder.changed.notify_all();
  EXPECT(service.stop().ok());
}

void test_terminal_receive_fault_is_propagated_and_restart_clears_state() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
  TurnRecorder recorder{};
  EXPECT(service.subscribe_turn(&record_turn, &recorder).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_count(recorder, 1));
  clock.set(30);
  EXPECT(source.inject(frame(mazda::candidate::kEngineDataId, 30,
                             {0x09, 0x5b, 0, 0, 0, 0, 0, 0})) == mazda::ResultCode::Ok);
  EXPECT(wait_for_reading(service));
  clock.set(31);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 31, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  EXPECT(wait_for_count(recorder, 2));
  source.fail();
  EXPECT(wait_for_lifecycle(service, mazda::LifecycleState::Faulted));
  EXPECT(wait_for_count(recorder, 3));
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.notices[2].current.value == mazda::TurnState::Left);
    EXPECT(recorder.notices[2].current.availability == mazda::Availability::Unavailable);
    EXPECT(recorder.notices[2].became_unavailable);
  }
  EXPECT(service.stop().status == mazda::ResultCode::Faulted);
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
  EXPECT(service.start().ok());
  EXPECT(service.diagnostics().transport == vehicle_core::TransportHealth::AwaitingTraffic);
  EXPECT(!service.speed_kph().value.has_value());
  EXPECT(service.stop().ok());
}

void test_lighting_startup_black_deadline_heartbeat_and_failure_retry() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
  EXPECT(service.start().ok());
  EXPECT(lighting.size() >= 1);
  EXPECT(lighting.at(0).turn == mazda::TurnState::Unknown);

  clock.set(1);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 1, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  const auto changed_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
  while (std::chrono::steady_clock::now() < changed_deadline && lighting.size() < 2)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT(lighting.size() >= 2);
  const auto left = lighting.at(1);
  EXPECT(left.turn == mazda::TurnState::Left);
  EXPECT(left.valid_until_us == 250'001);

  // A heartbeat is at most 100 ms and carries the earliest semantic or
  // transport deadline even when no new frame arrives.
  clock.set(100'001);
  const auto heartbeat_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
  while (std::chrono::steady_clock::now() < heartbeat_deadline && lighting.size() < 3)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT(lighting.size() >= 3);
  EXPECT(lighting.at(2).turn == mazda::TurnState::Left);
  lighting.fail_next();
  clock.set(200'001);
  // A failed sink call is retried by the independent processing owner; the
  // retry is not tied to user callback progress.
  const auto retry_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
  while (std::chrono::steady_clock::now() < retry_deadline && lighting.size() < 5)
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  EXPECT(lighting.size() >= 5);
  EXPECT(service.stop().ok());
}

void test_public_facade_lifecycle_contract() {
  mazda::VehicleTelemetry telemetry{};
  EXPECT(telemetry.configure(mazda::TelemetryConfig{}).ok());
  EXPECT(telemetry.start().ok());
  EXPECT(telemetry.diagnostics().lifecycle == mazda::LifecycleState::Running);
  EXPECT(telemetry.stop().ok());
  EXPECT(telemetry.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
}

} // namespace

int main() {
  test_lifecycle_and_subscription_state();
  test_unknown_frame_is_transport_traffic_and_expires();
  test_notifications_coalesce_and_recover();
  test_blocked_callback_does_not_block_polling_and_stop_is_retryable();
  test_terminal_receive_fault_is_propagated_and_restart_clears_state();
  test_lighting_startup_black_deadline_heartbeat_and_failure_retry();
  test_public_facade_lifecycle_contract();
  return failures == 0 ? 0 : 1;
}
