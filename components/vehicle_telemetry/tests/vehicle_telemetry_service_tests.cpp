#include "mazda/vehicle_telemetry.hpp"

#include "mazda/definitions.hpp"
#include "mazda/vehicle_telemetry_internal.hpp"
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

class RestartableSource final : public mazda::internal::AcquisitionSource {
public:
  [[nodiscard]] mazda::ResultCode start() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (running_)
      return mazda::ResultCode::AlreadyRunning;
    if (fail_next_start_) {
      fail_next_start_ = false;
      return mazda::ResultCode::Faulted;
    }
    running_ = true;
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::ResultCode stop() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!running_)
      return mazda::ResultCode::NotRunning;
    running_ = false;
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::internal::SourceReceiveStatus
  receive(vehicle_core::RawCanFrame &, const std::uint32_t timeout_ms) noexcept override {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (!running_)
        return mazda::internal::SourceReceiveStatus::NotStarted;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
    return mazda::internal::SourceReceiveStatus::Timeout;
  }

  [[nodiscard]] mazda::internal::SourceStatistics statistics() const noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    return statistics_;
  }

  void fail_next_start() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    fail_next_start_ = true;
  }

private:
  mutable std::mutex mutex_{};
  mazda::internal::SourceStatistics statistics_{};
  bool running_{false};
  bool fail_next_start_{false};
};

class PartialOwnershipSource final : public mazda::internal::AcquisitionSource {
public:
  [[nodiscard]] mazda::ResultCode start() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (running_)
      return mazda::ResultCode::AlreadyRunning;
    running_ = true;
    if (fail_next_start_) {
      fail_next_start_ = false;
      return mazda::ResultCode::Faulted;
    }
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::ResultCode stop() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    ++stop_calls_;
    if (!running_)
      return mazda::ResultCode::NotRunning;
    if (fail_next_stop_) {
      fail_next_stop_ = false;
      return mazda::ResultCode::Faulted;
    }
    running_ = false;
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::internal::SourceReceiveStatus
  receive(vehicle_core::RawCanFrame &, const std::uint32_t timeout_ms) noexcept override {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (!running_)
        return mazda::internal::SourceReceiveStatus::NotStarted;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{timeout_ms});
    return mazda::internal::SourceReceiveStatus::Timeout;
  }

  [[nodiscard]] mazda::internal::SourceStatistics statistics() const noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    return statistics_;
  }

  void fail_next_start_with_ownership() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    fail_next_start_ = true;
  }

  void fail_next_stop() noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    fail_next_stop_ = true;
  }

  [[nodiscard]] std::size_t stop_calls() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return stop_calls_;
  }

  [[nodiscard]] bool running() const noexcept {
    std::lock_guard<std::mutex> lock{mutex_};
    return running_;
  }

private:
  mutable std::mutex mutex_{};
  mazda::internal::SourceStatistics statistics_{};
  std::size_t stop_calls_{0};
  bool running_{false};
  bool fail_next_start_{false};
  bool fail_next_stop_{false};
};

class ImmediateFaultSource final : public mazda::internal::AcquisitionSource {
public:
  [[nodiscard]] mazda::ResultCode start() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (running_)
      return mazda::ResultCode::AlreadyRunning;
    running_ = true;
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::ResultCode stop() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    running_ = false;
    return mazda::ResultCode::Ok;
  }

  [[nodiscard]] mazda::internal::SourceReceiveStatus receive(vehicle_core::RawCanFrame &,
                                                             std::uint32_t) noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!running_)
      return mazda::internal::SourceReceiveStatus::NotStarted;
    fault_started_.store(true, std::memory_order_release);
    ++statistics_.driver_errors;
    return mazda::internal::SourceReceiveStatus::Fault;
  }

  [[nodiscard]] mazda::internal::SourceStatistics statistics() const noexcept override {
    if (fault_started_.load(std::memory_order_acquire)) {
      while (!release_statistics_.load(std::memory_order_acquire))
        std::this_thread::yield();
    }
    std::lock_guard<std::mutex> lock{mutex_};
    return statistics_;
  }

  [[nodiscard]] bool fault_started() const noexcept {
    return fault_started_.load(std::memory_order_acquire);
  }

  void release_statistics() noexcept { release_statistics_.store(true, std::memory_order_release); }

