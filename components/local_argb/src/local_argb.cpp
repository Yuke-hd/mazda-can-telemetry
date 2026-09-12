#include "../private_include/local_argb/renderer.hpp"

namespace local_argb::internal {

bool RendererController::start() noexcept {
  has_command_ = false;
  has_last_written_ = false;
  faulted_ = false;
  return write_desired(kBlack);
}

bool RendererController::apply(const LightingCommand command,
                               const vehicle_core::MonotonicTimestamp now_us) noexcept {
  command_ = command;
  has_command_ = true;
  // A fresh command is a recovery boundary only after black was successfully
  // written. If the clear also failed, keep retrying black.
  if (!faulted_ || (has_last_written_ && last_written_ == kBlack)) {
    faulted_ = false;
  }
  return tick(now_us);
}

bool RendererController::tick(const vehicle_core::MonotonicTimestamp now_us) noexcept {
  Rgb desired = kBlack;
  if (!faulted_ && has_command_ && command_.actionable && now_us <= command_.valid_until_us) {
    desired = {command_.color.red > kBrightnessCeiling ? kBrightnessCeiling : command_.color.red,
               command_.color.green > kBrightnessCeiling ? kBrightnessCeiling
                                                         : command_.color.green,
               command_.color.blue > kBrightnessCeiling ? kBrightnessCeiling : command_.color.blue};
  }
  return write_desired(desired);
}

bool RendererController::write_desired(const Rgb desired) noexcept {
  if (has_last_written_ && desired == last_written_) {
    return true;
  }
  if (sink_->write(desired)) {
    last_written_ = desired;
    has_last_written_ = true;
    return true;
  }

  faulted_ = true;
  has_last_written_ = false;
  if (desired != kBlack && sink_->write(kBlack)) {
    last_written_ = kBlack;
    has_last_written_ = true;
  }
  return false;
}

} // namespace local_argb::internal
