# bpe_test

Cross-package regression tests for Block Plugin Engine. The test-only
`TwistStamped` plugins are implemented and registered in this package as
`bpe_test/*`; no separate legacy test packages are required.

## Tests

- `test_01_twist_pipeline` checks a typed producer → transformer → checker
  pipeline over 128 samples, including sequence and missed-update checks.
- `test_02_multi_twist_pipeline` checks two channels with `MultiConsumer` and
  `MultiProducer`, staggered inputs and 128 samples.
- `test_03_engine_integration.py` starts the real BPE executable and checks
  five external YAML configurations:
  - `00_empty`: configure and clean shutdown without blocks;
  - `10_internal_transform`: configure an internal typed block;
  - `20_ros_input`: configure and destroy a generic ROS subscription without
    activating (lifetime regression);
  - `30_ros_output`: configure and destroy a generic ROS publisher;
  - `90_full_pipeline`: activate ROS input → typed transform → ROS output,
    validate all six `TwistStamped` components and clean shutdown.

The first two tests are C++ GoogleTest. The integration scenarios use Python
`pytest` and `rclpy` as test clients, **not** Python launch files. YAML remains
the source of BPE configuration.

## Run

```bash
colcon build --packages-up-to bpe_test
colcon test --packages-select bpe_test --event-handlers console_direct+
colcon test-result --test-result-base build/bpe_test --verbose
```

The ROS subscription-only configuration is intentionally not activated: it
regresses the lifetime problem diagnosed in
`bpe_ros_boundary_plugins/docs/life_cycle_issues.md`.