private:
  mutable std::mutex mutex_{};
  mazda::internal::SourceStatistics statistics_{};
  std::atomic<bool> fault_started_{false};
  std::atomic<bool> release_statistics_{false};
  bool running_{false};
};

class NonIdempotentStopSource final : public mazda::internal::AcquisitionSource {
public:
  [[nodiscard]] mazda::ResultCode start() noexcept override {
    if (stop_called_)
      return mazda::ResultCode::AlreadyRunning;
    const auto result = source_.start();
    if (result == mazda::ResultCode::Ok)
      stop_called_ = false;
    return result;
  }

  [[nodiscard]] mazda::ResultCode stop() noexcept override {
    if (stop_called_)
      return mazda::ResultCode::Faulted;
    stop_called_ = true;
    return source_.stop();
  }

  [[nodiscard]] mazda::internal::SourceReceiveStatus
  receive(vehicle_core::RawCanFrame &frame, const std::uint32_t timeout_ms) noexcept override {
    return source_.receive(frame, timeout_ms);
  }

  [[nodiscard]] mazda::internal::SourceStatistics statistics() const noexcept override {
    return source_.statistics();
  }

  [[nodiscard]] mazda::ResultCode inject(const vehicle_core::RawCanFrame &frame) noexcept {
    return source_.inject(frame);
  }

private:
  mazda::internal::HostAcquisitionSource source_{};
  bool stop_called_{false};
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

struct FrontWiperRecorder final {
  mutable std::mutex mutex{};
  mutable std::condition_variable changed{};
  std::array<mazda::Notification<mazda::FrontWiperPosition>, 8> notices{};
  std::size_t count{0};
};

template <typename T> struct ValidationRecorder final {
  mutable std::mutex mutex{};
  mutable std::condition_variable changed{};
  mazda::ValidationStatus validation{mazda::ValidationStatus::Reference};
  std::size_t count{0};
};

template <typename T>
void record_validation(void *context, const mazda::Notification<T> &notice) noexcept {
  auto &recorder = *static_cast<ValidationRecorder<T> *>(context);
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    recorder.validation = notice.current.validation;
    ++recorder.count;
  }
  recorder.changed.notify_all();
}

void record_front_wiper(void *context,
                        const mazda::Notification<mazda::FrontWiperPosition> &notice) noexcept {
  auto &recorder = *static_cast<FrontWiperRecorder *>(context);
  std::lock_guard<std::mutex> lock{recorder.mutex};
  if (recorder.count < recorder.notices.size())
    recorder.notices[recorder.count++] = notice;
  recorder.changed.notify_all();
}

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

bool wait_for_lighting_count(const FakeLightingSink &lighting, const std::size_t count,
                             const std::chrono::milliseconds timeout = std::chrono::milliseconds{
                                 500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (lighting.size() >= count)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return lighting.size() >= count;
}

template <typename Predicate>
bool wait_for_flag(Predicate predicate,
                   const std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return predicate();
}

template <typename T>
bool wait_for_notification(const ValidationRecorder<T> &recorder,
                           const std::chrono::milliseconds timeout = std::chrono::milliseconds{
                               500}) {
  std::unique_lock<std::mutex> lock{recorder.mutex};
  return recorder.changed.wait_for(lock, timeout, [&recorder] { return recorder.count >= 1; });
}

template <typename T>
mazda::ValidationStatus recorded_validation(const ValidationRecorder<T> &recorder) {
  std::lock_guard<std::mutex> lock{recorder.mutex};
  return recorder.validation;
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

void test_added_poll_and_notify_signals_use_service_workers() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
  FrontWiperRecorder recorder{};
  EXPECT(service.subscribe_front_wiper(&record_front_wiper, &recorder).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_flag([&recorder] {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    return recorder.count >= 1;
  }));

  // The test injects into the existing source seam only; it does not call a
  // decoder, tick, notification dispatcher, or LED update loop.
  clock.set(10);
  EXPECT(source.inject(frame(mazda::candidate::kEngineDataId, 10,
                             {0x09, 0x5b, 0, 0, 0, 0, 0, 0})) == mazda::ResultCode::Ok);
  EXPECT(wait_for_flag([&service] { return service.engine_rpm().value.has_value(); }));
  clock.set(11);
  EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 11, {0, 0, 0x10, 0, 0, 0, 0, 0})) ==
         mazda::ResultCode::Ok);
  EXPECT(wait_for_flag([&recorder] {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    return recorder.count >= 2;
  }));
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    EXPECT(recorder.notices[1].current.value == mazda::FrontWiperPosition::On);
    EXPECT(recorder.notices[1].current.availability == mazda::Availability::FreshnessUnverified);
  }
  EXPECT(service.stop().ok());
}

