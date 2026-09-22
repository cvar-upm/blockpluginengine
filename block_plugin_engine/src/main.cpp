#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "detail/bpe_lifecycle_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<bpe::BPE_LifecycleNode>();

  rclcpp::spin(node->get_node_base_interface());

  rclcpp::shutdown();

  return 0;
}
