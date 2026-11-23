#include <string>

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <pluginlib/class_list_macros.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"

namespace bpe_test {

// Two channels deliberately share the same ROS message type.
class MultiTwistTransformBlock final : public bpe::IBlockPlugin, public bpe::ITimerBlock {
public:
  using TwistSt = geometry_msgs::msg::TwistStamped;

  enum InputId : std::size_t { FIRST_INPUT, SECOND_INPUT };
  enum OutputId : std::size_t { FIRST_OUTPUT, SECOND_OUTPUT };

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    try {
      if (!config["blackboard_inputs"] || !config["blackboard_outputs"]) {
        return bpe::BlockResult::FAILURE;
      }
      const auto inputs = config["blackboard_inputs"];
      const auto outputs = config["blackboard_outputs"];
      if (!inputs.IsSequence() || inputs.size() != 2 || !outputs.IsSequence() ||
          outputs.size() != 2) {
        return bpe::BlockResult::FAILURE;
      }

      consumer_.bind(inputs[0].as<std::string>(), inputs[1].as<std::string>());
      producer_.bind(outputs[0].as<std::string>(), outputs[1].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) {
      return bpe::BlockResult::FAILURE;
    }
  }

  bpe::ExecutionResult execute() override {
    // Acquire a single group of read grants and a single group of write grants.
    // The two input snapshots are acquired before either output is modified.
    auto inputs = consumer_.read_access();
    auto outputs = producer_.write_access();

    auto& [in_acc_first, in_acc_second] = inputs;
    transform(in_acc_first.value(), outputs.get<FIRST_OUTPUT>().value());
    transform(in_acc_second.value(), outputs.get<SECOND_OUTPUT>().value());
    return bpe::ExecutionResult::SUCCESS;
  }

private:
  static void transform(const TwistSt& source, TwistSt& destination) {
    destination = source;
    destination.twist.linear.x *= 2.0;
    destination.twist.linear.y *= -1.0;
    destination.twist.angular.z += 0.25;
  }

  bpe::MultiConsumer<TwistSt, TwistSt> consumer_;
  bpe::MultiProducer<TwistSt, TwistSt> producer_;
};

} // namespace bpe_test

PLUGINLIB_EXPORT_CLASS(bpe_test::MultiTwistTransformBlock, bpe::IBlockPlugin)