void test_notifications_preserve_metadata_confidence() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  ValidationRecorder<mazda::SelectorPosition> selector{};
  ValidationRecorder<mazda::ActualGear> actual_gear{};
  ValidationRecorder<mazda::TurnState> turn{};
  ValidationRecorder<bool> hazard{};
  ValidationRecorder<bool> left_turn{};
  ValidationRecorder<bool> right_turn{};
  ValidationRecorder<bool> liftgate{};
  ValidationRecorder<bool> rear_right_door{};
  ValidationRecorder<bool> rear_left_door{};
  ValidationRecorder<bool> front_left_door{};
  ValidationRecorder<bool> front_right_door{};
  ValidationRecorder<bool> doors_unlocked{};
  ValidationRecorder<bool> left_lamp{};
  ValidationRecorder<bool> right_lamp{};
  ValidationRecorder<bool> wiper_low{};
  ValidationRecorder<mazda::FrontWiperPosition> front_wiper{};

  EXPECT(service.subscribe_selector(&record_validation<mazda::SelectorPosition>, &selector).ok());
  EXPECT(service.subscribe_actual_gear(&record_validation<mazda::ActualGear>, &actual_gear).ok());
  EXPECT(service.subscribe_turn(&record_validation<mazda::TurnState>, &turn).ok());
  EXPECT(service.subscribe_hazard(&record_validation<bool>, &hazard).ok());
  EXPECT(service.subscribe_left_turn(&record_validation<bool>, &left_turn).ok());
  EXPECT(service.subscribe_right_turn(&record_validation<bool>, &right_turn).ok());
  EXPECT(service.subscribe_liftgate(&record_validation<bool>, &liftgate).ok());
  EXPECT(service.subscribe_rear_right_door(&record_validation<bool>, &rear_right_door).ok());
  EXPECT(service.subscribe_rear_left_door(&record_validation<bool>, &rear_left_door).ok());
  EXPECT(service.subscribe_front_left_door(&record_validation<bool>, &front_left_door).ok());
  EXPECT(service.subscribe_front_right_door(&record_validation<bool>, &front_right_door).ok());
  EXPECT(service.subscribe_doors_unlocked(&record_validation<bool>, &doors_unlocked).ok());
  EXPECT(service.subscribe_left_lamp(&record_validation<bool>, &left_lamp).ok());
  EXPECT(service.subscribe_right_lamp(&record_validation<bool>, &right_lamp).ok());
  EXPECT(service.subscribe_wiper_low(&record_validation<bool>, &wiper_low).ok());
  EXPECT(service.subscribe_front_wiper(&record_validation<mazda::FrontWiperPosition>, &front_wiper)
             .ok());

  EXPECT(service.start().ok());
  EXPECT(wait_for_notification(selector));
  EXPECT(wait_for_notification(actual_gear));
  EXPECT(wait_for_notification(turn));
  EXPECT(wait_for_notification(hazard));
  EXPECT(wait_for_notification(left_turn));
  EXPECT(wait_for_notification(right_turn));
  EXPECT(wait_for_notification(liftgate));
  EXPECT(wait_for_notification(rear_right_door));
  EXPECT(wait_for_notification(rear_left_door));
  EXPECT(wait_for_notification(front_left_door));
  EXPECT(wait_for_notification(front_right_door));
  EXPECT(wait_for_notification(doors_unlocked));
  EXPECT(wait_for_notification(left_lamp));
  EXPECT(wait_for_notification(right_lamp));
  EXPECT(wait_for_notification(wiper_low));
  EXPECT(wait_for_notification(front_wiper));

  // The initial notifications are NoData, proving that validation evidence is
  // independent from runtime availability and freshness evaluation.
  EXPECT(recorded_validation(selector) == mazda::ValidationStatus::Confirmed);
  EXPECT(recorded_validation(selector) == mazda::candidate::kSelectorDefinition.confidence);
  EXPECT(recorded_validation(actual_gear) == mazda::ValidationStatus::Observed);
  EXPECT(recorded_validation(actual_gear) == mazda::candidate::kActualGearDefinition.confidence);
  EXPECT(recorded_validation(turn) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(turn) == mazda::candidate::kTurnLeftSwitchDefinition.confidence);
  EXPECT(recorded_validation(hazard) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(hazard) == mazda::candidate::kHazardDefinition.confidence);
  EXPECT(recorded_validation(left_turn) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(left_turn) == mazda::candidate::kTurnLeftSwitchDefinition.confidence);
  EXPECT(recorded_validation(right_turn) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(right_turn) ==
         mazda::candidate::kTurnRightSwitchDefinition.confidence);
  EXPECT(recorded_validation(liftgate) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(liftgate) == mazda::candidate::kLiftgateOpenDefinition.confidence);
  EXPECT(recorded_validation(rear_right_door) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(rear_right_door) ==
         mazda::candidate::kRearRightDoorOpenDefinition.confidence);
  EXPECT(recorded_validation(rear_left_door) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(rear_left_door) ==
         mazda::candidate::kRearLeftDoorOpenDefinition.confidence);
  EXPECT(recorded_validation(front_left_door) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(front_left_door) ==
         mazda::candidate::kFrontLeftDoorOpenRhdDefinition.confidence);
  EXPECT(recorded_validation(front_right_door) == mazda::ValidationStatus::Confirmed);
  EXPECT(recorded_validation(front_right_door) ==
         mazda::candidate::kFrontRightDoorOpenRhdDefinition.confidence);
  EXPECT(recorded_validation(doors_unlocked) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(doors_unlocked) ==
         mazda::candidate::kDoorsUnlockedDefinition.confidence);
  EXPECT(recorded_validation(left_lamp) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(left_lamp) ==
         mazda::candidate::kLeftIndicatorLampDefinition.confidence);
  EXPECT(recorded_validation(right_lamp) == mazda::ValidationStatus::Reference);
  EXPECT(recorded_validation(right_lamp) ==
         mazda::candidate::kRightIndicatorLampDefinition.confidence);
  EXPECT(recorded_validation(wiper_low) == mazda::ValidationStatus::Observed);
  EXPECT(recorded_validation(wiper_low) == mazda::candidate::kWiperLowDefinition.confidence);
  EXPECT(recorded_validation(front_wiper) == mazda::ValidationStatus::Observed);
  EXPECT(recorded_validation(front_wiper) == mazda::candidate::kFrontWiperDefinition.confidence);
  EXPECT(service.stop().ok());
}

