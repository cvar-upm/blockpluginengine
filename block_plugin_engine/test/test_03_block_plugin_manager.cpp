#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/logger.hpp>

#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "detail/block_plugin_manager.hpp"
#include "mocks/scheduler_mock.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {
namespace {

class BlockPluginManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    clear_events();

    scheduler_ = std::make_shared<SchedulerMock>();
    ros_topics_.reset();

    manager_ = std::make_unique<BlockPluginManager>(scheduler_,
                                                    ros_topics_,
                                                    rclcpp::get_logger("bpe_test"),
                                                    std::vector<std::string>{BPE_TEST_PLUGIN_XML});
  }

  std::shared_ptr<SchedulerMock> scheduler_;
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr ros_topics_;
  std::unique_ptr<BlockPluginManager> manager_;
};

YAML::Node load_blocks_config(const std::string& filename) {
  const YAML::Node root = YAML::LoadFile(std::string{BPE_TEST_CONFIG_DIR} + "/" + filename);
  return root["blocks"];
}

TEST_F(BlockPluginManagerTest, LoadsAndRunsValidConfiguration) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_valid.yaml")));

  ASSERT_EQ(scheduler_->entries().size(), 1U);
  EXPECT_EQ(scheduler_->entries()[0].name, "timer");
  EXPECT_EQ(scheduler_->entries()[0].config["policy"].as<std::string>(), "timer");
  EXPECT_EQ(scheduler_->entries()[0].config["period"].as<std::string>(), "10ms");

  EXPECT_EQ(events(),
            (std::vector<std::string>{
                "basic_a.configure:10",
                "basic_b.configure:20",
                "timer.configure:periodic",
                "idle_timer.configure:not_scheduled",
            }));

  ASSERT_TRUE(manager_->start());

  EXPECT_EQ(scheduler_->execute("timer"), ExecutionResult::SUCCESS);

  ASSERT_TRUE(manager_->stop());
  ASSERT_TRUE(manager_->cleanup());

  EXPECT_EQ(events(),
            (std::vector<std::string>{
                "basic_a.configure:10",
                "basic_b.configure:20",
                "timer.configure:periodic",
                "idle_timer.configure:not_scheduled",
                "basic_a.start",
                "basic_b.start",
                "timer.start",
                "idle_timer.start",
                "timer.execute",
                "idle_timer.stop",
                "timer.stop",
                "basic_b.stop",
                "basic_a.stop",
                "idle_timer.cleanup",
                "timer.cleanup",
                "basic_b.cleanup",
                "basic_a.cleanup",
            }));
}

TEST_F(BlockPluginManagerTest, RejectsDuplicateNamesBeforeLoadingPlugins) {
  EXPECT_FALSE(manager_->configure(load_blocks_config("test_03_duplicate_name.yaml")));
  EXPECT_TRUE(events().empty());
}

TEST_F(BlockPluginManagerTest, IgnoresExecutionOnNonTimerBlock) {
  EXPECT_TRUE(manager_->configure(load_blocks_config("test_03_invalid_execution.yaml")));

  EXPECT_EQ(events(),
            (std::vector<std::string>{
                "basic.configure:42",
            }));
  EXPECT_TRUE(scheduler_->entries().empty());
  EXPECT_TRUE(manager_->cleanup());
}

TEST_F(BlockPluginManagerTest, RollsBackPreviousBlocksOnConfigureFailure) {
  EXPECT_FALSE(manager_->configure(load_blocks_config("test_03_configure_failure.yaml")));

  EXPECT_EQ(events(),
            (std::vector<std::string>{
                "basic.configure:42",
                "failing.configure",
                "basic.cleanup",
            }));
}

TEST_F(BlockPluginManagerTest, StopsPreviouslyStartedBlocksOnStartFailure) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_start_failure.yaml")));

  clear_events();

  EXPECT_FALSE(manager_->start());

  EXPECT_EQ(events(),
            (std::vector<std::string>{
                "basic.start",
                "failing.start",
                "basic.stop",
            }));

  ASSERT_TRUE(manager_->cleanup());
}


// Construct invalid configurations in memory: these must fail before loading any plugin.
class BlockConfigValidationTest : public BlockPluginManagerTest,
                                  public ::testing::WithParamInterface<std::string> {};

