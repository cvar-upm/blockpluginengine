# BPE integration test

This package contains end-to-end launch tests for BlockPluginEngine.

The `twist_pipeline` scenario runs the real lifecycle engine with:

```text
ROS TwistStamped input
  -> bpe_ros_boundary_plugins/RosInputBlock
  -> bpe_system_test/TwistTransformBlock
  -> bpe_ros_boundary_plugins/RosOutputBlock
  -> ROS TwistStamped output
```

The launch file keeps the engine's internal `autostart` parameter disabled and
automates the `configure -> activate` sequence with `LifecycleTransition`.
This intentionally exercises launch-controlled lifecycle bring-up.

For a manual run:

```bash
ros2 launch bpe_integration_test twist_pipeline.launch.py
```
