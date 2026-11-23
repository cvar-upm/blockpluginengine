#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <time.h>
#include <sys/eventfd.h>

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

  void reset(duration initial = duration{0}) {
    std::lock_guard<std::mutex> lock(mutex_);
    // The caller must have stopped and destroyed all simulated schedulers.
    ticks_ = initial.count();
    deadline_fd_ = -1;
    deadline_ns_ = -1;
    deadline_armed_ = false;
    frozen_ = false;
    worker_frozen_ = false;
  }

  void advance_to(duration value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (value.count() > ticks_) ticks_ = value.count();
      fire_if_due();
    }
    cv_.notify_all();
  }

  void advance(duration delta) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ticks_ += delta.count();
      fire_if_due();
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
    // Synchronize with wait_until(): the worker must not enter the wait
    // after the stop notification has already been sent.
    {
      std::lock_guard<std::mutex> lock(mutex_);
    }
    cv_.notify_all();
  }

  // HybridScheduler uses the same virtual clock through an eventfd-backed
  // deadline source, retaining the production epoll path.
  std::int64_t now_ns() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ticks_;
  }

  int create_deadline_fd() {
    const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd >= 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      deadline_fd_ = fd;
      deadline_armed_ = false;
    }
    return fd;
  }

  bool arm_deadline(int fd, std::int64_t deadline) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd != deadline_fd_) return false;
    deadline_armed_ = deadline >= 0;
    deadline_ns_ = deadline;
    fire_if_due();
    return true;
  }

  void consume_deadline(int fd) {
    eventfd_t count{};
    while (::eventfd_read(fd, &count) == 0) {}
  }

  void release_deadline_fd(int fd) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd == deadline_fd_) {
      deadline_fd_ = -1;
      deadline_armed_ = false;
    }
  }

  // Gate used only to stage several ready pipelines at precisely the same
  // virtual instant. It does not run or schedule pipelines itself.
  void freeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = true;
  }

  bool wait_until_frozen(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return worker_frozen_; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      frozen_ = false;
    }
    cv_.notify_all();
  }

  void before_observe() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!frozen_) return;
    worker_frozen_ = true;
    cv_.notify_all();
    cv_.wait(lock, [&] { return !frozen_; });
    worker_frozen_ = false;
  }

  void on_stop() { release(); }

private:
  static std::int64_t to_ns(const timespec& t) {
    return static_cast<std::int64_t>(t.tv_sec) * 1'000'000'000LL + t.tv_nsec;
  }
  static timespec to_timespec(std::int64_t ns) {
    return {static_cast<time_t>(ns / 1'000'000'000LL),
            static_cast<long>(ns % 1'000'000'000LL)};
  }

  void fire_if_due() {
    // Called with mutex_ held. One wake-up per armed virtual deadline.
    if (deadline_armed_ && ticks_ >= deadline_ns_ && deadline_fd_ >= 0) {
      deadline_armed_ = false;
      (void)::eventfd_write(deadline_fd_, 1);
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::int64_t ticks_;
  std::int64_t deadline_ns_{-1};
  int deadline_fd_{-1};
  bool deadline_armed_{false};
  bool frozen_{false};
  bool worker_frozen_{false};
};

} // namespace bpe::test
