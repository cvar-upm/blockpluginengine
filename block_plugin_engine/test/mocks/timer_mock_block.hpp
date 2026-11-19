#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <utility>
#include <mutex>
#include <vector>

#include "block_plugin_engine/itimer_block_with_info.hpp"

namespace bpe::test {

// A block records invocations; it never measures Linux scheduling latency.
class PlainTimerMockBlock final : public ITimerBlock {
public:
  ExecutionResult execute() override {
    calls_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_all();
    return ExecutionResult::SUCCESS;
  }
  bool wait_for_count(unsigned n, std::chrono::milliseconds timeout = std::chrono::seconds{2}) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout,
                        [&] { return calls_.load(std::memory_order_relaxed) >= n; });
  }
  unsigned count() const { return calls_.load(std::memory_order_relaxed); }
private:
  std::atomic<unsigned> calls_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
};

class TimerMockBlock final : public ITimerBlockWithInfo {
public:
  TimerMockBlock() = default;

  void set_on_execute(std::function<void()> callback) { callback_ = std::move(callback); }

  ExecutionResult execute(const TimerExecutionInfo& info) override {
    if (callback_) callback_();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      infos_.push_back(info);
    }
    cv_.notify_all();
    return ExecutionResult::SUCCESS;
  }

  bool wait_for_count(std::size_t minimum,
                      std::chrono::milliseconds watchdog = std::chrono::seconds{2}) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, watchdog, [&] { return infos_.size() >= minimum; });
  }

  std::vector<TimerExecutionInfo> snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return infos_;
  }

private:
  std::function<void()> callback_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<TimerExecutionInfo> infos_;
};

} // namespace bpe::test
