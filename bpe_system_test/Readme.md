# BlockPluginEngine system test plugins

This package contains internal test Blocks used to validate the public typed
blackboard API with real ROS 2 message types, without involving ROS topic
boundaries.

The configured pipeline is:

```text
TwistProducerBlock
  -> raw_twist
TwistTransformBlock
  -> processed_twist
TwistCheckerBlock
```

All three Blocks use `geometry_msgs::msg::TwistStamped` as the typed blackboard
value. The producer generates deterministic samples, the transform applies the
configured transformation, and the checker validates both the transformed
payload and the blackboard sequence progression.

Run the package test with:

```bash
colcon test --packages-select bpe_system_test --event-handlers console_direct+
```

The same pipeline can also be run manually through the BPE lifecycle node:

```bash
ros2 run block_plugin_engine block_plugin_engine --ros-args \
  -p config_file:=$(ros2 pkg prefix bpe_system_test)/share/bpe_system_test/config/system_test.yaml

ros2 lifecycle set /block_plugin_engine configure
ros2 lifecycle set /block_plugin_engine activate
```
