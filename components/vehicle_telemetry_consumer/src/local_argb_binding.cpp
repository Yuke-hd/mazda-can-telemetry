#include "mazda/vehicle_telemetry_consumer.hpp"

#include "local_argb/lighting_sink.hpp"
#include "mazda/internal_contracts.hpp"
#include "mazda/vehicle_telemetry_internal.hpp"
#include "vehicle_lighting_policy/policy.hpp"

namespace mazda::application {
namespace {

class LocalArgbLightingSink final : public internal::LightingSink {
public:
  [[nodiscard]] bool publish(const LightingUpdate &update) noexcept override {
    Reading<TurnState> turn{};
    turn.value = update.turn;
    turn.availability = update.availability;
    turn.validation = ValidationStatus::Observed;
    const auto policy_command =
        vehicle_lighting_policy::command_for(turn, update.valid_until_us, update.valid_until_us);
    return local_argb::internal::sink().publish(
        local_argb::internal::adapt_command(policy_command));
  }
};

// The binding is process-wide because the ESP-IDF renderer is process-wide.
// VehicleTelemetry itself remains non-copyable and owns no renderer handle.
LocalArgbLightingSink g_local_argb_sink{};

} // namespace

StatusResult bind_local_argb_sink(VehicleTelemetry &telemetry) noexcept {
  return internal::VehicleTelemetryAccess::bind_lighting_sink(telemetry, g_local_argb_sink);
}

} // namespace mazda::application
