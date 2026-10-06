#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/qos.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/iros_boundary_block.hpp"
#include "block_plugin_engine_system_test/ros_topic_helpers.hpp"

namespace bpe_system_test {

class RosTwistInputBlock final
  : public bpe::IBlockPlugin,
    public bpe::IRosBoundaryBlock {
public:
  using Message = geometry_msgs::msg::TwistStamped;

  void set_ros_interface(
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics) override {
    topics_ = std::move(topics);
  }

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (!topics_ || !config["topic"] || !config["blackboard_output"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        topic_ = config["topic"].as<std::string>();
        blackboard_output_ =
          config["blackboard_output"].as<std::string>();

        producer_.bind(blackboard_output_);

        subscription_ =
          create_subscription<Message>(
            topics_,
            topic_,
            rclcpp::QoS{10},
            [this](std::shared_ptr<Message> message) {
              if (active_.load(std::memory_order_acquire)) {
                auto grant = producer_.write_access();
                grant.value() = *message;
              }
            });

        if (!subscription_) {
          result = bpe::BlockResult::FAILURE;
        }
      }
    } catch (...) {
      result = bpe::BlockResult::FAILURE;
    }

    return result;
  }

  bpe::BlockResult start() override {
    active_.store(true, std::memory_order_release);
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult stop() override {
    active_.store(false, std::memory_order_release);
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult cleanup() override {
    active_.store(false, std::memory_order_release);
    subscription_.reset();
    return bpe::BlockResult::SUCCESS;
  }

private:
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  rclcpp::Subscription<Message>::SharedPtr subscription_;

  bpe::TypedProducer<Message> producer_;

  std::string topic_;
  std::string blackboard_output_;

  std::atomic<bool> active_{false};
};

} // namespace bpe_system_test

PLUGINLIB_EXPORT_CLASS(
  bpe_system_test::RosTwistInputBlock,
  bpe::IBlockPlugin)
