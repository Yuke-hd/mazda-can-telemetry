#include "mazda/vehicle_telemetry.hpp"

#include "mazda/publication_store.hpp"
#include "mazda/vehicle_telemetry_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <new>
#if !defined(ESP_PLATFORM)
#include <thread>
#endif

#if defined(ESP_PLATFORM)
#include "can_bus/can_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace mazda::internal {
namespace {

constexpr std::uint64_t kNanosecondsPerMicrosecond = 1'000;
constexpr vehicle_core::Microseconds kLightingHeartbeatUs = 100'000;
constexpr std::uint16_t kInvalidChannel = 0xffffU;

vehicle_core::MonotonicTimestamp saturating_add(const vehicle_core::MonotonicTimestamp value,
                                                const vehicle_core::Microseconds delta) noexcept {
  if (value > std::numeric_limits<vehicle_core::MonotonicTimestamp>::max() - delta)
    return std::numeric_limits<vehicle_core::MonotonicTimestamp>::max();
  return value + delta;
}

template <typename T> bool is_available_reading(const Reading<T> &reading) noexcept {
  return reading.value.has_value() && (reading.availability == Availability::Fresh ||
                                       reading.availability == Availability::FreshnessUnverified);
}

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
    const vehicle_core::MonotonicTimestamp now_us) const noexcept {
  const auto transport = published_.diagnostics.transport;
  if ((transport != vehicle_core::TransportHealth::AwaitingTraffic &&
       transport != vehicle_core::TransportHealth::Live) ||
      !transport_reference_us_.has_value()) {
    return transport;
  }

  const auto age_us = now_us >= *transport_reference_us_
                          ? now_us - *transport_reference_us_
                          : static_cast<vehicle_core::Microseconds>(0);
  if (age_us > config_.transport_silence_timeout_us)
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
  const auto now_us = clock_->now();
  std::lock_guard<std::mutex> lock{mutex_};
  Diagnostics result = published_.diagnostics;
  result.transport = effective_transport(now_us);
  return result;
}

PublishedSnapshot PublicationStore::snapshot() const noexcept {
  const auto now_us = clock_->now();
  std::lock_guard<std::mutex> lock{mutex_};
  PublishedSnapshot result = published_;
  result.diagnostics.transport = effective_transport(now_us);
  return result;
}

#if defined(ESP_PLATFORM)

ResultCode map_can_result(const can_bus::Result result) noexcept {
  switch (result) {
  case can_bus::Result::kOk:
    return ResultCode::Ok;
  case can_bus::Result::kInvalidConfiguration:
    return ResultCode::InvalidConfiguration;
  case can_bus::Result::kAlreadyStarted:
    return ResultCode::AlreadyRunning;
  case can_bus::Result::kNotStarted:
    return ResultCode::NotRunning;
  case can_bus::Result::kStopping:
  case can_bus::Result::kTimeout:
    return ResultCode::Timeout;
  case can_bus::Result::kFaulted:
  case can_bus::Result::kDriverFailure:
  case can_bus::Result::kTaskFailure:
    return ResultCode::Faulted;
  }
  return ResultCode::Faulted;
}

ResultCode CanBusAcquisitionSource::start() noexcept {
  return map_can_result(can_bus::start(can_bus::Configuration{}));
}

ResultCode CanBusAcquisitionSource::stop() noexcept { return map_can_result(can_bus::stop()); }

SourceReceiveStatus CanBusAcquisitionSource::receive(vehicle_core::RawCanFrame &frame,
                                                     const std::uint32_t timeout_ms) noexcept {
  switch (can_bus::receive(frame, timeout_ms)) {
  case can_bus::Result::kOk:
    return SourceReceiveStatus::Frame;
  case can_bus::Result::kTimeout:
    return SourceReceiveStatus::Timeout;
  case can_bus::Result::kNotStarted:
    return SourceReceiveStatus::NotStarted;
  default:
    return SourceReceiveStatus::Fault;
  }
}

SourceStatistics CanBusAcquisitionSource::statistics() const noexcept {
  const auto stats = can_bus::statistics();
  SourceStatistics result{};
  result.frames_received = stats.frames_received;
  result.frames_dropped = stats.frames_dropped;
  result.queue_overflows = stats.queue_overflows;
  result.driver_errors = stats.bus_errors;
  result.missed_frames = stats.driver_rx_missed;
  result.controller_resets = stats.controller_resets;
  result.bus_off_events = stats.bus_off_events;
  return result;
}

#endif

VehicleTelemetryService::VehicleTelemetryService() noexcept
#if defined(ESP_PLATFORM)
    : VehicleTelemetryService(steady_clock_, can_bus_source_, null_lighting_sink_){}
