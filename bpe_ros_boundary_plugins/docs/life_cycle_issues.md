# ROS input subscription: lifecycle and plugin-loader diagnosis

## Symptom and findings

While investigating shutdown problems following configuration of `RosInputBlock`, three
reduced plugins were used to isolate the component responsible. Binding a serialized
Blackboard producer alone shut down correctly; creating a generic ROS 2 subscription
reproduced the lifetime issue whether the callback was a method or a free function.
This implicated the destruction order of ROS subscriptions and the `pluginlib` loader,
not the callback's capture of `this` or Blackboard registration.

The resulting ownership contract in `BPECompositionRoot` ensures that
`BPE_LifecycleNode` (and its ROS subscriptions) is destroyed **before**
`BlockPluginManager` and the `pluginlib::ClassLoader` that owns the plugin library.
The scheduler is destroyed last. `test_07_bpe_lifecycle` protects the basic
construction, shutdown, and cleanup paths.

The files below are retained verbatim as historical diagnostic source, not as
compiled plugins or examples. The original directory `src/debug/` was removed.

## Archived `src/debug/README.md`

````markdown
# Archived ROS input diagnostic fixtures

These source files were created while diagnosing a shutdown/lifetime fault triggered after configuring `RosInputBlock`.

They split the input boundary into independent operations:

- `DebugRosInputBindBlock`: serialized blackboard/type-registry producer binding only;
- `DebugRosSubscriptionMemberBlock`: generic ROS subscription with a member callback, without blackboard binding;
- `DebugRosSubscriptionFunctionBlock`: the same generic ROS subscription with a callback independent of the plugin instance.

The experiments showed that producer binding alone shut down correctly, while either generic-subscription variant reproduced the fault. A later lifetime experiment showed that keeping `BlockPluginManager` and its `pluginlib::ClassLoader` alive until after destruction of `BPE_LifecycleNode` removed the fault.

The permanent fix is the explicit ownership/destruction contract implemented by `BPECompositionRoot`:

```text
BPE_LifecycleNode
    ↓
BlockPluginManager / ClassLoader
    ↓
Scheduler
```

These files are intentionally **not compiled and not registered in `plugins.xml`**. They are retained only as diagnostic fixtures in case this lifetime issue must be investigated again. Do not add them back to the production plugin library unless they are explicitly needed for diagnostics.

The full rationale and retained regression test are documented in:

- `docs/architecture.md` — physical lifetime contract;
- `docs/testing.md` — lifetime regression and diagnostic reduction.
````

## Archived `src/debug/debug_ros_input_bind_block.cpp`

```cpp
#include <string>

#include <pluginlib/class_list_macros.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "detail/serialized_blackboard.hpp"

namespace bpe_ros_boundary_plugins::debug {

class DebugRosInputBindBlock final : public bpe::IBlockPlugin {
public:
  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (!config["ros_type"] || !config["blackboard_output"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        const std::string ros_type = config["ros_type"].as<std::string>();
        const std::string blackboard_output = config["blackboard_output"].as<std::string>();

        if (ros_type.empty() || blackboard_output.empty()) {
          result = bpe::BlockResult::FAILURE;
        } else {
          producer_.bind(blackboard_output, ros_type);
        }
      }
    } catch (...) {
      result = bpe::BlockResult::FAILURE;
    }

    return result;
  }

  bpe::BlockResult start() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult stop() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult cleanup() override {
    return bpe::BlockResult::SUCCESS;
  }

private:
  detail::SerializedProducer producer_;
};

} // namespace bpe_ros_boundary_plugins::debug

PLUGINLIB_EXPORT_CLASS(
  bpe_ros_boundary_plugins::debug::DebugRosInputBindBlock,
  bpe::IBlockPlugin)
```

## Archived `src/debug/debug_ros_subscription_member_block.cpp`