void test_public_facade_private_lighting_binding() {
  mazda::VehicleTelemetry telemetry{};
  FakeLightingSink lighting;
  EXPECT(mazda::internal::VehicleTelemetryAccess::bind_lighting_sink(telemetry, lighting).ok());
  EXPECT(telemetry.start().ok());
  EXPECT(wait_for_lighting_count(lighting, 1));
  EXPECT(lighting.at(0).turn == mazda::TurnState::Unknown);
  EXPECT(telemetry.stop().ok());
  EXPECT(mazda::internal::VehicleTelemetryAccess::bind_lighting_sink(telemetry, lighting).ok());
  EXPECT(telemetry.start().ok());
  EXPECT(telemetry.stop().ok());
}

void test_lighting_sink_binding_is_stopped_only() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink initial_lighting;
  FakeLightingSink bound_lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, initial_lighting, config};

  EXPECT(service.bind_lighting_sink(bound_lighting).ok());
  EXPECT(service.start().ok());
  EXPECT(wait_for_lighting_count(bound_lighting, 1));
  EXPECT(initial_lighting.size() == 0);
  EXPECT(service.bind_lighting_sink(initial_lighting).status == mazda::ResultCode::InvalidState);
  EXPECT(service.stop().ok());
}

void test_failed_shared_source_start_does_not_stop_existing_owner() {
  FakeClock clock;
  mazda::internal::HostAcquisitionSource source;
  FakeLightingSink first_lighting;
  FakeLightingSink second_lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService first{clock, source, first_lighting, config};
  EXPECT(first.start().ok());

  // A contending service remains stopped and must not release the source
  // owned by the already-running service.
  mazda::internal::VehicleTelemetryService second{clock, source, second_lighting, config};
  EXPECT(second.start().status == mazda::ResultCode::AlreadyRunning);
  EXPECT(second.diagnostics().lifecycle == mazda::LifecycleState::Stopped);

  clock.set(30);
  EXPECT(source.inject(frame(mazda::candidate::kEngineDataId, 30,
                             {0x09, 0x5b, 0, 0, 0, 0, 0, 0})) == mazda::ResultCode::Ok);
  EXPECT(wait_for_reading(first));
  EXPECT(first.diagnostics().lifecycle == mazda::LifecycleState::Running);
  EXPECT(first.stop().ok());
  EXPECT(second.start().ok());
  EXPECT(second.stop().ok());
}

