#pragma once

#include "mazda/facade_contracts.hpp"

namespace mazda {

// Application-facing background telemetry facade. This declaration is a
// contract seam in Stage 0; task, driver, decoder and dispatcher ownership is
// intentionally implemented by later stages.
class VehicleTelemetry final {
public:
  VehicleTelemetry() noexcept = default;
  ~VehicleTelemetry() = default;

  VehicleTelemetry(const VehicleTelemetry &) = delete;
  VehicleTelemetry &operator=(const VehicleTelemetry &) = delete;
  VehicleTelemetry(VehicleTelemetry &&) = delete;
  VehicleTelemetry &operator=(VehicleTelemetry &&) = delete;

  [[nodiscard]] StatusResult configure(const TelemetryConfig &config) noexcept;
  [[nodiscard]] StatusResult start() noexcept;
  [[nodiscard]] StatusResult stop() noexcept;

  // Polling copies the latest accepted state. It never requests a CAN frame
  // and does not consume or acknowledge a sample.
  [[nodiscard]] Reading<float> speed_kph() const noexcept;
  [[nodiscard]] Reading<float> engine_rpm() const noexcept;

  [[nodiscard]] Result<Subscription>
  on_selector_position_changed(Callback<SelectorPosition> callback, void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_actual_gear_changed(Callback<ActualGear> callback,
                                                            void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_turn_state_changed(Callback<TurnState> callback,
                                                           void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_hazard_request_changed(Callback<bool> callback,
                                                               void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_left_turn_request_changed(Callback<bool> callback,
                                                                  void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_right_turn_request_changed(Callback<bool> callback,
                                                                   void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_liftgate_open_changed(Callback<bool> callback,
                                                              void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_rear_right_door_open_changed(Callback<bool> callback,
                                                                     void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_rear_left_door_open_changed(Callback<bool> callback,
                                                                    void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_front_left_door_open_rhd_changed(Callback<bool> callback,
                                                                         void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_front_right_door_open_rhd_changed(Callback<bool> callback,
                                                                          void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_doors_unlocked_changed(Callback<bool> callback,
                                                               void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_left_indicator_lamp_changed(Callback<bool> callback,
                                                                    void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_right_indicator_lamp_changed(Callback<bool> callback,
                                                                     void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_wiper_low_changed(Callback<bool> callback,
                                                          void *context) noexcept;
  [[nodiscard]] Result<Subscription> on_front_wiper_changed(Callback<FrontWiperPosition> callback,
                                                            void *context) noexcept;

  [[nodiscard]] StatusResult unsubscribe(Subscription subscription) noexcept;
  [[nodiscard]] Diagnostics diagnostics() const noexcept;
};

} // namespace mazda
