#pragma once

#include <memory>

#include <rclcpp_lifecycle/lifecycle_node.hpp>

namespace bpe {

class BPE_LifecycleNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit BPE_LifecycleNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

protected:
  CallbackReturn on_configure(const rclcpp_lifecycle::&) override;

  CallbackReturn on_activate(const rclcpp_lifecycle::&) override;

  CallbackReturn on_deactivate(const rclcpp_lifecycle::&) override;

  CallbackReturn on_cleanup(const rclcpp_lifecycle::&) override;

  CallbackReturn on_shutdown(const rclcpp_lifecycle::&) override;

private:
  std::shared_ptr<BlockScheduler> scheduler_;
  std::shared_ptr<BlockPluginManager> plugin_manager_;
};

} // namespace bpe
