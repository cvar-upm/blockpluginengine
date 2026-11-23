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
