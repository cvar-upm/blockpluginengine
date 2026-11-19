#pragma once

#include <string>
#include <utility>

#include "block_plugin_engine/itimer_block.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {

class RecordingTimerBlock final : public ITimerBlock {
public:
  explicit RecordingTimerBlock(std::string name) : name_(std::move(name)) {}

  ExecutionResult execute() override {
    record_event(name_);
    return ExecutionResult::SUCCESS;
  }

private:
  std::string name_;
};

} // namespace bpe::test
