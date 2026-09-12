#include <cassert>
#include <vector>

#include "local_argb/lighting_sink.hpp"
#include "local_argb/local_argb.h"
#include "local_argb/renderer.hpp"
#include "vehicle_lighting_policy/command.hpp"

namespace {

class FakePixelSink final : public local_argb::PixelSink {
public:
  bool write(const local_argb::Rgb color) noexcept override {
    writes.push_back(color);
    if (failures_remaining != 0) {
      --failures_remaining;
      return false;
    }
    return true;
  }

  std::vector<local_argb::Rgb> writes;
  unsigned failures_remaining{0};
};

class FakeLightingSink final : public local_argb::internal::LightingSink {
public:
  bool publish(const local_argb::internal::LightingCommand &command) noexcept override {
    commands.push_back(command);
    return true;
  }

  std::vector<local_argb::internal::LightingCommand> commands;
};

local_argb::internal::LightingCommand green(const vehicle_core::MonotonicTimestamp deadline) {
  return {local_argb::internal::LightingRgb{0, local_argb::kBrightnessCeiling, 0}, deadline, true};
}

void test_renderer_enforces_brightness_ceiling() {
  FakePixelSink sink;
  local_argb::internal::RendererController renderer{sink};
  assert(renderer.start());

  const local_argb::internal::LightingCommand over_limit{{255, 127, 17}, 100, true};
  assert(renderer.apply(over_limit, 50));
  const local_argb::Rgb expected{local_argb::kBrightnessCeiling, local_argb::kBrightnessCeiling,
                                 local_argb::kBrightnessCeiling};
  assert(sink.writes.back() == expected);
}

void test_startup_and_independent_expiry() {
  FakePixelSink sink;
  local_argb::internal::RendererController renderer{sink};
  assert(renderer.start());
  assert(sink.writes.size() == 1);
  assert(sink.writes.front() == local_argb::kBlack);

  assert(renderer.apply(green(250), 100));
  const local_argb::Rgb green_rgb{0, local_argb::kBrightnessCeiling, 0};
  assert(sink.writes.back() == green_rgb);
  assert(renderer.tick(250));
  assert(sink.writes.back() != local_argb::kBlack);
  assert(renderer.tick(251));
  assert(sink.writes.back() == local_argb::kBlack);
}

void test_failed_colour_attempt_retries_black_then_recovers() {
  FakePixelSink sink;
  local_argb::internal::RendererController renderer{sink};
  assert(renderer.start());
  sink.failures_remaining = 1;
  assert(!renderer.apply(green(250), 100));
  assert(renderer.faulted());
  assert(sink.writes.size() == 3); // colour failure, then immediate black retry
  assert(sink.writes.back() == local_argb::kBlack);
  assert(renderer.tick(101)); // redundant black is suppressed after success
  assert(renderer.apply(green(300), 200));
  assert(!renderer.faulted());
}

void test_bounded_overwrite_and_generic_sink() {
  local_argb::internal::Mailbox mailbox;
  mailbox.submit(green(10));
  mailbox.submit(green(20));
  local_argb::internal::LightingCommand command{};
  assert(mailbox.take(command));
  assert(command.valid_until_us == 20);
  assert(!mailbox.take(command));

  FakeLightingSink sink;
  assert(sink.publish(green(42)));
  assert(sink.commands.size() == 1);
  assert(sink.commands.front().actionable);

  const vehicle_lighting_policy::LightingCommand policy_command{{1, 2, 3}, 99, true};
  const auto adapted = local_argb::internal::adapt_command(policy_command);
  assert(adapted.color.red == 1);
  assert(adapted.color.green == 2);
  assert(adapted.color.blue == 3);
  assert(adapted.valid_until_us == 99);
  assert(adapted.actionable);
}

} // namespace

int main() {
  test_startup_and_independent_expiry();
  test_failed_colour_attempt_retries_black_then_recovers();
  test_bounded_overwrite_and_generic_sink();
  test_renderer_enforces_brightness_ceiling();
  return 0;
}
