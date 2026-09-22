#include "detail/bpe_lifecycle_node.hpp"

#include "detail/block_plugin_manager.hpp"
#include "detail/bpe_simple_scheduler.hpp"

namespace bpe {

BPE_LifecycleNode::BPE_LifecycleNode(const rclcpp::NodeOptions& options)
  : rclcpp_lifecycle::LifecycleNode("block_plugin_engine", options) {
  scheduler_ = std::make_shared<BPE_SimpleScheduler>();
  plugin_manager_ = std::make_shared<BlockPluginManager>(scheduler_);
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_configure(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Configuring BlockPluginEngine");

  if (!plugin_manager_->configure()) {
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

  scheduler_->start();

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
