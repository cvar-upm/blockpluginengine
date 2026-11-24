// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <time.h>

#include <gtest/gtest.h>
#include <rclcpp/logger.hpp>
#include <yaml-cpp/yaml.h>

#include <pluginlib/class_loader.hpp>
#include "block_plugin_engine/iblock_scheduler.hpp"
#include "detail/block_plugin_manager.hpp"
#include "tools/event_log.hpp"
#include "tools/simulated_clock.hpp"

namespace bpe::test {
namespace {
using namespace std::chrono_literals;

// The shared clock is reset only with all plugin workers stopped.
std::chrono::nanoseconds monotonic_now() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return std::chrono::seconds{t.tv_sec} + std::chrono::nanoseconds{t.tv_nsec};
}

bool contains(const std::string& event) {
  const auto log = events();
  return std::find(log.begin(), log.end(), event) != log.end();
}

std::size_t count(const std::string& event) {
  const auto log = events();
  return static_cast<std::size_t>(std::count(log.begin(), log.end(), event));
}

class SchedulerIntegration : public ::testing::Test {
protected:
  void SetUp() override {
    clear_events();
    integration_clock().reset(monotonic_now());
    origin_ = monotonic_now() + 200ms;
    manager_ = std::make_unique<BlockPluginManager>(
        rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr{},
        rclcpp::get_logger("bpe_integration_test"),
        std::vector<std::string>{BPE_TEST_PLUGIN_XML});
  }

  void TearDown() override {
    if (scheduler_)
      scheduler_->stop();
    if (manager_) {
      if (started_blocks_)
        manager_->stop();
      manager_->cleanup();
      manager_.reset();
    }
    scheduler_.reset();
  }

  static YAML::Node load_config(const std::string& filename) {
    return YAML::LoadFile(std::string{BPE_TEST_CONFIG_DIR} + "/" + filename);
  }

  bool configure_file(const std::string& filename) {
    const auto yaml = load_config(filename);
    if (scheduler_) {
      scheduler_->stop();
      scheduler_->clear();
      scheduler_.reset();
    }
    const auto config = yaml["scheduler"];
    const std::string type = config && config["type"] ? config["type"].as<std::string>()
                                                      : "block_plugin_engine/FairScheduler";
    scheduler_ = loader_.createSharedInstance(type);
    const auto result = scheduler_->configure(config);
    if (result != SchedulerResult::SUCCESS) {
      ADD_FAILURE() << "Scheduler configure failed for " << filename
                    << " (SchedulerResult=" << static_cast<int>(result) << ")";
      return false;
    }
    manager_->set_scheduler(scheduler_);
    return manager_->configure(yaml["blocks"]);
  }

  bool start() {
    if (!manager_->start())
      return false;
    started_blocks_ = true;
    return scheduler_->start(origin_) == SchedulerResult::SUCCESS;
  }

  void advance(std::chrono::milliseconds delta) {
    // Advance to a known nominal instant, not relative to OS scheduling.
    integration_clock().advance_to(origin_ + delta);
  }

  void stop() {
    ASSERT_EQ(scheduler_->stop(), SchedulerResult::SUCCESS);
    if (started_blocks_) {
      ASSERT_TRUE(manager_->stop());
      started_blocks_ = false;
    }
  }

