# bpe_test

Cross-package integration regression tests.

## Stage 1 — non-destructive migration

* `test_01_twist_pipeline`: same assertions and 128 samples as the original
  `bpe_system_test/test/test_twist_pipeline.cpp`.
* `test_02_multi_twist_pipeline`: same assertions, two channels, offset samples,
  and 128 samples as the original `bpe_system_test/test/test_multi_twist_pipeline.cpp`.

The tests still load `bpe_system_test` plugin classes. This is deliberate:
no original test or plugin is removed until these migrated tests pass.
The next stage will move the remaining lifecycle and ROS integration tests.

No launch files are needed for these tests.

## Stage 2 — legacy integration coverage (still non-destructive)

`test_03_engine_integration.py` runs the real `block_plugin_engine` executable.
It preserves five scenarios from `bpe_integration_test`:

- `00_empty`, `10_internal_transform`, `20_ros_input`, and
  `30_ros_output`: request CONFIGURE via lifecycle ROS service, assert INACTIVE,
  send SIGTERM and require clean shutdown.
- `90_full_pipeline`: autostart, assert ACTIVE, publish `TwistStamped`,
  verify all six transformed components, and require clean shutdown.

The YAML files are copied verbatim. Both legacy packages and all their tests
remain untouched until this migration passes. No launch Python is needed.
