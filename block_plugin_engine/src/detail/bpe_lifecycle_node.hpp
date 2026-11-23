#pragma once

#include <rclcpp_lifecycle/lifecycle_node.hpp>

namespace bpe {
class BPECompositionRoot;

class BPE_LifecycleNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit BPE_LifecycleNode(BPECompositionRoot& root,
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

protected:
  CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;

private:
  BPECompositionRoot& root_;
};
} // namespace bpe
