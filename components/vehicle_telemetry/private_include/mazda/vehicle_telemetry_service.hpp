#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#if !defined(ESP_PLATFORM)
#include <chrono>
#include <condition_variable>
#include <thread>
#endif

#include "mazda/decoder.hpp"
#include "mazda/internal_contracts.hpp"
#include "mazda/publication_store.hpp"
#include "vehicle_core/notification_channel.hpp"

namespace mazda::internal {

class VehicleTelemetryService;

enum class SourceReceiveStatus : std::uint8_t { Frame, Timeout, Fault, NotStarted };

struct SourceStatistics final {
  std::uint64_t frames_received{0};
  std::uint64_t frames_dropped{0};
  std::uint64_t queue_overflows{0};
  std::uint64_t driver_errors{0};
  std::uint64_t missed_frames{0};
  std::uint64_t controller_resets{0};
  std::uint64_t bus_off_events{0};
};

// Acquisition is an explicit adapter boundary. The real adapter below maps to
// the already-tested can_bus receiver on ESP-IDF; the host adapter is a fixed
// test seam. Neither adapter exposes a second receive engine to the service.
class AcquisitionSource {
public:
  virtual ~AcquisitionSource() = default;
  [[nodiscard]] virtual ResultCode start() noexcept = 0;
  [[nodiscard]] virtual ResultCode stop() noexcept = 0;
  [[nodiscard]] virtual SourceReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                                    std::uint32_t timeout_ms) noexcept = 0;
  [[nodiscard]] virtual SourceStatistics statistics() const noexcept = 0;
};

// Fixed-capacity host acquisition source used by service tests. Injection is
// deliberately private to the internal header; no public manual pump or frame
// API is added to VehicleTelemetry.
#if !defined(ESP_PLATFORM)
class HostAcquisitionSource final : public AcquisitionSource {
public:
  static constexpr std::size_t kCapacity = 64;

  [[nodiscard]] ResultCode start() noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    if (running_)
      return ResultCode::AlreadyRunning;
    head_ = 0;
    tail_ = 0;
    statistics_ = SourceStatistics{};
    running_ = true;
    faulted_ = false;
    return ResultCode::Ok;
  }

  [[nodiscard]] ResultCode stop() noexcept override {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      running_ = false;
    }
    available_.notify_all();
    return ResultCode::Ok;
  }

  [[nodiscard]] SourceReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                            const std::uint32_t timeout_ms) noexcept override {
    std::unique_lock<std::mutex> lock{mutex_};
    const auto ready = [this] { return !frames_empty() || faulted_ || !running_; };
    if (!ready()) {
      if (timeout_ms == 0) {
        return SourceReceiveStatus::Timeout;
      }
      (void)available_.wait_for(lock, std::chrono::milliseconds{timeout_ms}, ready);
    }
    if (faulted_)
      return SourceReceiveStatus::Fault;
    if (!running_)
      return SourceReceiveStatus::NotStarted;
    if (frames_empty())
      return SourceReceiveStatus::Timeout;

    frame = frames_[tail_ % kCapacity];
    ++tail_;
    return SourceReceiveStatus::Frame;
  }

  [[nodiscard]] SourceStatistics statistics() const noexcept override {
    std::lock_guard<std::mutex> lock{mutex_};
    return statistics_;
  }

  // Test-only direct receive injection. It models can_bus delivery after the
  // source has been started and updates transport counters for every frame,
  // including unrelated identifiers.
  [[nodiscard]] ResultCode inject(const vehicle_core::RawCanFrame &frame) noexcept {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (!running_)
        return ResultCode::NotRunning;
      ++statistics_.frames_received;
      if ((head_ - tail_) >= kCapacity) {
        ++statistics_.frames_dropped;
        ++statistics_.queue_overflows;
        return ResultCode::CapacityExceeded;
      }
      frames_[head_ % kCapacity] = frame;
      ++head_;
    }
    available_.notify_one();
    return ResultCode::Ok;
  }

  void fail() noexcept {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      faulted_ = true;
      ++statistics_.driver_errors;
    }
    available_.notify_all();
  }

