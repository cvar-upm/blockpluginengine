#include "detail/bpe_lifecycle_node.hpp"

#include <string>

#include "detail/bpe_composition_root.hpp"
#include "detail/block_plugin_manager.hpp"

namespace bpe {

BPE_LifecycleNode::BPE_LifecycleNode(BPECompositionRoot& root,
                                     const rclcpp::NodeOptions& options)
    : rclcpp_lifecycle::LifecycleNode("block_plugin_engine", options), root_(root) {
  declare_parameter<std::string>("config_file", "");
  declare_parameter<bool>("autostart", false);
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_configure(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Configuring BlockPluginEngine");
  if (!root_.configure(get_parameter("config_file").as_string()))
    return CallbackReturn::FAILURE;
  RCLCPP_INFO(get_logger(), "BlockPluginEngine configured");
  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_activate(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Activating BlockPluginEngine");
  if (!root_.plugin_manager().start()) {
    RCLCPP_ERROR(get_logger(), "Could not start BlockPlugins");
    return CallbackReturn::FAILURE;
  }
  if (root_.scheduler()->start() != SchedulerResult::SUCCESS) {
    root_.plugin_manager().stop();
    RCLCPP_ERROR(get_logger(), "Could not start BlockScheduler");
    return CallbackReturn::FAILURE;
  }
  RCLCPP_INFO(get_logger(), "BlockPluginEngine active");
  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_deactivate(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Deactivating BlockPluginEngine");
  root_.scheduler()->stop();
  if (!root_.plugin_manager().stop()) {
    RCLCPP_ERROR(get_logger(), "Could not stop BlockPlugins");
    return CallbackReturn::FAILURE;
  }
  RCLCPP_INFO(get_logger(), "BlockPluginEngine inactive");
  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_cleanup(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Cleaning up BlockPluginEngine");
  if (!root_.cleanup()) {
    RCLCPP_ERROR(get_logger(), "Could not clean up BlockPlugins");
    return CallbackReturn::FAILURE;
  }
  RCLCPP_INFO(get_logger(), "BlockPluginEngine cleaned up");
  return CallbackReturn::SUCCESS;
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_shutdown(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Shutting down BlockPluginEngine");
  if (root_.scheduler()) root_.scheduler()->stop();
  root_.plugin_manager().stop();
  root_.cleanup();
  RCLCPP_INFO(get_logger(), "BlockPluginEngine shutdown completed");
  return CallbackReturn::SUCCESS;
}

} // namespace bpe
