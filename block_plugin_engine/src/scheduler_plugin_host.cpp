#include "detail/scheduler_plugin_host.hpp"

#include <pluginlib/exceptions.hpp>

namespace bpe {
namespace {
constexpr const char* DEFAULT_SCHEDULER = "block_plugin_engine/FairScheduler";
}

SchedulerPluginHost::SchedulerPluginHost()
  : loader_("block_plugin_engine", "bpe::IBlockScheduler") {}

SchedulerPluginHost::~SchedulerPluginHost() {
  if (scheduler_) {
    scheduler_->stop();
    scheduler_.reset(); // Unload the instance before destroying the loader.
  }
}

SchedulerResult SchedulerPluginHost::configure(const SchedulerConfig& config) {
  if (scheduler_) {
    scheduler_->stop();
    scheduler_->clear();
    scheduler_.reset();
  }
  if (config && !config.IsMap()) {
    return SchedulerResult::INVALID_CONFIG;
  }
  try {
    const std::string type = config && config["type"]
        ? config["type"].as<std::string>() : DEFAULT_SCHEDULER;
    if (type.empty()) {
      return SchedulerResult::INVALID_CONFIG;
    }
    auto selected = loader_.createSharedInstance(type);
    const SchedulerResult result = selected->configure(config);
    if (result == SchedulerResult::SUCCESS) {
      scheduler_ = std::move(selected);
    }
    return result;
  } catch (const YAML::Exception&) {
    return SchedulerResult::INVALID_CONFIG;
  } catch (const pluginlib::PluginlibException&) {
    return SchedulerResult::INVALID_CONFIG;
  }
}

SchedulerResult SchedulerPluginHost::add(const std::string& name, ITimerBlock& block,
                                        const ExecutionConfig& config) {
  return scheduler_ ? scheduler_->add(name, block, config) : SchedulerResult::INVALID_STATE;
}
SchedulerResult SchedulerPluginHost::remove(const std::string& name) {
  return scheduler_ ? scheduler_->remove(name) : SchedulerResult::INVALID_STATE;
}
SchedulerResult SchedulerPluginHost::start() {
  return scheduler_ ? scheduler_->start() : SchedulerResult::INVALID_STATE;
}
SchedulerResult SchedulerPluginHost::stop() {
  return scheduler_ ? scheduler_->stop() : SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerPluginHost::clear() {
  return scheduler_ ? scheduler_->clear() : SchedulerResult::SUCCESS;
}
} // namespace bpe