TEST_P(BlockConfigValidationTest, RejectsMalformedBlockConfiguration) {
  EXPECT_FALSE(manager_->configure(YAML::Load(GetParam())));
  EXPECT_TRUE(events().empty());
  EXPECT_TRUE(scheduler_->entries().empty());
}
INSTANTIATE_TEST_SUITE_P(InvalidBlocks, BlockConfigValidationTest,
  ::testing::Values(
    "{}", "null", "42", "[3]", "[{}]", "[{name: a}]", "[{type: bpe_test/TestBasicBlock}]",
    "[{name: '', type: bpe_test/TestBasicBlock}]", "[{name: x, type: ''}]"
  ));

TEST_F(BlockPluginManagerTest, InjectsPullSignalProviderBeforeConfigure) {
  scheduler_->allow_signal("ready");
  ASSERT_TRUE(manager_->configure(YAML::Load(
      "[{name: emitter, type: bpe_test/TestSignalUserMockBlock}]")));
  EXPECT_EQ(events(), (std::vector<std::string>{
      "signal.inject", "signal.configure:resolved"}));
  ASSERT_TRUE(manager_->start());
  EXPECT_EQ(events().back(), "signal.start:sent");
  EXPECT_TRUE(manager_->stop());
  EXPECT_TRUE(manager_->cleanup());
}

TEST_F(BlockPluginManagerTest, RejectsUnknownPluginType) {
  const auto blocks = YAML::Load("[{name: missing, type: bpe_test/NoSuchBlock}]");
  EXPECT_FALSE(manager_->configure(blocks));
  EXPECT_TRUE(scheduler_->entries().empty());
  EXPECT_TRUE(events().empty());
}

TEST_F(BlockPluginManagerTest, RegistersTimerExecutionConfiguration) {
  auto blocks = YAML::Load(R"(
- name: timer
  type: bpe_test/TestTimerBlock
  config: {id: timer, label: periodic}
  execution: {policy: timer, period: 20ms, offset: 2ms, thread: 3}
)");
  ASSERT_TRUE(manager_->configure(blocks));
  ASSERT_EQ(scheduler_->entries().size(), 1U);
  const auto& timer = scheduler_->entries()[0];
  EXPECT_EQ(timer.name, "timer");
  EXPECT_EQ(timer.config["policy"].as<std::string>(), "timer");
  EXPECT_EQ(timer.config["period"].as<std::string>(), "20ms");
  EXPECT_EQ(timer.config["offset"].as<std::string>(), "2ms");
  EXPECT_EQ(timer.config["thread"].as<unsigned>(), 3U);
  EXPECT_TRUE(manager_->cleanup());
}


