#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/create_generic_subscription.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/serialized_message.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/iros_boundary_block.hpp"
#include "detail/serialized_blackboard.hpp"

namespace bpe_ros_boundary_plugins {

class RosInputBlock final
  : public bpe::IBlockPlugin,
    public bpe::IRosBoundaryBlock,
    public bpe::IEventSignalUser {
public:
  void set_ros_interface(
      const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics) override {
    topics_ = topics;
  }

  void set_event_signal_provider(bpe::IEventSignalProvider& provider) override {
    signal_provider_ = &provider;
  }

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (topics_ == nullptr || !config["topic"] || !config["ros_type"] ||
          !config["blackboard_output"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        topic_ = config["topic"].as<std::string>();
        ros_type_ = config["ros_type"].as<std::string>();
        blackboard_output_ = config["blackboard_output"].as<std::string>();

        if (config["signal"]) {
          const auto name = config["signal"].as<std::string>();
          if (name.empty() || !signal_provider_ ||
              !(signal_ = signal_provider_->get_signal(name))) {
            return bpe::BlockResult::FAILURE;
          }
        }

        if (config["qos_depth"]) {
          qos_depth_ = config["qos_depth"].as<std::size_t>();
        }

        if (topic_.empty() || ros_type_.empty() || blackboard_output_.empty() || qos_depth_ == 0u) {
          result = bpe::BlockResult::FAILURE;
        } else {
          producer_.bind(blackboard_output_, ros_type_);

          subscription_ = rclcpp::create_generic_subscription(
              topics_,
              topic_,
              ros_type_,
              rclcpp::QoS(qos_depth_),
              std::bind(&RosInputBlock::on_message, this, std::placeholders::_1));
        }
      }
    } catch (...) {
      result = bpe::BlockResult::FAILURE;
    }

    return result;
  }

  bpe::BlockResult start() override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    if (!producer_.has_type_operations()) {
      result = bpe::BlockResult::FAILURE;
    } else {
      active_.store(true, std::memory_order_release);
    }

    return result;
  }

  bpe::BlockResult stop() override {
    active_.store(false, std::memory_order_release);
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult cleanup() override {
    active_.store(false, std::memory_order_release);
    subscription_.reset();
    signal_.reset();
    return bpe::BlockResult::SUCCESS;
  }

private:
  void on_message(std::shared_ptr<rclcpp::SerializedMessage> message) {
    if (!active_.load(std::memory_order_acquire)) return;
    {
      // Commit and deserialize the Blackboard grant before notifying a pipeline.
      auto output = producer_.write_access();
      output.serialized() = *message;
    }
    if (signal_) signal_->signal();
  }

  detail::SerializedProducer producer_;
  bpe::IEventSignalProvider* signal_provider_{nullptr};
  std::shared_ptr<bpe::IEventSignal> signal_;

  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  rclcpp::GenericSubscription::SharedPtr subscription_;

  std::string topic_;
  std::string ros_type_;
  std::string blackboard_output_;
  std::size_t qos_depth_{10u};
  std::atomic<bool> active_{false};
};

} // namespace bpe_ros_boundary_plugins

PLUGINLIB_EXPORT_CLASS(
  bpe_ros_boundary_plugins::RosInputBlock,
  bpe::IBlockPlugin)
