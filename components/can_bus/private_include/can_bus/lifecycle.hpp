#pragma once

#include <atomic>

#include "can_bus/can_bus.h"

namespace can_bus::internal {

// Tracks ownership independently from the externally visible lifecycle. A
// task may have stopped while the driver is still installed, or a stop may
// have timed out before the task acknowledges its exit. Keeping those facts
// separate is what prevents a retry from creating a second driver instance.
class LifecycleController final {
public:
  LifecycleController() noexcept = default;

  [[nodiscard]] LifecycleState state() const noexcept {
    return state_.load(std::memory_order_acquire);
  }

  // Reserve the singleton acquisition owner for a new run. Only a fully
  // stopped controller can transition to Running.
  [[nodiscard]] bool begin_start() noexcept {
    LifecycleState expected = LifecycleState::kStopped;
    return state_.compare_exchange_strong(expected, LifecycleState::kRunning,
                                          std::memory_order_acq_rel, std::memory_order_acquire);
  }

  // A startup failure before driver installation has no external owner to
  // clean up. This is intentionally not a general reset operation.
  void abort_start() noexcept {
    if (!driver_installed() && !task_owned()) {
      state_.store(LifecycleState::kStopped, std::memory_order_release);
    }
  }

  void mark_driver_installed() noexcept {
    driver_installed_.store(true, std::memory_order_release);
  }

  void mark_driver_started() noexcept { driver_started_.store(true, std::memory_order_release); }

  void mark_task_created() noexcept { task_owned_.store(true, std::memory_order_release); }

  void mark_driver_stopped() noexcept { driver_started_.store(false, std::memory_order_release); }

  void mark_driver_uninstalled() noexcept {
    driver_installed_.store(false, std::memory_order_release);
  }

  void acknowledge_task() noexcept { task_owned_.store(false, std::memory_order_release); }

  [[nodiscard]] bool driver_installed() const noexcept {
    return driver_installed_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool driver_started() const noexcept {
    return driver_started_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool task_owned() const noexcept {
    return task_owned_.load(std::memory_order_acquire);
  }

  // Running and Faulted both accept a cleanup request. Stopping is retained
  // for a later attempt after a bounded wait expires. Stopped is terminal for
  // this operation and must map to kNotStarted at the public boundary.
  [[nodiscard]] bool begin_stop() noexcept {
    LifecycleState current = state_.load(std::memory_order_acquire);
    for (;;) {
      if (current == LifecycleState::kStopped) {
        return false;
      }
      if (current == LifecycleState::kStopping) {
        return true;
      }
      if (state_.compare_exchange_weak(current, LifecycleState::kStopping,
                                       std::memory_order_acq_rel, std::memory_order_acquire)) {
        return true;
      }
    }
  }

  // A terminal receive/status fault stops the receive loop immediately and
  // leaves ownership intact for stop() to release. The bool tells the caller
  // whether this transition was new, so it emits one consumer wakeup instead
  // of repeatedly signalling a counting semaphore.
  [[nodiscard]] bool latch_fault() noexcept {
    LifecycleState current = state_.load(std::memory_order_acquire);
    for (;;) {
      if (current == LifecycleState::kFaulted) {
        return false;
      }
      if (current == LifecycleState::kStopped) {
        return false;
      }
      if (state_.compare_exchange_weak(current, LifecycleState::kFaulted, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return true;
      }
    }
  }

  // Reconcile ownership after one cleanup attempt. A task still owned means
  // stop remains in progress; driver-only ownership is a cleanup fault. The
  // controller becomes restartable only after both owners are gone.
  void reconcile() noexcept {
    if (task_owned()) {
      state_.store(LifecycleState::kStopping, std::memory_order_release);
    } else if (driver_installed()) {
      state_.store(LifecycleState::kFaulted, std::memory_order_release);
    } else {
      state_.store(LifecycleState::kStopped, std::memory_order_release);
    }
  }

private:
  std::atomic<LifecycleState> state_{LifecycleState::kStopped};
  std::atomic<bool> driver_installed_{false};
  std::atomic<bool> driver_started_{false};
  std::atomic<bool> task_owned_{false};
};

} // namespace can_bus::internal
