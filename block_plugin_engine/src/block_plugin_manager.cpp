#include "detail/block_plugin_manager.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <pluginlib/exceptions.hpp>
#include <rclcpp/logging.hpp>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/iros_boundary_block.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/event_signal.hpp"

namespace bpe {

BlockPluginManager::BlockPluginManager(
    const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& ros_topics,
    rclcpp::Logger logger,
    const std::vector<std::string>& plugin_xml_paths)
  : ros_topics_(ros_topics),
    logger_(std::move(logger)),
    loader_("block_plugin_engine", "bpe::IBlockPlugin", "plugin", plugin_xml_paths) {}

void BlockPluginManager::set_scheduler(std::shared_ptr<IBlockScheduler> scheduler) {
  // Scheduler replacement is legal only when no block instances are loaded.
  if (!blocks_.empty()) throw std::logic_error("Cannot replace scheduler with loaded blocks");
  scheduler_ = std::move(scheduler);
}

bool BlockPluginManager::configure(const YAML::Node& blocks_config) {
  RCLCPP_DEBUG(logger_, "BlockPluginManager: configuring plugins");
  if (!scheduler_) {
    RCLCPP_ERROR(logger_, "BlockPluginManager has no configured scheduler");
    return false;
  }
  if (!blocks_.empty()) {
    RCLCPP_ERROR(logger_, "BlockPluginManager is already configured");
    return false;
  }

  std::vector<BlockInstanceConfig> configs;
  if (!load_configuration(blocks_config, configs)) {
    return false;
  }

  RCLCPP_DEBUG(logger_, "BlockPluginManager: %zu plugin configurations loaded", configs.size());

  for (const auto& config : configs) {
    if (!configure_instance(config)) {
      rollback_configuration();
      return false;
    }
  }

  RCLCPP_DEBUG(logger_, "BlockPluginManager: configuration completed");
  return true;
}

bool BlockPluginManager::start() {
  RCLCPP_DEBUG(logger_, "BlockPluginManager: starting plugins");
  std::size_t started = 0;

  for (auto& instance : blocks_) {
    RCLCPP_DEBUG(logger_, "Starting Block '%s'", instance.name.c_str());
    if (instance.plugin->start() == BlockResult::SUCCESS) {
      RCLCPP_DEBUG(logger_, "Started Block '%s'", instance.name.c_str());
      ++started;
    } else {
      RCLCPP_ERROR(logger_, "Failed to start Block '%s'", instance.name.c_str());

      while (started > 0) {
        --started;
        blocks_[started].plugin->stop();
      }
      return false;
    }
  }
  RCLCPP_DEBUG(logger_, "BlockPluginManager: plugins started");
  return true;
}

bool BlockPluginManager::stop() {
  bool success = true;

  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    RCLCPP_DEBUG(logger_, "Stopping Block '%s'", it->name.c_str());

    if (it->plugin->stop() != BlockResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Failed to stop Block '%s'", it->name.c_str());
      success = false;
    } else {
      RCLCPP_DEBUG(logger_, "Stopped Block '%s'", it->name.c_str());
    }
  }

  return success;
}

bool BlockPluginManager::cleanup() {
  bool success = true;

  RCLCPP_DEBUG(logger_, "Clearing scheduler block references");
  if (scheduler_) scheduler_->clear();
  RCLCPP_DEBUG(logger_, "Scheduler block references cleared");

  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    RCLCPP_DEBUG(logger_, "Cleaning up Block '%s'", it->name.c_str());

    if (it->plugin->cleanup() != BlockResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Failed to clean up Block '%s'", it->name.c_str());
      success = false;
    } else {
      RCLCPP_DEBUG(logger_, "Cleaned up Block '%s'", it->name.c_str());
    }
  }

  RCLCPP_DEBUG(logger_, "Destroying Block instances");
  blocks_.clear();
  RCLCPP_DEBUG(logger_, "Block instances destroyed");
  return success;
}

