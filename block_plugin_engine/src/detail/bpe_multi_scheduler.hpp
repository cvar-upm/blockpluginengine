#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <pluginlib/class_loader.hpp>
#include "block_plugin_engine/iblock_scheduler.hpp"

namespace bpe {

// Static composition of independently configured scheduler plugins.
// All components receive the same absolute CLOCK_MONOTONIC origin.
class BPE_MultiScheduler final : public IBlockScheduler, public IEventSignalProvider {
public:
  BPE_MultiScheduler();
  ~BPE_MultiScheduler() override;

  SchedulerResult configure(const SchedulerConfig& config) override;
  SchedulerResult add(const std::string& name, ITimerBlock& block,
                      const ExecutionConfig& config) override;
  SchedulerResult bind_block(const std::string& name, ITimerBlock& block) override;
  IEventSignalProvider* event_signal_provider() override { return this; }
  std::shared_ptr<IEventSignal> get_signal(std::string_view name) override;
  SchedulerResult remove(const std::string& name) override;
  using IBlockScheduler::start;
  SchedulerResult start(std::chrono::nanoseconds origin) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;

private:
  struct Entry {
    std::string name;
    std::size_t index;
  };
  pluginlib::ClassLoader<IBlockScheduler> loader_;
  std::vector<std::shared_ptr<IBlockScheduler>> schedulers_;
  std::vector<Entry> entries_;
  std::chrono::nanoseconds start_delay_{std::chrono::milliseconds{100}};
  bool running_{false};
};

} // namespace bpe
