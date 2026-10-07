# BPE integration tests

This package contains lifecycle and end-to-end integration tests for BlockPluginEngine.

Configuration files use a numeric prefix to make the progression of scenarios visible:

- `00_empty.yaml`: engine with no blocks; baseline lifecycle configuration and shutdown;
- `10_internal_transform.yaml`: one dynamically loaded internal typed plugin;
- `20_ros_input.yaml`: generic ROS subscription only; regression case for lifecycle-node / plugin-DSO lifetime;
- `30_ros_output.yaml`: generic ROS publisher only; differential ROS boundary case;
- `90_full_pipeline.yaml`: complete ROS input -> transform -> ROS output pipeline.

The first four configurations are exercised by `test_configure_shutdown.py`. Each test configures the
real lifecycle engine, verifies that it reaches `INACTIVE`, and then verifies clean process shutdown.
These intentionally incomplete pipelines test configuration and destruction boundaries rather than
runtime message flow.

The `90_full_pipeline.yaml` scenario is exercised by `test_twist_pipeline.py`. It runs the real engine
with:

```text
ROS TwistStamped input
  -> bpe_ros_boundary_plugins/RosInputBlock
  -> bpe_system_test/TwistTransformBlock
  -> bpe_ros_boundary_plugins/RosOutputBlock
  -> ROS TwistStamped output
```

The full launch test automates `configure -> activate`, publishes a deterministic `TwistStamped`,
checks the transformed ROS output, and verifies clean shutdown.

For a manual full-pipeline run:

```bash
ros2 launch bpe_integration_test twist_pipeline.launch.py
```