void test_source_start_failure_without_ownership_is_restartable() {
  FakeClock clock;
  RestartableSource source;
  source.fail_next_start();
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  EXPECT(service.start().status == mazda::ResultCode::Faulted);
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
  EXPECT(service.start().ok());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Running);
  EXPECT(service.stop().ok());
}

void test_partial_source_start_cleanup_success_is_restartable() {
  FakeClock clock;
  PartialOwnershipSource source;
  source.fail_next_start_with_ownership();
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  // The source reports failure after acquiring ownership. The service must
  // release that ownership before returning and leave itself restartable.
  EXPECT(service.start().status == mazda::ResultCode::Faulted);
  EXPECT(source.stop_calls() == 1);
  EXPECT(!source.running());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
  EXPECT(service.start().ok());
  EXPECT(service.stop().ok());
}

void test_partial_source_start_cleanup_failure_is_retried_by_stop() {
  FakeClock clock;
  PartialOwnershipSource source;
  source.fail_next_start_with_ownership();
  source.fail_next_stop();
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  // Cleanup failure proves ownership was retained. A later stop must retry the
  // source release and finish the lifecycle once the source accepts it.
  EXPECT(service.start().status == mazda::ResultCode::Faulted);
  EXPECT(source.stop_calls() == 1);
  EXPECT(source.running());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Faulted);
  EXPECT(service.stop().status == mazda::ResultCode::Faulted);
  EXPECT(source.stop_calls() == 2);
  EXPECT(!source.running());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
}

void test_immediate_worker_receive_fault_is_not_overwritten_by_running() {
  FakeClock clock;
  ImmediateFaultSource source;
  FakeLightingSink lighting;
  mazda::TelemetryConfig config{};
  config.callback_stop_timeout_us = 20'000;
  mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};

  EXPECT(service.start().ok());
  EXPECT(wait_for_flag([&source] { return source.fault_started(); }));
  EXPECT(wait_for_lifecycle(service, mazda::LifecycleState::Running));
  source.release_statistics();
  EXPECT(wait_for_lifecycle(service, mazda::LifecycleState::Faulted));
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Faulted);
  EXPECT(service.stop().status == mazda::ResultCode::Faulted);
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
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

