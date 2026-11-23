#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"
#include "bpe_test/twist_checker_inspection.hpp"

#ifndef BPE_TEST_MULTI_CONFIG_PATH
#error "BPE_TEST_MULTI_CONFIG_PATH must point to the system_test_multi.yaml source file"
#endif

namespace bpe_test::test {
namespace {

class MultiTwistPipelineTest : public ::testing::Test {
protected:
  MultiTwistPipelineTest()
    : loader_("block_plugin_engine", "bpe::IBlockPlugin") {}

  pluginlib::ClassLoader<bpe::IBlockPlugin> loader_;
};

TEST_F(MultiTwistPipelineTest, TwoIndependentChannelsAreTransformedAndChecked) {
  const auto config = YAML::LoadFile(BPE_TEST_MULTI_CONFIG_PATH);
  const auto blocks_config = config["blocks"];
  ASSERT_TRUE(blocks_config.IsSequence());
  ASSERT_EQ(blocks_config.size(), 5U);

  std::array<std::shared_ptr<bpe::IBlockPlugin>, 5> blocks;
  std::array<bpe::IScheduledBlock*, 5> timers{};

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    ASSERT_TRUE(blocks_config[i]["type"]);
    blocks[i] = loader_.createSharedInstance(blocks_config[i]["type"].as<std::string>());
    ASSERT_NE(blocks[i], nullptr);
    timers[i] = dynamic_cast<bpe::IScheduledBlock*>(blocks[i].get());
    ASSERT_NE(timers[i], nullptr);
    ASSERT_EQ(blocks[i]->configure(blocks_config[i]["config"]), bpe::BlockResult::SUCCESS);
  }

  auto* checker_a = dynamic_cast<ITwistCheckerInspection*>(blocks[3].get());
  auto* checker_b = dynamic_cast<ITwistCheckerInspection*>(blocks[4].get());
  ASSERT_NE(checker_a, nullptr);
  ASSERT_NE(checker_b, nullptr);

  // Independent probes also verify which source reaches each output.
  using Message = geometry_msgs::msg::TwistStamped;
  bpe::TypedConsumer<Message> probe_a;
  bpe::TypedConsumer<Message> probe_b;
  probe_a.bind("multi_processed_twist_a");
  probe_b.bind("multi_processed_twist_b");

  for (const auto& block : blocks) {
    ASSERT_EQ(block->start(), bpe::BlockResult::SUCCESS);
  }

  // Offset channel B by one sample. This exposes incorrect input/output
  // indexing even though both channels have the same ROS message type.
  ASSERT_EQ(timers[1]->execute(), bpe::ExecutionResult::SUCCESS);

  constexpr std::size_t sample_count = 128;
  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    for (auto* timer : timers) {
      ASSERT_EQ(timer->execute(), bpe::ExecutionResult::SUCCESS);
    }
    auto result_a = probe_a.read_access();
    auto result_b = probe_b.read_access();
    ASSERT_TRUE(result_a.has_new_value());
    ASSERT_TRUE(result_b.has_new_value());
    EXPECT_DOUBLE_EQ(result_a.value().twist.linear.x,
                     2.0 * static_cast<double>(sample + 1));
    EXPECT_DOUBLE_EQ(result_b.value().twist.linear.x,
                     2.0 * static_cast<double>(sample + 2));
  }

  EXPECT_EQ(checker_a->checked_samples(), sample_count);
  EXPECT_EQ(checker_a->errors(), 0U);
  EXPECT_EQ(checker_a->missed_samples(), 0U);
  EXPECT_EQ(checker_b->checked_samples(), sample_count);
  EXPECT_EQ(checker_b->errors(), 0U);
  EXPECT_EQ(checker_b->missed_samples(), 0U);

  for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
    EXPECT_EQ((*it)->stop(), bpe::BlockResult::SUCCESS);
  }
  for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
    EXPECT_EQ((*it)->cleanup(), bpe::BlockResult::SUCCESS);
  }
}

} // namespace
} // namespace bpe_test::test
