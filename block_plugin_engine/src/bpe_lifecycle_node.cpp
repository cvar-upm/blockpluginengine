#include "detail/bpe_lifecycle_node.hpp"

#include <string>

#include "detail/block_plugin_manager.hpp"
#include "detail/bpe_simple_scheduler.hpp"

namespace bpe {

BPE_LifecycleNode::BPE_LifecycleNode(const rclcpp::NodeOptions& options)
  : rclcpp_lifecycle::LifecycleNode("block_plugin_engine", options) {
  declare_parameter<std::string>("config_file", "");

  scheduler_ = std::make_shared<BPE_SimpleScheduler>();
  plugin_manager_ =
      std::make_shared<BlockPluginManager>(scheduler_, get_node_topics_interface(), get_logger());
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_configure(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Configuring BlockPluginEngine");

  const auto config_file = get_parameter("config_file").as_string();
  if (config_file.empty()) {
    RCLCPP_ERROR(get_logger(), "Parameter 'config_file' cannot be empty");
    return CallbackReturn::FAILURE;
  }

  if (!plugin_manager_->configure(config_file)) {
    RCLCPP_ERROR(get_logger(), "BlockPluginManager configuration failed");
    return CallbackReturn::FAILURE;
  }

  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_activate(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Activating BlockPluginEngine");

  if (!plugin_manager_->start()) {
    RCLCPP_ERROR(get_logger(), "Could not start BlockPlugins");
    return CallbackReturn::FAILURE;
  }

  if (scheduler_->start() != SchedulerResult::SUCCESS) {
    plugin_manager_->stop();
    RCLCPP_ERROR(get_logger(), "Could not start BlockScheduler");
    return CallbackReturn::FAILURE;
  }

  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_deactivate(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Deactivating BlockPluginEngine");

  scheduler_->stop();

  if (!plugin_manager_->stop()) {
    RCLCPP_ERROR(get_logger(), "Could not stop BlockPlugins");
    return CallbackReturn::FAILURE;
  }

  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_cleanup(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Cleaning up BlockPluginEngine");

  scheduler_->stop();

  if (!plugin_manager_->cleanup()) {
    RCLCPP_ERROR(get_logger(), "Could not clean up BlockPlugins");
    return CallbackReturn::FAILURE;
  }

  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_shutdown(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Shutting down BlockPluginEngine");

  scheduler_->stop();
  plugin_manager_->stop();
  plugin_manager_->cleanup();

  return CallbackReturn::SUCCESS;
}

} // namespace bpe
