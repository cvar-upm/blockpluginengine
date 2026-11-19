#pragma once
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <pthread.h>
#include <sched.h>
#include <time.h>

namespace bpe {

// Production clock: both sleep and spin use CLOCK_MONOTONIC.
// No virtual dispatch is performed inside the busy-wait loop.
class MonotonicClock {
public:
  timespec now() const noexcept {
    timespec result{};
    clock_gettime(CLOCK_MONOTONIC, &result);
    return result;
  }
  void wait_until(const timespec& deadline, const std::atomic<bool>& running) const {
    if (before(now(), deadline)) {
      int result;
      do {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
      } while (result == EINTR && running.load(std::memory_order_acquire));
    }
  }
  void busy_wait_until(const timespec& deadline, const std::atomic<bool>& running) const {
    while (running.load(std::memory_order_acquire) && before(now(), deadline)) {
      // Deliberately busy spin near the absolute deadline.
    }
  }
  void notify_stop() const noexcept {} // Production sleep remains TIMER_ABSTIME-based.
private:
  static bool before(const timespec& a, const timespec& b) noexcept {
    return a.tv_sec < b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec < b.tv_nsec);
  }
};

// Separate from the clock: a test can verify the request without RT permissions.
struct SystemRealtimePolicy {
  bool request_fifo(int priority) const {
    sched_param param{};
    param.sched_priority = priority;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
  }
};

} // namespace bpe
