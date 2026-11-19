#include "detail/scheduler_plugin_host.hpp"

#include <pluginlib/exceptions.hpp>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>

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
  } catch (const YAML::Exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger("bpe.scheduler_plugin_host"),
                 "Invalid scheduler YAML: %s", error.what());
    return SchedulerResult::INVALID_CONFIG;
  } catch (const pluginlib::PluginlibException& error) {
    RCLCPP_ERROR(rclcpp::get_logger("bpe.scheduler_plugin_host"),
                 "Could not load scheduler plugin: %s", error.what());
    return SchedulerResult::INVALID_CONFIG;
  }
}

SchedulerResult SchedulerPluginHost::register_block(
    const std::string& name, ITimerBlock& block, const YAML::Node& execution) {
  return scheduler_ ? scheduler_->register_block(name, block, execution)
                    : SchedulerResult::INVALID_STATE;
}
IEventSignalProvider* SchedulerPluginHost::event_signal_provider() {
  return scheduler_ ? scheduler_->event_signal_provider() : nullptr;
}
SchedulerResult SchedulerPluginHost::remove(const std::string& name) {
  return scheduler_ ? scheduler_->remove(name) : SchedulerResult::INVALID_STATE;
}
SchedulerResult SchedulerPluginHost::start(std::chrono::nanoseconds origin) {
  return scheduler_ ? scheduler_->start(origin) : SchedulerResult::INVALID_STATE;
}
SchedulerResult SchedulerPluginHost::stop() {
  return scheduler_ ? scheduler_->stop() : SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerPluginHost::clear() {
  return scheduler_ ? scheduler_->clear() : SchedulerResult::SUCCESS;
}
} // namespace bpe
