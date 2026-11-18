#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/itimer_block.hpp"

namespace bpe::test {

// Reusable direct-injection mock. A pluginlib adapter can be added for SchedulerPluginHost tests.
class SchedulerMock : public IBlockScheduler {
public:
  struct Entry {
    std::string name;
    ITimerBlock* block;
    ExecutionConfig config;
  };

  SchedulerResult configure(const SchedulerConfig&) override;
  SchedulerResult add(const std::string&, ITimerBlock&, const ExecutionConfig&) override;
  SchedulerResult remove(const std::string&) override;
  SchedulerResult start() override;
  SchedulerResult start(std::chrono::nanoseconds) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

  const std::vector<Entry>& entries() const { return entries_; }
  const std::vector<std::string>& calls() const { return calls_; }
  void set_add_result(SchedulerResult result) { add_result_ = result; }
  void set_clear_result(SchedulerResult result) { clear_result_ = result; }
  void reset();
  ExecutionResult execute(const std::string& name);

private:
  std::vector<Entry> entries_;
  std::vector<std::string> calls_;
  SchedulerResult add_result_{SchedulerResult::SUCCESS};
  SchedulerResult clear_result_{SchedulerResult::SUCCESS};
  bool running_{false};
};

} // namespace bpe::test