private:
  [[nodiscard]] bool frames_empty() const noexcept { return head_ == tail_; }

  mutable std::mutex mutex_{};
  std::condition_variable available_{};
  std::array<vehicle_core::RawCanFrame, kCapacity> frames_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  SourceStatistics statistics_{};
  bool running_{false};
  bool faulted_{false};
};
#endif

#if defined(ESP_PLATFORM)

class CanBusAcquisitionSource final : public AcquisitionSource {
public:
  [[nodiscard]] ResultCode start() noexcept override;
  [[nodiscard]] ResultCode stop() noexcept override;
  [[nodiscard]] SourceReceiveStatus receive(vehicle_core::RawCanFrame &frame,
                                            std::uint32_t timeout_ms) noexcept override;
  [[nodiscard]] SourceStatistics statistics() const noexcept override;
};

#endif

// Generic lighting is a private sink. local_argb owns policy/driver details in
// S2-B; this service only sends bounded semantic updates and a validity
// deadline. Tests provide a fake sink without adding a production LED path.
class LightingSink {
public:
  virtual ~LightingSink() = default;
  [[nodiscard]] virtual bool publish(const LightingUpdate &update) noexcept = 0;
};

class NullLightingSink final : public LightingSink {
public:
  [[nodiscard]] bool publish(const LightingUpdate &) noexcept override { return true; }
};

inline constexpr std::uint16_t kSelectorNotificationChannel = 1;
inline constexpr std::uint16_t kActualGearNotificationChannel = 2;
inline constexpr std::uint16_t kTurnNotificationChannel = 3;
inline constexpr std::uint16_t kHazardNotificationChannel = 4;
inline constexpr std::uint16_t kLeftTurnNotificationChannel = 5;
inline constexpr std::uint16_t kRightTurnNotificationChannel = 6;
inline constexpr std::uint16_t kLiftgateNotificationChannel = 7;
inline constexpr std::uint16_t kRearRightDoorNotificationChannel = 8;
inline constexpr std::uint16_t kRearLeftDoorNotificationChannel = 9;
inline constexpr std::uint16_t kFrontLeftDoorNotificationChannel = 10;
inline constexpr std::uint16_t kFrontRightDoorNotificationChannel = 11;
inline constexpr std::uint16_t kDoorsUnlockedNotificationChannel = 12;
inline constexpr std::uint16_t kLeftLampNotificationChannel = 13;
inline constexpr std::uint16_t kRightLampNotificationChannel = 14;
inline constexpr std::uint16_t kWiperLowNotificationChannel = 15;
inline constexpr std::uint16_t kFrontWiperNotificationChannel = 16;

using SelectorNotificationChannel =
    vehicle_core::NotificationChannel<SelectorPosition, kSelectorNotificationChannel>;
using ActualGearNotificationChannel =
    vehicle_core::NotificationChannel<ActualGear, kActualGearNotificationChannel>;
using TurnNotificationChannel =
    vehicle_core::NotificationChannel<TurnState, kTurnNotificationChannel>;
using HazardNotificationChannel =
    vehicle_core::NotificationChannel<bool, kHazardNotificationChannel>;
using LeftTurnNotificationChannel =
    vehicle_core::NotificationChannel<bool, kLeftTurnNotificationChannel>;
using RightTurnNotificationChannel =
    vehicle_core::NotificationChannel<bool, kRightTurnNotificationChannel>;
using LiftgateNotificationChannel =
    vehicle_core::NotificationChannel<bool, kLiftgateNotificationChannel>;
using RearRightDoorNotificationChannel =
    vehicle_core::NotificationChannel<bool, kRearRightDoorNotificationChannel>;
using RearLeftDoorNotificationChannel =
    vehicle_core::NotificationChannel<bool, kRearLeftDoorNotificationChannel>;
using FrontLeftDoorNotificationChannel =
    vehicle_core::NotificationChannel<bool, kFrontLeftDoorNotificationChannel>;
using FrontRightDoorNotificationChannel =
    vehicle_core::NotificationChannel<bool, kFrontRightDoorNotificationChannel>;
using DoorsUnlockedNotificationChannel =
    vehicle_core::NotificationChannel<bool, kDoorsUnlockedNotificationChannel>;
using LeftLampNotificationChannel =
    vehicle_core::NotificationChannel<bool, kLeftLampNotificationChannel>;