#else
    : VehicleTelemetryService(steady_clock_, host_source_, null_lighting_sink_) {
}
#endif

      VehicleTelemetryService::VehicleTelemetryService(
          vehicle_core::MonotonicClock & clock, AcquisitionSource & source,
          LightingSink & lighting_sink, const TelemetryConfig config) noexcept
    : clock_(&clock), source_(&source), lighting_sink_(&lighting_sink), publication_(clock, config),
      config_(config) {
  initialize_registration_slots();
  processing_state_.apply_freshness_policy(config_.freshness);
}

VehicleTelemetryService::~VehicleTelemetryService() noexcept {
  if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Stopped) {
    (void)stop();
  }
#if !defined(ESP_PLATFORM)
  // A facade is required to outlive an unquiesced callback. If a caller
  // violates that precondition, finish cleanup here rather than allowing a
  // joinable std::thread destructor to terminate the process.
  if (processing_thread_.joinable() || dispatcher_thread_.joinable()) {
    run_requested_.store(false, std::memory_order_release);
    (void)source_->stop();
    while (!workers_done())
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    join_workers();
  }
#endif
}

bool VehicleTelemetryService::valid_config(const TelemetryConfig &config) noexcept {
  return config.transport_silence_timeout_us > 0 && config.max_frames_per_batch > 0 &&
         config.max_frames_per_batch <= kDefaultMaxFramesPerBatch &&
         config.availability_service_target_us > 0;
}

void VehicleTelemetryService::initialize_registration_slots() noexcept {
  constexpr std::array<std::uint16_t, kNotificationChannelCount> channels{
      kSelectorNotificationChannel,       kActualGearNotificationChannel,
      kTurnNotificationChannel,           kHazardNotificationChannel,
      kLeftTurnNotificationChannel,       kRightTurnNotificationChannel,
      kLiftgateNotificationChannel,       kRearRightDoorNotificationChannel,
      kRearLeftDoorNotificationChannel,   kFrontLeftDoorNotificationChannel,
      kFrontRightDoorNotificationChannel, kDoorsUnlockedNotificationChannel,
      kLeftLampNotificationChannel,       kRightLampNotificationChannel,
      kWiperLowNotificationChannel,       kFrontWiperNotificationChannel};
  for (std::size_t index = 0; index < registrations_.size(); ++index) {
    registrations_[index].channel =
        channels[index / vehicle_core::kNotificationSubscribersPerChannel];
    registrations_[index].slot =
        static_cast<std::uint8_t>(index % vehicle_core::kNotificationSubscribersPerChannel);
  }
}

VehicleTelemetryService::Registration *
VehicleTelemetryService::free_registration(const std::uint16_t channel) noexcept {
  for (auto &registration : registrations_) {
    if (registration.channel == channel && !registration.active)
      return &registration;
  }
  return nullptr;
}

VehicleTelemetryService::Registration *
VehicleTelemetryService::find_registration(const SubscriptionToken &token) noexcept {
  if (token.channel == kInvalidChannel)
    return nullptr;
  for (auto &registration : registrations_) {
    if (registration.active && registration.channel == token.channel &&
        registration.slot == token.slot && registration.generation == token.generation)
      return &registration;
  }
  return nullptr;
}

const VehicleTelemetryService::Registration *
VehicleTelemetryService::find_registration(const SubscriptionToken &token) const noexcept {
  if (token.channel == kInvalidChannel)
    return nullptr;
  for (const auto &registration : registrations_) {
    if (registration.active && registration.channel == token.channel &&
        registration.slot == token.slot && registration.generation == token.generation)
      return &registration;
  }
  return nullptr;
}

ResultCode VehicleTelemetryService::map_notification_status(
    const vehicle_core::NotificationStatus status) noexcept {
  switch (status) {
  case vehicle_core::NotificationStatus::Ok:
    return ResultCode::Ok;
  case vehicle_core::NotificationStatus::CapacityExceeded:
    return ResultCode::CapacityExceeded;
  case vehicle_core::NotificationStatus::InvalidSubscription:
    return ResultCode::InvalidSubscription;
  case vehicle_core::NotificationStatus::AlreadyRunning:
  case vehicle_core::NotificationStatus::NotRunning:
  case vehicle_core::NotificationStatus::InvalidState:
  case vehicle_core::NotificationStatus::InvalidCallback:
  case vehicle_core::NotificationStatus::NoPending:
    return ResultCode::InvalidState;
  }
  return ResultCode::InvalidState;
}

StatusResult VehicleTelemetryService::configure(const TelemetryConfig &config) noexcept {
  if (!valid_config(config))
    return {ResultCode::InvalidConfiguration};
  std::lock_guard<std::mutex> lock{lifecycle_mutex_};
  if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Stopped)
    return {ResultCode::InvalidState};
  const auto result = publication_.configure(config);
  if (result.ok())
    config_ = config;
  return result;
}