```cpp
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
#include "block_plugin_engine/iros_boundary_block.hpp"

namespace bpe_ros_boundary_plugins::debug {

class DebugRosSubscriptionMemberBlock final
  : public bpe::IBlockPlugin,
    public bpe::IRosBoundaryBlock {
public:
  void set_ros_interface(
      const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics) override {
    topics_ = topics;
  }

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (topics_ == nullptr || !config["topic"] || !config["ros_type"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        const std::string topic = config["topic"].as<std::string>();
        const std::string ros_type = config["ros_type"].as<std::string>();
        std::size_t qos_depth = 10u;

        if (config["qos_depth"]) {
          qos_depth = config["qos_depth"].as<std::size_t>();
        }

        if (topic.empty() || ros_type.empty() || qos_depth == 0u) {
          result = bpe::BlockResult::FAILURE;
        } else {
          subscription_ = rclcpp::create_generic_subscription(
              topics_,
              topic,
              ros_type,
              rclcpp::QoS(qos_depth),
              std::bind(
                  &DebugRosSubscriptionMemberBlock::on_message,
                  this,
                  std::placeholders::_1));
        }
      }
    } catch (...) {
      result = bpe::BlockResult::FAILURE;
    }

    return result;
  }

  bpe::BlockResult start() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult stop() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult cleanup() override {
    subscription_.reset();
    return bpe::BlockResult::SUCCESS;
  }

private:
  void on_message(std::shared_ptr<rclcpp::SerializedMessage>) {}

  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  rclcpp::GenericSubscription::SharedPtr subscription_;
};

} // namespace bpe_ros_boundary_plugins::debug

PLUGINLIB_EXPORT_CLASS(
  bpe_ros_boundary_plugins::debug::DebugRosSubscriptionMemberBlock,
  bpe::IBlockPlugin)
```

## Archived `src/debug/debug_ros_subscription_function_block.cpp`

```cpp
#include <cstddef>
#include <memory>
#include <string>

#include <pluginlib/class_list_macros.hpp>
#include <rclcpp/create_generic_subscription.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/serialized_message.hpp>

#include "block_plugin_engine/block_config.hpp"
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/iros_boundary_block.hpp"

namespace bpe_ros_boundary_plugins::debug {
namespace {

void discard_message(std::shared_ptr<rclcpp::SerializedMessage>) {}

} // namespace

class DebugRosSubscriptionFunctionBlock final
  : public bpe::IBlockPlugin,
    public bpe::IRosBoundaryBlock {
public:
  void set_ros_interface(
      const rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr& topics) override {
    topics_ = topics;
  }

  bpe::BlockResult configure(const bpe::BlockConfig& config) override {
    bpe::BlockResult result = bpe::BlockResult::SUCCESS;

    try {
      if (topics_ == nullptr || !config["topic"] || !config["ros_type"]) {
        result = bpe::BlockResult::FAILURE;
      } else {
        const std::string topic = config["topic"].as<std::string>();
        const std::string ros_type = config["ros_type"].as<std::string>();
        std::size_t qos_depth = 10u;

        if (config["qos_depth"]) {
          qos_depth = config["qos_depth"].as<std::size_t>();
        }

        if (topic.empty() || ros_type.empty() || qos_depth == 0u) {
          result = bpe::BlockResult::FAILURE;
        } else {
          subscription_ = rclcpp::create_generic_subscription(
              topics_, topic, ros_type, rclcpp::QoS(qos_depth), &discard_message);
        }
      }
    } catch (...) {
      result = bpe::BlockResult::FAILURE;
    }

    return result;
  }

  bpe::BlockResult start() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult stop() override {
    return bpe::BlockResult::SUCCESS;
  }

  bpe::BlockResult cleanup() override {
    subscription_.reset();
    return bpe::BlockResult::SUCCESS;
  }

private:
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics_;
  rclcpp::GenericSubscription::SharedPtr subscription_;
};

} // namespace bpe_ros_boundary_plugins::debug

PLUGINLIB_EXPORT_CLASS(
  bpe_ros_boundary_plugins::debug::DebugRosSubscriptionFunctionBlock,
  bpe::IBlockPlugin)
```

