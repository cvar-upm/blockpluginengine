#pragma once

#include <memory>
#include <string>
#include <pluginlib/class_loader.hpp>
#include "block_plugin_engine/iblock_scheduler.hpp"

namespace bpe {

// Loads the selected scheduler on configure, while preserving the shared
// IBlockScheduler instance already owned by the lifecycle node and manager.
class SchedulerPluginHost final : public IBlockScheduler {
public:
  SchedulerPluginHost();
  ~SchedulerPluginHost() override;
  SchedulerResult configure(const SchedulerConfig& config) override;
  SchedulerResult add(const std::string&, ITimerBlock&, const ExecutionConfig&) override;
  SchedulerResult bind_block(const std::string&, ITimerBlock&) override;
  IEventSignalProvider* event_signal_provider() override;
  SchedulerResult remove(const std::string&) override;
  using IBlockScheduler::start;
  SchedulerResult start(std::chrono::nanoseconds origin) override;
  SchedulerResult stop() override;
  SchedulerResult clear() override;
private:
  pluginlib::ClassLoader<IBlockScheduler> loader_;
  std::shared_ptr<IBlockScheduler> scheduler_;
};

} // namespace bpe
