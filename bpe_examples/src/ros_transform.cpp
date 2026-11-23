#include <algorithm>
#include <string>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <pluginlib/class_list_macros.hpp>
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
namespace bpe_examples {
// A real transform: pose position (metres) -> saturated planar velocity command (m/s).
class PoseToVelocity final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try { input_.bind(c["input"].as<std::string>());
      output_.bind(c["output"].as<std::string>());
      gain_=c["gain"] ? c["gain"].as<double>() : 0.5;
      return bpe::BlockResult::SUCCESS;
    } catch (...) { return bpe::BlockResult::FAILURE; }
  }
  bpe::ExecutionResult execute() override {
    auto in=input_.read_access(); if (!in.has_new_value()) return bpe::ExecutionResult::SUCCESS;
    auto out=output_.write_access();
    out.value()=geometry_msgs::msg::Twist{};
    out.value().linear.x=std::clamp(gain_*in.value().pose.position.x,-1.0,1.0);
    out.value().linear.y=std::clamp(gain_*in.value().pose.position.y,-1.0,1.0);
    return bpe::ExecutionResult::SUCCESS;
  }
private:
  bpe::TypedConsumer<geometry_msgs::msg::PoseStamped> input_;
  bpe::TypedProducer<geometry_msgs::msg::Twist> output_;
  double gain_{0.5};
};
}
PLUGINLIB_EXPORT_CLASS(bpe_examples::PoseToVelocity, bpe::IBlockPlugin)
