#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "detail/bpe_lifecycle_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<bpe::BPE_LifecycleNode>();
  int result = 0;

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
    rclcpp::spin(node->get_node_base_interface());
  }

  rclcpp::shutdown();

  return result;
}