using RightLampNotificationChannel =
    vehicle_core::NotificationChannel<bool, kRightLampNotificationChannel>;
using WiperLowNotificationChannel =
    vehicle_core::NotificationChannel<bool, kWiperLowNotificationChannel>;
using FrontWiperNotificationChannel =
    vehicle_core::NotificationChannel<FrontWiperPosition, kFrontWiperNotificationChannel>;

#if !defined(ESP_PLATFORM)
// Host-only extension seam. The channel is deliberately private and has no
// corresponding public facade callback; it proves a new fixed channel can be
// added by extending the descriptor tuple alone.
inline constexpr std::uint16_t kTestFrontWiperNotificationChannel = 17;
using TestFrontWiperNotificationChannel =
    vehicle_core::NotificationChannel<FrontWiperPosition, kTestFrontWiperNotificationChannel>;
#endif

// Fixed metadata records keep the service's typed state/channel wiring in one
// place. Member pointers preserve compile-time value types while allowing the
// worker loops to operate over a heterogeneous tuple without allocation.
template <typename T> struct PollingDescriptor final {
  using SignalMember = vehicle_core::Signal<T> VehicleState::*;

  const char *name{nullptr};
  SignalMember signal{nullptr};
  std::uint32_t identifier{0};
  ValidationStatus validation{ValidationStatus::Reference};
};

#if defined(ESP_PLATFORM)
using PollingDescriptorTuple = std::tuple<PollingDescriptor<float>, PollingDescriptor<float>>;

inline constexpr PollingDescriptorTuple kPollingDescriptors{
    PollingDescriptor<float>{"speed_kph", &VehicleState::speed_kph, candidate::kEngineDataId,
                             ValidationStatus::Reference},
    PollingDescriptor<float>{"engine_rpm", &VehicleState::engine_rpm, candidate::kEngineDataId,
                             ValidationStatus::Confirmed}};
#else
using PollingDescriptorTuple = std::tuple<PollingDescriptor<float>, PollingDescriptor<float>,
                                          PollingDescriptor<FrontWiperPosition>>;

inline constexpr PollingDescriptorTuple kPollingDescriptors{
    PollingDescriptor<float>{"speed_kph", &VehicleState::speed_kph, candidate::kEngineDataId,
                             ValidationStatus::Reference},
    PollingDescriptor<float>{"engine_rpm", &VehicleState::engine_rpm, candidate::kEngineDataId,
                             ValidationStatus::Confirmed},
    PollingDescriptor<FrontWiperPosition>{"test_front_wiper", &VehicleState::front_wiper,
                                          candidate::kTurnSwitchId, ValidationStatus::Observed}};
#endif

template <typename T, std::uint16_t ChannelId> struct NotificationDescriptor final {
  using Channel = vehicle_core::NotificationChannel<T, ChannelId>;
  using ChannelMember = Channel VehicleTelemetryService::*;
  using SignalMember = vehicle_core::Signal<T> VehicleState::*;

  const char *name{nullptr};
  ChannelMember channel{nullptr};
  SignalMember signal{nullptr};
  std::uint32_t identifier{0};
};

#if defined(ESP_PLATFORM)
using NotificationDescriptorTuple =
    std::tuple<NotificationDescriptor<SelectorPosition, kSelectorNotificationChannel>,
               NotificationDescriptor<ActualGear, kActualGearNotificationChannel>,
               NotificationDescriptor<TurnState, kTurnNotificationChannel>,
               NotificationDescriptor<bool, kHazardNotificationChannel>,
               NotificationDescriptor<bool, kLeftTurnNotificationChannel>,
               NotificationDescriptor<bool, kRightTurnNotificationChannel>,
               NotificationDescriptor<bool, kLiftgateNotificationChannel>,
               NotificationDescriptor<bool, kRearRightDoorNotificationChannel>,
               NotificationDescriptor<bool, kRearLeftDoorNotificationChannel>,
               NotificationDescriptor<bool, kFrontLeftDoorNotificationChannel>,
               NotificationDescriptor<bool, kFrontRightDoorNotificationChannel>,
               NotificationDescriptor<bool, kDoorsUnlockedNotificationChannel>,
               NotificationDescriptor<bool, kLeftLampNotificationChannel>,
               NotificationDescriptor<bool, kRightLampNotificationChannel>,
               NotificationDescriptor<bool, kWiperLowNotificationChannel>,
               NotificationDescriptor<FrontWiperPosition, kFrontWiperNotificationChannel>>;
