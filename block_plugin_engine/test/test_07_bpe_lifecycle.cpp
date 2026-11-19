#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <lifecycle_msgs/msg/state.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp>

#include "detail/bpe_composition_root.hpp"
#include "detail/bpe_lifecycle_node.hpp"

namespace bpe::test {
namespace {

using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

std::string config_file(const char* filename) {
  return std::string(BPE_TEST_CONFIG_DIR) + "/" + filename;
}

rclcpp::NodeOptions options_for(const std::string& path) {
  rclcpp::NodeOptions options;
  options.append_parameter_override("config_file", path);
  options.append_parameter_override("autostart", false);
  return options;
}

class LifecycleIntegration : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    if (!rclcpp::ok()) {
      int argc = 0;
      char** argv = nullptr;
      rclcpp::init(argc, argv);
    }
  }

  static void TearDownTestSuite() {
    if (rclcpp::ok()) rclcpp::shutdown();
  }
};

TEST_F(LifecycleIntegration, CompositionRootConstructsAndDestroys) {
  // The composition root owns the manager and scheduler. Do not retain node()
  // beyond its lifetime: destruction ordering is part of the root's contract.
  {
    BPECompositionRoot root(options_for(config_file("test_07_A_ValidLifecycle.yaml")));
    ASSERT_TRUE(root.node());
    EXPECT_EQ(root.node()->get_current_state().id(),
              lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED);
  }
}

TEST_F(LifecycleIntegration, LifecycleConfiguresActivatesDeactivatesAndCleansUp) {
  BPECompositionRoot root(options_for(config_file("test_07_A_ValidLifecycle.yaml")));
  auto& node = *root.node();
  CallbackReturn outcome = CallbackReturn::SUCCESS;

  node.configure(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  EXPECT_EQ(node.get_current_state().id(),
            lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  node.activate(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  EXPECT_EQ(node.get_current_state().id(),
            lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE);

  node.deactivate(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  EXPECT_EQ(node.get_current_state().id(),
            lifecycle_msgs::msg::State::PRIMARY_STATE_INACTIVE);

  node.cleanup(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  EXPECT_EQ(node.get_current_state().id(),
            lifecycle_msgs::msg::State::PRIMARY_STATE_UNCONFIGURED);
}

TEST_F(LifecycleIntegration, LifecycleReconfiguresAfterCleanup) {
  BPECompositionRoot root(options_for(config_file("test_07_A_ValidLifecycle.yaml")));
  auto& node = *root.node();
  CallbackReturn outcome = CallbackReturn::SUCCESS;

  for (int cycle = 0; cycle < 2; ++cycle) {
    node.configure(outcome);
    ASSERT_EQ(outcome, CallbackReturn::SUCCESS) << "cycle " << cycle;
    node.activate(outcome);
    ASSERT_EQ(outcome, CallbackReturn::SUCCESS) << "cycle " << cycle;
    node.deactivate(outcome);
    ASSERT_EQ(outcome, CallbackReturn::SUCCESS) << "cycle " << cycle;
    node.cleanup(outcome);
    ASSERT_EQ(outcome, CallbackReturn::SUCCESS) << "cycle " << cycle;
  }
}

TEST_F(LifecycleIntegration, RejectsMissingConfigFile) {
  BPECompositionRoot root(options_for(config_file("test_07_missing.yaml")));
  CallbackReturn outcome = CallbackReturn::SUCCESS;
  root.node()->configure(outcome);
  EXPECT_NE(outcome, CallbackReturn::SUCCESS);
}

TEST_F(LifecycleIntegration, RejectsInvalidSchedulerAndDestroysCleanly) {
  BPECompositionRoot root(options_for(config_file("test_07_B_InvalidScheduler.yaml")));
  CallbackReturn outcome = CallbackReturn::SUCCESS;
  root.node()->configure(outcome);
  EXPECT_NE(outcome, CallbackReturn::SUCCESS);
}

TEST_F(LifecycleIntegration, ShutdownDirectlyFromActive) {
  BPECompositionRoot root(options_for(config_file("test_07_A_ValidLifecycle.yaml")));
  auto& node = *root.node();
  CallbackReturn outcome = CallbackReturn::SUCCESS;
  node.configure(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  node.activate(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  node.shutdown(outcome);
  ASSERT_EQ(outcome, CallbackReturn::SUCCESS);
  EXPECT_EQ(node.get_current_state().id(),
            lifecycle_msgs::msg::State::PRIMARY_STATE_FINALIZED);
}

} // namespace
} // namespace bpe::test
