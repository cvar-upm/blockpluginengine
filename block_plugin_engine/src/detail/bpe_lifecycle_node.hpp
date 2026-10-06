#pragma once

#include <memory>
#include <string>

#include <rclcpp/context.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>

namespace bpe {

class IBlockScheduler;
class BlockPluginManager;

class BPE_LifecycleNode : public rclcpp_lifecycle::LifecycleNode {
public:
  explicit BPE_LifecycleNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~BPE_LifecycleNode() override;

protected:
  CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;

private:
  void register_rcl_preshutdown_callback();
  void on_rcl_preshutdown() noexcept;

  std::shared_ptr<IBlockScheduler> scheduler_;
  std::shared_ptr<BlockPluginManager> plugin_manager_;
  std::unique_ptr<rclcpp::PreShutdownCallbackHandle> rcl_preshutdown_callback_handle_;
};

} // namespace bpe