#else
using NotificationDescriptorTuple =
    std::tuple<NotificationDescriptor<SelectorPosition, kSelectorNotificationChannel>,
               NotificationDescriptor<ActualGear, kActualGearNotificationChannel>,
               NotificationDescriptor<TurnState, kTurnNotificationChannel>,
               NotificationDescriptor<bool, kHazardNotificationChannel>,
               NotificationDescriptor<bool, kLeftTurnNotificationChannel>,
               NotificationDescriptor<bool, kRightTurnNotificationChannel>,
               NotificationDescriptor<bool, kLiftgateNotificationChannel>,
               NotificationDescriptor<bool, kRearRightDoorNotificationChannel>,
               NotificationDescriptor<bool, kRearLeftDoorNotificationChannel>,
               NotificationDescriptor<bool, kFrontLeftDoorNotificationChannel>,
               NotificationDescriptor<bool, kFrontRightDoorNotificationChannel>,
               NotificationDescriptor<bool, kDoorsUnlockedNotificationChannel>,
               NotificationDescriptor<bool, kLeftLampNotificationChannel>,
               NotificationDescriptor<bool, kRightLampNotificationChannel>,
               NotificationDescriptor<bool, kWiperLowNotificationChannel>,
               NotificationDescriptor<FrontWiperPosition, kFrontWiperNotificationChannel>,
               NotificationDescriptor<FrontWiperPosition, kTestFrontWiperNotificationChannel>>;
#endif

inline constexpr std::size_t kNotificationChannelCount =
    std::tuple_size_v<NotificationDescriptorTuple>;
inline constexpr std::size_t kSubscriptionCapacity =
    kNotificationChannelCount * vehicle_core::kNotificationSubscribersPerChannel;

struct SubscriptionToken final {
  ResultCode status{ResultCode::InvalidState};
  std::uint16_t channel{0xffffU};
  std::uint8_t slot{0xffU};
  std::uint16_t generation{0};

  [[nodiscard]] constexpr bool ok() const noexcept { return status == ResultCode::Ok; }
};

class VehicleTelemetryService final {
public:
  VehicleTelemetryService() noexcept;
  VehicleTelemetryService(vehicle_core::MonotonicClock &clock, AcquisitionSource &source,
                          LightingSink &lighting_sink, TelemetryConfig config = {}) noexcept;
  ~VehicleTelemetryService() noexcept;

  VehicleTelemetryService(const VehicleTelemetryService &) = delete;
  VehicleTelemetryService &operator=(const VehicleTelemetryService &) = delete;

  [[nodiscard]] StatusResult configure(const TelemetryConfig &config) noexcept;
  [[nodiscard]] StatusResult bind_lighting_sink(LightingSink &lighting_sink) noexcept;
  [[nodiscard]] StatusResult start() noexcept;
  [[nodiscard]] StatusResult stop() noexcept;
  [[nodiscard]] Diagnostics diagnostics() const noexcept;

  [[nodiscard]] Reading<float> speed_kph() const noexcept { return publication_.speed_kph(); }
  [[nodiscard]] Reading<float> engine_rpm() const noexcept { return publication_.engine_rpm(); }

  // These records are implementation-only. Host extension tests can inspect
  // the exact metadata consumed by the service workers without widening the
  // public facade contract.
  [[nodiscard]] static const PollingDescriptorTuple &polling_descriptors() noexcept;
  [[nodiscard]] static const NotificationDescriptorTuple &notification_descriptors() noexcept;

  template <typename T>
  [[nodiscard]] Reading<T>
  read_polling_descriptor(const PollingDescriptor<T> &descriptor) const noexcept;

  template <typename T, std::uint16_t ChannelId>
  [[nodiscard]] SubscriptionToken
  subscribe_notification_descriptor(const NotificationDescriptor<T, ChannelId> &descriptor,
                                    Callback<T> callback, void *context) noexcept;

