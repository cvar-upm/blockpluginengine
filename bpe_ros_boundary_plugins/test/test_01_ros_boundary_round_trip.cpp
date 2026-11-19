#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <geometry_msgs/msg/twist.hpp>
#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/wait_for_message.hpp>
#include <std_msgs/msg/int32.hpp>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/iros_boundary_block.hpp"
#include "block_plugin_engine/itimer_block.hpp"

namespace {
using namespace std::chrono_literals;
using Number = std_msgs::msg::Int32;
using Twist = geometry_msgs::msg::Twist;

class BoundaryRoundTrip : public ::testing::Test {
protected:
  void SetUp() override {
    if (!rclcpp::ok()) rclcpp::init(0, nullptr);
    node_ = std::make_shared<rclcpp::Node>("bpe_boundary_test_node");
    observer_ = std::make_shared<rclcpp::Node>("bpe_boundary_test_observer");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    scheduler_loader_ = std::make_unique<pluginlib::ClassLoader<bpe::IBlockScheduler>>(
        "block_plugin_engine", "bpe::IBlockScheduler");
    block_loader_ = std::make_unique<pluginlib::ClassLoader<bpe::IBlockPlugin>>(
        "block_plugin_engine", "bpe::IBlockPlugin");
  }

  void TearDown() override {
    if (scheduler_) scheduler_->stop();
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) (*it)->stop();
    if (executor_) executor_->cancel();
    if (spin_thread_.joinable()) spin_thread_.join();
    for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) (*it)->cleanup();
    if (scheduler_) scheduler_->clear();
    blocks_.clear();
    scheduler_.reset();
    block_loader_.reset();
    scheduler_loader_.reset();
    executor_.reset();
    observer_.reset();
    node_.reset();
    if (rclcpp::ok()) rclcpp::shutdown();
  }

  void configure(const YAML::Node& yaml) {
    scheduler_ = scheduler_loader_->createSharedInstance(
        "block_plugin_engine/HybridFairScheduler");
    ASSERT_EQ(scheduler_->configure(yaml["scheduler"]), bpe::SchedulerResult::SUCCESS);
    auto* provider = scheduler_->event_signal_provider();
    ASSERT_NE(provider, nullptr);

    for (const auto& entry : yaml["blocks"]) {
      auto block = block_loader_->createSharedInstance(entry["type"].as<std::string>());
      if (auto* boundary = dynamic_cast<bpe::IRosBoundaryBlock*>(block.get()))
        boundary->set_ros_interface(node_->get_node_topics_interface());
      if (auto* producer = dynamic_cast<bpe::IEventSignalUser*>(block.get()))
        producer->set_event_signal_provider(*provider);
      blocks_.push_back(block);
      ASSERT_EQ(block->configure(entry["config"]), bpe::BlockResult::SUCCESS)
          << entry["name"].as<std::string>();
      if (auto* executable = dynamic_cast<bpe::ITimerBlock*>(block.get())) {
        ASSERT_EQ(scheduler_->register_block(entry["name"].as<std::string>(),
                   *executable, entry["execution"]), bpe::SchedulerResult::SUCCESS);
      }
    }
  }

  void start() {
    for (const auto& block : blocks_)
      ASSERT_EQ(block->start(), bpe::BlockResult::SUCCESS);
    ASSERT_EQ(scheduler_->start(), bpe::SchedulerResult::SUCCESS);
    spin_thread_ = std::thread([this] { executor_->spin(); });
  }

  static bool wait_discovery(
      const rclcpp::Publisher<Number>::SharedPtr& int_pub,
      const rclcpp::Publisher<Twist>::SharedPtr& twist_pub,
      const rclcpp::Subscription<Number>::SharedPtr& int_sub,
      const rclcpp::Subscription<Twist>::SharedPtr& twist_sub) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (int_pub->get_subscription_count() >= 1 &&
          twist_pub->get_subscription_count() >= 1 &&
          int_sub->get_publisher_count() >= 1 &&
          twist_sub->get_publisher_count() >= 1) return true;
      std::this_thread::sleep_for(5ms);
    }
    return false;
  }

  std::unique_ptr<pluginlib::ClassLoader<bpe::IBlockScheduler>> scheduler_loader_;
  std::unique_ptr<pluginlib::ClassLoader<bpe::IBlockPlugin>> block_loader_;
  std::shared_ptr<bpe::IBlockScheduler> scheduler_;
  std::vector<std::shared_ptr<bpe::IBlockPlugin>> blocks_;
  rclcpp::Node::SharedPtr node_;
  rclcpp::Node::SharedPtr observer_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_thread_;
};

TEST_F(BoundaryRoundTrip, TwoMessageTypesThroughTwoEventPipelines) {
  const auto yaml = YAML::LoadFile(
      std::string(BPE_BOUNDARY_TEST_CONFIG_DIR) + "/test_01_RosRoundTrip.yaml");
  configure(yaml);
  ASSERT_NE(scheduler_, nullptr);

  auto int_pub = node_->create_publisher<Number>("/bpe_boundary_test/input_int", 10);
  auto twist_pub = node_->create_publisher<Twist>("/bpe_boundary_test/input_twist", 10);
  // These subscriptions belong only to the wait_for_message wait-set,
  // not to the executor that services RosInputBlock callbacks.
  auto int_sub = observer_->create_subscription<Number>(
      "/bpe_boundary_test/output_int", 10, [](const Number&) {});
  auto twist_sub = observer_->create_subscription<Twist>(
      "/bpe_boundary_test/output_twist", 10, [](const Twist&) {});

  start();
  ASSERT_TRUE(wait_discovery(int_pub, twist_pub, int_sub, twist_sub));

  for (int round = 0; round != 2; ++round) {
    if (round != 0) std::this_thread::sleep_for(10ms);
    Number source_number;
    source_number.data = 10 + round * 15;
    Twist source_twist;
    source_twist.linear.x = 1.5 + round;
    source_twist.linear.y = 3.0 + round;
    source_twist.angular.z = 0.5 + round;

    int_pub->publish(source_number);
    twist_pub->publish(source_twist);

    Number result_number;
    Twist result_twist;
    ASSERT_TRUE(rclcpp::wait_for_message(
        result_number, int_sub, observer_->get_node_base_interface()->get_context(), 2s));
    ASSERT_TRUE(rclcpp::wait_for_message(
        result_twist, twist_sub, observer_->get_node_base_interface()->get_context(), 2s));
    EXPECT_EQ(result_number.data, source_number.data + 10);
    EXPECT_DOUBLE_EQ(result_twist.linear.x, source_twist.linear.x * 2.0);
    EXPECT_DOUBLE_EQ(result_twist.linear.y, source_twist.linear.y);
    EXPECT_DOUBLE_EQ(result_twist.angular.z, source_twist.angular.z + 0.25);
  }
}
} // namespace
