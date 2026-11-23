#include "detail/bpe_composition_root.hpp"

#include <memory>
#include <utility>

#include <pluginlib/exceptions.hpp>
#include <rclcpp/logging.hpp>
#include <yaml-cpp/yaml.h>

#include "detail/block_plugin_manager.hpp"
#include "detail/bpe_lifecycle_node.hpp"
#include "detail/runtime_state.hpp"

namespace bpe {
namespace {
constexpr const char* DEFAULT_SCHEDULER = "block_plugin_engine/FairScheduler";
}

BPECompositionRoot::BPECompositionRoot(const rclcpp::NodeOptions& options)
    : scheduler_loader_("block_plugin_engine", "bpe::IBlockScheduler") {
  const auto logger = rclcpp::get_logger("block_plugin_engine");
  RCLCPP_INFO(logger, "Constructing BlockPluginEngine composition root");
  node_ = std::make_shared<BPE_LifecycleNode>(*this, options);
  plugin_manager_ = std::make_shared<BlockPluginManager>(
      node_->get_node_topics_interface(), node_->get_logger());
  RCLCPP_INFO(logger, "BlockPluginEngine composition root constructed");
}

bool BPECompositionRoot::configure(const std::string& config_file) {
  const auto logger = node_->get_logger();
  if (config_file.empty()) {
    RCLCPP_ERROR(logger, "Parameter 'config_file' cannot be empty");
    return false;
  }
  if (scheduler_) {
    RCLCPP_ERROR(logger, "Composition root already configured");
    return false;
  }

  YAML::Node config;
  try {
    config = YAML::LoadFile(config_file);
    const auto scheduler_config = config["scheduler"];
    if (scheduler_config && !scheduler_config.IsMap()) {
      RCLCPP_ERROR(logger, "Scheduler configuration must be a map");
      return false;
    }
    const auto type = scheduler_config && scheduler_config["type"]
        ? scheduler_config["type"].as<std::string>() : DEFAULT_SCHEDULER;
    if (type.empty()) {
      RCLCPP_ERROR(logger, "Scheduler type cannot be empty");
      return false;
    }

    // Keep the selected scheduler behind its concrete interface. No proxy is
    // involved: optional capabilities are negotiated using dynamic_cast.
    auto selected = scheduler_loader_.createSharedInstance(type);
    if (selected->configure(scheduler_config) != SchedulerResult::SUCCESS) {
      RCLCPP_ERROR(logger, "Could not configure BlockScheduler '%s'", type.c_str());
      return false;
    }
    scheduler_ = std::move(selected);
    plugin_manager_->set_scheduler(scheduler_);
    if (!plugin_manager_->configure(config["blocks"])) {
      RCLCPP_ERROR(logger, "Could not configure BlockPlugins");
      cleanup();
      return false;
    }
  } catch (const YAML::Exception& error) {
    RCLCPP_ERROR(logger, "Could not load or interpret configuration '%s': %s",
                 config_file.c_str(), error.what());
    cleanup();
    return false;
  } catch (const pluginlib::PluginlibException& error) {
    RCLCPP_ERROR(logger, "Could not load scheduler plugin: %s", error.what());
    cleanup();
    return false;
  } catch (const std::exception& error) {
    RCLCPP_ERROR(logger, "Could not configure BlockPluginEngine: %s", error.what());
    cleanup();
    return false;
  }
  return true;
}

bool BPECompositionRoot::cleanup() {
  if (scheduler_) scheduler_->stop();
  // The manager drops block instances (and scheduler references) before the
  // scheduler DSO is released. The node remains alive for ROS teardown.
  const bool ok = plugin_manager_->cleanup();
  plugin_manager_->set_scheduler({});
  scheduler_.reset();
  detail::reset_blackboard();
  detail::reset_ros_type_registry();
  return ok;
}

BPECompositionRoot::~BPECompositionRoot() {
  const auto logger = rclcpp::get_logger("block_plugin_engine");
  RCLCPP_INFO(logger, "Destroying BlockPluginEngine composition root");
  // The lifecycle node must be destroyed while block DSOs are still loaded.
  node_.reset();
  cleanup();
  plugin_manager_.reset();
  scheduler_.reset();
  RCLCPP_INFO(logger, "BlockPluginEngine composition root destroyed");
}

const std::shared_ptr<BPE_LifecycleNode>& BPECompositionRoot::node() const noexcept {
  return node_;
}

} // namespace bpe