  [[nodiscard]] SubscriptionToken subscribe_selector(Callback<SelectorPosition> callback,
                                                     void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_actual_gear(Callback<ActualGear> callback,
                                                        void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_turn(Callback<TurnState> callback,
                                                 void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_hazard(Callback<bool> callback, void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_left_turn(Callback<bool> callback,
                                                      void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_right_turn(Callback<bool> callback,
                                                       void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_liftgate(Callback<bool> callback,
                                                     void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_rear_right_door(Callback<bool> callback,
                                                            void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_rear_left_door(Callback<bool> callback,
                                                           void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_front_left_door(Callback<bool> callback,
                                                            void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_front_right_door(Callback<bool> callback,
                                                             void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_doors_unlocked(Callback<bool> callback,
                                                           void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_left_lamp(Callback<bool> callback,
                                                      void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_right_lamp(Callback<bool> callback,
                                                       void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_wiper_low(Callback<bool> callback,
                                                      void *context) noexcept;
  [[nodiscard]] SubscriptionToken subscribe_front_wiper(Callback<FrontWiperPosition> callback,
                                                        void *context) noexcept;

  [[nodiscard]] StatusResult unsubscribe(const SubscriptionToken &token) noexcept;

private:
  struct Registration final {
    vehicle_core::NotificationHandle handle{};
    std::uint16_t channel{0xffffU};
    std::uint8_t slot{0xffU};
    std::uint16_t generation{0};
    bool active{false};
  };

  [[nodiscard]] static bool valid_config(const TelemetryConfig &config) noexcept;
  void initialize_registration_slots() noexcept;
  [[nodiscard]] Registration *free_registration(std::uint16_t channel) noexcept;
  [[nodiscard]] Registration *find_registration(const SubscriptionToken &token) noexcept;
  [[nodiscard]] const Registration *
  find_registration(const SubscriptionToken &token) const noexcept;
  [[nodiscard]] static ResultCode
  map_notification_status(vehicle_core::NotificationStatus status) noexcept;

  template <typename T, std::uint16_t ChannelId>
  [[nodiscard]] SubscriptionToken
  register_subscription(vehicle_core::NotificationChannel<T, ChannelId> &channel,
                        Callback<T> callback, void *context) noexcept;

  [[nodiscard]] bool start_channels() noexcept;
  [[nodiscard]] bool stop_channels() noexcept;
  [[nodiscard]] std::size_t dispatch_channels_once() noexcept;

  void processing_loop() noexcept;
  void dispatcher_loop() noexcept;
#if defined(ESP_PLATFORM)
  static void processing_task_entry(void *context) noexcept;
  static void dispatcher_task_entry(void *context) noexcept;
#endif
  void process_frame(const vehicle_core::RawCanFrame &frame) noexcept;
  void publish_current(bool received_frame) noexcept;
  template <typename T, std::uint16_t ChannelId>
  void
  publish_notification_descriptor(const PublishedSnapshot &snapshot,
                                  vehicle_core::MonotonicTimestamp now_us,
                                  const NotificationDescriptor<T, ChannelId> &descriptor) noexcept;
  void publish_notifications(const PublishedSnapshot &snapshot,
                             vehicle_core::MonotonicTimestamp now_us) noexcept;
  void publish_lighting(const PublishedSnapshot &snapshot,
                        vehicle_core::MonotonicTimestamp now_us) noexcept;
  void latch_processing_fault() noexcept;
  [[nodiscard]] bool workers_done() const noexcept;
  [[nodiscard]] bool wait_for_workers(std::uint64_t timeout_us) noexcept;
  void join_workers() noexcept;
  void publish_startup_black(vehicle_core::MonotonicTimestamp now_us) noexcept;

  SteadyClock steady_clock_{};
#if defined(ESP_PLATFORM)
  CanBusAcquisitionSource can_bus_source_{};
#else
  HostAcquisitionSource host_source_{};
#endif
  NullLightingSink null_lighting_sink_{};
  vehicle_core::MonotonicClock *clock_{nullptr};
  AcquisitionSource *source_{nullptr};
  LightingSink *lighting_sink_{nullptr};
  // Only the service which observed a successful source start may stop the
  // shared acquisition engine.  Keep this separate from lifecycle_state_:
  // startup can fail before ownership is acquired, and cleanup can complete
  // before the worker threads have quiesced.
  bool source_owned_{false};
  PublicationStore publication_;
  TelemetryConfig config_{};
  VehicleState processing_state_{};
  std::optional<vehicle_core::MonotonicTimestamp> last_transport_receive_us_{};
  vehicle_core::TransportHealth transport_{vehicle_core::TransportHealth::Stopped};
  std::uint64_t frames_processed_{0};

  SelectorNotificationChannel selector_channel_{};
  ActualGearNotificationChannel actual_gear_channel_{};
  TurnNotificationChannel turn_channel_{};
  HazardNotificationChannel hazard_channel_{};
  LeftTurnNotificationChannel left_turn_channel_{};
  RightTurnNotificationChannel right_turn_channel_{};
  LiftgateNotificationChannel liftgate_channel_{};
  RearRightDoorNotificationChannel rear_right_door_channel_{};
  RearLeftDoorNotificationChannel rear_left_door_channel_{};
  FrontLeftDoorNotificationChannel front_left_door_channel_{};
  FrontRightDoorNotificationChannel front_right_door_channel_{};
  DoorsUnlockedNotificationChannel doors_unlocked_channel_{};
  LeftLampNotificationChannel left_lamp_channel_{};
  RightLampNotificationChannel right_lamp_channel_{};
  WiperLowNotificationChannel wiper_low_channel_{};
  FrontWiperNotificationChannel front_wiper_channel_{};
#if !defined(ESP_PLATFORM)
  TestFrontWiperNotificationChannel test_front_wiper_channel_{};
#endif
  std::array<Registration, kSubscriptionCapacity> registrations_{};

  mutable std::mutex lifecycle_mutex_{};
  std::atomic<LifecycleState> lifecycle_state_{LifecycleState::Stopped};
  std::atomic<bool> run_requested_{false};
  std::atomic<bool> processing_done_{true};
  std::atomic<bool> dispatcher_done_{true};
  std::atomic<std::size_t> dispatch_cursor_{0};
#if defined(ESP_PLATFORM)
  void *processing_task_{nullptr};
  void *dispatcher_task_{nullptr};
#else
  std::thread processing_thread_{};
  std::thread dispatcher_thread_{};
#endif

  bool lighting_sent_{false};
  bool lighting_failure_{false};
  TurnState lighting_turn_{TurnState::Unknown};
  vehicle_core::Availability lighting_availability_{vehicle_core::Availability::NoData};
  vehicle_core::MonotonicTimestamp lighting_next_heartbeat_us_{0};
};

template <typename T, std::uint16_t ChannelId>
SubscriptionToken VehicleTelemetryService::register_subscription(
    vehicle_core::NotificationChannel<T, ChannelId> &channel, Callback<T> callback,
    void *context) noexcept {
  Registration *registration = free_registration(ChannelId);
  if (registration == nullptr)
    return {ResultCode::CapacityExceeded, ChannelId, 0xffU, 0};

  const auto result = channel.subscribe(callback, context);
  if (!result.ok())
    return {map_notification_status(result.status), ChannelId, registration->slot, 0};

  registration->active = true;
  ++registration->generation;
  if (registration->generation == 0)
    registration->generation = 1;
  registration->handle = *result.value;
  return {ResultCode::Ok, ChannelId, registration->slot, registration->generation};
}

template <typename T>
Reading<T> VehicleTelemetryService::read_polling_descriptor(
    const PollingDescriptor<T> &descriptor) const noexcept {
  return publication_.read_test_signal(descriptor.signal, descriptor.identifier,
                                       descriptor.validation);
}

template <typename T, std::uint16_t ChannelId>
SubscriptionToken VehicleTelemetryService::subscribe_notification_descriptor(
    const NotificationDescriptor<T, ChannelId> &descriptor, Callback<T> callback,
    void *context) noexcept {
  std::lock_guard<std::mutex> lock{lifecycle_mutex_};
  if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Stopped)
    return {ResultCode::InvalidState, 0xffffU, 0xffU, 0};
  return register_subscription(this->*descriptor.channel, callback, context);
}

} // namespace mazda::internal
