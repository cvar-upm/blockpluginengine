#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <time.h>

namespace bpe::test {

// Controlled monotonic time. Waits never depend on wall-clock deadlines.
// Advance the clock only after the test has observed the preceding execution.
class SchedulerClockMock {
public:
  using duration = std::chrono::nanoseconds;
  explicit SchedulerClockMock(duration initial = duration{0}) : ticks_(initial.count()) {}

  timespec now() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return to_timespec(ticks_);
  }

  duration elapsed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return duration{ticks_};
  }

  void advance_to(duration value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value.count() > ticks_) ticks_ = value.count();
    }
    cv_.notify_all();
  }

  void advance(duration delta) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ticks_ += delta.count();
    }
    cv_.notify_all();
  }

  void wait_until(const timespec& deadline, const std::atomic<bool>& running) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return !running.load(std::memory_order_acquire) || ticks_ >= to_ns(deadline); });
  }

  void busy_wait_until(const timespec& deadline, const std::atomic<bool>& running) {
    wait_until(deadline, running);
  }

  void notify_stop() {
    // Synchronize with wait_until() to prevent a lost wake-up: the worker
    // must not observe running == true and then sleep after this notification.
    {
      std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
  }

private:
  static std::int64_t to_ns(const timespec& t) {
    return static_cast<std::int64_t>(t.tv_sec) * 1'000'000'000LL + t.tv_nsec;
  }
  static timespec to_timespec(std::int64_t ns) {
    return {static_cast<time_t>(ns / 1'000'000'000LL),
            static_cast<long>(ns % 1'000'000'000LL)};
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::int64_t ticks_;
};

} // namespace bpe::test
