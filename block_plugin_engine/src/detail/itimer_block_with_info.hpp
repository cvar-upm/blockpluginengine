#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>

#include "block_plugin_engine/ischeduled_block.hpp"

namespace bpe {

// Times use CLOCK_MONOTONIC's epoch. Index starts at zero for each start().
struct TimerExecutionInfo {
  std::uint64_t activation_index{0};
  std::chrono::nanoseconds scheduled_time{0};
  std::chrono::nanoseconds lateness{0};
};

class ITimerBlockWithInfo : public IScheduledBlock {
public:
  ExecutionResult execute() override {
    throw std::logic_error("ITimerBlockWithInfo requires execution information");
  }

  virtual ExecutionResult execute(const TimerExecutionInfo& info) = 0;
};

} // namespace bpe
