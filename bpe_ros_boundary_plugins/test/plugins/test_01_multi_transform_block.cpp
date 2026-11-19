#include <cstddef>
#include <string>

#include <geometry_msgs/msg/twist.hpp>
#include <pluginlib/class_list_macros.hpp>
#include <std_msgs/msg/int32.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"

namespace bpe_ros_boundary_plugins::test {

// The same instance is intentionally registered in two event pipelines.
// A complete output pair is published only when *both* inputs have changed.
class MultiTransformBlock final : public bpe::IBlockPlugin, public bpe::ITimerBlock {
public:
  using Number = std_msgs::msg::Int32;
  using Twist = geometry_msgs::msg::Twist;

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    try {
      if (!config["inputs"] || !config["outputs"] ||
          !config["inputs"].IsSequence() || !config["outputs"].IsSequence() ||
          config["inputs"].size() != 2 || config["outputs"].size() != 2)
        return bpe::BlockResult::FAILURE;
      inputs_.bind(config["inputs"][0].as<std::string>(),
                   config["inputs"][1].as<std::string>());
      outputs_.bind(config["outputs"][0].as<std::string>(),
                    config["outputs"][1].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }

  bpe::ExecutionResult execute() override {
    try {
      {
        auto input = inputs_.read_access();
        if (input.get<0>().has_new_value()) {
          latest_number_ = input.get<0>().value();
          number_dirty_ = true;
        }
        if (input.get<1>().has_new_value()) {
          latest_twist_ = input.get<1>().value();
          twist_dirty_ = true;
        }
      }
      if (number_dirty_ && twist_dirty_) {
        {
          auto output = outputs_.write_access();
          output.get<0>().value().data = latest_number_.data + 10;
          output.get<1>().value() = latest_twist_;
          output.get<1>().value().linear.x *= 2.0;
          output.get<1>().value().angular.z += 0.25;
        }
        number_dirty_ = false;
        twist_dirty_ = false;
      }
      return bpe::ExecutionResult::SUCCESS;
    } catch (...) {
      return bpe::ExecutionResult::FAILURE;
    }
  }

private:
  bpe::MultiConsumer<Number, Twist> inputs_;
  bpe::MultiProducer<Number, Twist> outputs_;
  Number latest_number_{};
  Twist latest_twist_{};
  bool number_dirty_{false};
  bool twist_dirty_{false};
};

} // namespace bpe_ros_boundary_plugins::test

PLUGINLIB_EXPORT_CLASS(bpe_ros_boundary_plugins::test::MultiTransformBlock, bpe::IBlockPlugin)
