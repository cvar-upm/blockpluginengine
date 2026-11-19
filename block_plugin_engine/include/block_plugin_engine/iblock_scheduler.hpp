#pragma once

#include <string>
#include <chrono>
#include <time.h>

#include "block_plugin_engine/execution_config.hpp"
#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/scheduler_config.hpp"

namespace bpe {

class ITimerBlock;

enum class SchedulerResult { SUCCESS, INVALID_STATE, INVALID_CONFIG, NOT_FOUND, SYSTEM_ERROR };

class IBlockScheduler {
public:
  virtual ~IBlockScheduler() = default;

  virtual SchedulerResult configure(const SchedulerConfig& config) = 0;

  virtual SchedulerResult
  add(const std::string& instance_name, ITimerBlock& block, const ExecutionConfig& config) = 0;

  // Resolve blocks referenced by scheduler-owned pipelines. Default is a no-op
  // for schedulers that plan individual blocks with add().
  virtual SchedulerResult bind_block(const std::string&, ITimerBlock&) {
    return SchedulerResult::SUCCESS;
  }
  // Optional capability: schedulers without event pipelines return nullptr.
  virtual IEventSignalProvider* event_signal_provider() { return nullptr; }

  virtual SchedulerResult remove(const std::string& instance_name) = 0;
  // Monotonic absolute origin shared by composable schedulers.
  virtual SchedulerResult start(std::chrono::nanoseconds origin) = 0;
  virtual SchedulerResult start() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      return SchedulerResult::SYSTEM_ERROR;
    }
    return start(std::chrono::seconds{now.tv_sec} + std::chrono::nanoseconds{now.tv_nsec});
  }
  virtual SchedulerResult stop() = 0;
  virtual SchedulerResult clear() = 0;
};

} // namespace bpe
