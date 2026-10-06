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

      executor.remove_node(node->get_node_base_interface());

      if (rclcpp::ok()) {
        using CallbackReturn =
            rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

        CallbackReturn callback_result = CallbackReturn::SUCCESS;
        node->shutdown(callback_result);

        if (callback_result != CallbackReturn::SUCCESS) {
          RCLCPP_ERROR(node->get_logger(), "BlockPluginEngine shutdown transition failed");
          result = 1;
        }
      } else {
        RCLCPP_ERROR(node->get_logger(), "ROS context became invalid before BlockPluginEngine shutdown");
        result = 1;
      }
    }
  }

  rclcpp::shutdown();

  return result;
}
