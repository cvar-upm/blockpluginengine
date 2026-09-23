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
#include "block_plugin_engine/itimer_block.hpp"

namespace bpe {
namespace {

std::chrono::nanoseconds parse_period(const YAML::Node& node) {
  if (!node || !node.IsScalar()) {
    throw std::runtime_error("execution.period must be a scalar");
  }

  const std::string value = node.as<std::string>();

  struct Unit {
    const char* suffix;
    std::int64_t multiplier;
  };

  static constexpr Unit units[] = {
      {"ns", 1},
      {"us", 1000},
      {"ms", 1000 * 1000},
      {"s", 1000 * 1000 * 1000},
  };

  for (const auto& unit : units) {
    const std::string suffix{unit.suffix};
    if (value.size() <= suffix.size() ||
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0) {
      continue;
    }

    const auto number = value.substr(0, value.size() - suffix.size());
    std::size_t parsed = 0;
    const auto amount = std::stoll(number, &parsed);
    if (parsed != number.size() || amount <= 0) {
      throw std::runtime_error("execution.period must be greater than zero");
    }

    return std::chrono::nanoseconds{amount * unit.multiplier};
  }

  throw std::runtime_error("execution.period must use ns, us, ms or s");
}

ExecutionConfig parse_execution_config(const YAML::Node& node) {
  if (!node.IsMap()) {
    throw std::runtime_error("execution must be a map");
  }

  if (!node["policy"]) {
    throw std::runtime_error("execution.policy is required");
  }

  const auto policy = node["policy"].as<std::string>();
  if (policy != "timer") {
    throw std::runtime_error("unsupported execution.policy: " + policy);
  }

  if (!node["period"]) {
    throw std::runtime_error("execution.period is required for timer policy");
  }

  ExecutionConfig config;
  config.policy = ExecutionPolicy::TIMER;
  config.period = parse_period(node["period"]);
  return config;
}

} // namespace

BlockPluginManager::BlockPluginManager(
    const std::shared_ptr<IBlockScheduler>& scheduler,
    const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& ros_topics,
    rclcpp::Logger logger,
    const std::vector<std::string>& plugin_xml_paths)
  : scheduler_(scheduler),
    ros_topics_(ros_topics),
    logger_(std::move(logger)),
    loader_("block_plugin_engine", "bpe::IBlockPlugin", "plugin", plugin_xml_paths) {}

bool BlockPluginManager::configure(const std::string& config_file) {
  if (!blocks_.empty()) {
    RCLCPP_ERROR(logger_, "BlockPluginManager is already configured");
    return false;
  }

  std::vector<BlockInstanceConfig> configs;
  if (!load_configuration(config_file, configs)) {
    return false;
  }

  for (const auto& config : configs) {
    if (!configure_instance(config)) {
      rollback_configuration();
      return false;
    }
  }

  return true;
}

bool BlockPluginManager::start() {
  std::size_t started = 0;

  for (auto& instance : blocks_) {
    if (instance.plugin->start() == BlockResult::SUCCESS) {
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
  return true;
}

bool BlockPluginManager::stop() {
  bool success = true;

  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    if (it->plugin->stop() != BlockResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Failed to stop Block '%s'", it->name.c_str());
      success = false;
    }
  }

  return success;
}

bool BlockPluginManager::cleanup() {
  bool success = true;

  scheduler_->clear();

  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    if (it->plugin->cleanup() != BlockResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Failed to clean up Block '%s'", it->name.c_str());
      success = false;
    }
  }

  blocks_.clear();
  return success;
}

bool BlockPluginManager::load_configuration(const std::string& config_file,
                                            std::vector<BlockInstanceConfig>& configs) const {
  try {
    const auto root = YAML::LoadFile(config_file);
    const auto blocks = root["blocks"];

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
        config.execution = parse_execution_config(node["execution"]);
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

  try {
    plugin = loader_.createSharedInstance(config.type);
  } catch (const pluginlib::PluginlibException& error) {
    RCLCPP_ERROR(logger_, "Could not load Block type '%s': %s", config.type.c_str(), error.what());
    return false;
  }

  if (auto* ros = dynamic_cast<IRosBoundaryBlock*>(plugin.get())) {
    ros->set_ros_interface(ros_topics_);
  }

  if (plugin->configure(config.config) != BlockResult::SUCCESS) {
    RCLCPP_ERROR(logger_, "Failed to configure Block '%s'", config.name.c_str());
    return false;
  }

  blocks_.push_back({config.name, plugin});

  if (config.execution) {
    auto* timer = dynamic_cast<ITimerBlock*>(plugin.get());
    if (timer == nullptr) {
      RCLCPP_ERROR(logger_,
                   "Block '%s' defines execution settings but does not implement ITimerBlock",
                   config.name.c_str());
      return false;
    }

    const SchedulerResult scheduler_result =
        scheduler_->add(config.name, *timer, *config.execution);

    if (scheduler_result != SchedulerResult::SUCCESS) {
      RCLCPP_ERROR(logger_, "Could not register Block '%s' in the scheduler", config.name.c_str());
      return false;
    }
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
