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
#include "block_plugin_engine/itimer_block.hpp"
#include "block_plugin_engine_system_test/ros_topic_helpers.hpp"

namespace bpe_system_test {

class RosTwistOutputBlock final
  : public bpe::IBlockPlugin,
    public bpe::IRosBoundaryBlock,
    public bpe::ITimerBlock {
public:
  using Message = geometry_msgs::msg::TwistStamped;

  void set_ros_interface(
    rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics) override {
    topics_ = std::move(topics);
  }

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (!topics_ || !config["topic"] || !config["blackboard_input"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        topic_ = config["topic"].as<std::string>();
        blackboard_input_ =
          config["blackboard_input"].as<std::string>();

        consumer_.bind(blackboard_input_);

        publisher_ =
          create_publisher<Message>(
            topics_,
            topic_,
            rclcpp::QoS{10});

        if (!publisher_) {
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
    publisher_.reset();
    return bpe::BlockResult::SUCCESS;
  }

  bpe::ExecutionResult execute() override {
    bpe::ExecutionResult result = bpe::ExecutionResult::SUCCESS;

    if (active_.load(std::memory_order_acquire)) {
      auto input = consumer_.read_access();

      if (input.has_new_value()) {
        publisher_->publish(input.value());
      }
    }

    return result;
  }

private:
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  rclcpp::Publisher<Message>::SharedPtr publisher_;

  bpe::TypedConsumer<Message> consumer_;

  std::string topic_;
  std::string blackboard_input_;

  std::atomic<bool> active_{false};
};

} // namespace bpe_system_test

PLUGINLIB_EXPORT_CLASS(
  bpe_system_test::RosTwistOutputBlock,
  bpe::IBlockPlugin)
