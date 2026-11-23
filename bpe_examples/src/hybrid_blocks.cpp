// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <iostream>
#include <string>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/int32.hpp>
#include <pluginlib/class_list_macros.hpp>
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
namespace bpe_examples {
// Same instance executes in either event pipeline; new values can arrive independently.
class CommandMixer final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try { input_.bind(c["speed"].as<std::string>(),c["direction"].as<std::string>());
      outputs_.bind(c["output_twist"].as<std::string>(),c["output_level"].as<std::string>());
      return bpe::BlockResult::SUCCESS;
    } catch (...) { return bpe::BlockResult::FAILURE; }
  }
  bpe::ExecutionResult execute() override {
    auto in=input_.read_access();
    if(in.get<0>().has_new_value()) { speed_=in.get<0>().value(); speed_changed_=true; }
    if(in.get<1>().has_new_value()) { direction_=in.get<1>().value(); direction_changed_=true; }
    if(!speed_changed_ || !direction_changed_) return bpe::ExecutionResult::SUCCESS;
    auto out=outputs_.write_access();
    out.get<0>().value()=direction_;
    out.get<0>().value().linear.x=static_cast<double>(speed_.data)*0.1;
    out.get<1>().value().data=speed_.data>5 ? 1 : 0;
    speed_changed_=direction_changed_=false;
    return bpe::ExecutionResult::SUCCESS;
  }
private:
  bpe::MultiConsumer<std_msgs::msg::Int32,geometry_msgs::msg::Twist> input_;
  bpe::MultiProducer<geometry_msgs::msg::Twist,std_msgs::msg::Int32> outputs_;
  std_msgs::msg::Int32 speed_{}; geometry_msgs::msg::Twist direction_{};
  bool speed_changed_{false},direction_changed_{false};
};
// Periodic independent pipeline: no event is required to inspect last published data.
class CommandMonitor final : public bpe::IBlockPlugin, public bpe::IScheduledBlock {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& c) override {
    try { input_.bind(c["input"].as<std::string>()); return bpe::BlockResult::SUCCESS; }
    catch (...) { return bpe::BlockResult::FAILURE; }
  }
  bpe::ExecutionResult execute() override {
    auto in=input_.read_access();
    if(in.has_new_value()) std::cout<<"[monitor] linear.x="<<in.value().linear.x<<std::endl;
    return bpe::ExecutionResult::SUCCESS;
  }
private: bpe::TypedConsumer<geometry_msgs::msg::Twist> input_;
};
}
PLUGINLIB_EXPORT_CLASS(bpe_examples::CommandMixer, bpe::IBlockPlugin)
PLUGINLIB_EXPORT_CLASS(bpe_examples::CommandMonitor, bpe::IBlockPlugin)
