#pragma once

#include <memory>

#include <rclcpp/serialized_message.hpp>

namespace bpe {

// Physical storage used by the BPE blackboard for ROS 2 data.
//
// Plugins should not manipulate these members directly. Typed and boundary
// adapters provide the supported access paths to the native and serialized
// representations.
struct RosProtectedData {
  RosProtectedData() = default;

  std::shared_ptr<void> native;
  rclcpp::SerializedMessage serialized;
};

} // namespace bpe