  pluginlib::ClassLoader<IBlockScheduler> loader_{"block_plugin_engine", "bpe::IBlockScheduler"};
  std::shared_ptr<IBlockScheduler> scheduler_;
  std::unique_ptr<BlockPluginManager> manager_;
  bool started_blocks_{false};
  std::chrono::nanoseconds origin_{0};
};

TEST_F(SchedulerIntegration, FairLoadsPluginAndSchedulesFromBlockExecution) {
  ASSERT_TRUE(configure_file("test_06_A_FairLoadsPluginAndSchedulesFromBlockExecution.yaml"));
  ASSERT_TRUE(start());
  clear_events();
  advance(30ms);
  ASSERT_TRUE(wait_for_event_count("a.execute", 3, 2s));
  stop();
  EXPECT_EQ(count("a.execute"), 3u);
  EXPECT_EQ(count("idle.execute"), 0u);
}

TEST_F(SchedulerIntegration, HybridResolvesPipelinesAndIgnoresBlockExecution) {
  ASSERT_TRUE(configure_file("test_06_B_HybridResolvesPipelinesAndIgnoresBlockExecution.yaml"));
  auto* provider = dynamic_cast<IEventSignalProvider*>(scheduler_.get());
  ASSERT_NE(provider, nullptr);
  auto signal = provider->get_signal("ready");
  ASSERT_TRUE(signal);
  integration_clock().freeze();
  ASSERT_TRUE(start());
  ASSERT_TRUE(integration_clock().wait_until_frozen(2s));
  clear_events();
  advance(21ms);
  ASSERT_TRUE(signal->signal());
  integration_clock().release();
  ASSERT_TRUE(wait_for_event("c.execute", 2s));
  ASSERT_TRUE(wait_for_event("b.execute", 2s));
  stop();
  const auto log = events();
  const auto a = std::find(log.begin(), log.end(), "a.execute");
  const auto b = std::find(log.begin(), log.end(), "b.execute");
  ASSERT_NE(a, log.end());
  ASSERT_NE(b, log.end());
  EXPECT_LT(a, b);                   // One complete pipeline, in configured order.
  EXPECT_EQ(count("a.execute"), 1u); // Invalid individual execution was ignored.
  EXPECT_EQ(count("c.execute"), 1u);
}

TEST_F(SchedulerIntegration, MultiSchedulerForwardsTimedSignalToHybridOnOtherWorker) {
  ASSERT_TRUE(
      configure_file("test_06_C_MultiSchedulerForwardsTimedSignalToHybridOnOtherWorker.yaml"));
  ASSERT_TRUE(contains("producer.inject"));
  ASSERT_TRUE(contains("producer.configure"));
  ASSERT_TRUE(start());
  clear_events();
  // The origin of MultiScheduler is real CLOCK_MONOTONIC + 100ms.
  // Advancing past that origin drives the periodic producer without wall sleeps.
  integration_clock().advance_to(origin_ + 100ms + 40ms);
  ASSERT_TRUE(wait_for_event("producer.execute", 2s));
  ASSERT_TRUE(wait_for_event("consumer.execute", 2s));
  stop();
  EXPECT_GE(count("producer.execute"), 1u);
  EXPECT_GE(count("consumer.execute"), 1u);
  const auto log = events();
  const auto producer = std::find(log.begin(), log.end(), "producer.execute");
  const auto consumer = std::find(log.begin(), log.end(), "consumer.execute");
  ASSERT_NE(producer, log.end());
  ASSERT_NE(consumer, log.end());
  EXPECT_LT(producer, consumer); // Cross-thread notification is causal.
}

TEST_F(SchedulerIntegration, HybridFailsToStartWhenPipelineReferencesMissingBlock) {
  ASSERT_TRUE(
      configure_file("test_06_D_HybridFailsToStartWhenPipelineReferencesMissingBlock.yaml"));
  ASSERT_TRUE(manager_->start());
  started_blocks_ = true;
  EXPECT_EQ(scheduler_->start(), SchedulerResult::INVALID_CONFIG);
  EXPECT_FALSE(contains("present.execute"));
}

TEST_F(SchedulerIntegration, HybridCleanupInvalidatesSignalsAndAllowsReconfigure) {
  ASSERT_TRUE(configure_file("test_06_E_HybridCleanupInvalidatesSignalsAndAllowsReconfigure.yaml"));
  auto* old_provider = dynamic_cast<IEventSignalProvider*>(scheduler_.get());
  ASSERT_NE(old_provider, nullptr);
  auto old_signal = old_provider->get_signal("ready");
  ASSERT_TRUE(old_signal);
  ASSERT_TRUE(start());
  stop();
  ASSERT_TRUE(manager_->cleanup());
  EXPECT_FALSE(old_signal->signal());

  ASSERT_TRUE(configure_file("test_06_E_HybridCleanupInvalidatesSignalsAndAllowsReconfigure.yaml"));
  auto* new_provider = dynamic_cast<IEventSignalProvider*>(scheduler_.get());
  ASSERT_NE(new_provider, nullptr);
  auto new_signal = new_provider->get_signal("ready");
  ASSERT_TRUE(new_signal);
  EXPECT_NE(old_signal, new_signal);
  ASSERT_TRUE(start());
  clear_events();
  integration_clock().advance_to(origin_ + 20ms);
  ASSERT_TRUE(new_signal->signal());
  ASSERT_TRUE(wait_for_event("receiver.execute", 2s));
  stop();
  EXPECT_EQ(count("receiver.execute"), 1u);
}

TEST_F(SchedulerIntegration, SchedulerRejectsInvalidFairExecutionAndManagerRollsBack) {
  const auto config =
      load_config("test_06_F_SchedulerRejectsInvalidFairExecutionAndManagerRollsBack.yaml");
  scheduler_ = loader_.createSharedInstance("block_plugin_engine/FairScheduler");
  ASSERT_EQ(scheduler_->configure(config["scheduler"]), SchedulerResult::SUCCESS);
  manager_->set_scheduler(scheduler_);
  EXPECT_FALSE(manager_->configure(load_config(
      "test_06_F_SchedulerRejectsInvalidFairExecutionAndManagerRollsBack.yaml")["blocks"]));
  EXPECT_TRUE(contains("first.cleanup"));
  EXPECT_TRUE(contains("invalid.cleanup"));
  // Retrying after the manager's rollback must work.
  ASSERT_TRUE(manager_->configure(load_config(
      "test_06_F_SchedulerRejectsInvalidFairExecutionAndManagerRollsBack.yaml")["retry_blocks"]));
}

} // namespace
} // namespace bpe::test
