// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace bpe {

// Production clock/deadline policy. The scheduler's polling loop is shared
// with the simulated policy; only reading time and arming the timer differ.
class MonotonicHybridClock {
public:
  std::int64_t now_ns() const noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  }

  int create_deadline_fd() {
    return ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  }

  bool arm_deadline(int fd, std::int64_t deadline_ns) {
    itimerspec spec{};
    if (deadline_ns >= 0) {
      const auto ns = deadline_ns > 0 ? deadline_ns : 1;
      spec.it_value = {static_cast<time_t>(ns / 1'000'000'000LL),
                       static_cast<long>(ns % 1'000'000'000LL)};
    }
    return ::timerfd_settime(fd, TFD_TIMER_ABSTIME, &spec, nullptr) == 0;
  }

  void consume_deadline(int fd) {
    std::uint64_t expirations{};
    (void)::read(fd, &expirations, sizeof(expirations));
  }

  void release_deadline_fd(int) noexcept {}
  void before_observe() noexcept {}
  void on_stop() noexcept {}
};

} // namespace bpe
