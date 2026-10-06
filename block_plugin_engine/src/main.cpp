#include <chrono>
#include <csignal>
#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>

#include "detail/bpe_lifecycle_node.hpp"

namespace {

volatile std::sig_atomic_t shutdown_requested = 0;

void handle_signal(int) noexcept {
  shutdown_requested = 1;
}

} // namespace

int main(int argc, char* argv[]) {
  rclcpp::init(
      argc,
      argv,
      rclcpp::InitOptions(),
      rclcpp::SignalHandlerOptions::None);

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  int result = 0;

  {
    auto node = std::make_shared<bpe::BPE_LifecycleNode>();

    if (node->get_parameter("autostart").as_bool()) {
      using CallbackReturn =
          rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

      CallbackReturn callback_result = CallbackReturn::SUCCESS;
      node->configure(callback_result);

      if (callback_result != CallbackReturn::SUCCESS) {
        RCLCPP_ERROR(node->get_logger(), "BlockPluginEngine autostart configure transition failed");
        result = 1;
      } else {
        node->activate(callback_result);

        if (callback_result != CallbackReturn::SUCCESS) {
          RCLCPP_ERROR(node->get_logger(), "BlockPluginEngine autostart activate transition failed");
          result = 1;
        }
      }
    }

    if (result == 0) {
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.add_node(node->get_node_base_interface());

      while (rclcpp::ok() && shutdown_requested == 0) {
        executor.spin_once(std::chrono::milliseconds(20));
      }

      RCLCPP_INFO(node->get_logger(), "Main: leaving executor loop");
      executor.remove_node(node->get_node_base_interface());
      RCLCPP_INFO(node->get_logger(), "Main: node removed from executor");

      if (rclcpp::ok()) {
        using CallbackReturn =
            rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

        RCLCPP_INFO(node->get_logger(), "Main: requesting lifecycle shutdown");
        CallbackReturn callback_result = CallbackReturn::SUCCESS;
        node->shutdown(callback_result);

        if (callback_result != CallbackReturn::SUCCESS) {
          RCLCPP_ERROR(node->get_logger(), "BlockPluginEngine shutdown transition failed");
          result = 1;
        } else {
          RCLCPP_INFO(node->get_logger(), "Main: lifecycle shutdown completed");
        }
      } else {
        RCLCPP_ERROR(node->get_logger(), "ROS context became invalid before BlockPluginEngine shutdown");
        result = 1;
      }

      RCLCPP_INFO(node->get_logger(), "Main: executor scope ending");
    }

    RCLCPP_INFO(node->get_logger(), "Main: executor destroyed");
    RCLCPP_INFO(node->get_logger(), "Main: destroying lifecycle node");
    node.reset();
    RCLCPP_INFO(rclcpp::get_logger("block_plugin_engine"), "Main: lifecycle node destroyed");
  }

  RCLCPP_INFO(rclcpp::get_logger("block_plugin_engine"), "Main: shutting down ROS context");
  rclcpp::shutdown();

  return result;
}