TEST_F(BlockPluginManagerTest, ForwardsExecutionWithoutParsingIt) {
  auto blocks = YAML::Load(R"(
- name: timer
  type: bpe_test/TestTimerBlock
  config: {id: timer, label: raw_yaml}
  execution: {policy: invalid-for-fair-scheduler, period: nonsense}
)");
  ASSERT_TRUE(manager_->configure(blocks));
  ASSERT_EQ(scheduler_->entries().size(), 1U);
  EXPECT_EQ(scheduler_->entries()[0].config["policy"].as<std::string>(),
            "invalid-for-fair-scheduler");
  EXPECT_EQ(scheduler_->entries()[0].config["period"].as<std::string>(), "nonsense");
  EXPECT_TRUE(manager_->cleanup());
}
TEST_F(BlockPluginManagerTest, DoesNotScheduleBlocksWithoutExecution) {
  ASSERT_TRUE(manager_->configure(YAML::Load(R"(
- name: plain
  type: bpe_test/TestBasicBlock
  config: {id: plain, value: 5}
- name: idle_timer
  type: bpe_test/TestTimerBlock
  config: {id: idle, label: unscheduled}
)")));
  EXPECT_TRUE(scheduler_->entries().empty());
  EXPECT_EQ(scheduler_->calls(), (std::vector<std::string>{"register:idle_timer"}));
  EXPECT_TRUE(manager_->cleanup());
}

TEST_F(BlockPluginManagerTest, RollsBackWhenSchedulerRejectsAdd) {
  scheduler_->set_add_result(SchedulerResult::INVALID_CONFIG);
  EXPECT_FALSE(manager_->configure(YAML::Load(R"(
- name: plain
  type: bpe_test/TestBasicBlock
  config: {id: plain, value: 9}
- name: timer
  type: bpe_test/TestTimerBlock
  config: {id: timer, label: rejected}
  execution: {policy: timer, period: 10ms}
)")));
  EXPECT_EQ(scheduler_->calls(), (std::vector<std::string>{"register:timer", "clear"}));
  EXPECT_TRUE(scheduler_->entries().empty());
  EXPECT_EQ(events(), (std::vector<std::string>{
    "plain.configure:9", "timer.configure:rejected", "timer.cleanup", "plain.cleanup"}));
}

TEST_F(BlockPluginManagerTest, CleanupClearsSchedulerBeforeBlockCleanup) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_valid.yaml")));
  clear_events();
  EXPECT_TRUE(manager_->cleanup());
  EXPECT_EQ(scheduler_->calls(), (std::vector<std::string>{"register:timer", "register:idle_timer", "clear"}));
  EXPECT_TRUE(scheduler_->entries().empty());
  EXPECT_EQ(events(), (std::vector<std::string>{
      "idle_timer.cleanup", "timer.cleanup", "basic_b.cleanup", "basic_a.cleanup"}));
  // SchedulerMock records clear; block event log records cleanup separately.
  // A cross-log ordering assertion is intentionally not claimed here.
}

TEST_F(BlockPluginManagerTest, RejectsSecondConfiguration) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_valid.yaml")));
  clear_events();
  EXPECT_FALSE(manager_->configure(load_blocks_config("test_03_valid.yaml")));
  EXPECT_TRUE(events().empty());
  EXPECT_EQ(scheduler_->entries().size(), 1U);
  EXPECT_TRUE(manager_->cleanup());
}
TEST_F(BlockPluginManagerTest, CanConfigureAfterCleanup) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_valid.yaml")));
  ASSERT_TRUE(manager_->cleanup());
  clear_events();
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_valid.yaml")));
  EXPECT_EQ(scheduler_->entries().size(), 1U);
  EXPECT_EQ(events().size(), 4U);
  EXPECT_TRUE(manager_->cleanup());
}
TEST_F(BlockPluginManagerTest, ContinuesStoppingAfterFailure) {
  ASSERT_TRUE(manager_->configure(YAML::Load(R"(
- name: early
  type: bpe_test/TestBasicBlock
  config: {id: early, value: 1}
- name: failure
  type: bpe_test/TestFailingBlock
  config: {id: failure, fail_on: stop}
- name: late
  type: bpe_test/TestBasicBlock
  config: {id: late, value: 2}
)")));
  ASSERT_TRUE(manager_->start());
  clear_events();
  EXPECT_FALSE(manager_->stop());
  EXPECT_EQ(events(), (std::vector<std::string>{"late.stop", "failure.stop", "early.stop"}));
  EXPECT_TRUE(manager_->cleanup());
}
TEST_F(BlockPluginManagerTest, ContinuesCleaningAfterFailure) {
  ASSERT_TRUE(manager_->configure(YAML::Load(R"(
- name: early
  type: bpe_test/TestBasicBlock
  config: {id: early, value: 1}
- name: failure
  type: bpe_test/TestFailingBlock
  config: {id: failure, fail_on: cleanup}
- name: late
  type: bpe_test/TestBasicBlock
  config: {id: late, value: 2}
)")));
  clear_events();
  EXPECT_FALSE(manager_->cleanup());
  EXPECT_EQ(events(), (std::vector<std::string>{"late.cleanup", "failure.cleanup", "early.cleanup"}));
}
TEST_F(BlockPluginManagerTest, CleansUpAfterStartFailure) {
  ASSERT_TRUE(manager_->configure(load_blocks_config("test_03_start_failure.yaml")));
  EXPECT_FALSE(manager_->start());
  clear_events();
  EXPECT_TRUE(manager_->cleanup());
  EXPECT_EQ(events(), (std::vector<std::string>{"failing.cleanup", "basic.cleanup"}));
}

} // namespace
} // namespace bpe::test