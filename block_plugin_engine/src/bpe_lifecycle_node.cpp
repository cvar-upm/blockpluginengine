#include "detail/bpe_lifecycle_node.hpp"

#include <functional>
#include <string>

#include <lifecycle_msgs/msg/state.hpp>

#include "detail/block_plugin_manager.hpp"
#include "detail/bpe_simple_scheduler.hpp"
#include "detail/runtime_state.hpp"

namespace bpe {

BPE_LifecycleNode::BPE_LifecycleNode(const rclcpp::NodeOptions& options)
  : rclcpp_lifecycle::LifecycleNode("block_plugin_engine", options) {
  declare_parameter<std::string>("config_file", "");
  declare_parameter<bool>("autostart", false);

  scheduler_ = std::make_shared<BPE_SimpleScheduler>();
  plugin_manager_ =
      std::make_shared<BlockPluginManager>(scheduler_, get_node_topics_interface(), get_logger());

  register_rcl_preshutdown_callback();
}

BPE_LifecycleNode::~BPE_LifecycleNode() {
  const auto context = get_node_base_interface()->get_context();

  if (context->is_valid()) {
    on_rcl_preshutdown();
  }

  if (rcl_preshutdown_callback_handle_) {
    context->remove_pre_shutdown_callback(*rcl_preshutdown_callback_handle_);
    rcl_preshutdown_callback_handle_.reset();
  }
}

void BPE_LifecycleNode::register_rcl_preshutdown_callback() {
  const auto context = get_node_base_interface()->get_context();
  rcl_preshutdown_callback_handle_ = std::make_unique<rclcpp::PreShutdownCallbackHandle>(
      context->add_pre_shutdown_callback(
          std::bind(&BPE_LifecycleNode::on_rcl_preshutdown, this)));
}

void BPE_LifecycleNode::on_rcl_preshutdown() noexcept {
  try {
    if (get_current_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED) {
      CallbackReturn callback_result = CallbackReturn::SUCCESS;
      const auto& final_state = shutdown(callback_result);

      if (callback_result != CallbackReturn::SUCCESS ||
          final_state.id() != lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED) {
        RCLCPP_ERROR(get_logger(), "Could not finalize BlockPluginEngine before rcl shutdown");
      }
    }
  } catch (const std::exception& error) {
    RCLCPP_ERROR(get_logger(), "BlockPluginEngine pre-shutdown failed: %s", error.what());
  } catch (...) {
    RCLCPP_ERROR(get_logger(), "BlockPluginEngine pre-shutdown failed with an unknown error");
  }
}

BPE_LifecycleNode::CallbackReturn BPE_LifecycleNode::on_configure(const rclcpp_lifecycle::State&) {
  RCLCPP_INFO(get_logger(), "Configuring BlockPluginEngine");

  const auto config_file = get_parameter("config_file").as_string();
  if (config_file.empty()) {
    RCLCPP_ERROR(get_logger(), "Parameter 'config_file' cannot be empty");
    return CallbackReturn::FAILURE;
  }

  YAML::Node config;

  try {
    config = YAML::LoadFile(config_file);
  } catch (const YAML::Exception& error) {
    RCLCPP_ERROR(get_logger(),
                 "Could not load configuration file '%s': %s",
                 config_file.c_str(),
                 error.what());
    return CallbackReturn::FAILURE;
  }

  if (scheduler_->configure(config["scheduler"]) != SchedulerResult::SUCCESS) {
    RCLCPP_ERROR(get_logger(), "Could not configure BlockScheduler");
    return CallbackReturn::FAILURE;
  }

  if (!plugin_manager_->configure(config["blocks"])) {
    detail::reset_blackboard();
    detail::reset_ros_type_registry();
    RCLCPP_ERROR(get_logger(), "Could not configure BlockPlugins");
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
  const bool cleanup_ok = plugin_manager_->cleanup();

  detail::reset_blackboard();
  detail::reset_ros_type_registry();

  if (!cleanup_ok) {
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

  detail::reset_blackboard();
  detail::reset_ros_type_registry();

  return CallbackReturn::SUCCESS;
}

} // namespace bpe
