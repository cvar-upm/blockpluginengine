# bpe_test

Cross-package integration regression tests.

## Stage 1 — non-destructive migration

* `test_01_twist_pipeline`: same assertions and 128 samples as the original
  `bpe_system_test/test/test_twist_pipeline.cpp`.
* `test_02_multi_twist_pipeline`: same assertions, two channels, offset samples,
  and 128 samples as the original `bpe_system_test/test/test_multi_twist_pipeline.cpp`.

During stage 1 the tests loaded the legacy plugin classes. Stage 3 now
provides equivalent plugin classes within `bpe_test`.

No launch files are needed for these tests.

## Stage 2 — legacy integration coverage (still non-destructive)

`test_03_engine_integration.py` runs the real `block_plugin_engine` executable.
It preserves five scenarios from `bpe_integration_test`:

- `00_empty`, `10_internal_transform`, `20_ros_input`, and
  `30_ros_output`: request CONFIGURE via lifecycle ROS service, assert INACTIVE,
  send SIGTERM and require clean shutdown.
- `90_full_pipeline`: autostart, assert ACTIVE, publish `TwistStamped`,
  verify all six transformed components, and require clean shutdown.

The five scenarios retain the original behaviour. The plugin type names in
the YAML have since been updated to `bpe_test/*`. Both legacy packages and
their tests remain untouched. No launch Python is needed.

## Stage 3 — self-contained test plugins

The four TwistStamped test plugins now belong to `bpe_test`, with their
`pluginlib` descriptions in `plugins.xml`. Tests 01–03 and the YAML fixtures
load `bpe_test/*` implementations. The legacy `bpe_system_test` remains in the
workspace temporarily, but `bpe_test` no longer depends on it.

The plugin behaviour is otherwise unchanged, including the 128-sample
checks and the two-channel `MultiConsumer`/`MultiProducer` regression.