bool BlockPluginManager::load_configuration(const YAML::Node& blocks,
                                            std::vector<BlockInstanceConfig>& configs) const {
  try {
    if (!blocks || !blocks.IsSequence()) {
      RCLCPP_ERROR(logger_, "Configuration must contain a 'blocks' sequence");
      return false;
    }

    std::unordered_set<std::string> names;
    configs.reserve(blocks.size());

    for (const auto& node : blocks) {
      if (!node.IsMap() || !node["name"] || !node["type"]) {
        RCLCPP_ERROR(logger_, "Every Block must define 'name' and 'type'");
        return false;
      }

      BlockInstanceConfig config;
      config.name = node["name"].as<std::string>();
      config.type = node["type"].as<std::string>();
      config.config = node["config"] ? node["config"] : YAML::Node{};

      if (config.name.empty() || config.type.empty()) {
        RCLCPP_ERROR(logger_, "Block name and type cannot be empty");
        return false;
      }

      if (!names.insert(config.name).second) {
        RCLCPP_ERROR(logger_, "Duplicated Block instance name '%s'", config.name.c_str());
        return false;
      }

      if (node["execution"]) {
        config.execution = node["execution"];
      }

      configs.push_back(std::move(config));
    }
  } catch (const YAML::Exception& error) {
    RCLCPP_ERROR(logger_, "YAML configuration error: %s", error.what());
    return false;
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger_, "Invalid BlockPluginEngine configuration: %s", error.what());
    return false;
  }

  return true;
}

bool BlockPluginManager::configure_instance(const BlockInstanceConfig& config) {
  std::shared_ptr<IBlockPlugin> plugin;

  RCLCPP_DEBUG(logger_,
               "Loading Block '%s' of type '%s'",
               config.name.c_str(),
               config.type.c_str());

  try {
    plugin = loader_.createSharedInstance(config.type);
  } catch (const pluginlib::PluginlibException& error) {
    RCLCPP_ERROR(logger_, "Could not load Block type '%s': %s", config.type.c_str(), error.what());
    return false;
  }

  RCLCPP_DEBUG(logger_, "Loaded Block '%s'", config.name.c_str());

  if (auto* ros = dynamic_cast<IRosBoundaryBlock*>(plugin.get())) {
    RCLCPP_DEBUG(logger_, "Injecting ROS interface into Block '%s'", config.name.c_str());
    ros->set_ros_interface(ros_topics_);
  }

  if (auto* events = dynamic_cast<IEventSignalUser*>(plugin.get())) {
    if (auto* provider = dynamic_cast<IEventSignalProvider*>(scheduler_.get())) {
      events->set_event_signal_provider(*provider);
    } else {
      RCLCPP_WARN(logger_, "Block '%s' supports event signals, but scheduler does not",
                  config.name.c_str());
    }
  }

  RCLCPP_DEBUG(logger_, "Configuring Block '%s'", config.name.c_str());
  if (plugin->configure(config.config) != BlockResult::SUCCESS) {
    RCLCPP_ERROR(logger_, "Failed to configure Block '%s'", config.name.c_str());
    return false;
  }

  RCLCPP_DEBUG(logger_, "Configured Block '%s'", config.name.c_str());
  blocks_.push_back({config.name, plugin});

  // The scheduler alone interprets the execution node. For pipeline schedulers
  // it is ignored; for periodic schedulers it defines individual registration.
  if (auto* timer = dynamic_cast<IScheduledBlock*>(plugin.get())) {
    const auto result = scheduler_->register_block(config.name, *timer, config.execution);
    if (result != SchedulerResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Could not register Block '%s' in the scheduler", config.name.c_str());
      return false;
    }
  } else if (config.execution) {
    // With the IScheduledBlock-based API a non-executable block cannot be scheduled.
    // It is still valid for this block to carry an unused execution node.
    RCLCPP_DEBUG(logger_, "Ignoring execution configuration for non-IScheduledBlock '%s'",
                 config.name.c_str());
  }

  return true;
}

void BlockPluginManager::rollback_configuration() {
  scheduler_->clear();

  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    it->plugin->cleanup();
  }

  blocks_.clear();
}

} // namespace bpe
