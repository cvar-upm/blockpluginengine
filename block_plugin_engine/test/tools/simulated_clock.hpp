// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "mocks/scheduler_clock_mock.hpp"

namespace bpe::test {

// Process-wide clock shared by the test executable and pluginlib-loaded schedulers.
// Reset it only when all simulated scheduler workers are stopped.
SchedulerClockMock& integration_clock();

// Clock adapter: each plugin receives the same virtual time source.
class SharedSchedulerClock {
public:
  using duration = SchedulerClockMock::duration;
  timespec now() const { return integration_clock().now(); }
  std::int64_t now_ns() const noexcept { return integration_clock().now_ns(); }
  void wait_until(const timespec& time, const std::atomic<bool>& running) {
    integration_clock().wait_until(time, running);
  }
  void busy_wait_until(const timespec& time, const std::atomic<bool>& running) {
    integration_clock().busy_wait_until(time, running);
  }
  void notify_stop() { integration_clock().notify_stop(); }
  int create_deadline_fd() { return integration_clock().create_deadline_fd(); }
  bool arm_deadline(int fd, std::int64_t time) {
    return integration_clock().arm_deadline(fd, time);
  }
  void consume_deadline(int fd) { integration_clock().consume_deadline(fd); }
  void release_deadline_fd(int fd) noexcept { integration_clock().release_deadline_fd(fd); }
  void before_observe() { integration_clock().before_observe(); }
  void on_stop() { integration_clock().on_stop(); }
};

} // namespace bpe::test
