// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <rclcpp/node_interfaces/node_topics_interface.hpp>

namespace bpe {

class IRosBoundaryBlock {
public:
  virtual ~IRosBoundaryBlock() = default;

  virtual void
  set_ros_interface(
      const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics) = 0;
};

} // namespace bpe