bool VehicleTelemetryService::start_channels() noexcept {
  const bool selector = selector_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool actual_gear =
      selector && actual_gear_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool turn = actual_gear && turn_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool hazard = turn && hazard_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool left_turn =
      hazard && left_turn_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool right_turn =
      left_turn && right_turn_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool liftgate =
      right_turn && liftgate_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool rear_right =
      liftgate && rear_right_door_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool rear_left =
      rear_right && rear_left_door_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool front_left =
      rear_left && front_left_door_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool front_right =
      front_left && front_right_door_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool unlocked =
      front_right && doors_unlocked_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool left_lamp =
      unlocked && left_lamp_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool right_lamp =
      left_lamp && right_lamp_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool wiper_low =
      right_lamp && wiper_low_channel_.start() == vehicle_core::NotificationStatus::Ok;
  const bool front_wiper =
      wiper_low && front_wiper_channel_.start() == vehicle_core::NotificationStatus::Ok;
  return front_wiper;
}

bool VehicleTelemetryService::stop_channels() noexcept {
  bool result = true;
  result = (selector_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result =
      (actual_gear_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (turn_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (hazard_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (left_turn_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (right_turn_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (liftgate_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result =
      (rear_right_door_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result =
      (rear_left_door_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result =
      (front_left_door_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (front_right_door_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) &&
           result;
  result =
      (doors_unlocked_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (left_lamp_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (right_lamp_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result = (wiper_low_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  result =
      (front_wiper_channel_.stop() != vehicle_core::NotificationStatus::InvalidState) && result;
  return result;
}

std::size_t VehicleTelemetryService::dispatch_channels_once() noexcept {
  const std::size_t index =
      dispatch_cursor_.fetch_add(1, std::memory_order_relaxed) % kNotificationChannelCount;
  switch (index) {
  case 0:
    return selector_channel_.dispatch_pending(1);
  case 1:
    return actual_gear_channel_.dispatch_pending(1);
  case 2:
    return turn_channel_.dispatch_pending(1);
  case 3:
    return hazard_channel_.dispatch_pending(1);
  case 4:
    return left_turn_channel_.dispatch_pending(1);
  case 5:
    return right_turn_channel_.dispatch_pending(1);
  case 6:
    return liftgate_channel_.dispatch_pending(1);
  case 7:
    return rear_right_door_channel_.dispatch_pending(1);
  case 8:
    return rear_left_door_channel_.dispatch_pending(1);
  case 9:
    return front_left_door_channel_.dispatch_pending(1);
  case 10:
    return front_right_door_channel_.dispatch_pending(1);
  case 11:
    return doors_unlocked_channel_.dispatch_pending(1);
  case 12:
    return left_lamp_channel_.dispatch_pending(1);
  case 13:
    return right_lamp_channel_.dispatch_pending(1);
  case 14:
    return wiper_low_channel_.dispatch_pending(1);
  case 15:
    return front_wiper_channel_.dispatch_pending(1);
  default:
    return 0;
  }
}

StatusResult VehicleTelemetryService::start() noexcept {
  std::lock_guard<std::mutex> lock{lifecycle_mutex_};
  const auto current = lifecycle_state_.load(std::memory_order_acquire);
  if (current == LifecycleState::Running)
    return {ResultCode::AlreadyRunning};
  if (current != LifecycleState::Stopped)
    return {current == LifecycleState::Faulted ? ResultCode::Faulted : ResultCode::InvalidState};
  if (!valid_config(config_))
    return {ResultCode::InvalidConfiguration};

  processing_state_ = VehicleState{};
  processing_state_.apply_freshness_policy(config_.freshness);
  last_transport_receive_us_.reset();
  transport_ = vehicle_core::TransportHealth::AwaitingTraffic;
  frames_processed_ = 0;
  lighting_sent_ = false;
  lighting_failure_ = false;
  const auto now_us = clock_->now();
  publish_startup_black(now_us);
  publication_.reset(Diagnostics{LifecycleState::Running,
                                 vehicle_core::TransportHealth::AwaitingTraffic,
                                 AcquisitionMetrics{}});

  const auto source_result = source_->start();
  if (source_result != ResultCode::Ok) {
    const auto failure =
        source_result == ResultCode::InvalidConfiguration ? source_result : ResultCode::Faulted;
    lifecycle_state_.store(failure == ResultCode::Faulted ? LifecycleState::Faulted
                                                          : LifecycleState::Stopped,
                           std::memory_order_release);
    publication_.reset(Diagnostics{
        failure == ResultCode::Faulted ? LifecycleState::Faulted : LifecycleState::Stopped,
        failure == ResultCode::Faulted ? vehicle_core::TransportHealth::Faulted
                                       : vehicle_core::TransportHealth::Stopped,
        AcquisitionMetrics{}});
    return {failure};
  }
  if (!start_channels()) {
    (void)source_->stop();
    (void)stop_channels();
    lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
    publication_.reset();
    return {ResultCode::Faulted};
  }

  run_requested_.store(true, std::memory_order_release);
  processing_done_.store(false, std::memory_order_release);
  dispatcher_done_.store(false, std::memory_order_release);
  dispatch_cursor_.store(0, std::memory_order_relaxed);
#if defined(ESP_PLATFORM)
  if (xTaskCreate(&VehicleTelemetryService::processing_task_entry, "telemetry_service", 4096, this,
                  configMAX_PRIORITIES - 3,
                  reinterpret_cast<TaskHandle_t *>(&processing_task_)) != pdPASS) {
    run_requested_.store(false, std::memory_order_release);
    (void)source_->stop();
    processing_done_.store(true, std::memory_order_release);
    dispatcher_done_.store(true, std::memory_order_release);
    (void)stop_channels();
    lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
    publication_.reset(Diagnostics{LifecycleState::Faulted, vehicle_core::TransportHealth::Faulted,
                                   AcquisitionMetrics{}});
    return {ResultCode::Faulted};
  }
  if (xTaskCreate(&VehicleTelemetryService::dispatcher_task_entry, "telemetry_notify", 4096, this,
                  configMAX_PRIORITIES - 4,
                  reinterpret_cast<TaskHandle_t *>(&dispatcher_task_)) != pdPASS) {
    run_requested_.store(false, std::memory_order_release);
    (void)source_->stop();
    (void)wait_for_workers(config_.callback_stop_timeout_us);
    (void)stop_channels();
    lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
    publication_.reset(Diagnostics{LifecycleState::Faulted, vehicle_core::TransportHealth::Faulted,
                                   AcquisitionMetrics{}});
    return {ResultCode::Faulted};
  }
#else
  bool processing_started = false;
  bool dispatcher_started = false;
  try {
    processing_thread_ = std::thread([this] {
      processing_loop();
      processing_done_.store(true, std::memory_order_release);
    });
    processing_started = true;
    dispatcher_thread_ = std::thread([this] {
      dispatcher_loop();
      dispatcher_done_.store(true, std::memory_order_release);
    });
    dispatcher_started = true;
  } catch (...) {
    run_requested_.store(false, std::memory_order_release);
    (void)source_->stop();
    if (!processing_started)
      processing_done_.store(true, std::memory_order_release);
    if (!dispatcher_started)
      dispatcher_done_.store(true, std::memory_order_release);
    (void)wait_for_workers(config_.callback_stop_timeout_us);
    join_workers();
    (void)stop_channels();
    lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
    publication_.reset(Diagnostics{LifecycleState::Faulted, vehicle_core::TransportHealth::Faulted,
                                   AcquisitionMetrics{}});
    return {ResultCode::Faulted};
  }
#endif
  lifecycle_state_.store(LifecycleState::Running, std::memory_order_release);
  publish_current(false);
  return {ResultCode::Ok};
}

StatusResult VehicleTelemetryService::stop() noexcept {
  bool was_faulted = false;
  {
    std::lock_guard<std::mutex> lock{lifecycle_mutex_};
    const auto current = lifecycle_state_.load(std::memory_order_acquire);
    if (current == LifecycleState::Stopped)
      return {ResultCode::NotRunning};
    was_faulted = current == LifecycleState::Faulted;
    lifecycle_state_.store(LifecycleState::Stopping, std::memory_order_release);
    run_requested_.store(false, std::memory_order_release);
  }
  const auto source_result = source_->stop();
  // The processing owner may still be publishing while the source is being
  // stopped. Copy the already-published state so the lifecycle handoff does
  // not race its mutable decoder state; the final metrics publication occurs
  // only after both workers have quiesced below.
  const auto stopping_snapshot = publication_.snapshot();
  publication_.publish(stopping_snapshot.state,
                       Diagnostics{LifecycleState::Stopping, vehicle_core::TransportHealth::Stopped,
                                   stopping_snapshot.diagnostics.acquisition},
                       std::nullopt);
  if (!wait_for_workers(config_.callback_stop_timeout_us))
    return {ResultCode::Timeout};
  join_workers();
  if (source_result != ResultCode::Ok || !stop_channels()) {
    transport_ = vehicle_core::TransportHealth::Faulted;
    lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
    publish_current(false);
    return {source_result == ResultCode::Ok ? ResultCode::Faulted : source_result};
  }

  const auto metrics = source_->statistics();
  AcquisitionMetrics acquisition{};
  acquisition.frames_received = metrics.frames_received;
  acquisition.frames_processed = frames_processed_;
  acquisition.frames_dropped = metrics.frames_dropped;
  acquisition.queue_overflows = metrics.queue_overflows;
  acquisition.driver_errors = metrics.driver_errors;
  acquisition.missed_frames = metrics.missed_frames;
  acquisition.controller_resets = metrics.controller_resets;
  acquisition.bus_off_events = metrics.bus_off_events;
  publication_.reset(
      Diagnostics{LifecycleState::Stopped, vehicle_core::TransportHealth::Stopped, acquisition});
  processing_state_ = VehicleState{};
  last_transport_receive_us_.reset();
  transport_ = vehicle_core::TransportHealth::Stopped;
  frames_processed_ = 0;
  lifecycle_state_.store(LifecycleState::Stopped, std::memory_order_release);
  return {was_faulted ? ResultCode::Faulted : ResultCode::Ok};
}

Diagnostics VehicleTelemetryService::diagnostics() const noexcept {
  return publication_.diagnostics();
}

void VehicleTelemetryService::process_frame(const vehicle_core::RawCanFrame &frame) noexcept {
  if (!last_transport_receive_us_ || frame.timestamp_us > *last_transport_receive_us_)
    last_transport_receive_us_ = frame.timestamp_us;
  transport_ = vehicle_core::TransportHealth::Live;
  std::optional<TurnEdgeEvent> edge{};
  vehicle_core::DecoderObservation observation{};
  vehicle_core::HealthObservation health{};
  (void)candidate::decode(frame, processing_state_, &edge, &observation, &health);
  ++frames_processed_;
  publish_current(true);
}

void VehicleTelemetryService::latch_processing_fault() noexcept {
  transport_ = vehicle_core::TransportHealth::Faulted;
  lifecycle_state_.store(LifecycleState::Faulted, std::memory_order_release);
  publish_current(false);
}

void VehicleTelemetryService::processing_loop() noexcept {
  const auto timeout_ms = static_cast<std::uint32_t>(std::max<vehicle_core::Microseconds>(
      1, (config_.availability_service_target_us + 999) / 1'000));
  while (run_requested_.load(std::memory_order_acquire)) {
    bool received = false;
    for (std::uint8_t index = 0;
         index < config_.max_frames_per_batch && run_requested_.load(std::memory_order_acquire);
         ++index) {
      vehicle_core::RawCanFrame frame{};
      const auto status = source_->receive(frame, received ? 0 : timeout_ms);
      if (status == SourceReceiveStatus::Frame) {
        if (!run_requested_.load(std::memory_order_acquire))
          break;
        received = true;
        process_frame(frame);
        continue;
      }
      if (status == SourceReceiveStatus::Timeout)
        break;
      if (!run_requested_.load(std::memory_order_acquire))
        break;
      latch_processing_fault();
      // The processing owner is terminally faulted, but the dispatcher stays
      // alive until stop() so fault-induced unavailable notices can reach
      // subscribers. Returning marks only the processing worker done.
      return;
    }
    if (run_requested_.load(std::memory_order_acquire))
      publish_current(false);
  }
}

void VehicleTelemetryService::dispatcher_loop() noexcept {
  while (run_requested_.load(std::memory_order_acquire)) {
    if (dispatch_channels_once() == 0) {
#if defined(ESP_PLATFORM)
      vTaskDelay(pdMS_TO_TICKS(1));
#else
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
#endif
    }
  }
}

#if defined(ESP_PLATFORM)

void VehicleTelemetryService::processing_task_entry(void *context) noexcept {
  auto *service = static_cast<VehicleTelemetryService *>(context);
  service->processing_loop();
  service->processing_done_.store(true, std::memory_order_release);
  service->processing_task_ = nullptr;
  vTaskDelete(nullptr);
}

void VehicleTelemetryService::dispatcher_task_entry(void *context) noexcept {
  auto *service = static_cast<VehicleTelemetryService *>(context);
  service->dispatcher_loop();
  service->dispatcher_done_.store(true, std::memory_order_release);
  service->dispatcher_task_ = nullptr;
  vTaskDelete(nullptr);
}

#endif

void VehicleTelemetryService::publish_current(const bool received_frame) noexcept {
  SourceStatistics source_statistics = source_->statistics();
  AcquisitionMetrics acquisition{};
  acquisition.frames_received = source_statistics.frames_received;
  acquisition.frames_processed = frames_processed_;
  acquisition.frames_dropped = source_statistics.frames_dropped;
  acquisition.queue_overflows = source_statistics.queue_overflows;
  acquisition.driver_errors = source_statistics.driver_errors;
  acquisition.missed_frames = source_statistics.missed_frames;
  acquisition.controller_resets = source_statistics.controller_resets;
  acquisition.bus_off_events = source_statistics.bus_off_events;
  const auto lifecycle = lifecycle_state_.load(std::memory_order_acquire);
  const Diagnostics diagnostics{lifecycle, transport_, acquisition};
  publication_.publish(processing_state_, diagnostics,
                       received_frame ? last_transport_receive_us_ : std::nullopt);
  const auto now_us = clock_->now();
  const auto snapshot = publication_.snapshot();
  publish_notifications(snapshot, now_us);
}

void VehicleTelemetryService::publish_notifications(
    const PublishedSnapshot &snapshot, const vehicle_core::MonotonicTimestamp now_us) noexcept {
  const auto transport = snapshot.diagnostics.transport;
  (void)selector_channel_.publish(snapshot.state.reading_at(snapshot.state.selector_position,
                                                            candidate::kGearId, now_us,
                                                            ValidationStatus::Observed, transport));
  (void)actual_gear_channel_.publish(
      snapshot.state.reading_at(snapshot.state.actual_gear, candidate::kGearId, now_us,
                                ValidationStatus::Observed, transport));
  (void)turn_channel_.publish(snapshot.state.reading_at(snapshot.state.turn_state,
                                                        candidate::kTurnSwitchId, now_us,
                                                        ValidationStatus::Observed, transport));
  (void)hazard_channel_.publish(snapshot.state.reading_at(snapshot.state.hazard_request,
                                                          candidate::kTurnSwitchId, now_us,
                                                          ValidationStatus::Observed, transport));
  (void)left_turn_channel_.publish(
      snapshot.state.reading_at(snapshot.state.left_turn_request, candidate::kTurnSwitchId, now_us,
                                ValidationStatus::Observed, transport));
  (void)right_turn_channel_.publish(
      snapshot.state.reading_at(snapshot.state.right_turn_request, candidate::kTurnSwitchId, now_us,
                                ValidationStatus::Observed, transport));
  (void)liftgate_channel_.publish(snapshot.state.reading_at(snapshot.state.liftgate_open,
                                                            candidate::kDoorsId, now_us,
                                                            ValidationStatus::Observed, transport));
  (void)rear_right_door_channel_.publish(
      snapshot.state.reading_at(snapshot.state.rear_right_door_open, candidate::kDoorsId, now_us,
                                ValidationStatus::Observed, transport));
  (void)rear_left_door_channel_.publish(
      snapshot.state.reading_at(snapshot.state.rear_left_door_open, candidate::kDoorsId, now_us,
                                ValidationStatus::Observed, transport));
  (void)front_left_door_channel_.publish(
      snapshot.state.reading_at(snapshot.state.front_left_door_open_rhd, candidate::kDoorsId,
                                now_us, ValidationStatus::Observed, transport));
  (void)front_right_door_channel_.publish(
      snapshot.state.reading_at(snapshot.state.front_right_door_open_rhd, candidate::kDoorsId,
                                now_us, ValidationStatus::Observed, transport));
  (void)doors_unlocked_channel_.publish(
      snapshot.state.reading_at(snapshot.state.doors_unlocked, candidate::kDoorsId, now_us,
                                ValidationStatus::Observed, transport));
  (void)left_lamp_channel_.publish(
      snapshot.state.reading_at(snapshot.state.left_indicator_lamp, candidate::kBlinkInfoId, now_us,
                                ValidationStatus::Observed, transport));
  (void)right_lamp_channel_.publish(
      snapshot.state.reading_at(snapshot.state.right_indicator_lamp, candidate::kBlinkInfoId,
                                now_us, ValidationStatus::Observed, transport));
  (void)wiper_low_channel_.publish(
      snapshot.state.reading_at(snapshot.state.wiper_low, candidate::kBlinkInfoId, now_us,
                                ValidationStatus::Observed, transport));
  (void)front_wiper_channel_.publish(
      snapshot.state.reading_at(snapshot.state.front_wiper, candidate::kTurnSwitchId, now_us,
                                ValidationStatus::Observed, transport));
  publish_lighting(snapshot, now_us);
}

void VehicleTelemetryService::publish_lighting(
    const PublishedSnapshot &snapshot, const vehicle_core::MonotonicTimestamp now_us) noexcept {
  const auto turn_reading =
      snapshot.state.reading_at(snapshot.state.turn_state, candidate::kTurnSwitchId, now_us,
                                ValidationStatus::Observed, snapshot.diagnostics.transport);
  const bool actionable =
      is_available_reading(turn_reading) && *turn_reading.value != TurnState::Unknown &&
      snapshot.diagnostics.transport != vehicle_core::TransportHealth::Stopped &&
      snapshot.diagnostics.transport != vehicle_core::TransportHealth::Faulted &&
      snapshot.diagnostics.transport != vehicle_core::TransportHealth::TimedOut;
  LightingUpdate update{};
  update.turn = actionable ? *turn_reading.value : TurnState::Unknown;
  update.availability = actionable ? turn_reading.availability : turn_reading.availability;

  std::optional<vehicle_core::MonotonicTimestamp> deadline{};
  if (snapshot.state.turn_state.has_value && snapshot.state.turn_state.freshness_timeout_us) {
    deadline = saturating_add(snapshot.state.turn_state.last_update_us,
                              *snapshot.state.turn_state.freshness_timeout_us);
  }
  if (last_transport_receive_us_) {
    const auto transport_deadline =
        saturating_add(*last_transport_receive_us_, config_.transport_silence_timeout_us);
    deadline = deadline ? std::min(*deadline, transport_deadline) : transport_deadline;
  }
  if (!deadline)
    deadline = saturating_add(now_us, kLightingHeartbeatUs);
  update.valid_until_us = *deadline <= now_us ? now_us : *deadline;

  const bool changed = !lighting_sent_ || update.turn != lighting_turn_ ||
                       update.availability != lighting_availability_ || lighting_failure_;
  const bool heartbeat_due = actionable && lighting_sent_ && now_us >= lighting_next_heartbeat_us_;
  if (!changed && !heartbeat_due)
    return;

  const bool accepted = lighting_sink_->publish(update);
  lighting_sent_ = true;
  lighting_turn_ = update.turn;
  lighting_availability_ = update.availability;
  lighting_failure_ = !accepted;
  lighting_next_heartbeat_us_ =
      saturating_add(now_us, accepted && actionable ? kLightingHeartbeatUs : kLightingHeartbeatUs);
}

bool VehicleTelemetryService::workers_done() const noexcept {
  return processing_done_.load(std::memory_order_acquire) &&
         dispatcher_done_.load(std::memory_order_acquire);
}

bool VehicleTelemetryService::wait_for_workers(const std::uint64_t timeout_us) noexcept {
#if defined(ESP_PLATFORM)
  const auto deadline = saturating_add(clock_->now(), timeout_us);
  while (!workers_done()) {
    if (clock_->now() >= deadline)
      return false;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#else
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds{timeout_us};
  while (!workers_done()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
#endif
  return true;
}

void VehicleTelemetryService::join_workers() noexcept {
#if !defined(ESP_PLATFORM)
  if (processing_thread_.joinable())
    processing_thread_.join();
  if (dispatcher_thread_.joinable())
    dispatcher_thread_.join();
#endif
}

void VehicleTelemetryService::publish_startup_black(
    const vehicle_core::MonotonicTimestamp now_us) noexcept {
  LightingUpdate update{};
  update.turn = TurnState::Unknown;
  update.availability = Availability::NoData;
  update.valid_until_us = now_us;
  lighting_failure_ = !lighting_sink_->publish(update);
  lighting_sent_ = true;
  lighting_turn_ = update.turn;
  lighting_availability_ = update.availability;
  lighting_next_heartbeat_us_ = saturating_add(now_us, kLightingHeartbeatUs);
}

#define MAZDA_SUBSCRIBE_METHOD(method_name, channel_member, callback_type)                         \
  SubscriptionToken VehicleTelemetryService::method_name(callback_type callback,                   \
                                                         void *context) noexcept {                 \
    std::lock_guard<std::mutex> lock{lifecycle_mutex_};                                            \
    if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Stopped)               \
      return {ResultCode::InvalidState, kInvalidChannel, 0xffU, 0};                                \
    return register_subscription(channel_member, callback, context);                               \
  }

MAZDA_SUBSCRIBE_METHOD(subscribe_selector, selector_channel_, Callback<SelectorPosition>)
MAZDA_SUBSCRIBE_METHOD(subscribe_actual_gear, actual_gear_channel_, Callback<ActualGear>)
MAZDA_SUBSCRIBE_METHOD(subscribe_turn, turn_channel_, Callback<TurnState>)
MAZDA_SUBSCRIBE_METHOD(subscribe_hazard, hazard_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_left_turn, left_turn_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_right_turn, right_turn_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_liftgate, liftgate_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_rear_right_door, rear_right_door_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_rear_left_door, rear_left_door_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_front_left_door, front_left_door_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_front_right_door, front_right_door_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_doors_unlocked, doors_unlocked_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_left_lamp, left_lamp_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_right_lamp, right_lamp_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_wiper_low, wiper_low_channel_, Callback<bool>)
MAZDA_SUBSCRIBE_METHOD(subscribe_front_wiper, front_wiper_channel_, Callback<FrontWiperPosition>)

#undef MAZDA_SUBSCRIBE_METHOD

StatusResult VehicleTelemetryService::unsubscribe(const SubscriptionToken &token) noexcept {
  std::lock_guard<std::mutex> lock{lifecycle_mutex_};
  if (lifecycle_state_.load(std::memory_order_acquire) != LifecycleState::Stopped)
    return {ResultCode::InvalidState};
  Registration *registration = find_registration(token);
  if (registration == nullptr)
    return {ResultCode::InvalidSubscription};

  vehicle_core::NotificationStatus status = vehicle_core::NotificationStatus::InvalidSubscription;
  switch (token.channel) {
  case kSelectorNotificationChannel:
    status = selector_channel_.unsubscribe(registration->handle);
    break;
  case kActualGearNotificationChannel:
    status = actual_gear_channel_.unsubscribe(registration->handle);
    break;
  case kTurnNotificationChannel:
    status = turn_channel_.unsubscribe(registration->handle);
    break;
  case kHazardNotificationChannel:
    status = hazard_channel_.unsubscribe(registration->handle);
    break;
  case kLeftTurnNotificationChannel:
    status = left_turn_channel_.unsubscribe(registration->handle);
    break;
  case kRightTurnNotificationChannel:
    status = right_turn_channel_.unsubscribe(registration->handle);
    break;
  case kLiftgateNotificationChannel:
    status = liftgate_channel_.unsubscribe(registration->handle);
    break;
  case kRearRightDoorNotificationChannel:
    status = rear_right_door_channel_.unsubscribe(registration->handle);
    break;
  case kRearLeftDoorNotificationChannel:
    status = rear_left_door_channel_.unsubscribe(registration->handle);
    break;
  case kFrontLeftDoorNotificationChannel:
    status = front_left_door_channel_.unsubscribe(registration->handle);
    break;
  case kFrontRightDoorNotificationChannel:
    status = front_right_door_channel_.unsubscribe(registration->handle);
    break;
  case kDoorsUnlockedNotificationChannel:
    status = doors_unlocked_channel_.unsubscribe(registration->handle);
    break;
  case kLeftLampNotificationChannel:
    status = left_lamp_channel_.unsubscribe(registration->handle);
    break;
  case kRightLampNotificationChannel:
    status = right_lamp_channel_.unsubscribe(registration->handle);
    break;
  case kWiperLowNotificationChannel:
    status = wiper_low_channel_.unsubscribe(registration->handle);
    break;
  case kFrontWiperNotificationChannel:
    status = front_wiper_channel_.unsubscribe(registration->handle);
    break;
  default:
    return {ResultCode::InvalidSubscription};
  }
  if (status != vehicle_core::NotificationStatus::Ok)
    return {map_notification_status(status)};
  registration->active = false;
  return {ResultCode::Ok};
}

} // namespace mazda::internal

namespace mazda {

static_assert(sizeof(internal::VehicleTelemetryService) <= sizeof(VehicleTelemetry));
static_assert(alignof(internal::VehicleTelemetryService) <= alignof(std::max_align_t));

VehicleTelemetry::VehicleTelemetry() noexcept {
  ::new (static_cast<void *>(implementation_storage_)) internal::VehicleTelemetryService{};
}

VehicleTelemetry::~VehicleTelemetry() noexcept {
  reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)
      ->~VehicleTelemetryService();
}

StatusResult VehicleTelemetry::configure(const TelemetryConfig &config) noexcept {
  return reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)
      ->configure(config);
}

StatusResult VehicleTelemetry::start() noexcept {
  return reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)->start();
}

StatusResult VehicleTelemetry::stop() noexcept {
  return reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)->stop();
}

Reading<float> VehicleTelemetry::speed_kph() const noexcept {
  return reinterpret_cast<const internal::VehicleTelemetryService *>(implementation_storage_)
      ->speed_kph();
}

Reading<float> VehicleTelemetry::engine_rpm() const noexcept {
  return reinterpret_cast<const internal::VehicleTelemetryService *>(implementation_storage_)
      ->engine_rpm();
}

Diagnostics VehicleTelemetry::diagnostics() const noexcept {
  return reinterpret_cast<const internal::VehicleTelemetryService *>(implementation_storage_)
      ->diagnostics();
}

#define MAZDA_PUBLIC_SUBSCRIPTION_METHOD(method_name, service_method, callback_type)               \
  Result<Subscription> VehicleTelemetry::method_name(callback_type callback,                       \
                                                     void *context) noexcept {                     \
    const auto token =                                                                             \
        reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)             \
            ->service_method(callback, context);                                                   \
    if (!token.ok())                                                                               \
      return {token.status, std::nullopt};                                                         \
    return {ResultCode::Ok, Subscription{token.channel, token.slot, token.generation}};            \
  }

MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_selector_position_changed, subscribe_selector,
                                 Callback<SelectorPosition>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_actual_gear_changed, subscribe_actual_gear,
                                 Callback<ActualGear>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_turn_state_changed, subscribe_turn, Callback<TurnState>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_hazard_request_changed, subscribe_hazard, Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_left_turn_request_changed, subscribe_left_turn, Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_right_turn_request_changed, subscribe_right_turn,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_liftgate_open_changed, subscribe_liftgate, Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_rear_right_door_open_changed, subscribe_rear_right_door,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_rear_left_door_open_changed, subscribe_rear_left_door,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_front_left_door_open_rhd_changed, subscribe_front_left_door,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_front_right_door_open_rhd_changed, subscribe_front_right_door,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_doors_unlocked_changed, subscribe_doors_unlocked,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_left_indicator_lamp_changed, subscribe_left_lamp,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_right_indicator_lamp_changed, subscribe_right_lamp,
                                 Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_wiper_low_changed, subscribe_wiper_low, Callback<bool>)
MAZDA_PUBLIC_SUBSCRIPTION_METHOD(on_front_wiper_changed, subscribe_front_wiper,
                                 Callback<FrontWiperPosition>)

#undef MAZDA_PUBLIC_SUBSCRIPTION_METHOD

StatusResult VehicleTelemetry::unsubscribe(const Subscription subscription) noexcept {
  const internal::SubscriptionToken token{ResultCode::Ok, subscription.channel_, subscription.slot_,
                                          subscription.generation_};
  return reinterpret_cast<internal::VehicleTelemetryService *>(implementation_storage_)
      ->unsubscribe(token);
}

} // namespace mazda