void test_callback_stop_timeout_can_retry_after_source_already_stopped() {
  FakeClock clock;
  NonIdempotentStopSource source;
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

  EXPECT(service.stop().status == mazda::ResultCode::Timeout);
  {
    std::lock_guard<std::mutex> lock{recorder.mutex};
    recorder.release_block = true;
  }
  recorder.changed.notify_all();
  EXPECT(service.stop().ok());
  EXPECT(service.diagnostics().lifecycle == mazda::LifecycleState::Stopped);
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

void test_lighting_heartbeat_for_off_unknown_nodata_and_unavailable() {
  // Startup emits Unknown/NoData. A later heartbeat must still refresh the
  // private sink even though there is no actionable turn value.
  {
    FakeClock clock;
    mazda::internal::HostAcquisitionSource source;
    FakeLightingSink lighting;
    mazda::TelemetryConfig config{};
    config.callback_stop_timeout_us = 20'000;
    mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
    EXPECT(service.start().ok());
    EXPECT(wait_for_lighting_count(lighting, 1));
    EXPECT(lighting.at(0).turn == mazda::TurnState::Unknown);
    EXPECT(lighting.at(0).availability == mazda::Availability::NoData);
    clock.set(100'001);
    EXPECT(wait_for_lighting_count(lighting, 2));
    EXPECT(lighting.at(1).turn == mazda::TurnState::Unknown);
    EXPECT(lighting.at(1).availability == mazda::Availability::NoData);
    EXPECT(service.stop().ok());
  }

  // Off is semantically known but does not produce a colour command. It must
  // receive the same bounded private heartbeat as an active turn state.
  {
    FakeClock clock;
    mazda::internal::HostAcquisitionSource source;
    FakeLightingSink lighting;
    mazda::TelemetryConfig config{};
    config.callback_stop_timeout_us = 20'000;
    mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
    EXPECT(service.start().ok());
    EXPECT(wait_for_lighting_count(lighting, 1));
    clock.set(1);
    EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 1, {0, 0, 0, 0, 0, 0, 0, 0})) ==
           mazda::ResultCode::Ok);
    EXPECT(wait_for_lighting_count(lighting, 2));
    EXPECT(lighting.at(1).turn == mazda::TurnState::Off);
    EXPECT(lighting.at(1).availability == mazda::Availability::Fresh);
    clock.set(100'001);
    EXPECT(wait_for_lighting_count(lighting, 3));
    EXPECT(lighting.at(2).turn == mazda::TurnState::Off);
    EXPECT(service.stop().ok());
  }

  // A malformed turn message makes the private value unavailable while the
  // processing owner remains alive. That state also requires a heartbeat.
  {
    FakeClock clock;
    mazda::internal::HostAcquisitionSource source;
    FakeLightingSink lighting;
    mazda::TelemetryConfig config{};
    config.callback_stop_timeout_us = 20'000;
    mazda::internal::VehicleTelemetryService service{clock, source, lighting, config};
    EXPECT(service.start().ok());
    EXPECT(wait_for_lighting_count(lighting, 1));
    clock.set(1);
    EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 1, {0, 0x20, 0, 0, 0, 0, 0, 0})) ==
           mazda::ResultCode::Ok);
    EXPECT(wait_for_lighting_count(lighting, 2));
    EXPECT(source.inject(frame(mazda::candidate::kTurnSwitchId, 2, {0})) == mazda::ResultCode::Ok);
    EXPECT(wait_for_lighting_count(lighting, 3));
    EXPECT(lighting.at(2).turn == mazda::TurnState::Unknown);
    EXPECT(lighting.at(2).availability == mazda::Availability::Unavailable);
    clock.set(100'001);
    EXPECT(wait_for_lighting_count(lighting, 4));
    EXPECT(lighting.at(3).turn == mazda::TurnState::Unknown);
    EXPECT(lighting.at(3).availability == mazda::Availability::Unavailable);
    EXPECT(service.stop().ok());
  }
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
  test_added_poll_and_notify_signals_use_service_workers();
  test_notifications_preserve_metadata_confidence();
  test_lighting_sink_binding_is_stopped_only();
  test_failed_shared_source_start_does_not_stop_existing_owner();
  test_source_start_failure_without_ownership_is_restartable();
  test_partial_source_start_cleanup_success_is_restartable();
  test_partial_source_start_cleanup_failure_is_retried_by_stop();
  test_immediate_worker_receive_fault_is_not_overwritten_by_running();
  test_unknown_frame_is_transport_traffic_and_expires();
  test_notifications_coalesce_and_recover();
  test_blocked_callback_does_not_block_polling_and_stop_is_retryable();
  test_callback_stop_timeout_can_retry_after_source_already_stopped();
  test_terminal_receive_fault_is_propagated_and_restart_clears_state();
  test_lighting_startup_black_deadline_heartbeat_and_failure_retry();
  test_lighting_heartbeat_for_off_unknown_nodata_and_unavailable();
  test_public_facade_private_lighting_binding();
  test_public_facade_lifecycle_contract();
  return failures == 0 ? 0 : 1;
}
